# r117：日终活体盘点——零漂移（附 D 态误报教训）

今日所有动作（fabricated 数十次、gdb 二十余次、4 次批准 live 实验、
2 次内核构建）之后，活体会话零漂移：
`pending=0 completed=23`、`objects=34`、引用 38/1（1=Chrome）/0、
双 render 节点俱在、dmesg 0 WARN/BUG/Oops、D 态 0、`/tmp` 40%。
在盘桥含 TDM 代码、活体仍跑旧桥（加载窗口未动，符合预期）。

## 方法论（D 态误报，诚实记录）

`ps -eo stat,comm | grep -c '^D'` 两次给出 2——实为 comm 首字母 D
（DiscoverNotifier 之类）的误命中。正确查法是精确匹配 STAT 列
（`ps -eo stat | grep -c '^D$'`）或看 wchan；本轮复核 D 态确为 0。
"grep 行首"在 `stat,comm` 双列输出下不是状态断言——记入检查单经验。

## 待办快照（给用户的一句话）

- 未 push：自 `origin/main` 起 50 个提交（含今日全部 r72–r117 +
  AGENTS §9 + 0x89/shim/警告三组代码提交），等 push 批准。
- 待批事项：加载窗口（rmmod/insmod + L3/L4 重验）、特性开关立项、
  新会话执行——红线类一律未动。
