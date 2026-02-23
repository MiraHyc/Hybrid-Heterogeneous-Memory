# Tree::range_query 流程体系结构图（仅 Tree 主流程）

> 只展示 `Tree::range_query(from, to, ret)` 内部流程，不展开 LocalCache/YCSB 入口。

```text
┌──────────────────────────────────────────────────────────────────────┐
│                    Tree::range_query(from, to, ret)                 │
└──────────────────────────────────────────────────────────────────────┘
                │
                ▼
      [0] 初始化线程本地工作集
      survivors / rs / si / range_cache / tokens
                │
                ▼
      [1] 生成初始 survivors
         ├─(有 index cache) search_range_from_cache -> range_cache -> survivors
         └─(fallback) search_entries(from, to-1, lcp, survivors)
                │
                ▼
┌──────────────────────────────────────────────────────────────────────┐
│                      [2] next_level 循环                            │
│                  while survivors 非空 持续迭代                      │
└──────────────────────────────────────────────────────────────────────┘
                │
                ▼
      [2.1] 构造本层批量 RDMA 读请求
         - 遍历 survivors
         - 用 token(地址)去重
         - 组装 rs 与 si 映射
                │
                ▼
      [2.2] dsm->read_batches_sync(rs)
         - 批量读回本层 node/leaf 数据
                │
                ▼
      [2.3] 处理读回结果（按 si[i].e.is_leaf 分支）
         ├─ Leaf 分支
         │   a) is_valid? 否 -> (可选 invalidate) + 重读 entry + 回 survivors
         │   b) is_consistent? 否 -> 回 survivors 重试
         │   c) for_each_kv 过滤 [from,to) 写入 ret
         │
         └─ Node 分支
             a) is_valid? 否 -> (可选 invalidate) + 重读 entry + 回 survivors
             b) 是 -> range_query_on_page(...)
                   - 按 BORDER/INSIDE/OUTSIDE 做页内剪枝
                   - 生成下一层 survivors
                │
                ▼
      [2.4] 回到 next_level
                │
                ▼
      [3] survivors 为空 -> 结束返回
```

---

## 模块职责（仅 Tree 内）

- `range_query`：总调度（初始化、分层批读、校验、重试、结果汇总）。
- `range_query_on_page`：页内边界状态传播与子分支剪枝，产出下一层 `survivors`。
- `ret`：最终范围结果集（`[from,to)`）。

---

## 一句话总结

`Tree::range_query` = **初始候选发现** + **按层批量 RDMA 读取** + **读后校验/必要重试** + **页内状态剪枝下探**，直到 `survivors` 为空。
