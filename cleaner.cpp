#include "cleaner.h"

//+----------------------------------------------------------------------------+
//| Cleaner destructor                                                         |
//+----------------------------------------------------------------------------+

Cleaner::~Cleaner() {

    close(shmFile);
    if (shm_unlink(shmFilename.c_str()) == -1)
        std::cout << "[shm_unlink]:\t" << strerror(errno) << std::endl;

    if (sem_close(semaphore) == -1)
        std::cout << "[sem_close]:\t" << strerror(errno) << std::endl;

    delete hTable;
}

//+----------------------------------------------------------------------------+
//| Start cleaner process                                                      |
//+----------------------------------------------------------------------------+

void Cleaner::start() {

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

    /* Run cleaner */
    while (true) {

        /* Lock semaphore */
        if (sem_wait(semaphore) == -1) {
            std::cout << "[sem_wait]:\t" << strerror(errno) << std::endl;
            return;
        }

        hTable->checkTTL();

        /* Unlock semaphore */
        if (sem_post(semaphore) == -1) {
            std::cout << "[sem_post]:\t" << strerror(errno) << std::endl;
            return;
        }

        sleep(1);
    }
}