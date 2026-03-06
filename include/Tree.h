#if !defined(_TREE_H_)
#define _TREE_H_

#include "RadixCache.h"
#include "DSM.h"
#include "Common.h"
#include "LocalLockTable.h"

#include <atomic>
#include <city.h>
#include <functional>
#include <map>
#include <algorithm>
#include <queue>
#include <set>
#include <iostream>


/*
  Workloads
*/
// 请求结构体定义
struct Request {
  // 是否是搜索操作
  bool is_search;
  // 是否是插入操作
  bool is_insert;
  // 是否是更新操作
  bool is_update;
  // 键值对
  Key k;
  Value v;
  // 范围查询大小（若适用）
  int range_size;
};


// 请求生成器基类
class RequstGen {
public:
  RequstGen() = default;
  // 生成下一个请求
  virtual Request next() { return Request{}; }
};


/*
  Tree
*/
using GenFunc = std::function<RequstGen *(DSM*, Request*, int, int, int)>;
#define MAX_FLAG_NUM 12
enum {
  FIRST_TRY,
  CAS_NULL,
  INVALID_LEAF,
  CAS_LEAF,
  INVALID_NODE,
  SPLIT_HEADER,
  FIND_NEXT,
  CAS_EMPTY,
  INSERT_BEHIND_EMPTY,
  INSERT_BEHIND_TRY_NEXT,
  SWITCH_RETRY,
  SWITCH_FIND_TARGET,
};

/**
 * Tree 类 - 基于DSM的分布式树结构
 * 支持插入、搜索、范围查询，利用RDMA优化和协程并发
 */
class Tree {
public:
  // 构造函数，绑定DSM实例
  Tree(DSM *dsm, uint16_t tree_id = 0);

  // 协程运行入口
  using WorkFunc = std::function<void (Tree *, const Request&, CoroContext *, int)>;
  void run_coroutine(GenFunc gen_func, WorkFunc work_func, int coro_cnt, Request* req = nullptr, int req_num = 0);

  // 核心操作接口
  void insert(const Key &k, Value v, CoroContext *cxt = nullptr, int coro_id = 0, bool is_update = false, bool is_load = false);
  bool search(const Key &k, Value &v, CoroContext *cxt = nullptr, int coro_id = 0);
  void range_query(const Key &from, const Key &to, std::map<Key, Value> &ret);

  // 统计和调试
  // 统计缓存命中率等指标
  void statistics();
  // 重置性能计数器
  void clear_debug_info();

  // 根节点操作
  // 获取根节点指针的地址
  GlobalAddress get_root_ptr_ptr();
  // 读取当前根节点
  InternalEntry get_root_ptr(CoroContext *cxt, int coro_id);

  // 获取 index cache 信息
  void get_index_cache_state();

  // 从 local cache 迁移数据到 radix cache 中
  void migrate_local_cache_to_radix_cache(const Key &k, Value v, CoroContext *cxt = nullptr, int coro_id = 0);

private:
  // 协程调度相关
  void coro_worker(CoroYield &yield, RequstGen *gen, WorkFunc work_func, int coro_id);
  void coro_master(CoroYield &yield, int coro_cnt);

  // 读取叶子节点
  bool read_leaf(const GlobalAddress &leaf_addr, char *leaf_buffer, int leaf_size, const GlobalAddress &p_ptr, bool from_cache, CoroContext *cxt, int coro_id);
  // ternary-state lock read helper (FAA semantics emulated by lock read)
  bool faa_and_read(const GlobalAddress &leaf_addr, char *leaf_buffer, const GlobalAddress &p_ptr, bool from_cache, uint64_t *faa_buffer, CoroContext *cxt, int coro_id);
  bool wait_smo_end(const GlobalAddress &leaf_addr, uint64_t *faa_buffer, CoroContext *cxt);
  // 原地更新叶子
  void in_place_update_leaf(const Key &k, Value &v, const GlobalAddress &leaf_addr, Leaf *leaf,
                           CoroContext *cxt, int coro_id);
  // 异地更新叶子
  bool out_of_place_update_leaf(const Key &k, Value &v, int depth, GlobalAddress& leaf_addr, const GlobalAddress &e_ptr, InternalEntry &old_e, const GlobalAddress& node_addr,
                                CoroContext *cxt, int coro_id, bool disable_handover = false);
  // 写入新叶子节点
  bool out_of_place_write_leaf(const Key &k, Value &v, int depth, GlobalAddress& leaf_addr, uint8_t partial_key,
                               const GlobalAddress &e_ptr, const InternalEntry &old_e, const GlobalAddress& node_addr, uint64_t *ret_buffer,
                               CoroContext *cxt, int coro_id);
  // 读取内部节点
  bool read_node(InternalEntry &p, bool& type_correct, char *node_buffer, const GlobalAddress& p_ptr, int depth, bool from_cache,
                 CoroContext *cxt, int coro_id);
  // 写入新内部节点
  bool out_of_place_write_node(const Key &k, Value &v, int depth, GlobalAddress& leaf_addr, int partial_len, uint8_t diff_partial,
                               const GlobalAddress &e_ptr, const InternalEntry &old_e, const GlobalAddress& node_addr, uint64_t *ret_buffer,
                               CoroContext *cxt, int coro_id);
  // 在节点尾部插入
  bool insert_behind(const Key &k, Value &v, int depth, GlobalAddress& leaf_addr, uint8_t partial_key, NodeType node_type,
                     const GlobalAddress &node_addr, uint64_t *ret_buffer, int& inserted_idx,
                     CoroContext *cxt, int coro_id);
  // 范围查询辅助
  void search_entries(const Key &from, const Key &to, int target_depth, std::vector<ScanContext> &res,
                      CoroContext *cxt, int coro_id);
  // 原子修改节点类型
  void cas_node_type(NodeType next_type, GlobalAddress p_ptr, InternalEntry p, Header hdr,
                     CoroContext *cxt, int coro_id);
  // 单页范围查询处理
  void range_query_on_page(InternalPage* page, bool from_cache, int depth,
                           GlobalAddress p_ptr, InternalEntry p,
                           const Key &from, const Key &to, State l_state, State r_state,
                           std::vector<ScanContext>& res);
  // 获取硬件锁地址
  void get_on_chip_lock_addr(const GlobalAddress &leaf_addr, GlobalAddress &lock_addr, uint64_t &mask);



#ifdef TREE_TEST_ROWEX_ART
  void lock_node(const GlobalAddress &node_addr, CoroContext *cxt, int coro_id);
  void unlock_node(const GlobalAddress &node_addr, CoroContext *cxt, int coro_id);
#endif

private:
  // 分布式共享内存管理器
  DSM *dsm;
// #ifdef CACHE_ENABLE_ART
  // 基数树缓存加速查询
  RadixCache *index_cache;
// #else
//   NormalCache *index_cache;
// #endif
  // 本地锁表控制并发
  LocalLockTable *local_lock_table;

  // 工作协程
  static thread_local CoroCall worker[MAX_CORO_NUM];
  // 主调度协程
  static thread_local CoroCall master;
  // 忙等队列
  static thread_local CoroQueue busy_waiting_queue;

  // 树实例ID（多树支持）
  uint64_t tree_id;
  // 根节点指针的全局地址
  GlobalAddress root_ptr_ptr; // the address which stores root pointer;
};

extern void treecpp_allocate_on_numa();
extern void treecpp_free_numa();

#endif // _TREE_H_
