# r42: GPU VA 映射可扩展化 — 移除 24 mappings 假上限

## 1. 起点：一条被误当作硬件限制的驱动常量

r41 报告与 `PROTOCOL-NOTES.md` 把「S3000 vGPU MMU 单个虚拟地址空间最多容纳 24 个
mapping ranges」写成硬件物理上限。核查后确认这是**驱动自己的编译期常量**：

- `kernel/mt_mmu_bootstrap.h:11` 原本定义 `#define MT_BOOT_MAX_RANGES 24U`；
- 该常量唯一的实际用途是 `struct mt_gpu_vm` 里 `bindings[24]` 这个**内联数组的长度**，
  以及 `mt_gpu_vm_plan()` 中两个同样长度的**内核栈数组**。

真实 MMU 是三级页表 walk（`mt_mmu.h`，从 `mtkm64.sys` 逆向）：

| 层级 | 条目数 | 条目宽度 | 覆盖范围 |
| --- | --- | --- | --- |
| PC（root） | 1024 | 4 B | 每项 1 GiB |
| PD（directory） | 512 | 8 B | 每项 2 MiB |
| PT（table） | 512 | 8 B | 每项 4 KiB |

**硬件对映射数量没有任何上限**。单个地址空间的上限来自页表页预算：每个映射至少占用
一个页表项，一个 PT 页描述 512 个页表项。`MT_BOOT_MAX_MAPPED_BYTES`（64 MiB）对应的
16384 个页表项才是实际生效的约束。

结论：24 是驱动数组边界，不是 MMU 性质。任何真实图形栈（需要成百上千个 buffer 映射）
都过不了这一关，因此这是 r42 的前置阻塞项。

## 2. 改动

### 2.1 上限改为从页表几何推导

`mt_mmu_bootstrap.h` 新增：

```c
#define MT_BOOT_ROOT_PAGES 1U
#define MT_BOOT_PC_ENTRIES 1024U
#define MT_BOOT_PD_ENTRIES 512U
#define MT_BOOT_PT_ENTRIES 512U
#define MT_BOOT_PT_SPAN (MT_BOOT_PT_ENTRIES * 4096ULL)

static inline u32 mt_boot_max_ranges(u32 table_pages)
{
	if (table_pages <= MT_BOOT_ROOT_PAGES)
		return 0;
	slots = (u64)(table_pages - MT_BOOT_ROOT_PAGES) * MT_BOOT_PT_ENTRIES;
	bytes = MT_BOOT_MAX_MAPPED_BYTES / 4096ULL;
	return (u32)(slots < bytes ? slots : bytes);
}
```

`mt_mmu_build_pages()` 里原先的 `count > MT_BOOT_MAX_RANGES` 硬拒绝改为按调用方
实际页预算校验（`count > mt_boot_max_ranges(capacity / 4096)`），页预算耗尽仍由既有
循环返回 `-ENOSPC`——那才是贴近硬件的真实边界。

生产驱动实际请求 32 页 → 上限 `mt_boot_max_ranges(32)` = min(31×512, 16384) = 15872。

### 2.2 映射表与规划暂存移出内联数组和内核栈

`struct mt_gpu_vm` 的 `bindings[24]` 改为按需增长的堆指针，并新增 `ranges` /
`page_lists` 两个暂存数组：

- 若只是把 24 改成 15872 内联，`struct mt_gpu_vm` 会膨胀到约 500 KB，且每个
  `mt_gpu_vm_bind_many()` 会在 16 KB 内核栈上压入 `ranges`（24 B/项）与
  `page_lists`（8 B/项）——直接爆栈。必须堆分配。
- `mt_gpu_vm_grow()` 按 2 的幂增长，稳态下不重复分配；任一分配失败时保留旧数组
  与旧 `count`，被拒绝的 bind 不会污染既有 plan。

`mt_work_job.bos[]` 同样是按映射数定长的内联数组，一并改为精确分配的指针
（`vm->count + 1` 项，去重后可能更少）。

### 2.3 事务性语义保持

- `mt_gpu_vm_bind_many()` 现在**先整批校验、再 grow、最后原地暂存到尾部**。
  重叠检测从 `mt_mmu_build_pages()` 内部提升到校验循环里，因此一个冲突请求既不会
  增长数组，也不会改变 image / count / 页数 / 任何引用计数。
- 校验顺序与 planner 自身的范围预检一致（`va >= 1<<40` → `-EINVAL`，
  `bytes > (1<<40) - va` → `-ERANGE`），调用者观察到的 errno 不变。
- `mt_gpu_vm_commit()` 不再整体 memset + memcpy 绑定数组（那会与原地暂存自相矛盾），
  改为只清空退役尾部。
- `mt_gpu_vm_plan()` 中 `page_lists[i]` 改为**无条件赋值**。原实现只在 BO 有
  `page_pa` 时写入，否则沿用上一次 plan 的陈旧指针——在改为复用暂存数组后这会变成
  真实的映射错误来源。

### 2.4 共享 ABI 校验补全

`scripts/verify-runtime-integration.py` 原先只比对 `mt_guest` 一个结构。
`mt_guest_device` 内嵌 `address_spaces`，而所有 recovery 模块都直接读主模块分配的
`mt_gpu_vm` / `mt_vm_vram` 字段——这次 `mt_gpu_vm` 布局变更正是因此被静默放过。
现改为对 7 个共享结构取 pahole 摘要、写入 `reports/shared-abi-baseline.json` 并在
后续构建上门禁，任何漂移直接失败并提示「主模块与 recovery 模块必须一起重建、
一起重新加载」。

### 2.5 UAPI

`struct drm_mt_query` 追加三个只读字段（56 → 80 字节）：

```c
__u64 vm2d_mappings, vm3d_mappings;
__u64 vm3d_max_mappings;
```

仅在尾部追加，既有偏移不变。目的：让下一个映射阶段可度量——用户态能直接看到
地址空间余量而不必猜测。`mt-3d-check` 增加断言 `vm3d_max_mappings > 24`。

## 3. 验证

### 3.1 单元测试（新增 `tests/gpu_vm_scale_test.c`，ASan + UBSan）

- 推导上限：root 页不可用于映射；2 页预算恰好 512 项；页几何给出的 15872 与
  mapped-byte 上限 16384 取小；`capacity < 8192` 时上限为 0 并被 `init` 拒绝；
  超过编译期页数上限的 capacity 收敛而非增长。
- **512 个同时存在的映射**（旧上限的 21 倍），每个 BO 16 KiB，共占 6 个页表页；
  对全部 512×4 个页面做**独立三级 walk 逐页核对**物理地址，而非只检查非零。
- 拒绝路径不变式：重叠 / 非对齐 / 越界 / 非法 flags / 外部 store / 页预算耗尽，
  逐项确认 image、count、页数、**数组容量**、引用计数全部未变。
- 页预算耗尽在独立 VM 上测量，接受数远超 24 且等于 `vm.count`。

`scripts/verify-runtime-integration.py` 全量 21 个测试通过（含新增 3 项），
`W=1` 零警告。

过程中 ASan 抓到一个真实泄漏：`mt_work_job_prepare()` 在 `mt_bo_gpu_begin()` 失败
回滚时只释放了 BO pin，漏掉了新分配的 pin 数组。已修。

### 3.2 内核构建

`kernel/`、`kernel/recovery/`、`kernel/selftest/` 三处 `W=1` 全部零警告零错误。

### 3.3 真机：尚未完成，原因如下

当前 `mt_guest_probe` 仍在运行，且是**用旧 `mt_gpu_vm` 布局编译的**（`bindings[24]`
内联数组，`sizeof` 与字段偏移都不同）。recovery 模块按新布局访问主模块分配的对象，
`space_2d->vm.max_ranges` 读到 0，`create()` 在 `mt_gpu_vm_init()` 处失败，
insmod 报 `-EBUSY`。这正是 2.4 记录的那类静默错位——现在已被门禁捕获。

要完成真机验证必须重新加载主模块，这会**销毁当前已建立的固件会话**
（Guest=2 / FW=2 / started=1），属于会影响硬件状态的操作，未擅自执行。

## 4. 与 r41 结论的订正

- ❌ 「S3000 vGPU MMU 单个地址空间最多 24 个 mapping ranges」→ 不是硬件限制。
- ✅ 单个地址空间的上限由页表页预算决定，本配置下为 15872。
- ✅ r41 的双 VM 空间隔离策略（2D/3D 分开）仍然正确且必要，与本改动不冲突。
- ⚠️ 顶点缓冲 + 单三角形光栅化被另一条硬依赖挡住，见下。

## 5. 遗留：为什么 r42 不是三角形光栅化

核查 11 个 3D 上下文 BO 的实际内容（`kernel/mt_gfx_context_data.h`）：

| BO | 字节 | 非零 | 内容 |
| --- | --- | --- | --- |
| 0 | 3072 | 227 | PDS 上下文切换程序 |
| 1 | 6144 | 1216 | USC shader |
| 2 | 776 | **0** | DCE snapshot |
| 3 | 468 | **0** | **TA state** |
| 4, 5 | 1024 | **0** | PDS |
| 6, 7 | 16400 | **0** | **VDM uniform PDS state** |
| 8, 9 | 16400 | **0** | **DDM uniform PDS state** |
| 10 | 8192 | **0** | **Rasterisation context state** |

TA state、光栅化上下文、以及全部 VDM/DDM 程序**全为零**。对照
`FUN_00183d30`（原厂字符串即 `RGXGenerateContextSwitchUniformTasks`）可知它只负责
生成 uniform 存取程序，且因为上下文描述符全零而直接失败（`Failed to create USC task`）。
真正的顶点取数程序由原厂 PSC 编译器闭包（`001a4fc0`..`001b6e40`）现场生成，无法绕过。

所以 r41 文档里写的下一步「顶点缓冲 + 单三角形光栅化」实际是一个远大于本阶段的
工程。可达路径只有两条：逆向重实现 PSC 编译器，或让原厂 UMD 的渲染路径跑在自研驱动上。

另新增 `scripts/trace-packet-field-map.py`：对原厂 Linux UMD 逐 word 差分探测
（121 个可达 word），把 5 个描述符字段映射到包内偏移，落盘
`reports/packet-field-map.json`。这是后续定位顶点/图元寄存器字段的工具基础，
但它给出的是数据依赖，不解释寄存器语义。

## 6. 未验证事项

- 真机加载新布局主模块后的 512-mapping 场景（需要重建并重新加载主模块）
- 真机 QUERY 返回的 `vm3d_max_mappings` 实际值
- ~~重叠检测提前到校验循环后，是否与既有 `-EEXIST` 调用方语义完全一致（仅 RAM 测试覆盖）~~
  已由第 7 节收尾（RAM 测试 + 调用方核查）。

## 7. r42 遗留收尾：重叠检测前移的 errno 等价性（离线完成，不需硬件）

`tests/gpu_vm_scale_test.c` 新增 `errno_precedence()`（ASan + UBSan）：

- 逐项核对全部 bind 调用方（`mt_vm_vram_bind`、`mt_gem_bind_handle`、
  `mt_process_resources_bind(_pools)`、`mt_boot_bo_bind`）：**全部透传 errno，
  没有任何调用方按 `-EEXIST` vs `-E2BIG`/`-ENOSPC`/`-ERANGE` 的优先级分支**；
  用户态 `mt-3d-check` 只断言计数值，不依赖该 errno。
- 单错语义保持；两处优先级变化钉为**预期行为**（直接调 planner 交叉验证，
  证明两类上限仍被原生守护，只是优先级变了）：
  1. `va + bytes` 上溢：planner 报 `-EINVAL`，`bind_many` 报 `-ERANGE`
     （r42 起的变化；r42 文档「调用者观察到的 errno 不变」对此边不成立）；
  2. 重叠且同时超出 mapped-byte 预算：planner 报 `-E2BIG`，
     `bind_many` 报 `-EEXIST`（旧 planner 内顺序是 E2BIG 先）。
     单 bind 的 `-EEXIST` vs `-ENOSPC` 顺序新旧一致（重叠先）。
- plan 阶段拒绝（`-E2BIG` / 页预算 `-ENOSPC`）发生在 `grow()` 之后，
  可能保留已增长的**空暂存数组**；image / count / 页数 / 引用计数精确不变。
  旧固定数组无此现象，属良性过分配，不影响事务性语义。
- 仍需硬件（未做）：512-mapping 真机场景、QUERY `vm3d_max_mappings` 真机值。
  需重载主模块（销毁固件会话）；当前容器 `NoNewPrivs=1` + `CapEff=0`，
  `sudo`/`su` 均不可用，必须到有 root 权限的终端上执行。
