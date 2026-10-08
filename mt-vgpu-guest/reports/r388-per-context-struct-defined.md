# r388：R6-1 per-context 对象模型扩展落地（离线，纯结构，零硬件触碰）

## 结论

**`struct mt_pvr_render_context` 定义完成**，`struct mt_pvr_object` 挂载 `render_ctx` 指针。R6-1（对象模型扩展）关闭，为 R6-2（create 真实化）铺平道路。

## 实现落点

### 1. `kernel/mt_render_context.h`（新，45 行）

```c
struct mt_pvr_render_context {
    struct mt_bo bos[MT_GFX_CONTEXT_BO_COUNT];      /* 11 BO, 968B @0 */
    u64 vas[MT_GFX_CONTEXT_BO_COUNT];               /* 88B @968 */
    bool bos_ready[MT_GFX_CONTEXT_BO_COUNT];        /* 11B @1056 */

    struct mt_execution_process process;            /* 32B @1072 */
    struct mt_execution_context exec_ctx;           /* 72B @1104 */
    bool exec_ready;                                /* @1176 */

    u8 csw[MT_GFX_CONTEXT_CSW_BYTES];               /* 248B @1177 */

    struct mt_bridge_ta_vm *vm;                     /* @1432 (forward decl) */
    u64 vm_base_va;                                 /* @1440 */

    bool resources_ready;                           /* @1448 */
};  /* sizeof = 1456 */
```

- `#define MT_RENDER_CONTEXT_VA_STRIDE (16U << 20)` —— per-context VA 分区，16MB（r387 §3.3，TO-VALIDATE）
- 前向声明 `struct mt_bridge_ta_vm`（定义在 mt_pvr_bridge.c:3969，r376）
- 包含 `mt_bo.h`（struct mt_bo 内联数组需完整定义）、`mt_gfx_context.h`（BO_COUNT/CSW_BYTES）、`mt_execution_context.h`（process/context）

### 2. `kernel/recovery/mt_pvr_bridge.c`（+2 处）

- `#include "../mt_render_context.h"`（:48）
- `struct mt_pvr_object` 新增（:293-295）：
  ```c
  /* R6 Route A (r387/r388): per-context real state. NULL = uninitialized
   * or non-CONTEXT kind. kzalloc in pvr_object_new() zeroes it. */
  struct mt_pvr_render_context *render_ctx;
  ```

### 3. `pvr_object_new()` 无需改动

`kzalloc(sizeof(*obj), GFP_KERNEL)` 已将 `render_ctx` 置 NULL。门禁测试显式验证此不变式（见下）。

## 设计决策（r387 §1.2 落实）

1. **指针而非内嵌**：`mt_pvr_object` 被所有 kind 共用（SYNC/PMR/RESERVATION 等），内嵌 1456B 会浪费内存。指针按需分配（R6-2）。
2. **独立头文件**：`mt_render_context.h` 不依赖 mt_pvr_bridge.c 的内部定义，仅前向声明 `mt_bridge_ta_vm`。userspace 可编译（门禁测试）。
3. **VA stride 16MB**：r387 §3.3 方案 A（per-context 独立 VM）。TO-VALIDATE：firmware VA 限制。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**441 Python + 299 C 全绿**（新增 `tests/test_render_context_layout.py` 3 tests）
- `make kernel` W=1：**零警告**（r383 后保持）
- **反向验证**：`MT_RENDER_CONTEXT_VA_STRIDE` 改为 32MB → 1 test failure；恢复 → 全绿

## 测试（`tests/test_render_context_layout.py`）

1. `test_header_layout_pins`：编译 header，断言 sizeof=1456 及 11 个字段偏移 + stride/BO_COUNT/CSW_BYTES 常量
2. `test_pvr_object_has_render_ctx`：源码 grep 确认 `struct mt_pvr_object` 含 `render_ctx` 指针
3. `test_pvr_object_new_zeroes_render_ctx`：源码确认 `pvr_object_new` 用 kzalloc（NULL 默认）

## 诚实边界

- **纯结构，无逻辑**：R6-1 按计划不实现 create/destroy/kick（R6-2~R6-4）
- **零硬件触碰**：未加载/卸载模块，未提交 GPU 工作
- `render_ctx` 当前恒为 NULL（R6-2 分配后才有效）
- `bos_ready`/`exec_ready`/`resources_ready` 的语义在 R6-2 定义

## 标注

- **[实测]**：struct 布局（编译器实测 sizeof/offsets）；`mt_pvr_object` 修改位置（源码 :283）；`pvr_object_new` 的 kzalloc（源码 :1235）
- **[推断]**：16MB stride（r387 工程估计，待验证）
- **[待验证]**：r387 §5 的 6 项（VA stride、TA-only BO 需求等）
