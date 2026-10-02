# r77：DDK2 rsi 身份落定——完整创建的 kicksync 对象（语料三角验证）

r75/r76 遗留的"DDK2 第 2 参数是什么对象"已有解析解（§9 流程首验）：
它就是走完**完整创建路径**（features≥2 + SyncPrim + SubmissionBuf）的
kicksync 客户端对象——`+0x8/+0x18/+0x28` 三槽分别由三步 gated 调用填入，
rung6 的 legacy 对象三槽恒零所以必崩。render-obj 候选彻底排除
（其 `+0x20/+0x28` 是 PerfCount/AppHint 槽位，形状不对）。

## 三槽来源（`decompiled/linux-legacy-umd-5.2.0/decompiled.c` 行号即证据）

以 `RGXCreateKickSyncContextCCB@0x52180`（L28249）的 `plVar3`（`Calloc(0x30)`）为基准：

| 槽位 | 填入者 | 语料位置 |
|---|---|---|
| `+0x8` | `FUN_00135d90` 注册（server ctx） | L28320（`plVar3 + 1` 传出） |
| `+0x18` | `_SyncPrimAlloc`（`FUN_001a05d0@L77633`，`*param_2 = __ptr` 即 sync-prim 对象） | L28321（`plVar3 + 3` 传出） |
| `+0x20` | 置零（kick 计数器，DDK2 每次 `+1`） | L28327（`plVar3[4] = 0`） |
| `+0x28` | `SubmissionBufAlloctorCreate`（`+0x48` 出生置零） | L28331–28335 |

DDK2 侧消费（`RGXKickSyncDDK2@0x52fc0`，L28669 起 325 行）逐项对上：
`param_2+8` 进签发调用（L28952 `uVar17`）、`+0x18` 进 `local_1f8`、
`+0x20` 自增、`[+0x28]+0x48` 写计数器（崩溃点，L28929–28933）。
`param_4` 是可选 OUT 回显（`+0x408/+0x8/+0x10/+0x20`），传 0 跳过——
rung8 传 `u0` 是对的，不是缺参。

## 推论（待活体验收，不属本轮断言）

- DDK2 不可被"拼参数"在 fabricated 下驱动：三槽需要真 server 的
  SyncPrim/SubmissionBuf 分配，legacy 分支一个都不给。
- 唯一的 DDK2 可达路径：活体 passthrough + 非零 CCB create，走完三步门控，
  再以该对象为 rsi 调 DDK2。这正是 r76 提出的待批实验，本轮把它从
  "形状未知"推进到"对象已知、只差一次活体 create"。
- 方法论：r75 objdump 阶段对 render-obj 的怀疑方向是对的（富对象），
  但具体归属错了；语料伪 C 用 551 行 render 函数 + 128 行 create 函数
  的写入点一次性钉死——§9"先查语料"不是虚文。

## 边界

本轮零执行新实验（无新 trace 可归档）：全部结论来自语料行级引用 +
r74/r75 已归档证据复核；最终确认必须等活体 create（待批）。
