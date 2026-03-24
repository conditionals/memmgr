#pragma once

#include <functional>
#include <list>

class MemoryManager {
   private:
    unsigned _wordSize;
    unsigned _contiguous_mem_size;
    std::function<int(int, void*)> _allocator;
    void* _memBlock = nullptr;

    bool _isMemoryAllocated();

    struct Block {
        size_t offset;
        size_t length;
        bool isFree;
    };

    std::list<Block> _metadata;

   public:
    MemoryManager(unsigned wordSize, std::function<int(int, void*)> allocator);
    ~MemoryManager();
    void initialize(size_t sizeInWords);
    void shutdown();
    void* getList();
    void* allocate(size_t sizeInBytes);
    void free(void* address);
    void setAllocator(std::function<int(int, void*)> allocator);
    int dumpMemoryMap(char* filename);
    void* getBitmap();
    unsigned getWordSize();
    void* getMemoryStart();
    unsigned getMemoryLimit();
};

int bestFit(int sizeInWords, void* list);
int worstFit(int sizeInWords, void* list);