#include "MemoryManager.h"

#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include <climits>
#include <string>

MemoryManager::MemoryManager(unsigned wordSize,
                             std::function<int(int, void*)> allocator)
    : _wordSize(wordSize), _allocator(allocator) {}

MemoryManager::~MemoryManager() { shutdown(); }

void MemoryManager::initialize(size_t sizeInWords) {
    // clear memory if already allocated
    shutdown();

    _contiguous_mem_size = sizeInWords * _wordSize;

    _memBlock = new uint8_t[_contiguous_mem_size];

    Block base;
    base.offset = 0;
    base.length = sizeInWords;
    base.isFree = true;

    _metadata.push_back(base);
}

void MemoryManager::shutdown() {
    if (_isMemoryAllocated()) {
        delete[] static_cast<uint8_t*>(_memBlock);
        _memBlock = nullptr;
    }

    _metadata.clear();
}

void* MemoryManager::getList() {
    int num_blocks = 0;
    // only include holes
    for (const Block& b : _metadata) {
        if (b.isFree) num_blocks++;
    }

    if (num_blocks == 0) {
        return nullptr;
    }

    int arraySize = 1 + (num_blocks * 2);
    uint16_t* data = new uint16_t[arraySize];

    data[0] = num_blocks;

    // skip first element, as that contains size
    int i = 1;
    for (const Block& block : _metadata) {
        if (!block.isFree) continue;

        data[i++] = static_cast<uint16_t>(block.offset);
        data[i++] = static_cast<uint16_t>(block.length);
    }

    return data;
}

void* MemoryManager::allocate(size_t sizeInBytes) {
    if (sizeInBytes == 0) return nullptr;

    int sizeInWords = 1 + ((sizeInBytes - 1) / _wordSize);

    uint16_t* list = static_cast<uint16_t*>(getList());

    int wordOffset = _allocator(sizeInWords, list);

    if (list != nullptr) {
        delete[] list;
    }

    if (wordOffset == -1) return nullptr;

    for (auto it = _metadata.begin(); it != _metadata.end(); it++) {
        int length = static_cast<int>(it->length),
            offset = static_cast<int>(it->offset);

        if (it->isFree && offset == wordOffset) {
            if (length == sizeInWords) {
                it->isFree = false;
            } else {
                Block addtl_space_blk;
                addtl_space_blk.offset = offset + sizeInWords;
                addtl_space_blk.length = length - sizeInWords;
                addtl_space_blk.isFree = true;

                it->length = sizeInWords;
                it->isFree = false;

                _metadata.insert(std::next(it), addtl_space_blk);
            }

            // return a pointer to our base block of memory + the calculated
            // offset
            return static_cast<void*>(static_cast<uint8_t*>(_memBlock) +
                                      (wordOffset * _wordSize));
        }
    }

    // IF NO MEM AVAILABLE OR SIZE IS INVALID
    return nullptr;
}

void MemoryManager::free(void* address) {
    // in order to free the block, we must first find it's offset
    // we can do this by checking the metadata for a reserved block
    // where it's offset is equal to our calculated word offset.
    // take our current address and substract the base mem block.
    // from there, we know that our address will be pointing at the beginning of
    // some block with that same offset (although we do not validate this).
    int wordOffset =
        (static_cast<uint8_t*>(address) - static_cast<uint8_t*>(_memBlock)) /
        _wordSize;

    // we can't do Block& b : _metadata because we're going to need the iterator
    for (auto it = _metadata.begin(); it != _metadata.end(); it++) {
        if (!it->isFree && static_cast<int>(it->offset) == wordOffset) {
            it->isFree = true;

            // next, we should check to see if we can combine neighboring blocks
            // of memory
            auto nextBlock = std::next(it);
            if (nextBlock->isFree) {
                it->length += nextBlock->length;
                _metadata.erase(nextBlock);
            }

            if (it != _metadata.begin()) {
                auto prevBlock = std::prev(it);
                if (prevBlock->isFree) {
                    prevBlock->length += it->length;
                    _metadata.erase(it);
                }
            }

            return;
        }
    }
}

void MemoryManager::setAllocator(std::function<int(int, void*)> allocator) {
    _allocator = allocator;
}

int MemoryManager::dumpMemoryMap(char* filename) {
    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC);
    if (fd == -1) return -1;

    std::string out;
    bool first = true;

    for (const Block& b : _metadata) {
        if (!b.isFree) continue;

        if (!first) {
            out += " - ";
        }

        out += "[" + std::to_string(b.offset) + ", " +
               std::to_string(b.length) + "]";

        first = false;
    }

    int res = write(fd, out.c_str(), out.size());
    close(fd);

    return res != -1 ? 0 : -1;
}

void* MemoryManager::getBitmap() {
    int num_words = _contiguous_mem_size / _wordSize;
    int bitmap_words_size = (num_words + 7) / 8;
    int array_size = 2 + bitmap_words_size;

    uint8_t* bitmap = new uint8_t[array_size]();

    // split into 2-byte integer
    uint16_t wideSize = bitmap_words_size;
    bitmap[0] = static_cast<uint8_t>(wideSize & 0xFF);
    bitmap[1] = static_cast<uint8_t>(wideSize >> 8 & 0xFF);

    for (const Block& b : _metadata) {
        if (b.isFree) continue;

        // calculate place in bitmap
        // once we have that loop len as well
        for (int i = b.offset; i < b.offset + b.length; i++) {
            int byte_index = i / 8;
            int bit_index = i % 8;

            bitmap[2 + byte_index] |= (1 << bit_index);
        }
    }

    return bitmap;
};

unsigned MemoryManager::getWordSize() { return _wordSize; }
void* MemoryManager::getMemoryStart() { return _memBlock; }
unsigned MemoryManager::getMemoryLimit() { return _contiguous_mem_size; }

// helper functions
bool MemoryManager::_isMemoryAllocated() {
    return static_cast<uint8_t*>(_memBlock) != nullptr;
}

// allocator functions
int bestFit(int sizeInWords, void* list) {
    if (list == nullptr) {
        return -1;
    }

    uint16_t* holes = static_cast<uint16_t*>(list);
    int list_size = holes[0];

    int bestOffset = -1;
    int bestSize = INT_MAX;

    int index = 1;
    for (int i = 0; i < list_size; i++) {
        int offset = holes[index++];
        int length = holes[index++];

        if (length >= sizeInWords && length < bestSize) {
            bestSize = length;
            bestOffset = offset;
        }
    }

    return bestOffset;
}

int worstFit(int sizeInWords, void* list) {
    if (list == nullptr) {
        return -1;
    }

    uint16_t* holes = static_cast<uint16_t*>(list);
    int list_size = holes[0];

    int worstOffset = -1;
    int worstSize = -1;

    int index = 1;
    for (int i = 0; i < list_size; i++) {
        int offset = holes[index++];
        int length = holes[index++];

        if (length >= sizeInWords && length > worstSize) {
            worstSize = length;
            worstOffset = offset;
        }
    }

    return worstOffset;
}