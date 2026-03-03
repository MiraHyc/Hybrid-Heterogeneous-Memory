# 在代码中，谁在 range query 过程中做 validation check？

## 结论

`range_query` 过程中的 **validation 主体是 `Tree::range_query`**：

- 它在处理每个 `survivor` 的读回结果时，按“叶子 / 内部节点”分支主动调用校验逻辑；
- 具体校验函数由数据结构本身提供（`Leaf::is_valid` / `Leaf::is_consistent` / `InternalPage::is_valid`）；
- 若命中的是 index cache 入口并发现无效，会由 `Tree::range_query` 触发 `index_cache->invalidate(...)` 并重读 entry 再重试。

## 责任划分

### 1) 调度与重试：`Tree::range_query`

`Tree::range_query` 是整个 range 扫描循环的控制者：

1. 组装并批量读取候选项；
2. 对每个读回对象执行有效性检查；
3. 失败时失效缓存、重读 entry、把任务放回 `survivors` 重试。

### 2) 叶子校验：`Leaf` 提供规则，`Tree` 发起调用

- `Leaf::is_valid(p_ptr, from_cache)`：校验叶子是否有效、以及来自 cache 时反向指针是否匹配；
- `Leaf::is_consistent()`：校验叶内数据校验和（checksum）。

### 3) 内部节点校验：`InternalPage` 提供规则，`Tree` 发起调用

- `InternalPage::is_valid(p_ptr, depth, from_cache)`：校验节点未删除、深度合法、以及来自 cache 时反向指针一致。

### 4) cache 失效动作：`index_cache->invalidate`

当 `Tree::range_query` 发现来自 cache 的 entry 过期/失效时，由它触发失效，随后重读远端 entry 再继续。

## 一句话总结

- **谁在做 validation check？** 调度层面是 `Tree::range_query`；
- **检查依据是谁定义的？** `Leaf` / `InternalPage` 结构体的方法定义了校验标准；
- **失败后谁处理？** 仍是 `Tree::range_query` 负责 invalidate + re-read + retry。


## 补充：是用户线程在做校验吗？

是的，**在当前实现里，执行 `Tree::range_query` 的用户工作线程（worker thread）就在做这些校验**。

- `Tree::range_query` 在该线程上下文中发起 `read_batches_sync`，随后立刻在同一线程里逐项执行 `Leaf::is_valid / is_consistent`、`InternalPage::is_valid`；
- 校验失败后的 `index_cache->invalidate`、`read_sync` 重读 entry、`survivors` 重试，也都发生在这条执行 `range_query` 的线程路径上；
- 代码里没有单独的后台“校验线程”来接管这部分逻辑。

