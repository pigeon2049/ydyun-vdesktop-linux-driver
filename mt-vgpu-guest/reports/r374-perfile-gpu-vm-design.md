# r374：Per-file GPU VM/BO 后端设计定稿（R5，离线设计轮）

> **结论**：TA 真实渲染 payload 的内存路径设计完成——per-file GPU VM（真实设备 store，非 `store=file` facade）+ `mt_bo_system_borrow()` 借入 TA 命令缓冲 + 预留 VA 绑定 + `sealed`/`uploaded` 校验门。本轮纯设计，零实现代码，零硬件触碰。

## 1. 为什么是 R5（背景）

- r372/r373 打通了端到端 TA 路径（marker 级）：trial 建立 → dispatch → DM3 提交 → `0x100` 完成 → fence → OUT 回填，全通。但全是**零绘制** marker。
- 真实 TA 渲染需要把 TA 命令缓冲（`p_ta_cmd`，r363 实测 360B）送进 firmware 可见的 GPU 内存。
- r364 D8 已定方向：经 `mt_bo_system_borrow()` 进 per-file GPU VM，`vm->sealed` 校验门在 R5 落定前保持关闭。
- r355 缺口 R5：per-file GPU VM/BO 后端——真实渲染的前置基础。

## 2. 现状盘点

### 2.1 `mt_bo_system_borrow()`（`kernel/mt_bo_vram.h:149`）[MEASURED]

- 签名：`mt_bo_system_borrow(struct mt_bo *bo, struct mt_bo_store *s, struct mt_system_memory *m)`。
- 输入：`mt_system_memory {cpu, page_pa[], bytes}`——CPU 地址 + 已翻译的 GPU page vector。
- 输出：在设备 BO store 中构造 borrowed BO（`borrowed=true`，`page_pa` 指向原数组）。
- 约束：调用时持有 `s->lock`；`s->ops` 必须为 `&mt_bo_vram_ops`；`bytes` 按 4096 对齐；每页 `page_pa` 按 4096 对齐且 `< 2^MT_GPU_VA_BITS`。
- 关键限制：**BO 所引用的 memory descriptor 生命周期不短于 borrowed BO**——pin 住的 userspace 页在 unmap 前不得释放。

### 2.2 `vm->sealed` 与校验门 [MEASURED]

- `struct mt_gpu_vm { ..., bool uploaded, sealed; ... }`（`kernel/mt_gpu_vm.h:48`）。
- `sealed`：VM 不再接受新绑定（页表定稿）；`uploaded`：页表已上传并发布给 firmware。
- TQX 路径校验门（`mt_marker_fence.h:246`）：
  ```c
  if (!s->work_ready || !vm->sealed || !vm->uploaded)
      return -EOPNOTSUPP;
  ```
- TA 路径当前**无此门**（marker 级，无 VM 需求）——R5 实现后按同一模式接入。

### 2.3 `store=file` facade 为何不可用（r208）[MEASURED]

- 当前 PVR per-file GPU VM 的页表 BO 与 PMR facade 标记 `store=file`、ops=`pvr_gpu_plan_bo_ops`。
- 该 ops 只有 free no-op；VM 注释明确是 **unpublished CPU-side planning domain**。
- 不能进入设备 session VM，不能被 `mt_gpu_vm_bind_many()` 接受为真实映射。
- R5 必须：每个 render context 在**设备 store** 内建页表 BO、真实 VM、execution process/context；按 per-file reservation VA 把 PMR borrowed BO 绑进去，上传并发布页表。**不得复用 CPU-only plan BO**。

### 2.4 `submit_ta_work` 当前对 TA 命令缓冲的处理 [MEASURED]

- `struct mt_ta_submit_params` 有 `ta_cmd_va`（= `in->p_ta_cmd`，userspace VA）与 `ta_cmd_size`（r363 实测 360）。
- 但 `mt_marker_submit_ta_work()`（`mt_marker_fence.h:383`）**完全不引用**这两个字段——grep 为零命中。当前提交的是空 marker，firmware 侧无命令可执行。
- 这是 marker 级验证的诚实边界，也是 R5 要填的缺口。

### 2.5 Per-file 的含义 [MEASURED]

- 每个打开的 `/dev/dri/renderD128` 文件描述符（每个 UMD 上下文）拥有独立的 GPU VA 空间。
- TQX 路径已有模式：`c->process->vm`——execution context → process → VM。TA 的 per-file VM 沿用同一归属链。

## 3. 设计

### 3.1 数据结构（`kernel/mt_ta_vm.h`，本轮新增）

- `struct mt_ta_vm_context`：per-file 上下文。字段：`vm`（真实设备 store VM）、`page_tables`（设备 store 页表 BO）、`owner_file`（归属文件）、`bound_cmd_buffers`（计数）、`ready`（`sealed && uploaded` 时置位）。
- `struct mt_ta_cmd_mapping`：单次 TA 命令缓冲映射。字段：`gpu_va`（firmware 可见 VA）、`size`、`borrowed_bo`、`pinned_pages`/`nr_pages`、`vm_ctx`（回指，不拥有）。
- 常量：`MT_TA_CMD_VA_BASE=0x70000000`、`MT_TA_CMD_VA_SIZE=0x1000`（每 kick 预留一页；r363 实测 360B < 4KiB）。
- 宏：`MT_TA_VM_READY(vm)` = `(vm) && (vm)->sealed && (vm)->uploaded`，与 TQX 门语义一致。

### 3.2 TA 命令缓冲映射流程 [INFERRED]

每一步都在硬件写之前（D7 错误处理模式）：

1. **Decode 捕获**（已存在）：`p_ta_cmd`/`ta_cmd_size` 在 `mt_ta_params_from_musakickgfx2()` 时捕获，submit 路径只见内核侧值。
2. **Pin**：`pin_user_pages(p_ta_cmd, nr_pages)` → `struct page **`。失败 → `-EFAULT`，无 BO 创建。
3. **构造 `mt_system_memory`**：`{cpu=p_ta_cmd, page_pa[]=每页物理地址, bytes=PAGE_ALIGN(ta_cmd_size)}`。
4. **Borrow**：持 `s->lock` 调 `mt_bo_system_borrow()` 进设备 BO store。失败 → unpin，`-ENOMEM`。
5. **Bind**：`mt_gpu_vm_bind_many(vm, MT_TA_CMD_VA_BASE + slot*0x1000, borrowed_bo)`。失败 → 释放 BO + unpin，`-ENOSPC`。
6. **Seal/Upload**（若 VM 未 ready）：seal VM → 上传页表 → firmware publish → 置 `vm_ctx->ready`。失败 → 回滚绑定，`-EIO`。
7. **填包**：`gpu_va` 写入 TA firmware 命令包的命令缓冲地址字段。
8. **完成清理**：firmware 完成事件到达 → unpin pages → 释放 borrowed BO；**VM 保留**（per-file 复用，避免每 kick 重建页表）。

### 3.3 与 `submit_ta_work` 的接口（D8 实现点）[INFERRED]

- `mt_marker_submit_ta_work()` 在构造 firmware 包之前调用 `mt_ta_vm_map_cmd_buffer()`（新函数，R5 实现轮）。
- 校验门接入（照抄 TQX）：
  ```c
  if (!MT_TA_VM_READY(vm_ctx->vm))
      return -EOPNOTSUPP;
  ```
- `ta_cmd_va` 字段语义升级：当前是 userspace VA（仅记录）；R5 实现后，firmware 包中使用 `mapping.gpu_va`，`ta_cmd_va` 保留为调试/追踪用。

### 3.4 `vm->sealed` 校验门的开启条件 [INFERRED]

- 按文件独立：每个文件的 VM 在其绑定集合定稿后 seal。
- TA 的 seal 时机：首次 TA kick 的命令缓冲绑定完成后（后续 kick 复用同一 VA 槽位或分配新槽位但不再改页表结构——若需新槽位，则 seal 推迟到槽位分配完成）。
- 与 TQX 的关系：TQX 有自己的 per-process VM 与 seal 点；TA 的 per-file VM 独立 seal，互不阻塞。

### 3.5 错误处理与资源回收 [INFERRED，照抄 D7 实测模式]

- 所有校验与映射在 `mt_fw_queue_try_submit()` 之前；任一失败直接返回错误码，不碰队列。
- Borrowed BO 生命周期：由 `mt_ta_cmd_mapping` 持有，unmap 时释放；`page_pa` 数组随 pinned pages 释放（满足"descriptor 不短于 BO"）。
- 文件关闭：`vm_ctx` 释放 → 若有 in-flight marker，等待完成或 abandon（沿用 `pvr_ta_abandon()` 模式）→ 释放 VM 与页表 BO。
- wire_id 永不复用（框架已有保证），映射失败不消耗 wire_id。

## 4. 状态标注

| 项 | 状态 | 依据 |
|---|---|---|
| `mt_bo_system_borrow()` 可用 | [MEASURED] | `mt_bo_vram.h:149`，r208 验证 bind 接受 |
| `sealed`/`uploaded` 门语义 | [MEASURED] | TQX 路径 `mt_marker_fence.h:246` |
| `store=file` 不可用 | [MEASURED] | r208 实测 |
| TA 当前忽略 `ta_cmd_va` | [MEASURED] | grep 零命中 |
| per-file VM 生命周期 | [INFERRED] | 仿 `c->process->vm` 模式 |
| VA base `0x70000000` | [INFERRED] | 需避开 TQX/3D 预留区 |
| 映射流程 8 步 | [INFERRED] | 待实现轮细化 |
| Firmware 接受映射 VA | [TO-VALIDATE] | 需活体：真实 TA payload 提交 |
| Per-file VA 隔离 | [TO-VALIDATE] | 需活体：双上下文并发 |
| 页表上传正确性 | [TO-VALIDATE] | 需活体：firmware 读取验证 |

## 5. 待活体验证清单（V1–V4）

- **V1**：`0x70000000` VA 范围被 firmware 接受为 TA 命令缓冲地址。
- **V2**：页表上传后 firmware 能正确翻译并读取 360B 命令缓冲内容。
- **V3**：双文件（双 UMD 上下文）并发 TA kick，VA 空间隔离，无串扰。
- **V4**：文件关闭时 in-flight marker 的 abandon 路径不泄漏、不 UAF。

## 6. 红线

- 不得用 accept-and-log 代替执行——R5 实现轮必须真实 pin/bind/upload，不得"假装映射成功"。
- 不得复用 `store=file` facade（r208 明确禁止）。
- `vm->sealed` 门在 R5 实现完成并通过 V1 前保持关闭（TA 路径继续 marker 级）。

## 7. 交付物

- 接口头：`kernel/mt_ta_vm.h`（仅定义与静态断言，无实现）。
- 门禁测试：`tests/test_ta_vm_layout.py`（6 tests，含反向验证）。
- 本报告；证据 `reports/r374-inventory.txt`（0600）。
