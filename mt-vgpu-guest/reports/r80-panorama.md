# r80：vGPU 全景测试评估——从地址分配到执行的分段盘点（用户指令）

本轮把"内存地址分配 → 复制 → 渲染 → 执行"逐段重测/复核一遍：
L1 离线门禁本轮实跑（221 Python OK + 268 C OK），内核构建本轮实跑
（exit 0，增量零警告；干净重编暴露 4 处 r38 期旧警告，见 §5），
dmesg 全 boot 日志 0 WARN/BUG/Oops，活体 sysfs 只读快照与 §12 一致
（pending=0/completed=23，objects=34，引用 38/0/0）。
零硬件触碰（无 live 提交、无模块变动）。

## 0. 底座：会话与固件（✅ 本轮只读复验）

`trial pinned=1 connected=1`，`runtime guest=2 firmware=2`，
`connection driver=2 firmware=2`；在载桥 build-id `894faf50` 与在盘一致；
`/dev/dri` 双 render 节点俱在。会话健康，与 r71 收尾状态一字不差。

## 1. 内存地址分配（✅ 基本闭环，1 个已知近似）

| 环节 | 状态 | 证据 |
|---|---|---|
| 堆表（15 heaps，厂商蓝图） | ✅ | `test_windows_heap_table_decoded` 等 4 个门禁测试；r52 阶梯 |
| PMR 分配（rung8：12 PMR / 295 KiB，flags 解码） | ✅ | r54/r57；r73 活体复现 |
| VA 预留/映射/解绑台账 | ✅ | bA38/bA41；活体 plan 日志行 |
| arena backing（每文件 2 MiB lazy，`fallbacks=0`） | ✅ | r60；r73/r78 `arena close` 行复验 |
| cover-page + 先占独占 | ⚠️ 近似 | r61：未对齐 prefix/tail 整页近似，translator 用到时重审（§10 原话） |
| VA 上限 15872 | ✅ | r42 |

## 2. 复制（✅ 链路打通）

TQX 复制链（r23–r32：连续复制、用户态链路、DRM/GEM/syncobj、原生矩形填充）✅；
DMA 真机五步（r45–r49：smoke → mask40 → handoff → TQX 回读 → GPU-PA 窗口，
`dma_addr`/`gpu_pa` 分离）✅。约束：r67 后 DMA 路径只做挂起探测（短超时，
超时停手）——纪律仍有效。

## 3. 渲染（✅ 合成路径全绿；⚠️ 真实绘制三缺口）

- render/compute 上下文、syncprim、1080p 大表面、CSW 闭合、DM2 连通 ✅（r33–r38）
- kick 合成包：84B 只有同步记账，counts=0 ✅（r53）
- 非零 check：fabricated 6/6 + 活体 `ufo_known=1/1` ✅（r72/r73）
- 缺口 A——CCB 内容：server 侧持有，pack 公式已得，非零内容只能来自绘制路径 ⚠️（r56/r76）
- 缺口 B——update/DDK2：RGXKickSync 无此输入（r74 证伪）；DDK2 需桥特性开关
  （改代码+重编+重载，r78 待立项）⚠️
- 缺口 C——ZSBuffer：13 参数形状清，需真 heap/context 指针（MIW 崩溃点），未驱动 ⚠️；
  `RGXKickTA` 止于 `PrepareTA+0x253` ⚠️

## 4. 执行（✅ 手工负载；❌ 翻译负载从未执行）

手工 DM2：单帧 `completed=1`（r66，fence 证据）→ 64 KiB 像素读回（r70）→
20 帧批量零 fault（r71）。本轮**未重跑**（对象存储满，重跑会被 `-EBUSY` 拒；
现状计数器与 r71 收尾一致，侧证无退化）。
翻译 kick 执行：从未发生，真提交入口 `-ENOTTY` 仍是 S4 边界 ❌；
T3（DM 队列格式）未知 ❌——输出侧是唯一结构性空白（r79 主线）。

## 5. 门禁与卫生（本轮实跑 + 1 个新发现）

- L1：221 Python + 268 C 全绿 ✅（本轮）
- `make kernel`：exit 0 ✅；**新发现**：增量构建零警告，但干净重编有 4 处
  r38 期旧警告（`mt_drain_pending.c` 未用变量 ×2、`mt_fw_event_io.h` 未用 const ×1、
  `mt_fix_poll` 缺 MODULE_DESCRIPTION ×1）——快照"零警告"须加"增量"限定，
  或清掉这 4 处（ trivial，另起一轮，不在本轮动）。
- dmesg：0 WARN/BUG/Oops ✅（本轮，全量 boot 日志）
- L3/L4：上次全绿 r52/r59/r63；本轮未重跑（活会话禁用）

## 还差什么（按序）

1. T3 DM 格式 recon（离线语料，r79 主线，**最优先**）
2. check-only 首帧假设验证（绕开 DDK2 的首胜路径，r79 建议）
3. 真实 CCB 内容（需绘制路径活体）
4. 特性开关单独立项（update/DDK2；改桥+重载，待批）
5. ZSBuffer 堆指针（与 4 同家族，可并案）
6. 会话更新窗口规划（对象满；硬仗前置条件）
7. 门禁卫生：4 旧警告清零 + "零警告"加限定（小轮）
