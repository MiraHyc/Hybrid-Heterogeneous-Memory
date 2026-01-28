#ifndef _CONSTS_H_
#define _CONSTS_H_

constexpr int LOCAL_NUMA_NODE = 1; // 本地 NUMA 节点
constexpr int REMOTE_NUMA_NODE = 0; // 远端 NUMA 节点

// #define OLD_SMART

#define THREAD_NUMA_NODE LOCAL_NUMA_NODE          // 线程绑定 NUMA 节点 
#define NUMA_CPU_CORES   (16)                     // 一个 NUMA 节点的 CPU 核心数
#define ENABLE_NUMA_ALLOC                       // 是否启用 NUMA 分配

#ifdef OLD_SMART
#define RADIX_CACHE_NUMA_NODE LOCAL_NUMA_NODE    // 指定 Radix Cache 在哪个 NUMA 节点上
#define DSM_AND_RDMA_BUFFER_NUMA_NODE LOCAL_NUMA_NODE       // 指定 RDMA Buffer 在哪个 NUMA 节点上
#else
#define RADIX_CACHE_NUMA_NODE REMOTE_NUMA_NODE    // 指定 Radix Cache 在哪个 NUMA 节点上
#define DSM_AND_RDMA_BUFFER_NUMA_NODE REMOTE_NUMA_NODE       // 指定 RDMA Buffer 在哪个 NUMA 节点上
#define USE_LOCAL_CACHE                           // 是否使用本地缓存
#define LOCAL_CACHE_CONFIG_SIZE_MB 20                  // 本地缓存大小
#endif

// 是否启用 index cache，默认是在CMakelists里打开
// #define TREE_ENABLE_CACHE 


#endif // _CONSTS_H_