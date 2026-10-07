# r280：fire 分块循环离线实现（零硬件触碰）——全帧可发，验尾块

- **结论**：接盘盘内半成品（struct 已改数组、submit/teardown 仍用旧单 fence，构建已破坏），收尾为完整分块 fire：1280×1024×4=5MB 按 51 行/块拆 21 块（上限 64），每块复用 256KB scratch 基址，21 fence 逐序等待，验尾块内容。`check-offline` 354 Python（含 3 新门禁）+ 292 C 全绿；`make kernel` W=1 零警告；反向验证通过（删 `chunks=%u` 即 2 项失败，还原即绿）。**未加载，会话未碰。**

## 实测（执行过，零硬件触碰声明）

1. 开工即声明；只读预检：`mt_pvr_bridge` ref 1、`mt_guest_probe` ref 1 在载，`/dev/dri` 有 `card1`/`renderD128`；无 rmmod/insmod、无 GPU 提交。
2. 发现半成品：`git diff` 仅 `mt_pvr_bridge.c` 有未提交改动（work 端数组化，submit/teardown 仍 `fire_fence` 单字段，`grep` 实锤 6 处残留）。
3. 代码（`kernel/recovery/mt_pvr_bridge.c`）：
   - submit 按 `chunk_rows = SCRATCH_BYTES / row_bytes` 切条（零宽/单行超限/`>64` 块均 `-E2BIG` 大声拒绝）；每块 fresh `work` + 同 `fill_ws`，prepare→submit 取 fence 入数组；中途失败走 `fail_chunks` 全 put 后返回。
   - work 端逐序等全部 fence（首个未 signal 即记 `f/%u/%u` 行号），再读尾块像素比对 color，上报 `chunks=%u`。
   - teardown 首 `cancel_work_sync` 后循环 put 全数组（单字段残留清零）。
4. 门禁（`tests/test_pvr_tqx_fire.py` +3：分块切条/全 fence 等待/teardown 全放 + 单字段消亡断言）。
5. 反向验证：`chunks=%u`→`chunk=%u` 全替换后 2 项 FAIL，还原后 13/13 OK（首轮变异改宏名后缀因被子串包含未触发，作废，记教训）。
6. `make check-offline`：354 Python OK + 292 C OK；`make kernel` W=1：rc=0，warning/error 0 行，`.ko` 重建。

## 边界

- 全帧内容从不同时驻留：fence 证明每块执行过，内容只验尾块——首帧像素闭环仍需活体确认。
- `fire_width` 字段已置（rect.width）暂仅记录，供后续偏移读回使用。
- 活体未跑：分块 fired/verified 待批准窗口（`=2` + tqx_ctx + fire 三开 + 真实 blit）。

## 下一步（候选，需批准）

1. 分块 fired/verified 活体（批准执行）：三开重载 + 真实 blit，看 `chunks=21` + `verified=1` 行；blit hanging 仍按 r279 判据（可 rmmod 即 UMD 行为）。
