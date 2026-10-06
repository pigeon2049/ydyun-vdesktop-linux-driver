# r139：sealed 泄漏修复 + 活体验：空载 Δ0，带帧 Δ1（批准执行 ②）

- **结论**：25/周期结构泄漏已修。空载周期 probe 引用 **Δ0**
  （首周期曾 +9 的 boot borrow 现借还平衡），带帧周期 **Δ1**（Δ1 确认为提交路径残留，
  与提交数无关，精确对象待猎杀）。卸载零 WARNING（此前固定 2 条 destroy WARN 消失）。
- 修法（3 处，最小面）：`mt_gpu_vm.h` 加 `mt_gpu_vm_unseal`
  （仅 idle 可重开：`active_uses||owners` 即 `-EBUSY`，未 seal 即 `-EALREADY`）；
  `mt_vm_vram_destroy` 在 fini 前对 sealed 空间先 unseal（忙则原样 `-EBUSY` 返回）。
  **fini 未动**（selftest 的 `-EBUSY` 断言继续成立），**零结构体变更**
  （ABI 门 `mt_gpu_vm` 等 7 结构比对全过），live 各模块零改动。

## 门禁（执行过，全绿）

1. `tests/boot_bo_lifetime_test.c` 新增 sealed-teardown 块：
   seal→destroy(idle) 成功且计数平衡；seal→destroy(busy) `-EBUSY` 且保持 sealed；
   unseal 直测（0 / `-EALREADY` / busy `-EBUSY`）；直接 fini 仍 `-EBUSY`。
2. 反向验证：临时掐掉 destroy 内 auto-unseal → 新断言失败
   （`!vms.ops->destroy(a)` abort）；还原后通过。
3. `make check-offline` 全绿（含 272 checks）；`make kernel` W=1 零警告；
   `make check`（L2）全绿：C 模型 + 内核构建 + ABI 门（含 `shared-abi-baseline.json`
   无漂移；`runtime-integration-build.json` 系 gate 自刷新，module_sha 已对上新 probe.ko）。

## 活体验（新会话，修后二进制；需重启部署——旧 probe 被 ref 86 pin 住无法 rmmod）

- 重建（r125 流程）：cold finish=0/1 全 idle → `fresh-trial --run --runtime-context`
  rc=0（`connected=1/published=1/pinned=1`，trial `20261004T070741Z-1966ee3f`），基线 ref=1。
- 空载周期：MID=36（= 1 pin + 34 backing + 1 live 持有的 try_module_get 瞬态），卸后 **POST=1**；
  复算：25 create + 9 borrow 全释放（含 borrow——release 回调脱钩 slot，借还平衡；
  旧码下 borrow 恰因引用不到零而“缓存”，即首周期 +9 的来源）。
- 带帧周期（`mt-fill-check smoke`，3/3 completed 零 fault）：卸后 **POST=2**，即 Δ1 存活。
- 桥 + L3（会话配平）：新桥在载（ref 0），`card2`/`renderD129`；
  node probe 0 failing / dma smoke PASS（refs 2→3→2）；L4 未跑。dmesg 零 WARNING。

## 终态（freeze）与遗留

- probe ref **2**（= 1 pin + Δ1），bridge ref 0；freeze 生效（Δ1 猎杀除外）。
- 遗留：① Δ1（提交路径 +1/周期）精确对象未定位——修后二分已将其隔离为独立单变量，
  猎杀单独立项；② 74+ 提交未 push；③ `r135-major2-ccb-create.jsonl` 未入库文件仍在，未动。
