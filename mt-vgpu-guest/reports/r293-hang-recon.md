# r293：submit3 后 hanging 机制 recon（离线）——等 UMD 同步，无 fence 可等

- **结论**：r292 trace（8201 行）以 `0x89:0xa ret=0` 结束，其后**零 syscall 记录**（16 `ioctl_real` + 5 `mmap` 全在 submit3 之前；之后 `futex`/`poll` 亦不可见——shim 只记 open/ioctl/mmap/munmap/lseek/statx）。结合 r174（observe“无 fence”）与 r172（同形 submit `check=0/update=2`）：桥回 0 + 零填充 4B OUT + 无 fence + 不写 update 回写 + 目的池零像素——UMD 在等一个永远不会来的完成信号。候选按似然排序：① update 回写轮询（CPU spin，无 syscall，最吻合寂静）；② fence fd poll（shim 不记 poll，同样寂静）；③ 共享内存标志 spin（同①）。**r279 的“UMD 自身不定行为”命名收回**：hang 是确定性的因果（无完成语义），不是 UMD  flake。
- **推论**：scratch-fire 不解除 hang（r279 无 fire 照挂，r290–292 有 fire 照挂——fire 写 scratch，不写 UMD 同步/目的池）。要让 UMD 越过 submit3，必须补完成语义（二选一）：A. submit3 回 signaled fence（`0x88:0x4` 即时 fence 有先例 r63）；B. fire 写真实目的池 + update 回写（大改，跨 file 对象禁区）。
- **r294 活体设计（窗口，无代码改动）**：三开 `=2` 纯 observe（translator 全关，最小动参）→ 真实 blit → hanging 后**不杀**：读 `/proc/PID/wchan` + `status` + `stack`（ poll 调度 vs 用户态 spin，一读即分；`gdb bt` 备用，机上有 gdb）→ 拍照后 kill → 拆桥 → 默认 → L3 → 拉桌面。若 wchan=poll 系 → 补 fence 路线 A；若用户态 spin → 读 PC 附近映射定位轮询地址（`maps` + PMR 台账对照），走回写路线 B。

## 边界

- 本轮纯离线：无代码改动，门禁状态沿用 r292（366+292）。
- submit3 的 108B IN 本轮未解（trace 只记尺寸）；OUT 4B 内容语义（fence vs status）是 A 路线的前置未知，GDB 窗口一并看 UMD 取 OUT 后的第一动作（需 `catch syscall` 或单步——窗口内尽力，不保证一次得手）。
