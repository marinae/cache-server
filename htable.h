#ifndef __HTABLE_H__
#define __HTABLE_H__

//#define _DEBUG_MODE_

#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string>
#include <cstring>
#include <iostream>

const size_t MAX_KEY_SIZE   = 32;
const size_t MAX_VALUE_SIZE = 256;
const size_t MAX_CACHE_SIZE = 1024 * 1024;

//+----------------------------------------------------------------------------+
//| Hash table class                                                           |
//+----------------------------------------------------------------------------+

class CHashTable {
    /* Shared mem */
    int shmFile;

    /* Config */
    size_t cacheSize;
    size_t keySize;
    size_t valueSize;
    size_t entrySize;
    size_t tableSize;

    /* Hash table */
    void                   *hTable;
    std::hash<std::string> hashFunc;

    /* Private API */
    size_t findPlace(std::string key);
    size_t findEntry(std::string key);

public:
    
    CHashTable(size_t cacheSize = MAX_CACHE_SIZE,
               size_t keySize   = MAX_KEY_SIZE,
               size_t valueSize = MAX_VALUE_SIZE);
    ~CHashTable();

    int         allocate(int shmFile);
    void        checkTTL();
    std::string get(std::string key);
    std::string set(int ttl, std::string key, std::string value);
};

#endif /* __HTABLE_H__ */