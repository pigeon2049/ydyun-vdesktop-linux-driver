# r282：fire 独立模块离线实现（零硬件触碰）——自有 render 节点，不碰 bridge

- **结论**：r281 死胡同（renderD128 被会话桌面自持有，bridge 不可重载）的绕行方案落地：新模块 `mt_live_tqx_fire`（`kernel/recovery/mt_live_tqx_fire.c`，约 480 行）自有 DRM render 节点（driver `mtlivefire`，零 ioctl）+ 自有 64 页 space/TQX context/slices/scratch，直连 probe 会话（`pci_get_drvdata` + `try_module_get`，无 `__symbol_get`、无 `translator.` 引用），root-only `run` 参数单发 21 块 fill（默认 1280×1024/`0xff0000ff`，r226 值），21 fence 逐序等（5s 预算，失败即停不堆任务），验尾块内容。`check-offline` 363 Python（含 9 新门禁）+ 292 C 全绿；`make kernel` W=1 零警告；反向验证通过。**未加载，会话未碰。**

## 实测（执行过，零硬件触碰声明）

1. 开工即声明；只读预检：bridge ref 1（桌面持有）、probe ref 1；无 rmmod/insmod、无 GPU 提交。
2. 复用三处已验证模式：`mt_live_tqx` 会话获取/单发 param/upload 传输、`live_3d_drm` 的 `drm_dev_alloc/register` + `upload/seal` 顺序、bridge translator 的 bring-up 顺序（bind→`bind_boot_shared`→upload→seal→process→tqx ctx→slices）与 r280 分块数学。
3. 锁序（r263/r265 教训）：prepare/fill 的 `buffers->lock` 永不在 `trial_lock` 内（slices 与每块 prepare 前后都分段放/取 `trial_lock`，门禁断言顺序）；submit 逐块取 `trial_lock`；fence 等待全在锁外；verify 读回重取锁。
4. 门禁（`tests/test_pvr_live_tqx_fire.py`，9 项）：自有节点（`mtlivefire` + 不出现 `renderD128`）/无 bridge 依赖（无 `__symbol_get`/`translator.`）/64 页/fail 行号/slices 锁序/分块上限/单发/等待锁外/teardown 对称（含 `drm_dev_unregister`）。
5. 反向验证：`chunks=%u`→`chunk=%u` 即 1 项 FAIL，还原即 9/9 OK（另修门禁自身 bug：`cls.run` 覆盖 `TestCase.run`，改名 `run_body`）。
6. `make check-offline`：363 Python OK + 292 C OK；`make kernel` W=1：rc=0，warning/error 0 行；`.ko`（约 1MB）含 `mtlivefire` + `chunks=` 双字符串。

## 边界

- 活体未跑：bring-up（`prepared=1` + `slices: ready` + 新节点出现）与 fire（`fired=1 chunks=21 verified=1`）待批准窗口。加载本模块不碰 bridge/probe（只增不卸），renderD128 持有者无关。
- 排他门：会话 markers 非空闲或 `address_spaces/buffers.objects` 非零时加载即拒（`-EBUSY` 大声失败），bridge translator 残留会挡住而非串扰。
- 全帧内容从不同时驻留（r280 口径延续）：fence 证明每块执行，内容只验尾块。

## 下一步（候选，需批准）

1. fire 独立模块活体（批准执行）：`insmod mt_live_tqx_fire.ko enable=1`（bridge 未碰）→ 确认新 render 节点 + `fuser` 无持有 → `echo 1 > run` → 取 dmesg `fired/chunks/verified` 行 → `rmmod`（teardown 对称，refs 回落）→ 写 r283。
