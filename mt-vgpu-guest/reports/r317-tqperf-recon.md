# r317：copy producer 侦察（批准执行）——`TQJobSubmit` 内 abort，桥全 0 无罪

- **结论**：`musa_tq_performance_test -device 0 -n 1`（`=2` observe 窗口）：8200 行 trace，101 条 bridge 调用**全 0**（建链 + `0x89:0x5` + `0x89:0x8` TransferContext2 建成 + maps），止于一次 `0x6:0x13` map 后 UMD 内 abort（134，core 已入库）。core 验尸：`TQJobSubmit() (libsrv_um)` → abort——与 r162 fabricated 三重一致的 copy-setup 中止点**活体复现**。copy 路径缺的不是桥调用，是 UMD setup 自身过不去（RE 级工作，未展开）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**无代码改动。

## 实测（执行过）

1. 批准：standing 授权（part 2 第一步）。停桌面 → ref 0 → `=2` → tq-perf 单发 → 即时 abort → 读 trace（末调用 map，全零）→ 拆桥 → 默认 → L3 双绿 → 拉桌面（中途用户重开挡回一次 rmmod，二次停后 10 秒内关账）。
2. 证据：`r317-tqperf-abort.jsonl`（0600，8200 行）+ `r317-tqperf-core.bin`（0600，1MB，`TQJobSubmit` 栈）；暂存区已清空。
3. 时钟备注：本轮 `date`/`coredumpctl`/文件 mtime 显示 07:0x，而 dmesg ctime 早段为 04:xx——机器时钟疑似跳变约 3 小时（NTP？），dmesg uptime 轴自洽，报告时间以各证据自带戳为准，不混用。

## 边界与下一步

- copy 路径 = 多轮工程（UMD setup abort 根因 RE + copy 翻译新代码 + 验证），不是单窗口能烧完的；TA/3D 同理需 producer recon。transfer-fill 之外的新执行覆盖没有现成 UMD producer。
- 提议收敛：先 push + 快照刷新（part 1 已做：118 提交已推，§1/§5/§6/§11 已刷新到 r316，指针已推进并二推），再议大项。
