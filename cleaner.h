#ifndef __CLEANER_H__
#define __CLEANER_H__

#include "htable.h"
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h> /*  close, sleep */

//+----------------------------------------------------------------------------+
//| Cleaner class                                                              |
//+----------------------------------------------------------------------------+

class Cleaner {
    /* Shared memory */
    std::string shmFilename;
    int         shmFile;
    CHashTable  *hTable;

    /* Semaphore */
    std::string semFile;
    sem_t       *semaphore;

public:
    Cleaner(std::string shmFilename, std::string semFile)
        : shmFilename(shmFilename), semFile(semFile) {}
    ~Cleaner();

    void start();
};

#endif /* __CLEANER_H__ */