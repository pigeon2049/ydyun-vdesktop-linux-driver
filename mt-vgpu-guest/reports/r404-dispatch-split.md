# r404: pvr_bridge_dispatch 拆分为 8 个 per-group helper——168 行主函数变为 20 行路由，行为零变更（离线）

## 结论

`pvr_bridge_dispatch()`（168 行，8 重嵌套 switch）已按 bridge group 拆分为 8 个独立 helper 函数，主函数仅保留 `-ENOTCONN` 检查 + 外层 group 路由（~20 行）。`pvr_translator_prepare_locked`（294 行）经分析决定**不拆**：函数已是清晰的线性 5 阶段结构，错误处理经 `pvr_translator_teardown_locked()` 集中，拆分会分散 `fail_at` 调试信息，收益不及风险（r401 原评估为"可选"）。

门禁 474+299 全绿，`make kernel` W=1 零警告。13 个文本扫描测试随重构更新（dispatch 文本位置变更），反向验证通过。零硬件触碰。

## 1. 拆分方案：pvr_bridge_dispatch

**Before**（`kernel/recovery/mt_pvr_bridge.c:5051-5218`，168 行）：
```c
static int pvr_bridge_dispatch(file, bridge, function, cmd) {
    if (!file->conn->srv_handle) return -ENOTCONN;
    switch (bridge) {
    case MT_PVR_BRIDGE_SRVCORE:
        switch (function) { ... 23 行 ... }
    case MT_PVR_BRIDGE_SYNC:
        switch (function) { ... 17 行 ... }
    /* ... 6 more groups ... */
    default: return -ENOTTY;
    }
}
```

**After**：8 个 helper + 瘦主函数
```c
/* r404: per-bridge-group dispatch helpers */
static int pvr_dispatch_srvcore(file, function, cmd) { switch (function) { ... } }
static int pvr_dispatch_sync(file, function, cmd) { switch (function) { ... } }
static int pvr_dispatch_mm(file, function, cmd) { switch (function) { ... } }
static int pvr_dispatch_rgxcompute(file, function, cmd) { switch (function) { ... } }
static int pvr_dispatch_rgxta3d(file, function, cmd) { switch (function) { ... } }
static int pvr_dispatch_rgxkicksync(file, function, cmd) { switch (function) { ... } }
static int pvr_dispatch_rgxhwperf(file, function, cmd) { switch (function) { ... } }
static int pvr_dispatch_rgxtdm(file, function, cmd) { switch (function) { ... } }

static int pvr_bridge_dispatch(file, bridge, function, cmd) {
    if (!file->conn->srv_handle) return -ENOTCONN;
    switch (bridge) {
    case MT_PVR_BRIDGE_SRVCORE: return pvr_dispatch_srvcore(file, function, cmd);
    /* ... 7 more ... */
    default: return -ENOTTY;
    }
}
```

**机械性**：纯代码移动，零逻辑变更。每个 helper 的 inner switch 与原代码逐字相同（仅去一层缩进）。行为 100% 一致。

## 2. 跳过 pvr_translator_prepare_locked 的原因

**分析**（`kernel/recovery/mt_pvr_bridge.c:1936-2230`，294 行）：

| 阶段 | 行号 | 内容 |
|---|---|---|
| Setup | 1957-1969 | address space create + bind_boot_shared |
| Alloc | 1970-1989 | command BO + rt BO + binds |
| CSW | 1990-2040 | context switch state 构建 |
| TQX | 2041-2172 | 条件性 TQX BO bring-up |
| Seal+exec | 2173-2218 | upload/seal/exec process/context |
| Finalize | 2219-2230 | 字段赋值 + ready=true + out 清理 |

**不拆的理由**：
1. **已是清晰线性结构**：6 个阶段顺序执行，无分支交错，每个阶段有注释分隔
2. **错误处理集中**：所有失败走 `goto out` → `pvr_translator_teardown_locked()` 统一回滚；拆分后 `fail_at = __LINE__` 将指向 helper 调用点而非具体失败操作，调试信息退化
3. **r401 原评估**："可选"，"下次改动此函数时再拆"——当前无改动需求，不为拆而拆
4. **风险/收益**：中风险（初始化顺序敏感）vs 中收益（可读性小幅提升）→ 不值得

**结论**：记录为已知长函数，待未来实质性改动时再拆。本轮不强拆。

## 3. 测试更新（13 文件）

Dispatch 文本位置变更导致 13 个文本扫描测试失败，全部修复：

| 文件 | 修复方式 |
|---|---|
| `test_pvr_ddk2_kicksync2.py` | `kicksync_block()` → `fn_body(src, 'pvr_dispatch_rgxkicksync')` |
| `test_pvr_ddk2_render2.py` | `ta3d_block()` → `pvr_dispatch_rgxta3d`；2 处 inline SYNC 提取 → `pvr_dispatch_sync` |
| `test_pvr_kickta3d5_observe.py` | inline 提取 → `pvr_dispatch_rgxta3d` |
| `test_pvr_multicore_info.py` | 手动切片 → `pvr_dispatch_srvcore`（+新增 `fn_body`） |
| `test_pvr_syncprimimportfd.py` | inline → `pvr_dispatch_sync` |
| `test_pvr_syncprimset.py` | inline → `pvr_dispatch_sync` |
| `test_pvr_tdm_context2.py` | 手动切片 → `pvr_dispatch_rgxtdm`（+新增 `fn_body`） |
| `test_pvr_tdm_submit3.py` | inline → `pvr_dispatch_rgxtdm` |
| `test_pvr_fn_ids.py` | 提取范围从 `pvr_bridge_dispatch` 扩大到 `pvr_dispatch_srvcore`（覆盖 8 helpers） |

所有测试的**语义不变**：仍验证 "指定 bridge function 路由到指定 handler"，只是文本位置从主函数变为 helper。

## 4. 反向验证

- 破坏：在 `pvr_dispatch_rgxtdm` 中将 `MT_PVR_FN_RGXTDMSUBMITTRANSFER3` 改为返回 `-ENOTTY` → `test_submit3_routed` **FAIL**（符合预期）
- 还原后全绿。证明测试仍守住路由正确性。

## 5. 门禁

- `make -C mt-vgpu-guest check-offline`：**474 Python + 299 C 全绿**
- `make kernel` W=1：**零警告**（拆分前后一致）
- 每改一处即编译验证；零硬件触碰

## 6. 诚实边界

- 本轮纯离线重构；dispatch 行为等价性由"纯机械移动"保证，未做活体回归（dispatch 路径在 r395/r397/r399 已活体覆盖）
- `pvr_translator_prepare_locked` 未拆，记录原因（见 §2）
- 8 个 helper 函数均为 `static`，仅本文件可见，无 ABI 影响
