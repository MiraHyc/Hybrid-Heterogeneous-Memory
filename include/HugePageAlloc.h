#ifndef __HUGEPAGEALLOC_H__
#define __HUGEPAGEALLOC_H__


#include "Debug.h"

#include <cstdint>

#include <sys/mman.h>
#include <memory.h>

// added by pz
#include <numa.h>
#include <numaif.h>
#include <iostream>
#include "consts.h"

static size_t my_sum_size = 0;

char *getIP();
inline void *hugePageAlloc(size_t size) {
    // 1. 分配内存（不立即提交物理页）
    // void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
    //                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_NORESERVE,
    //                 -1, 0);
    // if (ptr == MAP_FAILED) return MAP_FAIL ED;
#ifdef ENABLE_NUMA_ALLOC
    void *ptr = numa_alloc_onnode(size, DSM_AND_RDMA_BUFFER_NUMA_NODE);
#else
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_NORESERVE,
                    -1, 0);
    if (ptr == MAP_FAILED) return MAP_FAILED;
#endif

    // // 2. 定义 NUMA 节点掩码
    // struct bitmask *nodemask = numa_allocate_nodemask();
    // numa_bitmask_setbit(nodemask, numa_node);

    // // 3. 绑定内存区域到节点（MPOL_BIND + MPOL_MF_STRICT）
    // if (mbind(ptr, size, MPOL_PREFERRED, nodemask->maskp, numa_max_node() + 1, MPOL_MF_STRICT) == -1) {
    //     Debug::notifyError("mbind failed: %s", strerror(errno));
    //     munmap(ptr, size);
    //     ptr = MAP_FAILED;
    // }

    // numa_free_nodemask(nodemask);
    return ptr;
    // const int numa_node = 1;

    // // 验证NUMA节点有效性
    // if(numa_node < 0 || numa_node >= numa_max_node() + 1){
    //     Debug::notifyError("Invalid NUMA node %d\n", numa_node);
    //     return MAP_FAILED;
    // }

    // // 创建NUMA节点掩码 node_mask = 0000
    // struct bitmask *node_mask = numa_allocate_nodemask();
    // // 在掩码中启用指定的NUMA节点 node_mask |= (1 << (numa_node - 1))
    // numa_bitmask_setbit(node_mask, numa_node);

    // // 设置内存绑定策略
    // if(set_mempolicy(MPOL_BIND, node_mask->maskp, node_mask->size + 1) == -1){
    //     Debug::notifyError("set_mempolicy failed: %s\n", strerror(errno));
    //     numa_free_nodemask(node_mask);
    //     return MAP_FAILED;
    // }

    // // void *p = numa_alloc_onnode(size, numa_node);

    // void *res = mmap(NULL, size, PROT_READ | PROT_WRITE,
    //                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    // my_sum_size += size;
    // std::cout << "sum_size: " << my_sum_size << std::endl;
    
    // // 恢复默认策略
    // set_mempolicy(MPOL_DEFAULT, NULL, 0);
    // numa_free_nodemask(node_mask);

    // if (res == MAP_FAILED) {
    //    // Debug::notifyError("%s mmap failed!\n", getIP());
    //     Debug::notifyError("%s mmap failed with error: %d (%s)\n", getIP(), errno, strerror(errno));
    // }

    // return res;
}

inline void hugePageFree(void *addr, size_t size) {
    int res = munmap(addr, size);
    if (res == -1) {
        Debug::notifyError("%s munmap failed! %d\n", getIP(), errno);
    }
    return;
}

#endif /* __HUGEPAGEALLOC_H__ */
