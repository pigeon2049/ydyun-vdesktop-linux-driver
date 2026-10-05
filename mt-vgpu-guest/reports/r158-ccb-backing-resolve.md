# r158：SubmitTransfer3 的 CCB VA 已关联到 PMR backing，窗口内容已转储

- **结论**：fabricated `musa_blit_test -device 0 -f -o`（`UMD_DRM_MAJOR=2` + shared backing）在 `0x89:0xa SubmitTransfer3` 的 `ccb_data=0x8000f44000` / `ccb_bytes=0x1200`（4608B）已用 shim 内 VA 台账关联到 reservation `0x900d`（`0x8000f430fe` + `0xa00fff`）→ PMR `0x500e` → backing `0x500e000` + `0xf02`。该 4608B 窗口有 39 个非零字节、首非零在窗内 `+0x10`、FNV-1a `0xb9e0f1a18201bf0f`；整个 10MB backing 的非零字节数同样是 39 且首非零 `0xf12`（= `0xf02+0x10`）与窗口一致，故该 PMR 的全部非零内容即 CCB 窗口内容，归属成立。本轮零硬件触碰。

## 实测

1. 核对 `libsrv_um_MUSA.so.1.0.0` SHA-256 为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与 5.2 语料一致。shim 以 `-Werror` 重编；`make -C mt-vgpu-guest check-offline` 通过：269 Python（1 skip，含新增 `test_pvr_shim_ccb_resolve`）+ 272 C。`lsmod` 确认无 `mt_*` 模块加载。
2. shim 新增 VA 台账：`0x6:0x15` 记录 reservation（addr/len/heap），`0x6:0x13` 记录 pmr ↔ reservation；`0x89:0xa` 提交前按 r151 ABI（`ccb_data@88` / `ccb_bytes@104`）查包含该区间的 reservation，再找 PMR，再找 `off>>12 == pmr` 的 shared backing，计算窗内非零数/首非零/FNV-1a 与 32B 采样，记为 `ccb_resolve`。不可达时记 `resolved:0`，不猜测。
3. 重放 `musa_blit_test -device 0 -f -o`（`UMD_DRM_MAJOR=2`，shared backing + snapshot），`timeout -s KILL 15` 终止（shim 回 fabricated 零值后 UMD 不退出，与 r157 相同）。trace 528 行、105 条 bridge ioctl；Submit3 输入解码为 `check=0/update=2/pmr_sync=0/flags=0/ccb=0x8000f44000/0x1200/opaque=0`。完整证据见 [`r158-ccb-resolve.jsonl`](r158-ccb-resolve.jsonl)（`seq=527` 为 `ccb_resolve`，`seq=528` 为 Submit）。
4. `ccb_resolve`：`reservation=0x900d`（`res_addr=0x8000f430fe`，`res_len=10489855`，即 seq 509/510 的 reserve+map 对：PMR `0x500e`，heap 2 General），`backing_off=0x500e000`，`backing_offset=3842`（`0xf02`），窗内 `nonzero=39` / `first=+0x10` / `fnv=0xb9e0f1a18201bf0f` / 采样 `5840f40080000000 0000000067000000 0000000000000000 7810000001000000`。同 trace 的 `pmr_snapshot` 中 `0x500e000` 整体 `nonzero=39`、`first=0xf12`、采样相同——整块 PMR 的非零内容恰好就是该 CCB 窗口，无其他写入者。
5. 新增门禁 `test_pvr_shim_ccb_resolve.py`：合成 client 做 PMR alloc → reserve → map → mmap → 写窗内 pattern → Submit3，断言 `resolved=1`、`backing_offset`、`nonzero` 与第二发越界 VA 的 `resolved=0`；反向（关掉 shared backing）断言无 `ccb_resolve` 记录。注入验证通过后恢复。

## 边界

- 看到 ioctl 不代表 bridge 接受或 GPU 执行：shim 回包仍是 fabricated 零值，trace 只证明 UMD 生成了 Submit3 输入及其 CCB 窗口字节，不证明命令有效或已执行；不重载硬件 bridge。
- 窗口 4608B 中仅 39B 非零：这是 UMD 经 CPU 映射写入的实际字节（含 `0x5840f40080000000…` 头），是否为完整合法 CCB 语义仍需对照 `SubmissionCmdGenerate` 语料解读，不在本轮断言内。
- `0x500d000` 的 2,621,440 非零字节与本 CCB 无关（像素/池数据，未归属，不断言）。

## 下一步

- 对照 5.2 UMD `SubmissionCmdGenerate` / `FUN_0015f890` 语料解读该 39B 稀疏窗口的字段含义（check/update 数组语义仍是 STATUS 下一步第 2 项）。
- 真实绘制 CCB 的翻译范围已收敛到该窗口；转真提交前仍需确定 update 数组可见性与完成条件。
