# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r175 扩展区算术闭合；T3 输入可解释）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r175：扩展区算术闭合）

- 零硬件触碰，无代码改动。离线 GDB 读生成器 job：`c1064=0/c106c=0/c2c=1`，单 type=3 条目；`0x1078+0x18+32+224+40=0x11B8` 内容终点（`0x1200` 系分配器量子）；条目 `+8` = `uVar16` 写回（r160 之谜亦解）；三段源字节与 39B runs 逐项对齐。载荷 B 为传输描述符形态（含 `0xa3xxxx` 小 VA）。
- 证据：`reports/r175-extension-arithmetic.md`。候选下一步：写 T3 translator 输入规约。

---

## 本次会话进展（r174：SubmitTransfer3 accept-and-log 与真实 CCB）

- 真机已批准（含内核改动+两次 `=2` 重载，均 ref0，probe 零触碰）。桥新增 observe handler（定界/鉴权/零嵌套读/零执行，5 项门禁+反向验证；r150 旧断言改判）；`make kernel` W=1 零警告，274+272 全绿。真实 blit 两轮：39/39 非零字节与 fabricated 逐字节一致（仅 `+0x40` 不同）；`+0x40` 非单调，计数器命名收回。桥恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r174-real-ccb-captured.md` + trace。候选下一步：T3 translator 输入规约重启。

---

