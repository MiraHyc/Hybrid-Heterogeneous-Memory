#if !defined(_RADIX_CACHE_H_)
#define _RADIX_CACHE_H_

#include "Common.h"
#include "Node.h"
#include "NormalCache.h"

#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_queue.h>
// #include <tbb/concurrent_vector.h>
#include <vector>
#include <queue>
#include <atomic>
#include <stack>

// 缓存节点值结构：包含缓存条目和指向下一节点的指针
// 用于基数树中的内部节点和叶子节点表示
struct CacheNodeValue {
  volatile CacheEntry* cache_entry; // 指向实际缓存数据的条目（叶子节点有效）
  volatile void * next; // 指向下一级子节点（内部节点有效）

  CacheNodeValue() :  cache_entry(nullptr), next(nullptr) {}
  CacheNodeValue(CacheEntry* cache_entry, void *next) :
                 cache_entry(cache_entry), next(next) {}
};

// 缓存节点头部：存储路径压缩信息和深度
// 实现基数树的路径压缩优化
class CacheHeader {
public:
  uint8_t depth; // 当前节点在树中的层级深度
  std::vector<uint8_t> partial; // 压缩存储的键片段（节省中间节点）

  CacheHeader() : depth(0) {}

  CacheHeader(const std::vector<uint8_t>& byte_array, int depth, int partial_len) : depth(depth) {
    // 从字节数组中提取partial_len长度的部分键
    for (int i = 0; i < partial_len; ++ i) {
      partial.push_back(byte_array[depth + i]);
    }
  }

  // 分裂头部：当节点需要分裂时生成新头部
  static CacheHeader* split_header(const CacheHeader* old_hdr, int diff_idx) {
    auto new_hdr = new CacheHeader();
    for (int i = diff_idx + 1; i < (int)old_hdr->partial.size(); ++ i) new_hdr->partial.push_back(old_hdr->partial[i]);
    new_hdr->depth = old_hdr->depth + diff_idx + 1;
    return new_hdr;
  }

  // 计算头部占用的内存大小
  uint64_t content_size() const {
    return sizeof(uint8_t) + sizeof(uint8_t) * partial.size();
  }
};


// 自定义哈希函数（直接返回键值，用于TBB哈希表）
class no_hash {
public:
  no_hash() {}
  uint8_t operator() (const uint8_t& key) const { return key; }
};
// 使用TBB并发无序映射实现节点记录表
using CacheMap = tbb::concurrent_unordered_map<uint8_t, CacheNodeValue, no_hash>;


/*
  node: [header, records]
*/
// 基数树缓存节点：包含头部和子节点映射
class CacheNode {
public:
  // 节点头部（带路径压缩信息）
  volatile CacheHeader* header;
  // 子节点映射表（并发安全）
  CacheMap records;  // value is const

  CacheNode() {
    header = new CacheHeader();
  }

  // insert leaf node
  // 构造函数（用于叶子节点）
  CacheNode(const std::vector<uint8_t>& byte_array, int start, CacheEntry* new_entry) {
    header = new CacheHeader(byte_array, start, byte_array.size() - start - 1);
    records[byte_array.back()] = CacheNodeValue(new_entry, nullptr);
  }

  // split internal node
  // 构造函数（用于分裂内部节点）
  CacheNode(const std::vector<uint8_t>& byte_array, int start, int partial_len,
            uint8_t partial_1, CacheNode* next_node, uint8_t partial_2, CacheEntry* new_entry, CacheNode* &nested_node) {
    header = new CacheHeader(byte_array, start, partial_len);
    if (partial_1 == partial_2) {  // split for insert new_entry at old header
      records[partial_1] = CacheNodeValue(new_entry, next_node);
    }
    else {
      records[partial_1] = CacheNodeValue(nullptr, next_node);
      if (start + partial_len >= (int)byte_array.size() - 1) {  // insert entry directly
        nested_node = nullptr;
        records[partial_2] = CacheNodeValue(new_entry, nullptr);
      }
      else {  // insert leaf node
        nested_node = new CacheNode(byte_array, start + partial_len + 1, new_entry);
        records[partial_2] = CacheNodeValue(nullptr, nested_node);
      }
    }
  }

  // 计算节点总内存占用
  uint64_t content_size() const {
    return ((CacheHeader *)header)->content_size() + (sizeof(uint8_t) + sizeof(CacheEntry *) + sizeof(CacheNode *)) * records.size();
  }

  ~CacheNode() { delete header; }
};


/*
  This class is used to calculate the cache memory consumption
  so as to trigger eviction.
*/
// 内存管理器：跟踪缓存内存使用，触发淘汰
class FreeMemManager {
public:
  FreeMemManager(int64_t free_size) : free_size(free_size) {}

  // 内存消耗接口
  void consume(int _size) {
    // free_size -= _size;
    free_size.fetch_add(-_size);
  }

  using NodeSizeMap = tbb::concurrent_hash_map<CacheNode*, uint64_t>;
  // 按节点计算内存消耗（ART优化模式）
  void consume_by_node(CacheNode* node) {
#ifdef CACHE_ENABLE_ART
    auto new_size = node->content_size();
    NodeSizeMap::accessor w_entry;
    auto new_inserted = node_mem_size.insert(w_entry, node);
    auto old_size = new_inserted ? 0 : w_entry->second;
    if (new_size == old_size) {
      return;
    }
    w_entry->second = new_size;
    // free_size -= new_size - old_size;
    free_size.fetch_add(-new_size + old_size);
#else
    return;  // emulate normal cache
#endif
  }

  // 内存释放接口
  void free(int _size) {
    free_size.fetch_add(_size);
  }

  // 获取剩余内存
  int64_t remain_size() const {
    return free_size.load();
  }

private:
  // 剩余可用内存（原子计数器）
  std::atomic<int64_t> free_size;
  // 节点内存占用记录（并发哈希表）
  NodeSizeMap node_mem_size;
};


struct SearchRet {
  volatile CacheEntry** entry_ptr_ptr;
  CacheEntry* entry_ptr;
  int next_idx;
  // uint64_t counter;
  SearchRet() {}
  SearchRet(volatile CacheEntry** entry_ptr_ptr, CacheEntry* entry_ptr, int next_idx) :
    entry_ptr_ptr(entry_ptr_ptr), entry_ptr(entry_ptr), next_idx(next_idx) {}
};

// 基数树缓存主类
class RadixCache {

public:
  // 构造函数：初始化内存池和根节点
  RadixCache(int cache_size, DSM *dsm);

  // 添加键值到缓存：将键分解为字节数组，构建基数树路径
  void add_to_cache(const Key& k, const InternalPage* p_node, const GlobalAddress &node_addr);

  // 缓存查找：返回找到的条目指针和索引
  bool search_from_cache(const Key& k, volatile CacheEntry**& entry_ptr_ptr, CacheEntry*& entry_ptr, int& entry_idx);
  // 范围查询：收集满足范围的缓存条目
  void search_range_from_cache(const Key &from, const Key &to, std::vector<RangeCache> &result);
  // 失效条目：原子标记条目为无效
  void invalidate(volatile CacheEntry** entry_ptr_ptr, CacheEntry* entry_ptr);
  // 统计信息输出
  void statistics();

  // added by pz 输出当前index cache状态
  void get_state();
private:
  // 插入核心逻辑：处理路径分裂和并发插入
  void _insert(const CacheKey& byte_array, CacheEntry* new_entry);

  using SearchRetStk = std::stack<SearchRet>;
  // 查找核心逻辑：使用栈记录搜索路径
  bool _search(const CacheKey& byte_array, SearchRetStk& ret);
  // bool _random_search(SearchRetStk& ret);

  // 淘汰机制：当内存不足时触发
  void _evict();
  // void _evict_one();
  // 安全删除：延迟释放避免并发访问问题
  void _safely_delete(CacheEntry* cache_entry);
  void _safely_delete(CacheHeader* cache_hdr);

private:
  // Cache
  // 缓存总大小（MB）
  uint64_t cache_size; // MB
  // 内存管理器
  FreeMemManager* free_manager;
  // 基数树根节点
  CacheNode* cache_root;
  // 节点追踪队列
  tbb::concurrent_queue<CacheNode*>* node_queue;

  // GC
  // 条目垃圾回收队列
  tbb::concurrent_queue<CacheEntry*> cache_entry_gc;
  // 头部垃圾回收队列
  tbb::concurrent_queue<CacheHeader*> cache_hdr_gc;
  static const int safely_free_epoch = 2 * MAX_APP_THREAD * MAX_CORO_NUM;

  // FIFIO Eviction
  // 底层分布式共享内存系统指针
  DSM *dsm;
  // 淘汰队列（FIFO策略）
  tbb::concurrent_queue<std::pair<volatile CacheEntry**, CacheEntry*> > eviction_list;
};

#endif // _RADIX_CACHE_H_
