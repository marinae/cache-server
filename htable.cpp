#include "htable.h"

//+----------------------------------------------------------------------------+
//| Hash table class constructor                                               |
//+----------------------------------------------------------------------------+

CHashTable::CHashTable(size_t cacheSize, size_t keySize, size_t valueSize)
    : shmFile(-1),
      hTable(nullptr),
      cacheSize(cacheSize),
      keySize(keySize),
      valueSize(valueSize) {

    entrySize = 2 * sizeof(bool) + (keySize + 1) + (valueSize + 1) + sizeof(size_t);
    tableSize = cacheSize / entrySize;

    #ifdef _DEBUG_MODE_
    printf("Entry size = %lu, max entries = %lu\n", entrySize, tableSize);
    #endif /* _DEBUG_MODE_ */
}

//+----------------------------------------------------------------------------+
//| Hash table class destructor                                                |
//+----------------------------------------------------------------------------+

CHashTable::~CHashTable() {

    if (munmap(nullptr, MAX_CACHE_SIZE) == -1)
        std::cout << "[munmap]:\t" << strerror(errno) << std::endl;
}

//+----------------------------------------------------------------------------+
//| Map shared memory                                                          |
//+----------------------------------------------------------------------------+

int CHashTable::allocate(int shmFile) {

    hTable = mmap(nullptr, MAX_CACHE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shmFile, 0);
    if (!hTable) {
        std::cout << "[mmap]:\t" << strerror(errno) << std::endl;
        return -1;
    }
    return 0;
}

//+----------------------------------------------------------------------------+
//| Get hash table config                                                      |
//+----------------------------------------------------------------------------+

void CHashTable::checkTTL() {

    for (size_t i = 0; i < tableSize; ++i) {

        /* Fill pointers */
        void *entry  = static_cast<char *>(hTable) + i * entrySize;
        bool *isBusy = static_cast<bool *>(entry);
        bool *rip    = static_cast<bool *>(entry) + 1;
        int  *pTTL   = static_cast<int *>(entry) + 2 + (keySize + 1) + (valueSize + 1);

        /* Check entry */
        if ((*isBusy) && !(*rip)) {

            #ifdef _DEBUG_MODE_
            printf("[entry #%lu]:\tTTL = %d\n", i, *pTTL);
            #endif /* _DEBUG_MODE_ */

            if (*pTTL == 0) {
                /* Mark entry as RIP */
                bool ripValue = true;
                memcpy(rip, &ripValue, sizeof(ripValue));

            } else {
                /* Decrement TTL */
                int ttl = *(pTTL) - 1;
                memcpy(pTTL, &ttl, sizeof(ttl));
            }
        }
    }
}

//+----------------------------------------------------------------------------+
//| Find place for key                                                         |
//+----------------------------------------------------------------------------+

size_t CHashTable::findPlace(std::string key) {

    size_t index = hashFunc(key) % tableSize;
    /* Where key is supposed to be */
    void *entry = static_cast<char *>(hTable) + index * entrySize;
    bool isBusy = *static_cast<bool *>(entry);
    bool rip = *(static_cast<bool *>(entry) + 1);

    /* Check entry */
    size_t nextIndex = index;
    while (isBusy || rip) {
        nextIndex = (nextIndex < tableSize - 1) ? nextIndex + 1 : 0;
        if (nextIndex == index) {
            /* No empty cells in hash table */
            nextIndex = tableSize;
            break;
        } else {
            /* Read next cell */
            entry = static_cast<char *>(hTable) + nextIndex * entrySize;
            isBusy = *static_cast<bool *>(entry);
            rip = *(static_cast<bool *>(entry) + 1);
        }
    }

    return nextIndex;
}

//+----------------------------------------------------------------------------+
//| Find entry containing key                                                  |
//+----------------------------------------------------------------------------+

size_t CHashTable::findEntry(std::string key) {

    size_t index = hashFunc(key) % tableSize;
    /* Where key is supposed to be */
    void *entry;
    bool isBusy = 1;
    bool rip = 1;

    /* Check entry */
    size_t nextIndex = index;
    while (isBusy || rip) {
        /* Read next cell */
        entry = static_cast<char *>(hTable) + nextIndex * entrySize;
        isBusy = *static_cast<bool *>(entry);
        rip = *(static_cast<bool *>(entry) + 1);
        /* Check cell */
        if (!isBusy && !rip) {
            /* No such key in hash table */
            nextIndex = tableSize;
            break;
        }
        if (!rip) {
            /* Check key */
            char *pKey = static_cast<char *>(entry) + 2;
            if (strncmp(pKey, key.c_str(), key.size()) == 0) {
                /* Key found in hash table */
                break;
            }
        }
        nextIndex = (nextIndex < tableSize - 1) ? nextIndex + 1 : 0;
        if (nextIndex == index) {
            /* No such key in hash table */
            nextIndex = tableSize;
            break;
        }
    }

    return nextIndex;
}

//+----------------------------------------------------------------------------+
//| Get value for key                                                          |
//+----------------------------------------------------------------------------+

std::string CHashTable::get(std::string key) {

    if (key.size() >= keySize)
        return std::string("error (too big key)\n");

    size_t index = findEntry(key);
    if (index == tableSize) {

        #ifdef _DEBUG_MODE_
        printf("> Get failed:\t[%s]\n", key.c_str());
        #endif /* _DEBUG_MODE_ */

        return std::string("error (key doesn't exist)\n");
    }

    /* Fill pointers */
    void *cell   = static_cast<char *>(hTable) + index * entrySize;
    char *pValue = static_cast<char *>(cell) + 2 + (keySize + 1);
    int  *pTTL   = static_cast<int *>(cell) + 2 + (keySize + 1) + (valueSize + 1);

    /* Get values */
    std::string value(pValue);
    int ttl = *pTTL;

    #ifdef _DEBUG_MODE_
    printf("> Get %lu:\t[%d, %s, %s]\n", index, ttl, key.c_str(), value.c_str());
    #endif /* _DEBUG_MODE_ */

    return std::string("ok ") + std::string(key.c_str()) + std::string(" ") +
           std::string(value.c_str()) + std::string("\n");
}

//+----------------------------------------------------------------------------+
//| Set value and TTL for key                                                  |
//+----------------------------------------------------------------------------+

std::string CHashTable::set(int ttl, std::string key, std::string value) {

    if (key.size() >= keySize)
        return std::string("error (too big key)\n");

    if (value.size() >= valueSize)
        return std::string("error (too big value)\n");

    if (ttl <= 0)
        return std::string("error (TTL is less than 1)\n");

    size_t index = findEntry(key);
    if (index != tableSize) {
        /* Key already exists */
        void *emptyCell = static_cast<char *>(hTable) + index * entrySize;
        char *pValue    = static_cast<char *>(emptyCell) + 2 + (keySize + 1);
        int  *pTTL      = static_cast<int *>(emptyCell) + 2 + (keySize + 1) + (valueSize + 1);

        /* Fill empty cell */
        strncpy(pValue, value.c_str(), value.size() + 1);
        memcpy(pTTL, &ttl, sizeof(ttl));

        #ifdef _DEBUG_MODE_
        printf("Set %lu:\t[%s, %s, %d] (replacing)\n", index, key.c_str(), value.c_str(), ttl);
        #endif /* _DEBUG_MODE_ */

        return std::string("ok ") + std::string(key.c_str()) + std::string(" ") +
               std::string(value.c_str()) + std::string("\n");
    }

    index = findPlace(key);
    if (index == tableSize) {

        #ifdef _DEBUG_MODE_
        printf("Set failed:\t[%s, %s, %d] (no memory)\n", key.c_str(), value.c_str(), ttl);
        #endif /* _DEBUG_MODE_ */

        return std::string("error (no empty cells)\n");
    }

    /* Fill pointers */
    void *emptyCell = static_cast<char *>(hTable) + index * entrySize;
    bool *isBusy    = static_cast<bool *>(emptyCell);
    char *pKey      = static_cast<char *>(emptyCell) + 2;
    char *pValue    = static_cast<char *>(emptyCell) + 2 + (keySize + 1);
    int  *pTTL      = static_cast<int *>(emptyCell) + 2 + (keySize + 1) + (valueSize + 1);

    /* Fill empty cell */
    memset(isBusy, 1, 1);
    strncpy(pKey, key.c_str(), key.size() + 1);
    strncpy(pValue, value.c_str(), value.size() + 1);
    memcpy(pTTL, &ttl, sizeof(ttl));

    #ifdef _DEBUG_MODE_
    printf("Set %lu:\t[%s, %s, %d]\n", index, key.c_str(), value.c_str(), ttl);
    #endif /* _DEBUG_MODE_ */

    return std::string("ok ") + std::string(key.c_str()) + std::string(" ") +
           std::string(value.c_str()) + std::string("\n");
}