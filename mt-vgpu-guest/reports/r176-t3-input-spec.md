# r176：T3 translator 输入规约 v1（SubmitTransfer3 CCB；离线）

- **结论**：translator 输入侧冻结为规约 v1：envelope、header 字段表、body 拷贝语义、扩展区状态机、观测实例全账、未知项清单、translator 消费契约。输出侧（DM 队列格式）仍缺，规约明确标出边界，不越界。本轮纯文档（门禁复核 274+272 全绿），零硬件触碰。

## 1. Envelope（来自 `0x89:0xa` 108B IN，r151 ABI）

| 槽位 | 含义 | 观测值（fill） | 状态 |
|---|---|---|---|
| `ccb_data@88` | CCB 的 GPU VA | `0x8000f44000`（跨轮稳定） | 已定 |
| `ccb_bytes@104` | 生成缓冲长度 | `0x1200`（分配器量子，非内容长度） | 已定 |
| `check_count@8` / `update_count@36` / `pmr_sync_count@64` | 同步组规模 | `0/2/0` | 已定（update 走 IN 指针，不在 CCB 内，r159–r160） |
| `submit_flags@84` / `submit_opaque@96` | 透传 | `0/0` | 未知 |

## 2. Header `[0x0, 0x58)`（`← job+0x0` 逐字节拷贝）

| 偏移 | 内容 | 含义状态 |
|---|---|---|
| `+0x00–0x0F` | 零 | job 该段为空 |
| `+0x10` | u64 = CCB+`0x58` | 已定（生成器 `*(job+0x10)` 写回，r161 活体确认写入者） |
| `+0x1C` | u32 `0x67` | 未知（只记录值） |
| `+0x28` | u32 `0x1078`（=`0x58+0x1020`） | 已定（两段定长拷贝基线） |
| `+0x2C` | u32 `1` | 未知 |
| `+0x40` | u16 轮变（`28xx/7c/80/5c`，非单调） | 未知（r161 计数器命名已收回；r165 GDB 证写入者为头拷贝，源欄位未命名） |

## 3. Body `[0x58, 0x1078)`（`← job+0x58`，`0x1020B` 逐字节拷贝）

- 稀疏：`+0x58`（`02`）、`+0x1060`（u16 `0x1020` = `0x1078−0x58`，观测值不解释）、余全零。
- 语义：job 传输描述主体的影子；逐字节一致性由 r174 真实捕获背书。

## 4. 扩展区状态机（生成器伪 C，`decompiled.c:37014` 起）

```
u = 0x1078
u += *(job+0x1064) * 0x80        # 段A（本实例 0）
u += *(job+0x106c) * 0x30        # 段B（本实例 0）
w = u + *(job+0x2c) * 0x18       # region 条目区尾 = 载荷区首
for e in job[+0x1130 .. +0x1128):
    emit(e, 0x18) at u; u += 0x18
    if e.type in {2,3,4,5}:
        *(job_e - 0x20) = w                    # 条目+8  patch（r175 解）
        copy(*(lvar+0x28)B, lvar -> w); w += len
        for s in subs: copy(lenA); copy(lenB)  # w 推进
    elif e.type in {8,10}: pass                # 裸条目
    else: "Not Supported"                      # 未见实例
```

条目 0x18 布局（type=3 实例）：`[type@0][w写回@8][0x100@16]`。

## 5. 观测实例全账（fill，`0x1200` 量子）

`c1064=0/c106c=0/c2c=1`，单 type=3 条目；`0x1078+0x18+32+224+40 = 0x11B8` 内容终点，尾部补零；39 非零字节逐项溯源见 r175；真实捕获 39/39 一致见 r174。

## 6. Translator 消费契约（输入侧）

- T3 输入 = `(ccb VA, ccb len)` + `check/update/pmr_sync` 三数组指针（UMD 内存，调用进程上下文可达，r63 只读观察已验证路径）。
- VA→字节解析沿用 observe handler 的三重定界（reservation 包含 → binding → PMR host + 偏移，r174 已上线）。
- T3 输出（DM 队列重编）与完成语义（fence/update 写回）**不在本规约内**；check-only 翻译（r147–r149）是当前唯一已验证的执行语义。

## 7. 未知项（诚实清单）

`+0x1C`、`+0x2C`、`+0x40` 源、`submit_opaque`、type≠3 的条目/载荷形状、多 region 行为、`0xa3xxxx` 小 VA 归属、DM 队列格式。任一项落定即发规约 v2。
