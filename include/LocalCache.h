#ifndef _LOCALCACHE_H_
#define _LOCALCACHE_H_

#include "RadixCache.h"
#include "SpecialPrint.h"
#include "Key.h"
#include <ctime>

class LocalCache {
public:
    explicit LocalCache(const int cache_size_mb, Tree* tree) : LOCAL_CACHE_SIZE_MB(cache_size_mb), tree_ptr(tree), cur_size(0), capacity(LOCAL_CACHE_SIZE_MB * 1024 * 1024 / sizeof(Node)) {
        buckets.resize(capacity); // 初始化哈希表桶
        bucket_elements.resize(capacity); // 初始化每个桶的元素个数
        bucket_locks.reserve(capacity); // 预留空间, 不能使用resize，因为 mutex 是不可拷贝的
        for (size_t i = 0; i < capacity; ++i) {
            bucket_locks.emplace_back(std::make_unique<std::mutex>()); // 初始化每个桶的锁
            bucket_elements[i] = 0; // 初始化每个桶的元素个数
        }
        // lru_lock = std::make_unique<std::mutex>(); // 初始化 LRU 链表锁
        sum_evict_time = 0;
        local_cache_hit = 0;
        local_cache_miss = 0;
        sum_reject_time = 0;
        sum_migrate_time = 0;
        SpecialPrint::blue("local cache used %d MB. capacity = %d\n", LOCAL_CACHE_SIZE_MB, capacity);
    }

    ~LocalCache() = default;

    void get_local_cache_state(){
        // std::lock_guard<std::mutex> lock(*lru_lock); // 锁定 LRU 链表
        size_t max_one_bucket_elements = 0;
        size_t used_buckets = 0;
        for(size_t i = 0; i < capacity; ++i){
            if(bucket_elements[i] > max_one_bucket_elements){
                max_one_bucket_elements = bucket_elements[i];
            }
            if(bucket_elements[i] > 0){
                used_buckets++;
            }
        }

        SpecialPrint::blue("##################### LOCAL CACHE STATE #####################");
        SpecialPrint::blue("capactity = %d", capacity);
        SpecialPrint::blue("sum_evict_cnt = %d sum_migrate_time = %d buckets_elements = %d", sum_evict_time, sum_migrate_time, cur_size.load(std::memory_order_relaxed));
        SpecialPrint::blue("max_one_bucket_elements = %d", max_one_bucket_elements);
        SpecialPrint::blue("used_buckets = %d used_buckets_percent = %.2lf%%", 
                            used_buckets, 
                            (double)used_buckets / capacity * 100);
        SpecialPrint::blue("sum_reject_cnt = %d", sum_reject_time);
        SpecialPrint::blue("cache_hit = %d cache_miss = %d", local_cache_hit.load(std::memory_order_relaxed), local_cache_miss.load(std::memory_order_relaxed));
        SpecialPrint::blue("cache_hit_percent = %.2lf%%", 
                            (double)local_cache_hit.load(std::memory_order_relaxed) / 
                            (local_cache_hit.load(std::memory_order_relaxed) + local_cache_miss.load(std::memory_order_relaxed)) * 100);
    }


    // 插入键值对
    void insert(const Key &key,const Value &value, CoroContext *cxt, int coro_id){
        auto candidates = _get_candidate_buckets(key);
        size_t index;
        int min_load = bucket_elements_limit + 1;
        for (size_t i = 0; i < candidates.size(); ++i) {
            std::lock_guard<std::mutex> lock(*bucket_locks[candidates[i]]); // 锁定当前桶
            size_t idx = candidates[i];
            if (bucket_elements[idx] < min_load) {
                min_load = bucket_elements[idx];
                index = idx;
            }
        }
        {
            std::lock_guard<std::mutex> lock(*bucket_locks[index]); // 锁定当前桶
            auto &bucket = buckets[index];

            for(auto it = bucket.begin(); it != bucket.end(); ++it){
                if(it->key == key){
                    it->value = value; // update
                    it->update_access_time(); // 更新访问时间
                    bucket.splice(bucket.begin(), bucket, it); // 将当前节点移动到链表头部
                    return ;
                }
            }

            if(cur_size.load(std::memory_order_relaxed) >= capacity){
                // 哈希桶已满，拒绝插入
                sum_reject_time++;
                return;
            }

            buckets[index].emplace_front(key, value);
            bucket_elements[index]++;
            cur_size.fetch_add(1, std::memory_order_relaxed);
            if(bucket_elements[index] > bucket_elements_limit){
                Node evict_node = buckets[index].back(); // 删除链表尾部元素
                buckets[index].pop_back();
                bucket_elements[index]--;
                sum_evict_time++;
                cur_size.fetch_sub(1, std::memory_order_relaxed);

                if(_should_migrate(evict_node)){ // 判断是否需要迁移
                    // 将淘汰的节点迁移到 radix cache 中
                    sum_migrate_time++;
                    tree_ptr->migrate_local_cache_to_radix_cache(evict_node.key, evict_node.value, cxt, coro_id);
                }
            }
        }

    }

    Value* query(const Key &key){
        // size_t index = _get_hashcode(key);
        auto candidates = _get_candidate_buckets(key);

        for (size_t index : candidates) {
            std::lock_guard<std::mutex> bucket_guard(*bucket_locks[index]);
            auto &bucket = buckets[index];
            for(auto it = bucket.begin(); it != bucket.end(); ++it){
                if(it->key == key){
                    Value* value_ptr = &it->value;
                    it->update_access_time(); // 更新访问时间
                    bucket.splice(bucket.begin(), bucket, it); // 将当前节点移动到链表头部
                    local_cache_hit.fetch_add(1, std::memory_order_relaxed);
                    return value_ptr;
                }
            }
        }

        local_cache_miss.fetch_add(1, std::memory_order_relaxed);
        return nullptr; // 未命中
    }


private:
    const size_t LOCAL_CACHE_SIZE_MB; // 缓存大小
    const size_t capacity; // 哈希表桶的大小
    std::atomic<size_t> cur_size; // 当前大小
    Tree* tree_ptr; // 树的指针
    std::atomic<size_t> memory_used;

    // 描述节点信息
    struct Node {
        Key key;
        Value value;

        std::deque<std::time_t> access_time_deque; // 访问时间队列
        const size_t window_time_duration = 10; // 窗口时间(秒)

        Node(const Key &key, const Value &value) : key(key), value(value) {}

        // 更新访问时间
        void update_access_time(){
            std::time_t now = std::time(nullptr);
            // 清除过期的访问时间
            while (!access_time_deque.empty() && (now - access_time_deque.front()) > window_time_duration) {
                access_time_deque.pop_front();
            }

            // 添加当前访问时间
            access_time_deque.push_back(now);
        }

        // 计算访问频率
        size_t get_access_count(){
            std::time_t now = std::time(nullptr);
            size_t count = 0;

            // 清除过期的访问时间
            while (!access_time_deque.empty() && (now - access_time_deque.front()) > window_time_duration) {
                access_time_deque.pop_front();
            }

            // 队列中剩余的元素个数就是最近窗口时间内的访问次数
            return access_time_deque.size();
        }
    };

    std::vector<std::unique_ptr<std::mutex>> bucket_locks; // 哈希桶锁，每一个桶一个锁，更细粒度的锁
    std::vector<std::list<Node>> buckets; // 哈希表的桶
    std::vector<size_t> bucket_elements; // 每个桶的元素个数
    // std::unique_ptr<std::mutex> lru_lock;                  // lru锁，保护 lru_list 和 lru_map

    int sum_evict_time;
    int sum_migrate_time;
    std::atomic<size_t> local_cache_hit, local_cache_miss; // 统计命中和未命中次数
    const size_t bucket_elements_limit = 2; // 每个桶的元素个数限制
    int sum_reject_time;
    static constexpr int HASH_CANDIDATE_COUNT = 3;  // 每个 key 候选桶的个数
    static constexpr size_t MIGRATION_THRESHOLD = 2; // 迁移阈值



    size_t _get_hashcode(const Key& key, int salt) const {
        // 基于 seed+扰动 生成多种 hash
        return CityHash64WithSeed((const char*)key.data(), key.size(), salt) % capacity;
    }

    std::array<size_t, HASH_CANDIDATE_COUNT> _get_candidate_buckets(const Key& key) const {
        std::array<size_t, HASH_CANDIDATE_COUNT> buckets;
        for (int i = 0; i < HASH_CANDIDATE_COUNT; ++i) {
            buckets[i] = _get_hashcode(key, i * 0x9e3779b9); // 使用不同 salt (黄金比例质数扰动)
        }
        return buckets;
    }

    bool _should_migrate(Node& node){
        return node.get_access_count() > MIGRATION_THRESHOLD;
    }


};



#endif // _LOCALCACHE_H_