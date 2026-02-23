# RadixCache `Frame` 剪枝判断表（`search_range_from_cache`）

> 适用函数：`src/RadixCache.cpp::RadixCache::search_range_from_cache`。
>
> 目标：把 `Frame{node, prefix, l_state, r_state}` 在 DFS 遍历中的“保留/剪枝”规则做成可直接核对的判断表。

## 1) 状态定义

- `BORDER`：当前前缀与范围边界（`from` 或 `to_inc = to - 1`）仍然相等，后续字节还需要继续比较。
- `INSIDE`：当前前缀已经严格落在范围内部，不再受该侧边界约束。
- `OUTSIDE`：当前前缀已经越界，可直接剪枝。

---

## 2) Header 层状态推进（`hdr.partial`）

设当前要比较的 header 字节为 `h`，对应边界字节分别为 `f`（来自 `from`）和 `t`（来自 `to_inc`）。

### 2.1 左边界 `l_state` 推进

| 进入状态 | 比较条件 | 推进后状态 | 说明 |
|---|---|---|---|
| `BORDER` | header 全字节都与 `from` 对应字节相等 | `BORDER` | 仍贴着左边界 |
| `BORDER` | 首个不等字节满足 `h > f` | `INSIDE` | 已经进入区间内部 |
| `BORDER` | 首个不等字节满足 `h < f` | `OUTSIDE` | 落到左边界之外，剪枝 |
| `INSIDE` | 不比较 | `INSIDE` | 已在区间内部，保持 |
| `OUTSIDE` | 不比较 | `OUTSIDE` | 已越界，保持 |

### 2.2 右边界 `r_state` 推进

| 进入状态 | 比较条件 | 推进后状态 | 说明 |
|---|---|---|---|
| `BORDER` | header 全字节都与 `to_inc` 对应字节相等 | `BORDER` | 仍贴着右边界 |
| `BORDER` | 首个不等字节满足 `h < t` | `INSIDE` | 已经进入区间内部 |
| `BORDER` | 首个不等字节满足 `h > t` | `OUTSIDE` | 超过右边界，剪枝 |
| `INSIDE` | 不比较 | `INSIDE` | 已在区间内部，保持 |
| `OUTSIDE` | 不比较 | `OUTSIDE` | 已越界，保持 |

### 2.3 Frame 级剪枝

| 条件 | 动作 |
|---|---|
| `l_state == OUTSIDE` 或 `r_state == OUTSIDE` | **直接剪枝当前 Frame**（`continue`） |
| 其他情况 | 继续检查子分支（`records`） |

---

## 3) Child 分支层状态推进（`records` 的 `partial`）

设当前 child 的 `partial = p`，并令

- `from_partial = from[next_depth_idx]`
- `to_partial = to_inc[next_depth_idx]`

### 3.1 左边界 `e_l_state`（由父状态 `l_state` 派生）

| 父状态 | 比较条件 | 子状态 `e_l_state` |
|---|---|---|
| `BORDER` | `p == from_partial` | `BORDER` |
| `BORDER` | `p > from_partial` | `INSIDE` |
| `BORDER` | `p < from_partial` | `OUTSIDE` |
| `INSIDE` | 不比较 | `INSIDE` |
| `OUTSIDE` | 不比较 | `OUTSIDE` |

### 3.2 右边界 `e_r_state`（由父状态 `r_state` 派生）

| 父状态 | 比较条件 | 子状态 `e_r_state` |
|---|---|---|
| `BORDER` | `p == to_partial` | `BORDER` |
| `BORDER` | `p < to_partial` | `INSIDE` |
| `BORDER` | `p > to_partial` | `OUTSIDE` |
| `INSIDE` | 不比较 | `INSIDE` |
| `OUTSIDE` | 不比较 | `OUTSIDE` |

### 3.3 Child 级剪枝

| 条件 | 动作 |
|---|---|
| `e_l_state == OUTSIDE` 或 `e_r_state == OUTSIDE` | **剪枝该 child 分支**（不产出结果，不入栈） |
| 其他情况 | 可产出 `RangeCache`，并在 `next_node != nullptr` 时把 child 入栈 |

---

## 4) 一页速查（最常用）

| 层级 | 规则 | 结果 |
|---|---|---|
| Header | 左侧首个不等：`h < f` | 剪枝（`l_state=OUTSIDE`） |
| Header | 右侧首个不等：`h > t` | 剪枝（`r_state=OUTSIDE`） |
| Child | `p < from_partial` 且左侧仍 `BORDER` | 剪枝该 child |
| Child | `p > to_partial` 且右侧仍 `BORDER` | 剪枝该 child |
| Frame/Child | 任一侧 `OUTSIDE` | 直接剪枝 |
| Frame/Child | 两侧都非 `OUTSIDE` | 保留继续遍历 |

---

## 5) 与当前实现字段对应

- `Frame`：`CacheNode* node, Key prefix, State l_state, State r_state`
- Header 比较：`hdr->partial` + `hdr->depth`
- Child 比较：`cache_map_entry.first`（`partial`）
- 剪枝位置：
  - Frame 级：`if (l_state == OUTSIDE || r_state == OUTSIDE) continue;`
  - Child 级：`if (e_l_state == OUTSIDE || e_r_state == OUTSIDE) continue;`

---

## 6) 一个可直接讲解的例子（带剪枝）

下面给一个 2 字节前缀的简化示例（只为了说明状态流转）。

- 查询范围：`[from, to)`
- 设 `from = [0x40, 0x20]`
- 设 `to   = [0x40, 0x80]`，因此 `to_inc = to - 1 = [0x40, 0x7f]`

初始 frame：`{prefix=??, l_state=BORDER, r_state=BORDER}`。

### Step A：Header 层判断

假设当前节点 `hdr.partial = [0x40]`。

- 对左边界：`0x40 == from[0]`，左状态保持 `BORDER`；
- 对右边界：`0x40 == to_inc[0]`，右状态保持 `BORDER`。

所以该 frame **不剪枝**，继续看 child 分支。

### Step B：Child 层判断（关键剪枝）

此时比较第二字节，

- `from_partial = 0x20`
- `to_partial   = 0x7f`

假设该节点有 4 个 child：`p in {0x10, 0x20, 0x50, 0x90}`。

| child partial `p` | 左状态变化（相对 `0x20`） | 右状态变化（相对 `0x7f`） | 结果 |
|---|---|---|---|
| `0x10` | `BORDER -> OUTSIDE` (`p < from_partial`) | `BORDER -> INSIDE` | **剪枝**（左侧越界） |
| `0x20` | `BORDER -> BORDER` | `BORDER -> INSIDE` | **保留**（边界分支） |
| `0x50` | `BORDER -> INSIDE` | `BORDER -> INSIDE` | **保留**（完全在内部） |
| `0x90` | `BORDER -> INSIDE` | `BORDER -> OUTSIDE` (`p > to_partial`) | **剪枝**（右侧越界） |

### Step C：你可以怎么讲

- `0x10` 和 `0x90` 被“当场丢弃”，这就是剪枝收益；
- `0x20` 是左边界分支，要继续精细比较后续字节；
- `0x50` 已经是 `INSIDE/INSIDE`，后续基本可快速放行。

一句话总结这个例子：

> 在 `BORDER/BORDER` 状态下，child partial 只要落到 `[from_partial, to_partial]` 之外，就会立刻转成 `OUTSIDE` 并被剪枝；区间内分支才会进入下一轮遍历。

