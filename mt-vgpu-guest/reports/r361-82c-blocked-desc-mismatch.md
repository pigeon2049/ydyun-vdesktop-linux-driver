# r361：0x82:0xC 活体 IN 参数观察——被 b10 描述子布局变化阻塞，SyncPrimRef 崩溃无法复现 r353 路径

**结论**：本轮未能完成活体 0x82:0xC IN 参数观察。阻塞根因为 b10 `CreateSyncPrim` 描述子的 `+0x18` 字段在本环境为 NULL（r352/r353 记载为有效指针），导致 `SyncPrimRef` 在 `0xa0fa0: sub 0x30(%rdx),%eax` 处解引用崩溃，TA 路径无法推进到 `ZeusSyncPrimImportFD → 0x92930`。静态分析确认了 0x82:0xC 的 268B IN 缓冲构造逻辑（`0x36ec0` 函数，`in_len=0x10c`）。**本轮未发送任何 bridge ioctl，未提交 GPU 工作，freeze 完好。**

## 1. 基线（开工前）

| 检查项 | 结果 |
|---|---|
| 在载桥 build-id | `0d6bb8d7…`（r356 构建，含 observer）✓ |
| `mt_pvr_bridge` refs | 0 ✓ |
| `mt_guest_probe` refs | 1 ✓（freeze 未动） |
| UMD `.so` SHA | `b3058c02…34237b0` ✓ |
| dmesg | 干净，无 WARN/BUG/Oops |

## 2. 尝试路径与结果

### 2.1 复现 r353 的 GDB 驱动 TA 路径

按任务要求复用 r353 方法：
- 真实建连：`connect 0` + `PVRSRVConnectionCreateDevice b7 u1 u0`（fabricated，与 r353 一致）
- GDB 开 ASLR（`set disable-randomization off`）
- 在 `CreateSyncPrim` 返回处捕获 b10 描述子（验证 `desc+8=1` ✓）
- 在 `0x79c92`（`mov 0x48(%rdx),%rdi`）处按 `$rdx+0x48` 定位槽位，poke b10 描述子（`old=0x0 → verify=desc` ✓）

**结果**：poke 成功后，`SyncPrimRef` 在 `0xa0fa0` 处 SIGSEGV，未能如 r353 般返回 0。

### 2.2 崩溃根因定位

反汇编 `SyncPrimRef @ 0xa0f70`：

```asm
a0f98: mov 0x18(%rdi),%rdx    # rdx = *(desc+0x18)
a0f9c: mov 0x20(%rdi),%rax    # rax = *(desc+0x20)
a0fa0: sub 0x30(%rdx),%eax    # <-- CRASH: dereferences rdx
```

GDB 实测本轮 b10 描述子布局：

| 偏移 | 本轮实测 | r352/r353 记载 |
|---|---|---|
| `+0x08` | 1 ✓ | 1 |
| `+0x18` | **0x0 (NULL)** ✗ | `<ptr>` |
| `+0x20` | `0x56226978fe60` (ptr) | 0 |

**`+0x18` 为 NULL 导致 `*(0x0+0x30)` 崩溃。** 描述子由 `CreateSyncPrim` 生成，输入参数与 r353 完全一致（`b14* b10 b11`，`b14` 来自 `*b7*+176`），但输出布局不同。原因待查（可能是 UMD 内部堆状态/全局状态差异，或 `*b7*+176` 指向内容变化）。

### 2.3 尝试过的绕过方案（均未成功）

1. **去掉 early fd 预开**：确认崩溃与 GDB 嵌套 `call open/malloc` 无关（纯 r353 setup 仍崩溃）。
2. **SyncPrimRef 入口短路**：在 `0xa0f70` 处伪造 `*(rsi)=0`、`rax=0` 并跳转到返回地址。跳转成功（`ret=0x...79caf`，即 `call` 后指令），但下游仍 SIGSEGV（调用者依赖 `SyncPrimRef` 的其他副作用）。
3. **基础链路验证**：不做 poke 时 `RGXKickTA → 3`（符合预期，SyncPrimRef 返回 3 后 TA 路径正常退出，无崩溃）。证明 harness 与 UMD 基本交互正常。

### 2.4 静态分析：0x82:0xC IN 缓冲构造（`0x36ec0`）

虽未拿到运行时值，但反汇编确认了构造逻辑：

```asm
36ece: mov $0x82,%esi        # bridge = 0x82
36edc: mov $0x10c,%r8d       # in_len = 268 (0x10c) ✓ 与 r356 wire 结构一致
36ec0: lea 0x8(%rsp),%r10    # r10 指向输入参数结构
36ef4: movd 0x78(%r10),%xmm3 # 从输入结构多处偏移加载字段...
36efa: movd 0x80(%r10),%xmm2
36f0d: movd 0x60(%r10),%xmm1
...
```

函数从 `r10` 指向的结构体多处偏移（`0x58/0x60/0x68/0x78/0x80/0x98/0xd0/0xe0`）加载字段，经 XMM 打包后写入 268B IN 缓冲。完整 72 条指令的反汇编已存档（`build/traces/r361/func_36ec0.asm`）。

**意义**：IN 缓冲字段来源于 TA kick 上下文结构体，而非零散构造。R2 实现时需按此映射填充。

## 3. Live 边界（如实记录）

- **未发送任何 `0x82:0xC` ioctl**：TA 路径在 `SyncPrimRef` 即崩溃，从未到达 `0x92930`（`PVRSRVBridgeCall`）。
- **未提交 GPU 工作**：符合本轮安全约束。
- **未做 rmmod/insmod**：桥保持 r356 构建在载。
- **未跑 `make probe`**：WITH_BRIDGE 会 rmmod，以完整交互为等效健康证据（沿用 r358/r360 取舍）。
- GDB 实验均为 userspace 进程（harness），对内核零影响。

## 4. 收尾安全检查

| 检查项 | 结果 |
|---|---|
| `mt_pvr_bridge` refs | 0（不变）✓ |
| `mt_guest_probe` refs | 1（不变，freeze 完好）✓ |
| dmesg | 无新增 WARN/BUG/Oops，仅正常 arena close 日志 ✓ |
| 桥 build-id | 仍为 `0d6bb8d7…`（未重载）✓ |

## 5. 证据文件

- `reports/r361-desc-dump.txt`（0600）：b10 描述子 `+0x18=NULL` 的 GDB 实测输出
- `build/traces/r361/func_36ec0.asm`（gitignore）：`0x36ec0` 完整反汇编（72 指令）
- 本报告：`reports/r361-82c-blocked-desc-mismatch.md`

## 6. 建议的 r362

**方向 A（推荐）**：调查 b10 描述子 `+0x18` 为 NULL 的根因。
- 对比 `*b7*+176` 在 r353 环境 vs 当前环境的值
- 追踪 `CreateSyncPrim` 内部如何设置 `+0x18`（反汇编）
- 若确认是环境差异，调整 fabricated 参数使描述子合法化

**方向 B**：绕过 `SyncPrimRef` 的替代观察路径。
- 直接分析 `ZeusSyncPrimImportFD` 的参数需求，尝试独立触发 `0x82:0xC`
- 或从 `0x36ec0` 的输入结构体反推，构造最小输入直接调用

**方向 C**：接受静态分析结论，推进 R2 设计。
- 基于 `0x36ec0` 反汇编完成 IN 缓冲字段映射表
- R2 实现时按映射填充，待真实 TA 路径打通后再做活体验
...[truncated 152 chars]