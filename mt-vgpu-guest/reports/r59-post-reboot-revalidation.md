# r59：重启后新 bridge（e812d938）验证链重放

重启原因：用户执行。旧 pinned 会话随重启清除；工作区改动（r58 复核修复
含在内）在盘完好。

## 恢复序列（与既往一致，均绿）

- `mtgpu` 启动仍 `PhysHeapsInit [644]`/`-19` 失败，QXL 显示，引用 0 → 解绑。
- 只读 `Guest=2/FW=1` → `mt_cold_disconnect finish=0`（18 环 idle，
  `fw_pa=0x77dfef000`）→ `finish=1` 回读 `Guest=0/FW=1` → 卸载 helper。
- `fresh-trial --run --runtime-context`：`load_rc=0`，
  `connected=1/published=1/pinned=1`，主模块散列与集成报告一致。

## 新 bridge 验证（loaded == 在盘，e812d938）

本次加载的是含 r58 复核修复的构建（release 路径域修正仅影响未加载的
helper；bridge 本体为注释/shadowing 同义改动）。重放全部：

- DMA smoke：引用 `1→2→1`，PASS；节点探针 0 failing/0 mismatch。
- UMD 八级阶梯 rung1–rung8 全部 `exit=0`。
- 日志见 `DMA domains` + `CPU-only PVR VM plan` 行；无 WARN/BUG/Oops。
- 终态：`Guest/FW 2/2 pinned`、`pending=0/completed=0`；主模块引用 1，
  bridge 引用 0。

r58 记载的 “loaded c77ad92d” 已被本轮替换；回滚快照
（`/tmp/opencode/bridge-rollback-r55/`，重启已清空）不再需要——
当前构建即源码 HEAD 构建，行为与验证一致。
