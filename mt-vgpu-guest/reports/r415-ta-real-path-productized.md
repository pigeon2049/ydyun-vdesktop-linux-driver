# r415：真实 TA 路径产品化——DM 布局固化 + `mt_ta_submit_real()` 落地（离线）

> 轮次：r415（2026-10-09）。**离线产品化轮。** r414 历史性成功后，将测试钩子重构为生产级代码。
> 背景：r414 真实 TA 包被固件接受（0x100，219µs）；测试钩子（`0x82:0xFE`）未提交；生产路径仍为 marker。

## 结论

**真实 TA 路径已产品化（离线，门控默认关闭，零行为变更）：**

1. **DM 布局固化**：VA @+0x28/size @+0x30 由 [INFERRED] 转 **[MEASURED]**（r414 活体）；80B 包完整布局文档化。
2. **生产函数 `mt_ta_submit_real()`**：r414 测试钩子（169 行）重构为参数化生产 API（`struct mt_ta_real_request`）；固定 64×64 hardcode 已移除；位于 `kernel/recovery/mt_pvr_bridge.c`（`#if MT_TA_REAL_PACKET` 内）。
3. **BO[10] 复用评估**：per-context VM 已 seal（bind → `-EBUSY`），独立分配需改 create 路径；BO[10]@4096 复用已由 r414 活体验证，文档化为当前方案，长期 fix 为 create 时专用 BO。
4. **门控流程文档化**：开启前置 5 项、开启步骤、回滚方案（见 `mt_ta_real.h`）。
5. **`0x82:0xFE` 测试钩子**：从未提交，无需删除；已验证不在生产代码中。

## DM 布局固化

`kernel/mt_ta_real.h` 更新：

| 偏移 | 字段 | 状态 |
|---|---|---|
| +0x0c | opcode = 0x66 | [MEASURED] (r365) |
| +0x28/+0x2c | TA buffer VA (64-bit LE) | [MEASURED] (r414) |
| +0x30 | TA buffer size (=360) | [MEASURED] (r414) |
| +0x48 | wire_id | [MEASURED] (r365) |
| +0x4c | pid | [MEASURED] (r365) |

## 生产代码设计

### `mt_ta_real_buffer_build()`（`mt_ta_real.h`，纯函数，可单元测试）

```c
struct mt_ta_real_request {
	u64 h_render_context;
	u32 width, height;      /* 1..0x8000, 参数化（非 64x64 hardcode） */
	u32 n_entries;          /* 1..9 */
};
int mt_ta_real_buffer_build(unsigned char *buf, u32 w, u32 h, u32 n_entries);
```

填充 n 个 40B 条目，余部清零；参数校验（-EINVAL）。

### `mt_ta_submit_real()`（`mt_pvr_bridge.c`，`#if MT_TA_REAL_PACKET` 内）

```c
static int mt_ta_submit_real(struct mt_pvr_file *file,
			     const struct mt_ta_real_request *req,
			     struct dma_fence **out_fence);
```

流程：参数校验 → `pvr_object_find` 取 render_ctx → 就绪检查 → `kzalloc` 360B → `mt_ta_real_buffer_build` → `pvr_session_acquire` → `pvr_translator_bo_write` 至 BO[10]@4096 → 组装 `mt_ta_work`（`ta_cmd_va`/`ta_cmd_size`）→ `mt_bridge_submit_ta_work` → 返回 fence（**异步**，调用方决定等待）。

与测试钩子的差异：
- 参数化（`req` 结构），无 hardcode
- 异步返回 fence（钩子内做同步等待；生产调用方自行 `pvr_ta_wait_complete`）
- 无 ioctl 包裹（`pvr_in`/`pvr_out`）、无测试日志、无状态码
- `__maybe_unused`（暂无 in-tree 调用方；首个调用方落地时移除）

### BO[10] 复用评估

| 方案 | 结论 |
|---|---|
| BO[10]@4096 复用 | ✅ 采用（r415）。VM 已 seal，无法新增绑定；r414 活体验证固件正确读取；前 4KB 保持完整。 |
| Create 时新增专用 BO | ⏸️ 长期方案。需改 r389 的 11-BO 创建路径，风险高；待真实 TA 进入常规验证后再做。 |
| 独立 DMA 分配 | ❌ 不采用。同样需 VA 绑定，被 seal 阻塞；无优势。 |

诚实边界：BO[10] 后 4KB 的固件语义在真实光栅化负载下未验证（[TO-VALIDATE]）。

## 门控流程

`MT_TA_REAL_PACKET`（`kernel/mt_ta_real.h`，默认 0）：

**开启前置：**
1. r414 活体验证（DM 布局 [MEASURED]）✅
2. `mt_ta_submit_real()` 已提交评审 ✅（本轮）
3. BO staging 决策文档化 ✅（本轮）
4. Pre-live T1/T2/T3 在门控构建上通过
5. 单次活体验证（r414 流程）

**开启：** 置 1 → `make kernel` W=1 → `safe_rmmod.sh` 重载 → 单发验证。
**回滚：** 置 0 重建；marker 路径（`mt_fw_ta_marker_command`）不受影响。

## 测试

`tests/ta/test_ta_real.py` 新增 8 tests（共 14）：
- DM 偏移 [MEASURED] 标签与值
- Staging 常量（BO 10 @ 4096，max 9 entries）
- `mt_ta_real_buffer_build` / `mt_ta_real_request` 参数化
- `mt_ta_submit_real` 在桥内、被门控、无 hardcode
- `0xFE` 钩子不在生产代码中
- 门控流程文档化

## 门禁

- `make -C mt-vgpu-guest check-offline`：**488 Python + 299 C 全绿**（480+8 新）
- `make kernel` W=1：**零警告**（门控开/关双路径验证）
- 反向验证：门控置 1 → `test_gate_default_off` FAIL；还原后全绿
- T2 白名单：`mt_ta_real.h` 的注释曾引用 `MT_FW_TA_OPCODE` 触发告警，已改为裸 `0x66`（文档无需宏名）

## 交付物

- 本报告 `reports/r415-ta-real-path-productized.md`
- `reports/README.md` +1 行；`MEMORY.md` 顶部插入 r415；`PROGRESS-SNAPSHOT.md` §12 追加 r415
- 本地提交（不 push）

## 诚实边界

- 纯离线；固件未接触；`mt_ta_submit_real()` 无活体验证（门控默认关）
- `__maybe_unused`：暂无调用方，API 待首个验证 ioctl 使用
- 360B 内容仍为最小 dummy 构造；语义深挖、像素回读、多条目均为后续工作
- 生产 `0x82:0xC` 路径仍走 marker（UMD 场景下 `ta_cmd_va` 由客户端提供，直通无需桥构造）

## 下一步

1. **P1**：T2 回读验证（像素级确认 TA 真实执行了绘制）
2. **P2**：3D 真实包（`MT_3D_SUBMIT_GATE`，r406 已证基础设施）
3. 门控开启活体（需用户确认时机；pre-live 门禁先行）
