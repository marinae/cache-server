#include "server.h"

//+----------------------------------------------------------------------------+
//| Server class constructor                                                   |
//+----------------------------------------------------------------------------+

Server::Server(std::string ip, uint16_t port, std::string shm, std::string sem)
: ip(ip), port(port), shmFilename(shm), base(nullptr), mainEvent(nullptr),
semaphore(nullptr), semFile(sem), ttl_cleaner(-1) {}

//+----------------------------------------------------------------------------+
//| Server class destructor                                                    |
//+----------------------------------------------------------------------------+

Server::~Server() {

    /* Free memory and close sockets */
    for (size_t i = 0; i < workers.size(); ++i) {
        if (workers[i].first != -1)
            kill(workers[i].first, SIGINT);
        close(workers[i].second);
    }

    if (ttl_cleaner != -1)
        kill(ttl_cleaner, SIGINT);

    if (mainEvent) {
        event_free(mainEvent);
        mainEvent = nullptr;
    }
    if (base) {
        event_base_free(base);
        base = nullptr;
    }
    close(master);
    close(shmFile);

    if (shm_unlink(shmFilename.c_str()) == -1)
        std::cout << "[shm_unlink]:\t" << strerror(errno) << std::endl;

    if (sem_close(semaphore) == -1)
        std::cout << "[sem_close]:\t" << strerror(errno) << std::endl;

    if (sem_unlink(semFile.c_str()) == -1)
        std::cout << "[sem_unlink]:\t" << strerror(errno) << std::endl;
}

//+----------------------------------------------------------------------------+
//| Configure master socket                                                    |
//+----------------------------------------------------------------------------+

int Server::configMaster() {
    
    /* Create TCP socket for handling incoming connections */
    int masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (masterSocket == -1) {
        std::cout << "[socket]:\t" << strerror(errno) << std::endl;
        return -1;
    }

    /* Fill parameters */
    struct sockaddr_in sAddr;
    bzero(&sAddr, sizeof(sAddr));
    sAddr.sin_family = AF_INET;
    sAddr.sin_port   = htons(port);
    int result = inet_pton(AF_INET, ip.c_str(), &(sAddr.sin_addr));

    /* Check result of parsing IP */
    if (result == 0) {
        printf("[configMaster]:\tIP address is not parseable\n");
        return -1;

    } else if (result == -1) {
        std::cout << "[inet_pton]:\t" << strerror(errno) << std::endl;
        return -1;
    }

    /* Set socket options */
    int optval = 1;
    setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, (void *)&optval, sizeof(optval));
    evutil_make_socket_nonblocking(masterSocket);

    /* Bind socket with specific parameters */
    result = bind(masterSocket, (struct sockaddr*)&sAddr, sizeof(sAddr));
    if (result == -1) {
        std::cout << "[bind]:\t" << strerror(errno) << std::endl;
        return -1;
    }
    
    /* Start listening socket */
    result = listen(masterSocket, SOMAXCONN);
    if (result == -1) {
        std::cout << "[listen]:\t" << strerror(errno) << std::endl;
        return -1;
    }

    return masterSocket;
}

//+----------------------------------------------------------------------------+
//| Send descriptor to selected worker                                         |
//+----------------------------------------------------------------------------+

void Server::sendDescriptor(int worker, int fd) {

    int  BUF_LEN = 1;
    char buf[BUF_LEN];

    /* Send fd in message */
    ssize_t       size;
    struct msghdr msg;
    struct iovec  iov;
    union {
        struct cmsghdr cmsghdr;
        char control[CMSG_SPACE(sizeof(int))];
    } cmsgu;
    struct cmsghdr *cmsg;

    iov.iov_base = buf;
    iov.iov_len  = BUF_LEN;

    msg.msg_name    = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov     = &iov;
    msg.msg_iovlen  = 1;

    msg.msg_control = cmsgu.control;
    msg.msg_controllen = sizeof(cmsgu.control);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;

    *((int *)CMSG_DATA(cmsg)) = fd;
    size = sendmsg(worker, &msg, 0);

    if (size == -1)
        std::cout << "[sendmsg]:\t" << strerror(errno) << std::endl;

    close(fd);
}

//+----------------------------------------------------------------------------+
//| Create worker process                                                      |
//+----------------------------------------------------------------------------+

int Server::createWorker(size_t i) {

    /* Create socket pair */
    int pair_fd[2];
    int result = socketpair(AF_UNIX, SOCK_DGRAM, 0, pair_fd);
    if (result == -1) {
        std::cout << "[socketPair]:\t" << strerror(errno) << std::endl;
        return -1;
    }

    /* Remember descriptor for communicating with worker */
    workers.push_back(std::make_pair(0, pair_fd[PARENT]));

    /* Fork process */
    pid_t pid = fork();

    if (pid == -1) {
        /* Error */
        std::cout << "[fork]:\t" << strerror(errno) << std::endl;
        return -1;

    } else if (pid == 0) {
        /* Child process */
        close(pair_fd[PARENT]);

        /* Create worker */
        Worker w(i + 1, pair_fd[CHILD], shmFilename, semFile);
        w.start();
        exit(1);

    } else {
        /* Server process */
        close(pair_fd[CHILD]);
        workers[i].first = pid;
    }

    return 0;
}

//+----------------------------------------------------------------------------+
//| Create cleaner process                                                     |
//+----------------------------------------------------------------------------+

int Server::createCleaner() {

    /* Fork process */
    pid_t pid = fork();

    if (pid == -1) {
        /* Error */
        std::cout << "[fork]:\t" << strerror(errno) << std::endl;
        return -1;

    } else if (pid == 0) {
        /* Create cleaner */
        Cleaner cl(shmFilename, semFile);
        cl.start();
        exit(1);

    } else {
        /* Server process */
        ttl_cleaner = pid;
    }

    return 0;
}

//+----------------------------------------------------------------------------+
//| Configure server                                                           |
//+----------------------------------------------------------------------------+

int Server::configure(int numWorkers) {

    /* Create hash table in shared memory */
    if (shm_unlink(shmFilename.c_str()) == -1)
        std::cout << "[shm_unlink]:\t" << strerror(errno) << std::endl;
    shmFile = shm_open(shmFilename.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (shmFile == -1) {
        std::cout << "[shm_open]:\t" << strerror(errno) << std::endl;
        return -1;
    }
    if (ftruncate(shmFile, MAX_CACHE_SIZE) == -1) {
        std::cout << "[ftruncate]:\t" << strerror(errno) << std::endl;
        return -1;
    }

    /* Create semaphore */
    if (sem_unlink(semFile.c_str()) == -1)
        std::cout << "[sem_unlink]:\t" << strerror(errno) << std::endl;
    semaphore = sem_open(semFile.c_str(), O_CREAT, S_IRUSR | S_IWUSR, 1);
    if (semaphore == SEM_FAILED) {
        std::cout << "[sem_open]:\t" << strerror(errno) << std::endl;
        return -1;
    }
    
    /* Create workers */
    for (size_t i = 0; i < numWorkers; ++i) {
        if (createWorker(i) == -1)
            return -1;  
    }

    /* Create cleaner */
    if (createCleaner() == -1)
        return -1; 
    
    /* Create TCP socket for handling incoming connections */
    master = configMaster();
    if (master == -1) {
        return -1;
    }

    /* Create event base */
    base = event_base_new();

    /* Create event for incoming connections */
    mainEvent = event_new(base, master, EV_READ | EV_PERSIST, accept_cb, (void *)this);
    event_add(mainEvent, nullptr);

    return 0;
}

//+----------------------------------------------------------------------------+
//| Main server event loop                                                     |
//+----------------------------------------------------------------------------+

void Server::start() {

    /* Start event loop */
    printf("[server]:\tstarted at %s:%d\n", ip.c_str(), port);
    event_base_dispatch(base);
}

//+----------------------------------------------------------------------------+
//| Add new client                                                             |
//+----------------------------------------------------------------------------+

void Server::acceptClient(int fd) {

    /* Select random worker */
    size_t id = rand() % workers.size();
    printf("[server]:\tadd client to worker #%d\n", id + 1);
    sendDescriptor(workers[id].second, fd);
}

//+----------------------------------------------------------------------------+
//| Accept callback                                                            |
//+----------------------------------------------------------------------------+

void accept_cb(evutil_socket_t evs, short events, void *ptr) {
    
    /* Last parameter is a server object */
    Server *srv = (Server *)ptr;

    /* Accept incoming connection */
    int fd = accept(evs, 0, 0);
    evutil_make_socket_nonblocking(fd);

    srv->acceptClient(fd);
}