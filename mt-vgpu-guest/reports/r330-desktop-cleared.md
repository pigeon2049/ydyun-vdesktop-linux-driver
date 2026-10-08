# r330：桌面进程全清（用户指令冷启动前）

- **操作**：用户指令 kill 所有 opencode 进程后自行冷启动。按 exe 精确匹配 `/opt/OpenCode/*` 清掉桌面树 10 个进程（9 桌面 + 1 crashpad，TERM 即净，无需 KILL）；serve 后端（`opencode-cli serve`，会话命脉）保留——如需连它一起杀另行指令（等于结束本会话）。未用 `pkill -f`（模式串自匹配教训，r295 回响）。
- **现态**：renderD128 零持有，bridge ref 0，probe ref 1，节点 card0/card1/renderD128，dmesg 干净。**当前为天然重载窗口**（桌面本就关闭中）：bridge 相关实验此刻无需额外停机。
