# r327：CheckFences 反汇编切入（批准执行）

- **内容**：真入口（`nm -D`：`TQ_CheckFences @ 0x65240`）对齐反汇编得验证形状：`type=[rdi+0x3c]` 开关（1/2/4/5/default）+ `count=[ctx+0x204]` 比对 + fence 对象 `0x8` 位循环 + `[r8]` 三处写入；dprintf 配方首发因 python `%` 转义漏改空跑，修正后 r328 命中。停桌面 → `=2` → 尝试 → 拆桥 → 默认 → L3 双绿 → 拉桌面；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**无代码改动。
- **证据**：`r327-cf-attempt.txt`（0600，空跑现场）；暂存区已清空。门禁沿用（386+299）。
