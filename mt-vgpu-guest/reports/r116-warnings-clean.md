# r116：4 处旧警告清零（门禁卫生小轮）

r80 发现、r115 记账的 4 处 r38 期 `W=1` 旧警告已清除：
`mt_drain_pending.c` 未用变量 ×2（连带无用的 `bar1` iomap/释放一并删，
`bar0`/`fw` 保留），`mt_fw_event_io.h` 加 `__maybe_unused`
（该 ops 有真用户 `mt_guest_probe.c:279`，警告只因部分 TU 纯 `(void)` 引用），
`mt_fix_poll.c` 补 `MODULE_DESCRIPTION`。
`make kernel` exit 0 且**零警告**（干净重编亦然）；
L1 226 Python + 268 C 全绿；反向验证（注入未用变量 → `W=1` 抓到 → 还原）。
代码提交 `d3b7453`（2+/6-，与文档分开）。
快照"零警告"口径现无需"增量"限定。零硬件触碰（纯构建 + 用户态测试）。
