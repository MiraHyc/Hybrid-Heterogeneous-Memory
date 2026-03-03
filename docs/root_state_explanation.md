# 代码中 root 是空吗？

## 结论

- **初始化阶段通常会被置空**：`Tree` 构造时会读取 `root_ptr_ptr`，在 `Node 0` 上通过 CAS 把非空根指针重置为 `InternalEntry::Null()`。
- **运行阶段不一定为空**：一旦有插入发生，若当前根为 `Null`，插入路径会直接写入叶子（或后续结构），此后 root 就不再是空。

所以更准确地说：**root 在“初始化后、首个有效插入前”可以是空；不是永久为空。**

## 代码依据（关键路径）

1. `Tree::Tree(...)` 中：
   - 读取 `root_ptr_ptr` 当前值；
   - 若当前节点是 `Node 0` 且值非空，CAS 置为 `InternalEntry::Null()`。

2. `Tree::insert(...)` 中：
   - 从 `root_ptr_ptr` 读取根 entry；
   - 若 `p == InternalEntry::Null()`，走“注入 leaf”的分支，把根从空状态写成有效 entry。

