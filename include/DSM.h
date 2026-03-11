#ifndef __DSM_H__
#define __DSM_H__

#include <atomic>

#include "RdmaCache.h"
#include "Config.h"
#include "Connection.h"
#include "DSMKeeper.h"
#include "GlobalAddress.h"
#include "LocalAllocator.h"
#include "RdmaBuffer.h"
#include "Common.h"


class DSMKeeper;
class Directory;

/**
 * DSM (Distributed Shared Memory) 类
 * 基于RDMA实现跨节点的分布式共享内存，支持原子操作和高效内存访问
 */
class DSM {

public:
  // 核心接口方法
  // -------------------- 初始化与配置 --------------------
  void registerThread(); // 注册当前线程，分配线程本地资源（如RDMA缓冲区）
  void loadKeySpace(const std::string& load_workloads_path, bool is_str); // 从文件加载键空间（测试用）
  Key getRandomKey(); // 生成随机键（负载测试）
  Key getNoComflictKey(uint64_t key_hash, uint64_t global_thread_id, uint64_t global_thread_num); // 生成无冲突键（并发控制）
  static DSM *getInstance(const DSMConfig &conf); // 单例模式获取DSM实例

  // -------------------- 节点信息获取 --------------------
  uint16_t getMyNodeID() { return myNodeID; } // 获取当前节点ID
  uint16_t getMyThreadID() { return thread_id; } // 获取当前线程ID
  uint16_t getClusterSize() { return conf.machineNR; } // 集群节点数 
  uint64_t getThreadTag() { return thread_tag; } // 唯一线程标识符（用于操作追踪）

  // RDMA operations
  // buffer is registered memory

  // -------------------- RDMA基础操作 --------------------

  // 异步读（signal表示是否触发完成事件，ctx用于协程调度）
  void read(char *buffer, GlobalAddress gaddr, size_t size, bool signal = true,
            CoroContext *ctx = nullptr);
  // 同步读（阻塞直到完成）
  void read_sync(char *buffer, GlobalAddress gaddr, size_t size,
                 CoroContext *ctx = nullptr);

  // 异步写
  void write(const char *buffer, GlobalAddress gaddr, size_t size,
             bool signal = true, CoroContext *ctx = nullptr);
  // 同步写
  void write_sync(const char *buffer, GlobalAddress gaddr, size_t size,
                  CoroContext *ctx = nullptr);

  // -------------------- 批量操作 --------------------


  // 批量异步读（k个操作）
  void read_batch(RdmaOpRegion *rs, int k, bool signal = true,
                  CoroContext *ctx = nullptr);
  // 批量同步读
  void read_batch_sync(RdmaOpRegion *rs, int k, CoroContext *ctx = nullptr);
  // 多节点批量读优化版本
  void read_batches_sync(const std::vector<RdmaOpRegion>& rs, CoroContext *ctx = nullptr, int coro_id = 0);

  // 批量异步写
  void write_batch(RdmaOpRegion *rs, int k, bool signal = true,
                   CoroContext *ctx = nullptr);
  void write_batch_sync(RdmaOpRegion *rs, int k, CoroContext *ctx = nullptr);
  // 多节点批量写优化
  void write_batches_sync(RdmaOpRegion *rs, int k, CoroContext *ctx = nullptr, int coro_id = 0);

  // -------------------- 原子操作组合 --------------------

  // 写后原子加（Fetch-and-Add）
  void write_faa(RdmaOpRegion &write_ror, RdmaOpRegion &faa_ror,
                 uint64_t add_val, bool signal = true,
                 CoroContext *ctx = nullptr);
  void write_faa_sync(RdmaOpRegion &write_ror, RdmaOpRegion &faa_ror,
                      uint64_t add_val, CoroContext *ctx = nullptr);

  // 写后原子比较交换（Compare-and-Swap）
  void write_cas(RdmaOpRegion &write_ror, RdmaOpRegion &cas_ror,
                 uint64_t equal, uint64_t val, bool signal = true,
                 CoroContext *ctx = nullptr);
  void write_cas_sync(RdmaOpRegion &write_ror, RdmaOpRegion &cas_ror,
                      uint64_t equal, uint64_t val, CoroContext *ctx = nullptr);

  // 带掩码的CAS（部分位比较）
  void write_cas_mask(RdmaOpRegion &write_ror, RdmaOpRegion &cas_ror,
                      uint64_t equal, uint64_t val, uint64_t mask, bool signal = true,
                      CoroContext *ctx = nullptr);
  void write_cas_mask_sync(RdmaOpRegion &write_ror, RdmaOpRegion &cas_ror,
                           uint64_t equal, uint64_t val, uint64_t mask, CoroContext *ctx = nullptr);

  // -------------------- 基础原子操作 --------------------

  // 原子比较交换
  void cas(GlobalAddress gaddr, uint64_t equal, uint64_t val,
           uint64_t *rdma_buffer, bool signal = true,
           CoroContext *ctx = nullptr);
  bool cas_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                uint64_t *rdma_buffer, CoroContext *ctx = nullptr);

  // 原子加（Fetch-and-Add）
  void faa(GlobalAddress gaddr, int64_t add_val, uint64_t *rdma_buffer,
           bool signal = true, CoroContext *ctx = nullptr);
  void faa_sync(GlobalAddress gaddr, int64_t add_val, uint64_t *rdma_buffer,
                CoroContext *ctx = nullptr);

  // CAS成功后读
  void cas_read(RdmaOpRegion &cas_ror, RdmaOpRegion &read_ror, uint64_t equal,
                uint64_t val, bool signal = true, CoroContext *ctx = nullptr);
  bool cas_read_sync(RdmaOpRegion &cas_ror, RdmaOpRegion &read_ror,
                     uint64_t equal, uint64_t val, CoroContext *ctx = nullptr);

  // 读后CAS
  void read_cas(RdmaOpRegion &read_ror, RdmaOpRegion &cas_ror, uint64_t equal,
                uint64_t val, bool signal = true, CoroContext *ctx = nullptr);
  bool read_cas_sync(RdmaOpRegion &read_ror, RdmaOpRegion &cas_ror,
                     uint64_t equal, uint64_t val, CoroContext *ctx = nullptr);

  // CAS成功后写
  void cas_write(RdmaOpRegion &cas_ror, RdmaOpRegion &write_ror, uint64_t equal,
                 uint64_t val, bool signal = true, CoroContext *ctx = nullptr);
  bool cas_write_sync(RdmaOpRegion &cas_ror, RdmaOpRegion &write_ror,
                      uint64_t equal, uint64_t val, CoroContext *ctx = nullptr);

  // -------------------- 高级原子操作 --------------------

  // 跨两个地址的带掩码CAS（原子性保证）
  void two_cas_mask(RdmaOpRegion &cas_ror_1, uint64_t equal_1, uint64_t val_1, uint64_t mask_1,
                    RdmaOpRegion &cas_ror_2, uint64_t equal_2, uint64_t val_2, uint64_t mask_2,
                    bool signal = true, CoroContext *ctx = nullptr);
  // 返回两个CAS结果
  std::pair<bool, bool> two_cas_mask_sync(RdmaOpRegion &cas_ror_1, uint64_t equal_1, uint64_t val_1, uint64_t mask_1,
                                          RdmaOpRegion &cas_ror_2, uint64_t equal_2, uint64_t val_2, uint64_t mask_2,
                                          CoroContext *ctx = nullptr);

  // -------------------- 带掩码操作 --------------------

  // 掩码CAS（仅比较指定位）
  void cas_mask(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                uint64_t *rdma_buffer, uint64_t mask = ~(0ull),
                bool signal = true, CoroContext *ctx = nullptr);
  bool cas_mask_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                     uint64_t *rdma_buffer, uint64_t mask = ~(0ull), CoroContext *ctx = nullptr);

  // 带边界检查的原子加（防止溢出）
  void faa_boundary(GlobalAddress gaddr, uint64_t add_val,
                    uint64_t *rdma_buffer, uint64_t mask = 63,
                    bool signal = true, CoroContext *ctx = nullptr);
  void faa_boundary_sync(GlobalAddress gaddr, uint64_t add_val,
                         uint64_t *rdma_buffer, uint64_t mask = 63,
                         CoroContext *ctx = nullptr);

  // for on-chip device memory
  // -------------------- 设备内存操作（如GPU显存）----------------

  // 设备内存读
  void read_dm(char *buffer, GlobalAddress gaddr, size_t size,
               bool signal = true, CoroContext *ctx = nullptr);
  void read_dm_sync(char *buffer, GlobalAddress gaddr, size_t size,
                    CoroContext *ctx = nullptr);
  // 设备内存写
  void write_dm(const char *buffer, GlobalAddress gaddr, size_t size,
                bool signal = true, CoroContext *ctx = nullptr);
  void write_dm_sync(const char *buffer, GlobalAddress gaddr, size_t size,
                     CoroContext *ctx = nullptr);

  // 设备内存CAS
  void cas_dm(GlobalAddress gaddr, uint64_t equal, uint64_t val,
              uint64_t *rdma_buffer, bool signal = true,
              CoroContext *ctx = nullptr);
  bool cas_dm_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                   uint64_t *rdma_buffer, CoroContext *ctx = nullptr);
  // 设备内存带掩码CAS
  void cas_dm_mask(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                   uint64_t *rdma_buffer, uint64_t mask = ~(0ull),
                   bool signal = true, CoroContext *ctx = nullptr);
  bool cas_dm_mask_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                        uint64_t *rdma_buffer, uint64_t mask = ~(0ull), CoroContext *ctx = nullptr);
  // 设备内存边界检查原子加
  void faa_dm_boundary(GlobalAddress gaddr, uint64_t add_val,
                       uint64_t *rdma_buffer, uint64_t mask = 63,
                       bool signal = true, CoroContext *ctx = nullptr);
  void faa_dm_boundary_sync(GlobalAddress gaddr, uint64_t add_val,
                            uint64_t *rdma_buffer, uint64_t mask = 63,
                            CoroContext *ctx = nullptr);
  // -------------------- 完成队列管理 --------------------

  // 轮询完成队列（返回完成的工作请求ID）
  uint64_t poll_rdma_cq(int count = 1);
  // 单次轮询
  bool poll_rdma_cq_once(uint64_t &wr_id);
  // 批量轮询
  int poll_rdma_cq_batch_once(uint64_t *wr_ids, int count);

  // -------------------- 集群同步原语 --------------------

  // 跨节点求和（聚合操作）
  uint64_t sum(uint64_t value) {
    static uint64_t count = 0;
    return keeper->sum(std::string("sum-") + std::to_string(count++), value);
  }

  // Memcached operations for sync

  // 分布式键值存储接口（Memcached风格）
  size_t Put(uint64_t key, const void *value, size_t count) {

    std::string k = std::string("gam-") + std::to_string(key);
    keeper->memSet(k.c_str(), k.size(), (char *)value, count);
    return count;
  }
  size_t Get(uint64_t key, void *value) {

    std::string k = std::string("gam-") + std::to_string(key);
    size_t size;
    char *ret = keeper->memGet(k.c_str(), k.size(), &size);
    memcpy(value, ret, size);

    return size;
  }

private:
  DSM(const DSMConfig &conf); // 构造函数（分配内存、初始化RDMA）
  ~DSM(); // 释放资源

  // 初始化RDMA连接（QP/CQ创建）
  void initRDMAConnection(); 
  // 填充操作密钥 
  void fill_keys_dest(RdmaOpRegion &ror, GlobalAddress addr, bool is_chip);

  // 配置参数（内存大小、节点数等）
  DSMConfig conf;
  // 应用级线程ID分配器
  std::atomic_int appID;
  // RDMA缓存管理
  Cache cache;

  // 线程局部存储（TLS）
  // 当前线程ID
  static thread_local int thread_id;
  // 线程唯一标识（节点ID + 线程ID）
  static thread_local uint64_t thread_tag;
  // 线程级RDMA连接
  static thread_local ThreadConnection *iCon;
  // 线程本地RDMA缓冲区
  static thread_local char *rdma_buffer;
  // 本地内存分配器
  static thread_local LocalAllocator local_allocators[MEMORY_NODE_NUM][NR_DIRECTORY];
  // 协程级缓冲区（用于并发）
  static thread_local RdmaBuffer rbuf[MAX_CORO_NUM];

  // 内存管理
  // 共享内存基地址
  uint64_t baseAddr;
  // 当前节点ID
  uint32_t myNodeID;
  // 键空间大小（测试用）
  uint64_t keySpaceSize;

  // 连接管理
  // 远程节点连接信息
  RemoteConnection *remoteInfo;
  // 线程连接池
  ThreadConnection *thCon[MAX_APP_THREAD];
  // 目录服务连接
  DirectoryConnection *dirCon[NR_DIRECTORY];
  // 集群管理（同步/协调）
  DSMKeeper *keeper;

  // 目录服务
  // 目录服务代理（处理远程请求）
  Directory *dirAgent[NR_DIRECTORY];
  // 预加载键缓冲区（测试用）
  Key keyBuffer[MAX_KEY_SPACE_SIZE];

public:
  // 检查线程是否注册
  bool is_register() { return thread_id != -1; }
  // 集群屏障同步
  void barrier(const std::string &ss) { keeper->barrier(ss); }
  // 获取线程缓冲区
  char *get_rdma_buffer() { return rdma_buffer; }
  // 获取协程缓冲区
  RdmaBuffer &get_rbuf(int coro_id) { return rbuf[coro_id]; }
  // 全局内存分配
  GlobalAddress alloc(size_t size, bool align = true);
  // 释放内存
  void free(const GlobalAddress& addr, int size);
  // 跨节点批量分配
  void alloc_nodes(int node_num, GlobalAddress *addrs, bool align = true);
  // RPC通信
  // 发送RPC请求
  void rpc_call_dir(const RawMessage &m, uint16_t node_id,
                    uint16_t dir_id = 0) {

    auto buffer = (RawMessage *)iCon->message->getSendPool();

    memcpy(buffer, &m, sizeof(RawMessage));
    buffer->node_id = myNodeID;
    buffer->app_id = thread_id;

    iCon->sendMessage2Dir(buffer, node_id, dir_id);
  }

  // 等待RPC响应
  RawMessage *rpc_wait() {
    ibv_wc wc;

    pollWithCQ(iCon->rpc_cq, 1, &wc);
    return (RawMessage *)iCon->message->getMessage();
  }
};

inline GlobalAddress DSM::alloc(size_t size, bool align) {
  thread_local int cur_target_node = (this->getMyThreadID() + this->getMyNodeID()) % MEMORY_NODE_NUM;
  thread_local int cur_target_dir_id = (this->getMyThreadID() + this->getMyNodeID()) % NR_DIRECTORY;
  if (++cur_target_dir_id == NR_DIRECTORY) {
    cur_target_node = (cur_target_node + 1) % MEMORY_NODE_NUM;
    cur_target_dir_id = 0;
  }

  auto& local_allocator = local_allocators[cur_target_node][cur_target_dir_id];

  // alloc from the target node
  bool need_chunk = true;
  GlobalAddress addr = local_allocator.malloc(size, need_chunk, align);
  // if (need_chunk) {
  //           cxl_address = cxl_chunckAlloc->alloc_chunck(); 
  //           // 检查 alloc_chunck 返回的有效性以及标志位
  //           if (cxl_address.nodeID == 2) {
  //               //Debug::notifyError("Local chunk allocation failed: shared memory space run out.");
  //               // 立即尝试远程分配
  //               RawMessage m;
  //               m.type = RpcType::MALLOC;
  //               this->rpc_call_dir(m, cur_target_node, cur_target_dir_id);
  //               local_allocator.set_chunck(rpc_wait()->addr);
  //               addr = local_allocator.malloc(size, need_chunk, align);
  //           } else {
  //               local_allocator.set_chunck(cxl_address);
  //               addr = local_allocator.malloc(size, need_chunk, align);
  //               return addr;
  //           }
  //           return addr;
  //           }
  //           return addr;
  //           }
   if (need_chunk)  {
     RawMessage m;
    m.type = RpcType::MALLOC;
    this->rpc_call_dir(m, cur_target_node, cur_target_dir_id);
    local_allocator.set_chunck(rpc_wait()->addr);
    //std::cout << "myNodeID: " << myNodeID<< std::endl;
    // retry
    addr = local_allocator.malloc(size, need_chunk, align);
    //std::cout << "remote allocation successful. Address: " << addr << std::endl;
  }
  return addr;
}

inline void DSM::alloc_nodes(int node_num, GlobalAddress *addrs, bool align) {
  for (int i = 0; i < node_num; ++ i) {
    addrs[i] = alloc(define::allocationPageSize, align);
  }
}

inline void DSM::free(const GlobalAddress& addr, int size) {
  local_allocators[addr.nodeID][0].free(addr, size);
}

#endif /* __DSM_H__ */