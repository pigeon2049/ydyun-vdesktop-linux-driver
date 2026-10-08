# r384：R6 DDK2 context statefulness——handle token 无 server 侧状态；阻塞真实 UMD 渲染（离线调研，零硬件触碰）

## 结论

**R6 的准确定义**（r355）：DDK2 的 `0x82:0x12`/`0x88:0x5` 从"只 mint handle token"升级为"server 侧真实状态"（firmware context、CCB 管理）。

**现状**：三个 DDK2 context 命令全部是 bookkeeping token，无 firmware 状态：
- `0x82:0x12` (RGXCreateRenderContext2)：`pvr_cmd_render2_create` mint `MT_PVR_KIND_CONTEXT`，IN（`priv_data`/`priority`）被 `(void)in` 丢弃
- `0x88:0x5` (BridgeRGXCreateKickSyncContext2)：`pvr_cmd_kicksyncctx2_create` mint `MT_PVR_KIND_KICKSYNC`，IN 8B opaque
- `0x89:0x8/0x9` (TDM context)：mint `MT_PVR_KIND_TDM_CONTEXT`，仅存 2 个 u64 arg

**缺口**：真实 render context 需要（`mt_live_3d.c` 实证 + `mt_gfx_context.h` 规格）：
1. 分配 11 个 BO（86,300B：PDS/USC/context-switch snapshot/TA state/VDM/DDM uniform/raster state）
2. 填 `mt_gfx_context_data.h` 初始数据
3. 绑定到 GPU VA
4. 构建 CSW（context switch words）
5. `mt_execution_context_create(node_type=5, DM=2)` 创建 firmware 执行上下文

**是否阻塞真实 UMD**：
- TA marker（当前）：否——`pvr_cmd_musakickgfx2` 忽略 `h_render_context`，R5 per-file VM 处理映射
- 真实 TA 渲染：**是**——firmware 需要 TA state BO
- 3D marker（r382 设计）：可能否——R5 VM + 0x68 opcode
- 真实 3D 渲染：**是**——firmware 需要 11 BO + 执行上下文（`mt_live_3d.c` 证明缺一不可）

**关键设计张力**：R5 的 VM 是 per-**FILE**（`mt_pvr_file.ta_vm_ctx`），不是 per-**CONTEXT**。单个文件可有多个 render context，每个需要独立 BO 状态。R6 需决定：context 状态挂在 `mt_pvr_object` 上，还是扩展 per-file 模型。

## 1. R6 定义澄清（r355 原文）

> **R6（DDK2 context 状态）**：0x82:0x12/0x88:0x5 从 handle token 到 server 侧状态（firmware context、CCB 管理）。

"Context statefulness" 具体指三层：
1. **创建时分配真实资源**：BO、GPU VA、firmware 上下文对象（而非仅返回一个整数 handle）
2. **跨调用保持状态**：context 的 BO 在多次 kick 间持久存在；destroy 时正确释放（无泄漏、无 UAF）
3. **Kick 时可用**：`0x82:0xC`/`0x82:0x14` 传入的 `h_render_context` 能解析到真实状态，供 firmware 执行

当前三层皆无——只有第 0 层（handle 存在性校验）。

## 2. 现状盘点（实测：读源码）

### 2.1 0x82:0x12 RGXCreateRenderContext2

`kernel/recovery/mt_pvr_bridge.c:3229` `pvr_cmd_render2_create()`：
```c
ret = pvr_in(cmd, &in, sizeof(in));   /* 12B: {u64 priv_data, u32 priority} */
if (ret) return ret;
obj = pvr_object_new(file, MT_PVR_KIND_CONTEXT);
if (!obj) return -ENOMEM;
out.handle = obj->handle;
(void)in;                              /* IN 被丢弃 */
return pvr_out(cmd, &out, sizeof(out)); /* 12B: {u64 handle, u32 error} */
```

- IN 结构已入库（`mt_pvr_wire.h:301`，12B，r142 活体确认 out_size=12）
- `priv_data`/`priority` 未解读
- 无 BO、无 VA、无 firmware 调用

### 2.2 0x88:0x5 BridgeRGXCreateKickSyncContext2

`mt_pvr_bridge.c:2710` `pvr_cmd_kicksyncctx2_create()`：
- IN 8B opaque，OUT 12B `{kicksync_context, error}`
- mint `MT_PVR_KIND_KICKSYNC` token，`(void)in`
- 无 server 侧 CCB 分配（r355 R3 提到"CCB 窗口（server 侧持有，r56–r57）"，当前无实现）

### 2.3 0x89:0x8/0x9 RGXTDMCreateTransferContext2/Destroy

`mt_pvr_bridge.c:3385`：
- mint `MT_PVR_KIND_TDM_CONTEXT`，存 `arg0=device_mem_context`、`arg1=context_type`
- 比 render2 多存 2 个字段，但仍无 firmware context
- destroy 正确 unlink + kfree（生命周期 bookkeeping 完整）

### 2.4 Destroy 路径

- `0x82:0x13`/`0x88:0x6`/TDM destroy：`pvr_cmd_handle_release` → `pvr_object_find` → `list_del` + `kfree`
- Bookkeeping 层面生命周期完整（无泄漏），但释放的是空 token

### 2.5 使用侧：kick 如何对待 context handle

| Kick | 对 `render_context` 的处理 | 位置 |
|---|---|---|
| `0x82:0xC` (TA, r367) | **忽略**：仅 dmesg 打印 `in.h_render_context`，不校验不使用 | `mt_pvr_bridge.c:4018` |
| `0x82:0x14` (3D, r215 observer) | **仅校验存在性**：`pvr_object_find(..., MT_PVR_KIND_CONTEXT)`，不用其状态 | `mt_pvr_bridge.c:4170` |

## 3. 真实状态的参照实现（实测：mt_live_3d.c）

`kernel/recovery/mt_live_3d.c` 是仓库内唯一真实 3D 上下文创建路径（r37–r41 活体验收）：

1. **11 BO**：`mt_gfx_context_bo_specs`（`mt_gfx_context.h:44`），共 86,300B，29 页
   - PDS code/data ×3、USC shader ×1、DCE context-switch snapshot ×1
   - **TA state** ×1（468B）、VDM uniform PDS ×2、DDM uniform PDS ×1、Raster state ×1
2. **初始数据**：`mt_gfx_context_data.h` 填充每个 BO
3. **GPU VA 绑定**：`d->address_spaces.ops->bind()` 逐个绑定
4. **CSW**：`mt_gfx_context_build_csw()` 生成 0xf8B context-switch words
5. **Firmware 上下文**：`mt_execution_context_create(&st->context, &st->process, 5, 0)`（node_type=5 → DM2）
6. **提交**：`submit_context(s, &st->context, &command_bo, &req, &fence)`

**关键事实**：`mt_live_3d.c` 不经过 bridge 的 `0x82:0x12`——它自建全套状态。这证明了 bridge 侧缺失的部分，也提供了可复用的代码路径。

## 4. 与 R5 的关系：per-file VM vs per-context 状态

R5（r374–r378）的 `mt_bridge_ta_vm` 挂在 `struct mt_pvr_file` 上（`mt_pvr_bridge.c:352` `ta_vm_ctx`）：
- 创建时机：`pvr_cmd_musakickgfx2` 首次调用时（`:4035`）
- 销毁时机：文件 close（`:902`）
- 作用域：**整个文件描述符**，不区分 render context

**设计张力**：
- 真实 UMD：一个 renderD128 fd 可创建多个 render context（游戏多窗口/多线程）
- 每个 context 需要独立的 11 BO（至少 TA state / raster state 是 per-context 可变的）
- 当前 R5 模型：所有 kick 共享同一个 per-file VM

**R6 的两种路线**（待决策，属推断）：
- A. **Context 对象挂载状态**：扩展 `struct mt_pvr_object`（`MT_PVR_KIND_CONTEXT`），持有 11 BO + firmware ctx + 可选 per-context VM；kick 时由 handle 解析到状态
- B. **Per-file 模型扩展**：context 仅为 VM 内的命名空间，BO 仍挂 file 下；kick 时用 context handle 选择 VA 命名空间

路线 A 更接近 Windows 语义（context 是独立内核对象）；路线 B 改动小但多 context 隔离弱。本轮不做选择，记录为 R6 设计输入。

## 5. 缺口清单（具体工作项）

| # | 工作项 | 涉及位置 | 前置 |
|---|---|---|---|
| R6-1 | `struct mt_pvr_object` 扩展：为 `MT_PVR_KIND_CONTEXT` 增加 BO 数组 + firmware ctx 句柄 + 状态标志 | `mt_pvr_bridge.c` 对象模型 | 无 |
| R6-2 | `pvr_cmd_render2_create` 真实化：11 BO 分配 + init data + GPU VA 绑定 + CSW + `mt_execution_context_create`（复用 `mt_live_3d.c` 流程） | `mt_pvr_bridge.c:3229` | R6-1 |
| R6-3 | Destroy 真实化：BO 释放 + VA 解绑 + firmware ctx 销毁（当前仅 kfree token） | `pvr_cmd_handle_release` | R6-2 |
| R6-4 | Kick 侧解析：`0x82:0xC`/`0x82:0x14` 由 `h_render_context` 查到真实状态，供提交路径使用 | `:3995`/`:4170` | R6-2 |
| R6-5 | CCB（`0x88:0x5`）：server 侧 CCB 分配与管理（r56–r57 的"CCB 窗口"需先落实定义） | `:2710` | 调研 |
| R6-6 | TDM context（`0x89:0x8`）：transfer 上下文的 BO 需求调研（可能比 3D 简单） | `:3385` | 调研 |

## 6. 是否阻塞真实 UMD：分层结论

| 场景 | 是否阻塞 | 卡点 |
|---|---|---|
| TA marker（r372/r373 已通） | 否 | `h_render_context` 被忽略，R5 VM 够用 |
| 真实 UMD TA 调用 | **是** | firmware 执行 TA 命令需要 TA state BO（`mt_gfx_context.h` BO#4，468B）；当前 context 无 BO，firmware 将读到未初始化状态 |
| 3D marker（r382 设计） | 可能否 | R5 VM 映射 + 0x68 opcode；待活体验证 |
| 真实 UMD 3D 调用 | **是** | firmware 需要 11 BO + 执行上下文；`mt_live_3d.c` 证明缺一不可 |
| 多 context UMD（如多窗口） | **是** | per-file VM 不区分 context；R6-1/R6-4 未实现则状态串扰 |

**一句话**：R6 不阻塞 marker 级验证，但阻塞任何真实 UMD 的渲染调用——firmware 侧的状态是空的。

## 7. 标注

- **[实测]**：三个 handler 的 token 行为（源码）；`mt_live_3d.c` 的 11 BO + 执行上下文流程；`0x82:0xC` 忽略 handle / `0x82:0x14` 仅校验（源码）；per-file VM 挂载点（源码）；IN/OUT 尺寸（`mt_pvr_wire.h` + r142 活体）
- **[推断]**：真实 UMD 渲染被阻塞（基于 firmware 需要 context 状态的架构常识 + `mt_live_3d.c` 实证；尚未有真实 UMD 调用失败的活体证据）；路线 A/B 的设计权衡
- **[待验证]**：R6-5 CCB 的 server 侧定义（r56–r57 需回查）；TDM context 的 BO 需求；3D marker 在无 context BO 下是否真能工作（r382 活体待定）

## 门禁

- 本轮纯调研、无代码改动：`make -C mt-vgpu-guest check-offline` 全绿（见下）。
- 引用路径核对：`kernel/recovery/mt_pvr_bridge.c`（3229/2710/3385/3995/4170）、`kernel/mt_pvr_wire.h`（301）、`kernel/mt_gfx_context.h`、`kernel/recovery/mt_live_3d.c` 均存在。

## 证据

- `mt-vgpu-guest/reports/r384-evidence.txt`：关键源码行摘录（handler 行为、对象模型、挂载点）。
