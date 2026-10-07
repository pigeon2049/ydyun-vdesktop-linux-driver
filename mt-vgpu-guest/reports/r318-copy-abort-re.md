# r318：copy abort 根因 RE（离线）——断言式自杀，setup 深水区

- **结论**：`TQJobSubmit` 内 abort 是断言式自杀（`ud2; ud2; call abort`，非错误传播）：core 栈 `TQJobSubmit+0x2e2 → abort-helper → abort`；trace 101 调用全零止于 `0x6:0x13` map，无后续 bridge 流量；与 blit 链 47 调用后分叉（测试不同，预期内）。abort 点在 setup 深水区（`TQ_BlitInit`/`TQ_CheckFences`/`TQ_LookUpEOT` 一带，静态对齐不可靠，未逐字命名）。**copy 缺的是 UMD setup 自身的前置条件，不是桥调用**（桥已全 0）。
- **候选排序（按验证成本）**：
  1. `-src/-dst` 系统内存表面（绕开 device PMR 路径；若仍 abort 即与 PMR 处理无关，一轮窗口可判）。
  2. 小几何（640×480；若 abort 消失即尺寸/预算相关）。
  3. GDB 活体断 abort-helper（`0x2c8cc` 处，按 ASLR 基址换算读参——r314/r296 同手法；需窗口）。
  4. 全量静态反汇编 `TQJobSubmit`（数百分支，重，不到万不得已不动）。
- **边界**：本轮纯离线；无代码改动，门禁沿用（386+299）。`+0x40` 式“逐字节对照”在此不适用（abort 在字节可比对之前）。

## 下一步（候选，需窗口约 5 分钟）

1. r319：同一窗口三连发（`-src -dst` / 小几何 / 默认复现对照），定位 abort 条件形状。
