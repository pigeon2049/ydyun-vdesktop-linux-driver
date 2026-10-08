# r378：真实页表绑定验证通过（V2 非空，无 oops）

> **结论**：首次成功执行非空 `mt_gpu_vm_bind_many()`——1 真实页面绑定到 VA `0x70000000`，返回 0，**无 oops**，r375 的 oops 根因已彻底消除。V1（VM 创建）/V2（真实绑定）均通过。

## 1. 测试方法

r377 仅验证了空绑定（`count=0` → `-EINVAL`）。本轮构造真实绑定：

**一次性内核模块 `mt_live_ta_bind`**（`mt-vgpu-guest/kernel/recovery/`，测试后已清理，未入库）：
1. 分配 4 页页表（`__get_free_pages`，清零）
2. 合成 tables BO（`page_pa==NULL`，`gpu_pa` 为页表物理地址，3D 既证模式）
3. `mt_gpu_vm_init()` 正式初始化 VM（非手动拼装）
4. `alloc_page()` 分配 1 真实数据页（`pa=0x1688f8000`）
5. 合成 data BO（`backing.gpu_pa` 为数据页 PA，`ops`/`store` 与 tables 一致）
6. `mt_gpu_vm_bind_many()`：1 个 binding，`va=0x70000000`，`bytes=4096`，`flags=0x1`
7. 退出时清理（释页、释 VM 资源）

**约束**：本轮**未重载** probe/bridge（r376 构建在载）；**未重启**（用户明确禁止自作主张重启）。

## 2. 验证结果

```
[ 1286.135740] mt_live_ta_bind: r378 real bind test starting
[ 1286.135754] mt_live_ta_bind: VM initialized (capacity=16384)
[ 1286.135756] mt_live_ta_bind: data page allocated pa=0x1688f8000
[ 1286.135761] mt_live_ta_bind: r378 V2 real bind ret=0 (expect 0, no oops)
[ 1286.135762] mt_live_ta_bind: REAL BIND OK va=0x70000000 pa=0x1688f8000
```

- **V2 真实绑定**：`ret=0`（成功），**无 oops**、无 WARN
- **VA 分配**：`0x70000000`（r374 设计值）
- **页表绑定**：VM capacity=16384，binding 已安装

**清理**：
```
[ 1294.424141] mt_live_ta_bind: cleaning up (bound=1 vm_ready=1)
[ 1294.424149] mt_live_ta_bind: cleanup done, no leaks
```
- 模块卸载干净，无泄漏
- dmesg 无新增 WARN/BUG/Oops
- refs 不变：bridge 0 / probe 1

## 3. 回归验证

Marker 级 TA 路径仍正常（r373 harness 格式）：
- INIT 通过
- `0x82:0xC` TA-only kick：`OUT.error=0`，`OUT.update_fence=3`（dmesg `submitted wire=3` 精确匹配）

## 4. 与 r375 的对比

| 项 | r375 | r378 |
|---|---|---|
| VM 初始化 | 手动拼装（遗漏字段） | 正式 `mt_gpu_vm_init()` |
| BO ops | 跨模块 `static const` 地址不一致 | 测试模块自带 ops，tables/data 一致 |
| `bind_many` 非空 | **内核 oops**（`+0x300`） | **返回 0**，无 oops |
| 结论 | 设计错误 | 根因彻底消除 |

## 5. 诚实边界

- 验证的是 CPU 侧页表构造（`mt_mmu_build_pages` 成功），**firmware 侧 VA 翻译未验证**（无 firmware 查询接口，待 V1 完整验证）。
- 测试模块为一次性验证工具，未入库；复现信息见 §1。
- `MT_TA_VM_READY` 门保持关闭，真实 TA payload 未开启（按计划）。
- V3（双上下文隔离）/V4（文件关闭清理）待后续轮次。
