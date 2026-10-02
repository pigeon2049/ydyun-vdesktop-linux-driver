# r52：新 bridge（PVR VM plan）真实 UMD 阶梯验证

在 r51 同一 retained session 上完成（Guest/FW `2/2`，`pending=0`）。
bridge 为带 CPU-only VM plan 的新构建（build-id
`c77ad92d33bc5e58787fc3dbab30d23dce7cb2d0`），未重载。

## 方法

从树内留档恢复 UMD（`/tmp/mtt-linux-umd-5.2.0/.../libsrv_um_MUSA.so.1.0.0`），
按 `Makefile umd` 的 8 个 rung 逐级执行（`eval` 语义展开引号参数；
直接展开会把 `'b5*+0'` 当字面量传给 harness，导致 UMD 侧段错误，
与 bridge 无关，已确认并纠正）。

## 结果

8 个 rung 全部 `exit=0`，每个 SYMBOL 返回 0：

1. connect 2. device 3. devmemctx 4. render 5. syncprim
6. kicksync（create+destroy） 7. compute（create+destroy）
8. kicksubmit（`RGXKickSync` accept-and-inspect，即时 fence，无 GPU 执行）

trace 留存于 `/tmp/opencode/umda/l4-rung*.jsonl`。

## 运行后状态

- 会话保持 Guest/FW `2/2`、`pending=0/completed=0`；主模块引用 1，
  bridge 引用 0。
- 内核日志无新增 WARN/BUG/Oops、无 DMA-API 报错；本轮 UMD 阶梯未触发
  新的 DMA/VM-plan 日志行（r51 smoke 已覆盖该路径）。
- 未提交 GPU 工作；kick 走 S4-1 accept-and-inspect 路径。
