# r265：锁序修复（离线实现，零硬件触碰）——trial_lock 分段，slices 移出

- **结论**：r263 死锁的锁序修复离线落地：bring-up 在 slices 调用前后分段放/取 trial_lock（`unlock → slices → lock`），slices 的 buffers->lock 不再嵌套；translator_lock（全体 prepare 调用者持有）防重入。门禁 +1（顺序断言：unlock < slices < relock）+ 反向验证（删 unlock 即红）；`check-offline` 335 Python + 292 C 全绿；`make kernel` W=1 零警告。**未加载（在载桥仍 r263 死锁构建，默认参数下休眠），会话未碰。**

## 实测（执行过，零硬件触碰声明）

1. 开工即声明零硬件触碰；`lsmod` 开工收工一致（probe ref 1 / bridge 默认 ref 0）。
2. 代码：bring-up tqx 块加解锁对 + 注释（锁序说明）；门禁顺序断言。
3. 反向验证：删 unlock 对 → 门禁 FAIL；还原 → OK。
4. `make kernel` 全模块零警告；`check-offline` 全绿。

## 边界

- 修复正确性待活体（slices 重验，批准执行）；fire 函数仍缺。
- 解锁窗口期 translator.ready=false，但 translator_lock 挡住并发 prepare（调用点全持该锁，r265 注释在位；若有例外调用点则假设失效——已走查 submit3/kicksync/exit 三处全持）。
- requirements.json 未动。

## 下一步（候选，需批准）

1. slices 重验（批准执行）：`=2` + tqx_ctx 重载 + 真实 blit → `tqx slices: ready` 行。
2. fire 函数（离线实现+门禁）。
