#include "RadixCache.h"

#include <random>
#include <queue>
#include <vector>
#include <set>
#include <queue>

#include "SpecialPrint.h"
#include "NumaObjectPool.h"


NumaObjectPool<CacheEntry> cache_entry_pool(5000000, RADIX_CACHE_NUMA_NODE);

RadixCache::RadixCache(int cache_size, DSM *dsm) : cache_size(cache_size), dsm(dsm) {
  // free_manager = new FreeMemManager(define::MB * cache_size);
  // cache_root = new CacheNode();
  // node_queue = new tbb::concurrent_queue<CacheNode*>();
  // node_queue->push(cache_root);

  // updated by pz
  // 将 Radix Cache 内存分配在远程 NUMA 节点上
  void* free_mem_ptr = numa_alloc_onnode(sizeof(FreeMemManager), RADIX_CACHE_NUMA_NODE);
  free_manager = new (free_mem_ptr) FreeMemManager(define::MB * cache_size);

  // 分配 CacheNode 到指定 NUMA 节点
  void* root_ptr = numa_alloc_onnode(sizeof(CacheNode), RADIX_CACHE_NUMA_NODE);
  cache_root = new (root_ptr) CacheNode();

  // 分配队列到指定 NUMA 节点
  void* queue_ptr = numa_alloc_onnode(sizeof(tbb::concurrent_queue<CacheNode*>), RADIX_CACHE_NUMA_NODE);
  node_queue = new (queue_ptr) tbb::concurrent_queue<CacheNode*>();

  // sizeof(CacheEntry) == 40
  // cache_entry_pool = (CacheEntry *)numa_alloc_onnode(20000 * sizeof(CacheEntry), RADIX_CACHE_NUMA_NODE);
  // printf("cache_entry_pool size %ld\n", 10000 * sizeof(CacheEntry));
  // cache_entry_pool_id = 0;


  node_queue->push(cache_root);
}

// RadixCache::~RadixCache(){
//     // SpecialPrint::redBold("enter ~RadixCache()\n");
//     // // numa_free(temp, 10000ll * define:: MB);

//     // if (free_manager) {
//     //   free_manager->~FreeMemManager();
//     //   numa_free(free_manager, sizeof(FreeMemManager));
//     // }
//     // if (cache_root) {
//     //   cache_root->~CacheNode();
//     //   numa_free(cache_root, sizeof(CacheNode));
//     // }
//     // if (node_queue) {
//     //   node_queue->~concurrent_queue<CacheNode*>();
//     //   numa_free(node_queue, sizeof(tbb::concurrent_queue<CacheNode*>));
//     // }  
// }

int entry_num;
void RadixCache::add_to_cache(const Key& k, const InternalPage* p_node, const GlobalAddress &node_addr) {
  auto depth = p_node->hdr.depth - 1;
  if (depth == 0) return;

  std::vector<uint8_t> byte_array(k.begin(), k.begin() + depth);
  for (int i = 0; i < (int)p_node->hdr.partial_len; ++ i) byte_array.push_back(p_node->hdr.partial[i]);

  // old
  // auto new_entry = new CacheEntry(p_node, node_addr);

  // void *entry_mem = numa_alloc_onnode(sizeof(CacheEntry), RADIX_CACHE_NUMA_NODE);
  // // SpecialPrint::redBold("new_entry %d\n", ++entry_num);
  // auto new_entry = new (entry_mem) CacheEntry(p_node, node_addr);
  auto new_entry = cache_entry_pool.allocate(p_node, node_addr);
  // auto new_entry = new ((void *)(cache_entry_pool) + cache_entry_pool_id + sizeof(CacheEntry)) CacheEntry(p_node, node_addr);
  // cache_entry_pool_id += 5;
  // printf("cache_entry_pool_id %d\n", cache_entry_pool_id);

  _insert(byte_array, new_entry);
#ifndef CACHE_ENABLE_ART
  free_manager->consume(sizeof(Key));  // emulate hash-based cache
#endif
  if (free_manager->remain_size() < 0) {
    _evict();
  }
  return;
}


void RadixCache::_insert(const std::vector<uint8_t>& byte_array, CacheEntry* new_entry) {
  CacheNode* parent_node = nullptr;
  CacheNode* node = cache_root;
  int idx = 0;

next:
  // 1. parse header
  auto hdr = (CacheHeader *)node->header;
  for (int i = 0; i < (int)hdr->partial.size(); ++ i) {
    auto cur_partial = byte_array[hdr->depth + i];
    if (hdr->depth + i == (int)byte_array.size() - 1 || cur_partial != hdr->partial[i]) {
      // split
      auto partial_len = hdr->depth + i - idx;
      CacheNode* nested_node = nullptr;
      auto new_node = new CacheNode(byte_array, idx, partial_len, hdr->partial[i], node, cur_partial, new_entry, nested_node);
      auto& parent_node_entry = parent_node->records[byte_array[idx - 1]];
      auto ret_node = __sync_val_compare_and_swap(&(parent_node_entry.next), node, new_node);
      if (ret_node == node) {  // cas success
        auto new_hdr = CacheHeader::split_header(hdr, i);
        // update header
        auto ret_hdr = (CacheHeader *)__sync_val_compare_and_swap(&(node->header), hdr, new_hdr);
        if (ret_hdr == hdr) _safely_delete(ret_hdr);  // cas success
        else delete new_hdr;
        free_manager->consume_by_node(new_node);
        if (nested_node) free_manager->consume_by_node(nested_node);
        free_manager->consume_by_node(node);
        free_manager->consume(new_entry->content_size());
      }
      else {  // cas fail
        if (ret_node) {
          node = (CacheNode *)ret_node;
          delete new_node;
          if (nested_node) delete nested_node;
          goto next;
        }
        else  {  // node is deleted
          node = (CacheNode *)__sync_val_compare_and_swap(&(parent_node_entry.next), 0UL, new_node);
          if (node) {
            delete new_node;
            if (nested_node) delete nested_node;
            goto next;
          }
          free_manager->consume_by_node(new_node);
          if (nested_node) free_manager->consume_by_node(nested_node);
          free_manager->consume(new_entry->content_size());
        }
      }
      // record
      node_queue->push(new_node);
      if (nested_node) node_queue->push(nested_node);
      CacheMap::const_iterator tmp = (nested_node ? nested_node->records.find(byte_array.back()) : new_node->records.find(cur_partial));
      eviction_list.push(std::make_pair(&(tmp->second.cache_entry), new_entry));
      return;
    }
  }
  idx = hdr->depth + hdr->partial.size();

  // 2. parse_node
  auto& cache_map = node->records;
  auto partial = byte_array[idx];

  // 2.1 last level
  if (idx == (int)byte_array.size() - 1) {
    auto& node_entry = cache_map[partial];
    auto old_entry = (CacheEntry *)node_entry.cache_entry;
    if (__sync_bool_compare_and_swap(&(node_entry.cache_entry), old_entry, new_entry)) {
      free_manager->consume_by_node(node);
      free_manager->consume(new_entry->content_size());
      if (old_entry) {
        free_manager->free(old_entry->content_size());
        _safely_delete(old_entry);
      }
      eviction_list.push(std::make_pair(&(node_entry.cache_entry), new_entry));
    }
    else {
      // numa_free(new_entry, sizeof(CacheEntry));
      cache_entry_pool.release(new_entry);
      // delete new_entry;
    }
    return;
  }
  // 2.2 internal level
  else {
    auto& node_entry = cache_map[partial];
    if (node_entry.next == nullptr) {
      auto next_node = new CacheNode(byte_array, idx + 1, new_entry);
      auto ret_node = __sync_val_compare_and_swap(&(node_entry.next), 0UL, next_node);
      if (ret_node == 0UL) {  // cas success
        // record
        node_queue->push(next_node);
        CacheMap::const_iterator tmp = next_node->records.find(byte_array.back());
        eviction_list.push(std::make_pair(&(tmp->second.cache_entry), new_entry));
        free_manager->consume_by_node(node);
        free_manager->consume_by_node(next_node);
        free_manager->consume(new_entry->content_size());
        return;
      }
      else {  // cas fail
        delete next_node;
        parent_node = node;
        node = (CacheNode *)ret_node;
        idx ++;
        goto next;
      }
    }
    else {
      parent_node = node;
      node = (CacheNode *)(node_entry.next);
      idx ++;
      goto next;
    }
  }
}


bool RadixCache::search_from_cache(const Key& k, volatile CacheEntry**& entry_ptr_ptr, CacheEntry*& entry_ptr, int& entry_idx) {
  CacheKey byte_array(k.begin(), k.begin() + define::keyLen - 1);

  SearchRetStk ret;
  if(_search(byte_array, ret)) {
    while(!ret.empty()) {
      const auto& item = ret.top();
      auto cache_entry = item.entry_ptr;
      auto next_partial = k.at(item.next_idx);
      if (cache_entry) {
#if ENABLE_INDEX_CACHE_TTL
        if (cache_entry->is_expired()) {
          invalidate(item.entry_ptr_ptr, cache_entry);
          ret.pop();
          continue;
        }
#endif
        for (int i = 0; i < (int)cache_entry->records.size(); ++ i) {
          const auto& e = cache_entry->records[i];
          if (e != InternalEntry::Null() && e.partial == next_partial) {
            entry_ptr = cache_entry;
            // __sync_fetch_and_add(&(entry_ptr->counter), 1UL);
            entry_ptr_ptr = item.entry_ptr_ptr;
            entry_idx = i;
            return true;
          }
        }
      }
      ret.pop();
    }
  }
  return false;
}

bool RadixCache::_search(const CacheKey& byte_array, SearchRetStk& ret) {
  CacheNode* node = cache_root;
  int idx = 0;

next:
  if (idx >= (int)byte_array.size()) {  // exit
    return !ret.empty();
  }

  // 1. parse header
  auto hdr = (CacheHeader *)node->header;
  for (int i = 0; i < (int)hdr->partial.size(); ++ i) {
    if (hdr->depth + i == (int)byte_array.size() - 1 || byte_array[hdr->depth + i] != hdr->partial[i]) {
      return !ret.empty();
    }
  }
  idx = hdr->depth + hdr->partial.size();

  // 2. parse_node
  auto& cache_map = node->records;
  auto partial = byte_array[idx];

  CacheMap::const_iterator r_entry = cache_map.find(partial);
  if (r_entry != cache_map.end()) {
    auto cache_entry = (CacheEntry *)r_entry->second.cache_entry;
    // ret.push(std::make_pair(std::make_pair(&(r_entry->second.cache_entry), cache_entry), idx + 1));
    ret.push(SearchRet(&(r_entry->second.cache_entry), cache_entry, idx + 1));
    node = (CacheNode *)(r_entry->second.next);
    if (node) {
      idx ++;
      goto next;
    }
  }
  return !ret.empty();
}


void RadixCache::search_range_from_cache(const Key &from, const Key &to, std::vector<RangeCache> &result) {
  if (to <= from) return;

  GlobalAddress p_ptr;
  InternalEntry p;
  int depth;
  auto to_inc = to - 1;

  struct Frame {
    CacheNode* node;
    Key prefix;
    State l_state;
    State r_state;
  };

  std::vector<Frame> stack;
  stack.push_back(Frame{cache_root, Key{}, BORDER, BORDER});

  std::set<std::pair<uint64_t, uint64_t>> seen_entries;

  while (!stack.empty()) {
    auto frame = stack.back();
    stack.pop_back();

    auto* node = frame.node;
    if (node == nullptr) continue;

    auto* hdr = (CacheHeader *)node->header;
    auto l_state = frame.l_state;
    auto r_state = frame.r_state;
    auto prefix = frame.prefix;

    if (l_state == BORDER) {
      int j;
      for (j = 0; j < (int)hdr->partial.size(); ++j) {
        auto depth_idx = hdr->depth + j;
        auto h = hdr->partial[j];
        prefix.at(depth_idx) = h;
        if (h != from.at(depth_idx)) break;
      }
      if (j == (int)hdr->partial.size()) l_state = BORDER;
      else if (hdr->partial[j] > from.at(hdr->depth + j)) l_state = INSIDE;
      else l_state = OUTSIDE;
    } else {
      for (int j = 0; j < (int)hdr->partial.size(); ++j) {
        prefix.at(hdr->depth + j) = hdr->partial[j];
      }
    }

    if (r_state == BORDER) {
      int j;
      for (j = 0; j < (int)hdr->partial.size(); ++j) {
        auto depth_idx = hdr->depth + j;
        auto h = hdr->partial[j];
        prefix.at(depth_idx) = h;
        if (h != to_inc.at(depth_idx)) break;
      }
      if (j == (int)hdr->partial.size()) r_state = BORDER;
      else if (hdr->partial[j] < to_inc.at(hdr->depth + j)) r_state = INSIDE;
      else r_state = OUTSIDE;
    }

    if (l_state == OUTSIDE || r_state == OUTSIDE) continue;

    int next_depth_idx = hdr->depth + hdr->partial.size();
    if (next_depth_idx >= (int)define::keyLen) continue;

    const auto from_partial = from.at(next_depth_idx);
    const auto to_partial = to_inc.at(next_depth_idx);

    for (auto &cache_map_entry : node->records) {
      auto partial = cache_map_entry.first;
      auto e_l_state = l_state;
      auto e_r_state = r_state;

      if (e_l_state == BORDER) {
        if (partial == from_partial) e_l_state = BORDER;
        else if (partial > from_partial) e_l_state = INSIDE;
        else e_l_state = OUTSIDE;
      }
      if (e_r_state == BORDER) {
        if (partial == to_partial) e_r_state = BORDER;
        else if (partial < to_partial) e_r_state = INSIDE;
        else e_r_state = OUTSIDE;
      }
      if (e_l_state == OUTSIDE || e_r_state == OUTSIDE) continue;

      auto next_prefix = prefix;
      next_prefix.at(next_depth_idx) = partial;

      auto entry_ptr_ptr = &(cache_map_entry.second.cache_entry);
      auto entry_ptr = (CacheEntry *)cache_map_entry.second.cache_entry;
      if (entry_ptr) {
#if ENABLE_INDEX_CACHE_TTL
        if (entry_ptr->is_expired()) {
          invalidate(entry_ptr_ptr, entry_ptr);
          entry_ptr = nullptr;
        }
#endif
      }

      if (entry_ptr) {
        for (int i = 0; i < (int)entry_ptr->records.size(); ++ i) {
          const auto& e = entry_ptr->records[i];
          if (e == InternalEntry::Null() || e.partial != partial) continue;

          p_ptr = GADD(entry_ptr->addr, sizeof(InternalEntry) * i);
          p = e;
          depth = entry_ptr->depth;

          auto leftmost = p.is_leaf ? next_prefix : get_leftmost(next_prefix, depth);
          auto rightmost = p.is_leaf ? next_prefix : get_rightmost(next_prefix, depth);

          auto dedup_key = std::make_pair((uint64_t)p_ptr.val, (uint64_t)p.val);
          if (seen_entries.insert(dedup_key).second) {
            result.push_back(RangeCache(leftmost, rightmost, p_ptr, p, depth, entry_ptr_ptr, entry_ptr));
          }
        }
      }

      auto* next_node = (CacheNode *)cache_map_entry.second.next;
      if (next_node != nullptr) {
        stack.push_back(Frame{next_node, next_prefix, e_l_state, e_r_state});
      }
    }
  }
  return;
}

void RadixCache::invalidate(volatile CacheEntry** entry_ptr_ptr, CacheEntry* entry_ptr) {
  if (entry_ptr_ptr && entry_ptr && __sync_bool_compare_and_swap(entry_ptr_ptr, entry_ptr, 0UL)) {
    free_manager->free(entry_ptr->content_size());
    _safely_delete(entry_ptr);
  }
}

void RadixCache::_evict() {
  bool flag;
  do {
    // _evict_one();
    std::pair<volatile CacheEntry**, CacheEntry*> next;
    if(eviction_list.try_pop(next) && *next.first == next.second) {
      invalidate(next.first, next.second);
    }
    flag = eviction_list.empty();
  } while (free_manager->remain_size() < 0 && !flag);
  if (flag) {
    // rebuild cache  TODO: memory leak
    if (__sync_bool_compare_and_swap(&cache_root, cache_root, new CacheNode())) {
      free_manager = new FreeMemManager(define::MB * cache_size);
      node_queue = new tbb::concurrent_queue<CacheNode*>();
      node_queue->push(cache_root);
    }
  }
}

// void RadixCache::_evict_one() {
//   SearchRetStk stk1, stk2;
//   Key k1, k2;
//   do {
//     k1 = dsm->getRandomKey();
//   } while(!_search(CacheKey(k1.begin(), k1.begin() + define::keyLen - 1), stk1));
//   do {
//     k2 = dsm->getRandomKey();
//   } while(!_search(CacheKey(k2.begin(), k2.begin() + define::keyLen - 1), stk2));

//   // while(!_random_search(stk1));
//   // while(!_random_search(stk2));
//   // evict
//   uint64_t min_cnt = UINT_MAX;
//   CacheEntry * min_entry = nullptr;
//   volatile CacheEntry** min_entry_ptr = nullptr;

//   auto select_smallest = [&](SearchRetStk& stk){
//     while(!stk.empty()) {
//       const auto& item = stk.top();
//       auto cache_entry = item.entry_ptr;
//       if (!cache_entry) {
//         stk.pop();
//         continue;
//       }
//       if (item.counter <= min_cnt) {
//         min_cnt = item.counter;
//         min_entry = cache_entry;
//         min_entry_ptr = item.entry_ptr_ptr;
//       }
//       stk.pop();
//     }
//   };
//   select_smallest(stk1);
//   select_smallest(stk2);
//   invalidate(min_entry_ptr, min_entry);
// }

// bool RadixCache::_random_search(SearchRetStk& ret) {
//   static std::default_random_engine e;
//   static std::uniform_int_distribution<uint8_t> u(0, 255);

//   CacheNode* node = cache_root;
//   int idx = 0;

// next:
//   if (idx >= (int)define::keyLen - 1) {  // exit
//     return !ret.empty();
//   }

//   // 1. ignore header
//   auto hdr = (CacheHeader *)node->header;
//   idx = hdr->depth + hdr->partial.size();

//   // 2. parse_node
//   auto& cache_map = node->records;

//   CacheMap::const_iterator r_entry;
//   int s = cache_map.size();
//   if (s == 0) {
//     return !ret.empty();
//   }
//   else if (s < 16) {
//     int i = u(e) % s;
//     r_entry = cache_map.begin();
//     while(i --) ++ r_entry;
//   }
//   else {
// retry:
//     r_entry = cache_map.find(u(e));
//     if (r_entry == cache_map.end()) goto retry;
//   }

//   if (r_entry != cache_map.end()) {
//     auto cache_entry = (CacheEntry *)r_entry->second.cache_entry;
//     if (cache_entry) {
//       ret.push(SearchRet(&(r_entry->second.cache_entry), cache_entry, idx + 1, cache_entry->counter));
//     }
//     node = (CacheNode *)(r_entry->second.next);
//     if (node) {
//       idx ++;
//       goto next;
//     }
//   }
//   return !ret.empty();
// }

void RadixCache::_safely_delete(CacheEntry* cache_entry) {
  cache_entry_gc.push(cache_entry);
  while (cache_entry_gc.unsafe_size() > safely_free_epoch) {
    CacheEntry* next = nullptr;
    if (cache_entry_gc.try_pop(next) && next) {
      // next->~CacheEntry();
      // numa_free(next, sizeof(CacheEntry));
      cache_entry_pool.release(next);
      // delete next;
    }
  }
}

void RadixCache::_safely_delete(CacheHeader* cache_hdr) {
  cache_hdr_gc.push(cache_hdr);
  while (cache_hdr_gc.unsafe_size() > safely_free_epoch) {
    CacheHeader* next = nullptr;
    if (cache_hdr_gc.try_pop(next) && next) {
      delete next;
    }
  }
}

void RadixCache::statistics() {
  std::cout << " ----- [IndexCache(192.168.0.189)]: " << " cache size=" << cache_size << " MB"
                                       << " free_size=" << free_manager->remain_size() / define::MB << " MB" 
                                       << " node_cnt=" << node_queue->unsafe_size() << " ----- " << std::endl;
  // SpecialPrint::greenBold(("[IndexCache(192.168.0.189]: cache size=" + std::to_string(cache_size) + " MB"
  //                                      + " free_size=" + std::to_string(free_manager->remain_size() / define::MB) + " MB"
  //                                      + " node_cnt=" + std::to_string(node_queue->unsafe_size())).c_str());
  // return ;
  std::map<int, int64_t> cnt;
  uint64_t kp_cnt = 0;
  for (auto node_iter = node_queue->unsafe_begin(); node_iter != node_queue->unsafe_end(); ++ node_iter) {
    auto& node = *node_iter;
    auto& cache_map = node->records;
    // auto header = (CacheHeader *)node->header;
    for (auto entry_iter = cache_map.begin(); entry_iter != cache_map.end(); ++ entry_iter) {
      auto cache_entry = (CacheEntry *)entry_iter->second.cache_entry;
      if (cache_entry) {
        int depth = cache_entry->depth;
        if (cnt.find(depth) == cnt.end()) cnt[depth] = 0;
        cnt[depth] ++;
        kp_cnt += cache_entry->records.size();
      }
    }
  }
  for (const auto& e : cnt) {
    std::cout << "depth=" << e.first << " cnt=" << e.second << std::endl;
  }
  printf("consumed cache size = %.3lf MB\n", (double)cache_size - (double)free_manager->remain_size() / define::MB);
}


// added by pz
void RadixCache::get_state(){
  SpecialPrint::greenBold(("[IndexCache(192.168.0.189]: cache size=" + std::to_string(cache_size) + " MB"
                                       + " free_size=" + std::to_string(free_manager->remain_size() / define::MB) + " MB"
                                       + " node_cnt=" + std::to_string(node_queue->unsafe_size())).c_str());
  std::cout << " ----- [IndexCache(192.168.0.189)]: " << " cache size=" << cache_size << " MB"
                                       << " free_size=" << free_manager->remain_size() / define::MB << " MB" 
                                       << " node_cnt=" << node_queue->unsafe_size() << " ----- " << std::endl;
}
