# r140：Δ1 猎杀终结——`lease_free` 漏 `drm_gem_object_release`，修后带帧 Δ0

- **结论**：Δ1 与 fence/BO/ẵn 无关，是 `mt_live_3d_drm.c:lease_free`
  漏掉了自定义 GEM free 必须做的两件事（兄弟 `mt_live_drm.c:125-127`
  与 `mt_gem.h:126-128` 都有）：`drm_gem_object_release()`（内含 `dma_resv_fini`）
  和 `drm_dev_put()`。无 resv fini → 每个 reservation 里最后一次提交的 fence
  永被引用 → +1 probe ref/提交文件（与提交数无关）；无 dev put → +1 drm_device
  引用/CREATE（smoke 计 +2，drm 层泄漏）。修后单 fill 与全 smoke 均为 **Δ0**。
- 本轮 2 行修复（`lease_free` 尾部，与兄弟实现逐行对齐），probe/bridge 未动，
  无需重启，会话完整保留。

## 定位链（执行过）

1. 排除 fence：`mt_drain_pending enable=1` 在 3/3 completed 后查全 DM
   `count=0/empty`、`drained=0`——pending 无滞留。
2. 排除 open/create：`/tmp` 一次性探针（open+query+CREATE+close，零提交）→ Δ0。
3. 缩小到提交：单 fill（无 syncobj）→ +1；单 copy（无 syncobj）→ +1。
   此时 fence 已排除、BO 台账（objects=34/gem=0/processes=2/contexts=2）零增长、
   dmesg 零 WARN——唯一剩下的提交期 probe 引用源是 resv 里的 fence。
4. 对账：`dma_resv_add_fence` 每次替换旧 fence（旧者释放），只剩最后一次的
   resv 引用；文件关闭 free 对象时 resv 从未 fini（自定义 free 的责任）→ 恰 +1/文件。
   与“每提交周期 +1、与提交数无关、空载 +0”逐项吻合。

## 门禁与验证（执行过）

- `make kernel` W=1 零警告/零错误（仅 live_3d_drm.ko 重编；probe/bridge 二进制未变）。
- `make check-offline` 全绿（272 checks）。
- 活体（同一会话，ref 基线 8）：单 fill → 卸后 **8**（Δ0）；全 smoke（3/3 completed）
  → 卸后 **8**（Δ0）；两次均 dmesg 零 WARNING。反向证据：修前同路径 +1 ×4 轮。
- 离线 C 门禁缺口（如实记录）：`lease_free` 在 recovery 模块内，无 userspace
  harness 覆盖（`gem_lifetime_test` 只覆盖 `mt_gem.h`）；本次以活体 refcnt
  （恰为泄漏的原始度量）为门禁，行为变更仅为补齐与兄弟实现一致的配对调用。

## 终态（freeze）与遗留

- probe ref **8**（= 1 会话 pin + 历史 Δ1×7，已冻结不再增长；清零只能重启，
  当前无必要），bridge ref 0，`card2`/`renderD129` 在位，dmesg 零 WARNING。
- `/tmp` 一次性探针（nosubmit/onefill/onecopy）为诊断脚本，不入库。
- 遗留：① 75+ 提交未 push；② `r135-major2-ccb-create.jsonl` 未入库文件仍在，未动；
  ③ 主线 STATUS 的 DDK2/Translator 待办不受本轮影响。
