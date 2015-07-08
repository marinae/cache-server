#include "worker.h"

//+----------------------------------------------------------------------------+
//| Client destructor                                                          |
//+----------------------------------------------------------------------------+

Client::~Client() {
    
    if (readEvent) {
        event_free(readEvent);
        readEvent = nullptr;
    }
    if (writeEvent) {
        event_free(writeEvent);
        writeEvent = nullptr;
    }
}

//+----------------------------------------------------------------------------+
//| Worker destructor                                                          |
//+----------------------------------------------------------------------------+

Worker::~Worker() {

    /* Free memory and close sockets */
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        close(it->first);
        delete it->second;
    }

    event_free(mainEvent);
    event_base_free(base);

    close(serverFd);
    close(shmFile);

    if (shm_unlink(shmFilename.c_str()) == -1)
        std::cout << "[shm_unlink]:\t" << strerror(errno) << std::endl;

    if (sem_close(semaphore) == -1)
        std::cout << "[sem_close]:\t" << strerror(errno) << std::endl;

    delete hTable;
}

//+----------------------------------------------------------------------------+
//| Compose response to query                                                  |
//+----------------------------------------------------------------------------+

std::string Worker::composeResponse(std::string query) {

    std::string key;
    std::string value;
    int         ttl;

    /* Answer to compose */
    std::string answer;

    if (query.empty()) {
        answer = "error (empty query)\n";

    } else if (!CParser::parseLine(query, &key, &value, &ttl)) {

        /* Lock semaphore */
        if (sem_wait(semaphore) == -1) {
            std::cout << "[sem_wait]:\t" << strerror(errno) << std::endl;
            return "";
        }

        if (value == "" && ttl == 0) {
            /* Get key from hash table */
            answer = hTable->get(key);

        } else {
            /* Set in hash table */
            answer = hTable->set(ttl, key, value);
        }

        /* Unlock semaphore */
        if (sem_post(semaphore) == -1) {
            std::cout << "[sem_post]:\t" << strerror(errno) << std::endl;
            return "";
        }

    } else {
        /* Bad query */
        answer = "error (bad query)\n";
    }

    return answer;
}

//+----------------------------------------------------------------------------+
//| Start worker process                                                       |
//+----------------------------------------------------------------------------+

void Worker::start() {

    /* Open hash table */
    shmFile = shm_open(shmFilename.c_str(), O_RDWR);
    if (shmFile == -1) {
        std::cout << "[shm_open]:\t" << strerror(errno) << std::endl;
        return;
    }
    hTable = new CHashTable();
    if (hTable->allocate(shmFile) == -1)
        return;

    /* Open semaphore */
    semaphore = sem_open(semFile.c_str(), 0);
    if (semaphore == SEM_FAILED) {
        std::cout << "[sem_open]:\t" << strerror(errno) << std::endl;
        return;
    }

    /* Create event base */
    base = event_base_new();

    /* Create event for new clients */
    mainEvent = event_new(base, serverFd, EV_READ | EV_PERSIST, worker_cb, (void *)this);
    event_add(mainEvent, nullptr);

    /* Start event loop */
    printf("[worker #%d]:\tstarted\n", myID);
    event_base_dispatch(base);
}

//+----------------------------------------------------------------------------+
//| Add client to worker                                                       |
//+----------------------------------------------------------------------------+

void Worker::addClient(int fd) {

    assert(clients.find(fd) == clients.end());

    /* Create new event for client socket */
    struct event *ev;
    ev = event_new(base, fd, EV_READ | EV_PERSIST, read_cb, (void *)this);
    event_add(ev, nullptr);

    clients[fd] = new Client(ev, nullptr);
    printf("[worker #%d]:\tnew client (%d)\n", myID, fd);
}

//+----------------------------------------------------------------------------+
//| Close client connection                                                    |
//+----------------------------------------------------------------------------+

void Worker::closeClient(int fd) {

    assert(clients.find(fd) != clients.end());

    delete clients[fd];
    clients.erase(fd);
    close(fd);
    printf("[worker #%d]:\tclient (%d) closed\n", myID, fd);
}

//+----------------------------------------------------------------------------+
//| Receive descriptor of new client                                           |
//+----------------------------------------------------------------------------+

int Worker::receiveDescriptor(int parent) {

    int client_fd;
    ssize_t size;

    int  BUF_LEN = 1;
    char buf[BUF_LEN];

    struct msghdr msg;
    struct iovec  iov;
    union {
        struct cmsghdr cmsghdr;
        char control[CMSG_SPACE(sizeof(int))];
    } cmsgu;
    struct cmsghdr *cmsg;

    iov.iov_base = (void *)buf;
    iov.iov_len  = BUF_LEN;

    msg.msg_name    = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov     = &iov;
    msg.msg_iovlen  = 1;
    msg.msg_control = cmsgu.control;
    msg.msg_controllen = sizeof(cmsgu.control);

    size = recvmsg(parent, &msg, 0);
    if (size < 0) {
        std::cout << "[recvmsg]:\t" << strerror(errno) << std::endl;
        exit(1);
    }
    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
        if (cmsg->cmsg_level != SOL_SOCKET) {
            printf("[worker #%d]:\tinvalid cmsg_level %d\n", myID, cmsg->cmsg_level);
            exit(1);
        }
        if (cmsg->cmsg_type != SCM_RIGHTS) {
            printf("[worker #%d]:\tinvalid cmsg_type %d\n", myID, cmsg->cmsg_type);
            exit(1);
        }

        client_fd = *((int *)CMSG_DATA(cmsg));

    } else {
        client_fd = -1;
    }

    return client_fd;
}

//+----------------------------------------------------------------------------+
//| Add response to the client                                                 |
//+----------------------------------------------------------------------------+

void Worker::addResponse(int fd, std::string resp) {
    
    assert(!resp.empty());
    assert(clients.find(fd) != clients.end());

    clients[fd]->outBuf.append(resp);

    if (!clients[fd]->writeEvent) {
        /* Add write event */
        struct event *ev;
        ev = event_new(base, fd, EV_WRITE | EV_PERSIST, write_cb, (void *)this);
        event_add(ev, nullptr);

        clients[fd]->writeEvent = ev;
    }
}

//+----------------------------------------------------------------------------+
//| Get response to the client                                                 |
//+----------------------------------------------------------------------------+

std::string Worker::getResponse(int fd) {

    assert(clients.find(fd) != clients.end());

    std::string outBuf = clients[fd]->outBuf;
    clients[fd]->outBuf.clear();

    return outBuf;
}

//+----------------------------------------------------------------------------+
//| Remember partial query                                                     |
//+----------------------------------------------------------------------------+

void Worker::pushToInBuf(int fd, std::string str) {

    assert(!str.empty());
    assert(clients.find(fd) != clients.end());

    clients[fd]->inBuf.append(str);
    int pos = clients[fd]->inBuf.find('\n');

    /* Compose response */
    while (pos != std::string::npos) {

        std::string query = clients[fd]->inBuf.substr(0, pos);
        clients[fd]->inBuf = clients[fd]->inBuf.substr(pos + 1);
        addResponse(fd, composeResponse(query));

        pos = clients[fd]->inBuf.find('\n');
    }
}

//+----------------------------------------------------------------------------+
//| Answer client with string from out buffer                                  |
//+----------------------------------------------------------------------------+

void Worker::answer(int fd) {

    assert(clients.find(fd) != clients.end());
    assert(!clients[fd]->outBuf.empty());

    std::string resp = clients[fd]->outBuf;
    printf("[worker #%d]:\t%s", myID, resp.c_str());
    ssize_t sent = send(fd, resp.c_str(), resp.size() + 1, 0);

    if (sent > 0) {

        if (sent == resp.size() + 1) {
            /* Clear outBuf */
            clients[fd]->outBuf.clear();
            /* Remove write event for client */
            finishWriting(fd);

        } else {
            /* Decrease string size */
            clients[fd]->outBuf = clients[fd]->outBuf.substr(sent);
        }
    }
}

//+----------------------------------------------------------------------------+
//| Finish reading from client socket                                          |
//+----------------------------------------------------------------------------+

void Worker::finishReading(int fd) {
    
    assert(clients.find(fd) != clients.end());
    event_del(clients[fd]->readEvent);

    event_free(clients[fd]->readEvent);
    clients[fd]->readEvent = nullptr;

    if (!clients[fd]->writeEvent) {
        /* Close client */
        closeClient(fd);
    }
}

//+----------------------------------------------------------------------------+
//| Finish writing to client socket                                            |
//+----------------------------------------------------------------------------+

void Worker::finishWriting(int fd) {

    assert(clients.find(fd) != clients.end());
    event_del(clients[fd]->writeEvent);

    event_free(clients[fd]->writeEvent);
    clients[fd]->writeEvent = nullptr;

    if (!clients[fd]->readEvent) {
        /* Close client */
        closeClient(fd);
    }
}

//+----------------------------------------------------------------------------+
//| Accept connection in worker                                                |
//+----------------------------------------------------------------------------+

void worker_cb(evutil_socket_t evs, short events, void *ptr) {

    /* Last parameter is a worker object */
    Worker *wrk = (Worker *)ptr;

    /* Receive client descriptor */
    int client_fd = wrk->receiveDescriptor(evs);

    if (client_fd != -1) {
        /* Add client to worker process */
        wrk->addClient(client_fd);
    }
}

//+----------------------------------------------------------------------------+
//| Read callback                                                              |
//+----------------------------------------------------------------------------+

void read_cb(evutil_socket_t evs, short events, void *ptr) {

    /* Last parameter is a worker object */
    Worker *wrk = (Worker *)ptr;

    /* Read from socket */
    char buf[BUF_SIZE];
    ssize_t len = recv(evs, buf, BUF_SIZE, 0);

    if (len == -1) {
        /* Error */
        std::cout << "[recv]:\t" << strerror(errno) << std::endl;
        wrk->closeClient(evs);

    } else if (len == 0) {
        /* Close connection */
        wrk->finishReading(evs);

    } else {
        /* Push string to in buffer */
        wrk->pushToInBuf(evs, std::string(buf, len));
    }
}

//+----------------------------------------------------------------------------+
//| Write callback                                                             |
//+----------------------------------------------------------------------------+

void write_cb(evutil_socket_t evs, short events, void *ptr) {

    /* Last parameter is a worker object */
    Worker *wrk = (Worker *)ptr;

    /* Answer with a string from outBuf */
    wrk->answer(evs);
}