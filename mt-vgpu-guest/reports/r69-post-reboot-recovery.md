# r69：重启恢复（r68 流程复走）

常规重启后重建：`mtgpu` 照例 `[644]`/`-19` 失败 → 解绑；只读
`Guest=2/FW=1` → cold 检查（18 环 idle）→ `finish=1` 回读 `Guest=0/FW=1`；
`fresh-trial --run --runtime-context`（`load_rc=0`，
`connected=1/published=1/pinned=1`）；bridge（`894faf50`，未改动）加载。

验证：DMA smoke（`1→2→1`，120 秒超时包装）、节点探针 0 失败、
UMD 八级阶梯全部 `exit=0`；各文件 `fallbacks=0`，最高占用 99/512；
零 WARN/BUG/Oops，无 D/Z 任务残留。终态 `Guest/FW 2/2 pinned`、
`pending=0`；主模块引用 1，bridge 引用 0。

不要卸载模块、解绑设备或提交额外工作。
