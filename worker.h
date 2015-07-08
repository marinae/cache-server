#ifndef __WORKER_H__
#define __WORKER_H__

#include "parser.h"
#include "htable.h"
#include <assert.h>
#include <event.h>
#include <unistd.h> /* close */
#include <semaphore.h>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <utility>

static const int BUF_SIZE = 1024;

//+----------------------------------------------------------------------------+
//| Client class                                                               |
//+----------------------------------------------------------------------------+

class Client {
public:
    /* Events */
    struct event *readEvent;
    struct event *writeEvent;

    /* In/out buffers */
    std::string inBuf;
    std::string outBuf;

    Client();
    Client(struct event *readEv, struct event *writeEv) :
        readEvent(readEv),
        writeEvent(writeEv) {}
    ~Client();
};

typedef std::unordered_map<int, Client *> ServClients;

//+----------------------------------------------------------------------------+
//| Worker class                                                               |
//+----------------------------------------------------------------------------+

class Worker {
    struct event_base *base;
    struct event  *mainEvent;
    ServClients   clients;
    int           serverFd;
    int           myID;

    /* Shared memory */
    std::string   shmFilename;
    int           shmFile;
    CHashTable    *hTable;
    std::string   semFile;
    sem_t         *semaphore;

    std::string composeResponse(std::string query);

public:
    Worker(int id, int fd, std::string shm, std::string sem)
        : myID(id), serverFd(fd), shmFilename(shm), semFile(sem) {}
    ~Worker();

    /* Worker methods */
    void start();
    void addClient(int fd);
    void closeClient(int fd);
    int  receiveDescriptor(int parent);

    /* Add and get response (= out buffer) */
    void        addResponse(int fd, std::string resp);
    std::string getResponse(int fd);

    /* Push query (= in buffer) */
    void pushToInBuf(int fd, std::string str);

    /* Answer to client */
    void answer(int fd);

    /* Remove events */
    void finishReading(int fd);
    void finishWriting(int fd);
};

typedef std::vector<std::pair<pid_t, int>> ServWorkers;

//+----------------------------------------------------------------------------+
//| Callbacks                                                                  |
//+----------------------------------------------------------------------------+

void worker_cb(evutil_socket_t evs, short events, void *ptr);
void read_cb  (evutil_socket_t evs, short events, void *ptr);
void write_cb (evutil_socket_t evs, short events, void *ptr);

#endif /* __WORKER_H__ */