#ifndef __SERVER_H__
#define __SERVER_H__

#include "worker.h"
#include "cleaner.h"
#include <assert.h>
#include <arpa/inet.h> /* inet_pton */
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <iostream>
#include <vector>

static const std::string SHM_FILE     = "shared_ht";
static const std::string SEM_FILE     = "mycache_sem";
static const std::string DEFAULT_IP   = "127.0.0.1";
static const uint16_t    DEFAULT_PORT = 8080;
static const int         NUM_WORKERS  = 4;
static const int         PARENT       = 0;
static const int         CHILD        = 1;

//+----------------------------------------------------------------------------+
//| Server class                                                               |
//+----------------------------------------------------------------------------+

class Server {
	struct event_base *base;
	struct event  *mainEvent;
	int           master;
	std::string   ip;
	uint16_t      port;
	ServWorkers   workers;

	/* Shared memory */
	std::string   shmFilename;
	int           shmFile;
	sem_t         *semaphore;
	std::string   semFile;

	/* Cleaner process ID */
	pid_t         ttl_cleaner;

	int  configMaster();
	void sendDescriptor(int worker, int fd);
	int  createWorker(size_t i);
	int  createCleaner();

public:
    Server(std::string ip         = DEFAULT_IP,
    	   uint16_t    port       = DEFAULT_PORT,
    	   std::string shm        = SHM_FILE,
    	   std::string sem        = SEM_FILE);
    ~Server();

    /* Server methods */
    int  configure(int numWorkers = NUM_WORKERS);
	void start();
	void acceptClient(int fd);
};

//+----------------------------------------------------------------------------+
//| Callbacks                                                                  |
//+----------------------------------------------------------------------------+

void accept_cb(evutil_socket_t evs, short events, void *ptr);

#endif /* __SERVER_H__ */