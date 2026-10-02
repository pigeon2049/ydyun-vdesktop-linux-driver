# r68：重启恢复 + 泄漏清除验证

r67 的 device-mutex 泄漏经重启干净清除（锁属 PCI core，无他法）。
重建与重验证一次走完：

## 恢复序列（均绿）

- `mtgpu` 仍 `[644]`/`-19` 失败 → 解绑；只读 `Guest=2/FW=1` →
  `cold_disconnect`（18 环 idle）→ `finish=1` 回读 `Guest=0/FW=1`。
- `fresh-trial --run --runtime-context`：`load_rc=0`，
  `connected=1/published=1/pinned=1`；主模块散列与集成报告一致。
- bridge（`894faf50`，未改动）加载，build-id 与在盘一致。

## 泄漏清除验证

- 首个 MapPMR 即成功（`1→2→1`）——泄漏若在，必挂起。
- 节点探针 0 失败；UMD 八级阶梯全部 `exit=0`，**含 r67 当事的 rung4**。
- plan 行显示未对齐 range 正常入 plan（10/43 页）；各文件
  `fallbacks=0`，最高占用 99/512；零 WARN/BUG/Oops，无 D 任务，
  无 hung_task 刷屏。
- 终态 `Guest/FW 2/2 pinned`、`pending=0`；主模块引用 1，bridge 引用 0。

## 操作纪律（r67 教训的执行版）

- 所有 bridge-DMA 路径命令一律 `timeout` 包装且超时值收紧到 120 秒；
  超时即停手、不堆任务——D 态任务杀不掉，堆一个多一份永久泄漏风险。
- `timeout` 本身不背锅（TERM 只杀跑得动的），背锅的是临界区内被杀；
  因此超时只用于“挂起探测”，成功路径依然秒级返回。
