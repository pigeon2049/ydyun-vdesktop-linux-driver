# r152：fabricated replay 返回单核，解除 UMD 零尺寸 TDM store 阻塞

- **结论**：离线 shim 的 `0x1:0xc GetMultiCoreInfo` 不再全零回包；现在回显请求 caps 并报告单核，与 r150 bridge handler 对齐。定向测试、反向验证及离线总门禁通过。尚未运行完整 Rogue2D replay，不能据此声称 UMD 已到达 `0x89:0xa`；零硬件触碰。

## 实测

1. 留档 `reports/r87-rogue2d-spike.jsonl` 中，`0x1:0xc` 的 12B IN / 16B OUT 为全零；r150 已定位这会让后续 TDM context-store 分配大小为零。内核 bridge 在 r150 已改为回显 caps、返回 `num_cores=1`。
2. `probe/umd_bridge_shim.c` 新增 `fabricate_multicore_info()`，仅在 fabricated `0x1:0xc` 路径中复制 caps 到 OUT `+0`，并写 `num_cores=1` 到 `+12`；错误字段保留预清零的 0。扩大/缩小尺寸仍沿用外层 bounded output 缓冲规则。
3. `tests/test_pvr_multicore_info.py` 新增 shim 路径检查。定向测试 3 项通过；将 core count 暂改为 0 时新测试失败，恢复后通过。
4. `make -C mt-vgpu-guest check-offline` 全绿：264 Python（1 skip）和 C RAM 272 checks。shim 独立 `-Wall -Wextra -Werror` 构建成功。

## 推断与边界

- 单核来源仍是 r28 已验证的 S3000 topology；本轮只把既有假设一致地用于离线 UMD replay，不构成新的拓扑实测。
- 该修改让后续 fabricated replay 有机会跨过零尺寸 store 墙；并未实际调用完整 Rogue2D 栈，也未验证 TDM store contents、CCB 内容或 submit completion。下一步应在无硬件的前提下复用既有 harness，确认回包后 UMD 的后续调用序列；hardware bridge 仍因 Oops 根因未定而禁止重载。
