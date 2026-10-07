# r196：fabricated RGXKickGfx 生成 flag&2 update 并到达 0x82:0x14（零硬件触碰）

## 结论

在 fabricated bridge harness 中，直接调用 `RGXKickGfx` 可进入
`SubmissionSetUpdateSyncPrim`，产生 1 个 `flags=2` 的 update 条目，并发出
`0x82:0x14`（IN 108 / OUT 4）。这证明 UMD producer 链可离线触达；不证明
真实 CCB 已执行或当前 bridge 支持该命令。到达该路径前，harness 手动把
render context 的 slot 字段从 `0xffffffff` 改为 0，这个初始化来源尚未找到。

## 实测

- 离线 UMD SHA-256 为
  `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与
  `DECOMPILATION.md` 期望值一致；按该版语料核对 producer/helper 调用关系。
- 使用 shim 与 `UMD_DRM_MAJOR=2` 建立 fabricated harness；调用
  `RGXCreateDeviceMemContext` 和 `RGXCreateRenderContext` 均成功。由于
  `RGXCreateRenderContextCCB` 的 `param_2` 为 `uint32_t *`，`param_2 + 4`
  对应字节偏移 `+0x10`，在 harness 中设置为 device context 指针。
- 新建 render context 的 `+0x24` 为 `0xffffffff`。仅在本次离线 harness
  中执行 `poke b11*+0x20 u0`，将包含该 slot 的 qword 清零后，producer 才能
  继续。该 poke 是手动状态种子，不是已确认的正常初始化流程。
- GDB 在 `SubmissionSetUpdateSyncPrim` 观测到 `update_count=1`，首项
  `flags=2`；`RGXKickGfx` 返回 0。
- trace 第 107 条记录 bridge command `0x82:0x14`，`in_size=108`、
  `out_size=4`；fake shim 返回 0。完整 trace：
  `r196-gfx-update-producer.jsonl`。

## 推断与边界

结果与 r190 定位的 `0x82:0x14` 路径吻合，并说明 `RGXKickGfx` 是可到达
update helper 的 producer。所有对象、地址及 bridge 响应均为 fabricated；
手动清 slot 的做法未证明应用正常路径会如此初始化。trace 中的指针和字节
内容不能单独用于推断真实 wire 字段语义。本轮没有真实 bridge handler、GPU
执行、CCB 完成或像素读回证据，不得据此标记为已支持。

## 后续（由 r197 更正）

r197 更正了手动 poke 的字段身份和 update-list 初始化路径。下一步保留 render
context perf callback defaults，在调用者 `psKickTA` 中按该版 UMD 的输入结构构造
update 项，再用 GDB 和 fabricated bridge 验证 producer 输出；活会话仍 freeze。

## 更正（r197）

r196 将手动清零的 render-context `+0x20/+0x24` 描述为 update slot。按同 SHA
语料复核，这两个位置是 `PerfCountStartCbID` / `PerfCountEndCbID` AppHint 字段；
真正的 GFX update 列表由 `RGXPrepareTA` 分配并初始化。r196 的 bridge trace 和
helper 观测仍有效，但“清 slot 是到达 update producer 的必要条件”已撤回。详见
[`r197-correct-gfx-update-init.md`](r197-correct-gfx-update-init.md)。

## 补充更正（r198）

临时 `musa.ini` AppHint 可以把 `PerfCountEndCbID` 合法初始化为 0，因此不需要
直接 poke render-context 对象。r198 又实测该字段会影响 RGXPrepareTA 的状态表
索引；之前把它称作 update-list slot 或称其与 kick 无关都不准确。r196 的 bridge
trace仍有效，r198 的无 poke 重放尚未到 update helper。见
[`r198-gfx-apphint-replay.md`](r198-gfx-apphint-replay.md)。
