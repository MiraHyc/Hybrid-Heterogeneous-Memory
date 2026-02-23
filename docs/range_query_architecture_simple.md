# 更易懂的体系结构图（Range Query / Cache 协同）

下面这张图把系统简化成 **3 层路径 + 1 个反馈环**，用于讲解“请求到底怎么走”。

```text
                        ┌──────────────────────────────┐
                        │        Client / YCSB         │
                        │   (GET / UPDATE / SCAN)      │
                        └──────────────┬───────────────┘
                                       │
                                       ▼
                    ┌──────────────────────────────────┐
                    │   L1: LocalCache (key->value)    │
                    │  - 命中: 直接返回 value           │
                    │  - 未命中: 进入 Tree              │
                    └──────────────┬───────────────────┘
                                   │ miss
                                   ▼
         ┌──────────────────────────────────────────────────────────┐
         │ L2: Tree::search / Tree::range_query                    │
         │  a) 先查 IndexCache(RadixCache) 找“更深入口”            │
         │  b) 未命中则从 Root 开始                                │
         │  c) 按层批量 RDMA 读 nodes/leaves（range 用 survivors） │
         │  d) 在本线程做 valid/consistent 校验 + 必要重试         │
         └──────────────┬───────────────────────────────────────────┘
                        │
                        ▼
              ┌──────────────────────────────┐
              │ L3: Global Tree / Remote DSM │
              │  (权威数据与索引结构)         │
              └──────────────────────────────┘


反馈环（命中与学习）：

  [Global/Tree 返回结果]
          │
          ├─► 回填 LocalCache（下次 GET/小SCAN 更快）
          │
          └─► 访问路径写入 RadixCache（下次更快定位入口）

```

---

## 一眼看懂版（按请求类型）

- **GET**：`LocalCache -> (miss) Tree.search -> RadixCache辅助定位 -> Remote读 -> 回填LocalCache`
- **小范围 SCAN**：逐 key 优先走 LocalCache；miss 回源。
- **大范围 SCAN**：直接 `Tree.range_query`，按 survivors 分层批量读。

---

## 你讲解时可以用的 3 句话

1. `LocalCache` 管“值命中”；`RadixCache` 管“索引入口命中”；`Global Tree` 是最终真值。
2. `range_query` 不是简单逐 key 点查，而是“候选入口 -> 分层批量读 -> 页内剪枝 -> 下一层”。
3. 校验（valid/consistent）和失败重试发生在调用 `Tree` 的工作线程中，不是后台线程。
