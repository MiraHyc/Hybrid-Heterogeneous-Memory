#ifndef _NUMAOBJECTPOOL_H_
#define _NUMAOBJECTPOOL_H_

#include "SpecialPrint.h"

#include <tbb/concurrent_queue.h>

template<typename T>
class NumaObjectPool {
private:
    std::vector<T*> blocks;
    std::atomic<int> cur_idx;
    int capacity;
    int node;
    std::atomic<int> release_cnt;

    tbb::concurrent_queue<T*> freelist;

public:
    NumaObjectPool(int total_objects, int numa_node)
        : capacity(total_objects), cur_idx(0), node(numa_node), release_cnt(0) {
        SpecialPrint::blue("NumaObjectPool used %d MB.\n", total_objects * sizeof(T) / (1024 * 1024));
        T* block = (T*)numa_alloc_onnode(sizeof(T) * total_objects, numa_node);
        if (!block) {
            fprintf(stderr, "numa_alloc_onnode failed\n");
            std::abort();
        }
        blocks.push_back(block);
    }

    ~NumaObjectPool() {
        for (T* block : blocks) {
            numa_free(block, sizeof(T) * capacity);
        }
    }

    template<typename... Args>
    T* allocate(Args&&... args) {
        // 优先复用空闲对象
        T* reused = nullptr;
        if (freelist.try_pop(reused)) {
            return new (reused) T(std::forward<Args>(args)...);
        }

        // 否则分配新对象
        int idx = cur_idx.fetch_add(1);
        if (idx >= capacity) {
            fprintf(stderr, "NumaObjectPool exhausted\n");
            std::abort();
        }

        uint8_t* base = reinterpret_cast<uint8_t*>(blocks[0]);
        T* addr = reinterpret_cast<T*>(base + idx * sizeof(T));

        if ((uintptr_t)addr % alignof(T) != 0) {
            fprintf(stderr, "Misaligned allocation: addr=%p\n", addr);
            std::abort();
        }

        return new (addr) T(std::forward<Args>(args)...);
    }

    void release(T* ptr) {
        ptr->~T();
        freelist.push(ptr);
    }
};




#endif // _NUMAOBJECTPOOL_H_