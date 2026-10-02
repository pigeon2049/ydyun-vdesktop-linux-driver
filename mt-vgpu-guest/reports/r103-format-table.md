# r103：格式表解出但全灭——门在别处（维度/validator）

r102 的下步执行：反汇编抠出格式能力表（`table[fmt*7]`，bit7/bit8
两谓词；fmt 0 非法，1–11+ 有效，`0x20202020` 系 RGBA 类），
13 个格式值逐个实测——**全部返回 3**。结论：Layout 的拒绝门
不在 format（a3），结合调用流（rsi=宽 → r12d，rdx=高 → r14d，
rcx=format → ebp 进两 validator），下步二选一：
(a) 扫宽/高（64 可能越界：min/max validator 未读）；
(b) 读两 validator 之后的分支（`jne bb90` 两条路各通向哪道门）。
离线，零硬件触碰。

## 格式表（`.rodata @0x1bba0`，u32×7/fmt）

fmt1–4：`0x20202020` 系；fmt5–8：`0x202020` 系；fmt9–11：`0x10101010` 系；
entry[0] 高位各异（`0x10/0x8000/0x8/0x800…`），entry[1] 跨系复用。
谓词：`bit7(entry[0])` / `bit8(entry[0])`。
