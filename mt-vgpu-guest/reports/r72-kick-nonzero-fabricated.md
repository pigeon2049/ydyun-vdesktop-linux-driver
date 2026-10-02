# r72：fabricated RGXKickSync 复现非零 check count（真实绘制 kick 观察第一步）

STATUS 下一步 1 的前半句已闭环：非零 counts 不需要真机也能复现——用
≥436 字节的手工 kick 结构体驱动 `RGXKickSync`，fabricated 重放 6/6 发出
`0x88:0x4 check=1 update=0`，三组 check 数组指针有效。全程零硬件触碰
（无 PASSTHROUGH，shim 从不转发真实 ioctl；活会话引用数未变）。

## 复现（`reports/r72-kick-nonzero-check1.jsonl`，seq 124）

harness 在 rung8 链尾把 `buf 26` 从 224 字节扩大到 512 字节并注入
`u32@216=1`（count）、`u64@224=*b10`（fabricated sync 句柄）、
`u32@232=1`（fence 值），`RGXKickSync → 0`，桥包 IN（84 字节）：

```text
check=1 update=0，三 check 指针均为 UMD 堆有效地址，update 侧全零
```

gdb（`UMD_TRAP="136:4"`，地址具确定性）单步验证数组内容正是注入值：

```text
pCheckDevVarOffset → 0x00000000   （struct+0 未注入，保持 0）
pCheckValue       → 0x00000001   （注入的 fence 值）
phCheckUFOBlock   → 0x000000000000600c （fabricated sync 句柄）
checkCount        → 1
```

调用栈确认走真实签发路径：`RGXKickSync → marshalling 包装 → ioctl`（与 r62 一致）。

## 结构体映射（`RGXKickSync` 反汇编实测，`RGXKickSync@@Base+0x…`）

`RGXKickSync` 第 3 参数（rung8 的 `b26`）是远大于 224 字节的客户端结构体：

| 偏移 | 形状 | 含义 |
|---|---|---|
| `0xd8` | u32 | check 条数（`jne` 进 unrolled 拷贝；`cmpl` 上限 12） |
| `0xe0+i*0x10` | u64 | 第 i 条 sync 句柄/指针（`i < count`，≤12 条展开） |
| `0xe8+i*0x10` | u32 | 第 i 条 fence 值 |
| `0x1b0` | u32 | 第二个条数（传进签发调用的 r11d 参数；疑似 update 侧，数组偏移本轮未定位） |

`count != 0` 时 UMD 先 `PVRSRVCallocUserModeMem(count*24)` 做 server 侧数组，
再逐条拷贝——这正是 T1（数组拷贝）要在 bridge 里复刻的形状（r62 算法不变，
输入地址从"未知"变为上表）。

## 为什么 rung8 的 224 字节恰好能用（合成路径零 count 的解释）

- `0xd8=216 < 224`：count 本体在缓冲区内，零值使 `0xe0+` 的越界读永不发生。
- `0x1b0=432 > 224`：**每次调用都稳定越界读 4 字节**，落在 calloc 后的新鲜
  零页堆上 → 读到 0 → update 侧跳过。这是运气，不是规约：
  堆复用弄脏该位置即可能走偏（下条附带证据）。
- 结论：224 只是"零值下恰好不炸"的截断尺寸；真实绘制路径的结构体下限是
  **436 字节（`0x1b4`）**，rung8 之后任何手工 kick 必须按此分配。

## 附带 caveat（离线侧，不影响活会话）

fabricated 重放约 4/16 次在 `RGXCreateRenderContext` 段错误（`coredumpctl`
有记录），重试即过，成功 6 连击后稳定。形态符合"UMD 读了堆垃圾指针"
（与 0x1b0 越界读同类问题），纯用户态 flake，无硬件影响。
`UMD_TRAP` 仍只在 fabricated 分支生效（r56 结论不变）。

## 未做与下一步

- update 侧（`0x1b0` 条数对应的数组偏移）未定位：需要继续跟签发调用的
  数组参数来源，不在本轮。
- 真机问题仍冻结：对象存储已满 + 活会话 freeze，passthrough 抓包
  （`UMD_DUMP_BRIDGE` 真实绘制）需单独批准，本轮未碰。
- 门禁建议（未动手）：`test_pvr_kick_packet.py` 可加"客户端结构体下限 436 字节"
  断言，等 update 侧定位后一起落。
