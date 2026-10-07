# MEMORY-HISTORY-2026-10-04（只读归档）

> 本文件是 `MEMORY.md` 的溢出归档，只读。状态冲突时以
> `STATUS.md` → `PROGRESS-SNAPSHOT.md` → `MEMORY.md` 为准。

---

## 本次会话进展（r123：意外重启后复核；只读）

- 外部重启：树完好（b4e0b5a，干净）、L1 全绿（+1 诚实 skip）、
  UMD 留档可用；会话已失（无模块、设备解绑、对象清零）。
- §12 活页已更新 + STATUS 现状加 stale 标注；重建待明确批准。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r123-post-reboot.md`。
- 遗留：重建批准；加载窗口；push；T3 首帧执行。

---

## 本次会话进展（r124：内核 107→111 漂移评估；只读+离线）

- 运行内核已是 6.12.111（107 headers 并存）；在盘桥 vermagic 实测即
  111，可直接加载，重建不需重编；L1 全绿（226+1 skip，268 C）。
- 会话仍失（无模块、设备解绑、仅 card0）；canvas 脏文件随 test 提交刷新。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r124-kernel-drift.md`。
- 遗留：重建批准（含 r113 首帧执行）；59 提交 push 待明确指令。

---

## 本次会话进展（r125：新会话重建完成；批准执行）

- cold-disconnect（finish=0/1）→ fresh-trial（2/2 retained）
  → 桥加载（build-id `2c6bede3…`）→ L3 全绿 → L4 八级全 0。
- 终态：probe 引用 1 / bridge 引用 0，dmesg 干净，无 D 任务；
  会话即刻起 freeze。证据：`mt-vgpu-guest/reports/r125-session-rebuild.md`。
- 遗留：r113 首帧执行下一轮单独确认；61 提交未 push（用户明确暂不 push）。

---

## 本次会话进展（r127：活体首帧执行成功；批准执行）

- 单帧 DM2 空包（frame_tag=1，无 RT）：seq=1，completed 0→1，
  faulted=0；`0x4668` 直通值被接受；模块已卸（留 2 条 r44 同类 WARN）。
- probe 引用 stays 35（已知泄漏类）；桥探针复核全绿，会话健康，继续 freeze。
- 证据：`mt-vgpu-guest/reports/r127-first-frame-live.md`。
- 遗留：T3 下一步（非零 CCB 仍缺，STATUS 第 1 项）；63 提交未 push。

---

## 本次会话进展（r126：r113 首帧 fabricated 门禁；离线）

- 新增 `tests/test_r113_first_frame_envelope.py` 8 项全绿并反向验证；
  L1 226→234，STATUS/快照 §6 计数已同步；会话仍 freeze。
- 新发现：模板 `0x4668=0xed00000000` 直通（活体风险，已标注）；
  STATUS 桥 build-id 旧值攒入刷新 pass。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r126-first-frame-gate.md`。
- 遗留：活体首帧执行待明确批准；62 提交未 push；STATUS 桥 id 刷新。

---

## 本次会话进展（r128：RT 绑定帧成功；批准执行）

- 单帧 RT 绑定 DM2（frame_tag=2）：seq=2，completed+1，faulted=0；
  64KiB 读回全 0x5a（空 marker 无绘制，符合设计）；模块已卸。
- probe 引用 35→61（+26，同 r44；r127 的 +34 差异未解释）；
  桥探针全绿，会话健康，继续 freeze。
- 证据：`mt-vgpu-guest/reports/r128-rt-bound-frame.md`。
- 遗留：真绘制内容仍无；65 提交未 push。

---

## 本次会话进展（r129：fill smoke 红，契约分歧；批准执行）

- smoke 倒在 bad[3]（0-syncobj 期望 EINVAL，驱动接受执行）；
  旧测试 vs 现驱动分歧，非回归；绘制像素待契约裁决后重跑。
- 旁证 +1 fill 已执行（1/1/0）；模块已卸（同签名 WARN 累计 6）；
  probe 引用 61→87（+26）；桥探针全绿，继续 freeze。
- 证据：`mt-vgpu-guest/reports/r129-fill-contract-dispute.md`。
- 遗留：契约离线裁决；绘制像素；66 提交未 push。

---

## 本次会话进展（r130：契约裁决，测试过期；批准执行）

- 离线裁决：驱动 `if (r->out_syncobj)` 自 r40，强制要求从未存在；
  测试期望是 day-one 误期。改测试（bad[3]→正向断言）+ uapi 注释。
- 活体 smoke 全绿：13 非法拒、0-syncobj fill、16×16 像素三重验证、
  copy 闭环；3/3/0，模块已卸（WARN 累计 8，同签名）。
- probe 引用 87→113（+26）；桥探针全绿，继续 freeze。
- 证据：`mt-vgpu-guest/reports/r130-fill-contract-verdict.md`。
- 遗留：67 提交未 push。

---

---

## 本次会话进展（r131：DDK2 特性开关，离线实现，零硬件触碰）

- 桥加 `ddk_feature_set` 模块参数（默认 0=legacy），helper + 4 条断言；
  check-offline 全绿（C 272），`make kernel` W=1 无警告，反向验证已做。
- 未加载新 `.ko`；活会话 freeze 不变。启用需卸桥重载，待用户批准。
- 证据：`mt-vgpu-guest/reports/r131-ddk-feature-switch.md`。
- 遗留：68 提交未 push；STATUS 门禁行写 234+268，实为 234+272（刷新 pass 时改）。

---

---

## 本次会话进展（r132：ddk_feature_set=2 活体，rung5 与默认一致；批准执行）

- 用户释放 Chrome 占用后，桥重载为 `ddk_feature_set=2`，rung5 全 0；
  再换默认重载同链：89=89 桥调用逐项一致。DDK2 可达性未确认（未跑 r78 的非零 CCB create）。
- 模块现为**默认参数的新桥**（已重载，build 与旧 freeze 不同）；引用 0/113；无新 WARN。
- 证据：`mt-vgpu-guest/reports/r132-ddk-switch-live-rung5.md`。
- 遗留：r78 非零 CCB create 命令需重建；69 提交未 push。

---

## 本次会话进展（r139：sealed 修复 + 活体验；批准执行 ②）

- 修法：`unseal`（仅 idle）+ destroy 自重开；fini/ABI 不动，live 模块零改动。
  门禁：新 C 断言 + 反向验证 + check-offline + kernel W=1 零警告 + make check（ABI 无漂移）。
- 活体验：新会话 ref 基线 1；空载 MID=36→POST=1（Δ0）；带帧 POST=2（Δ1 存活，待猎杀）。
  桥在载 ref 0，L3 全绿，dmesg 零 WARNING。证据：`reports/r139-sealed-fix-live.md`。
- Freeze（Δ1 猎杀除外）。遗留：74+ 提交未 push；重建+二分待批准。

---

## 本次会话进展（r137：+26/周期根因静态定位，只读；零硬件触碰）

- `fini` 见 sealed 即 `-EBUSY`（`mt_gpu_vm.h:309`），两 space 皆 seal 且全树无 unseal；
  destroy 的 2 条 WARN 即证据；VM 引用不断 → 25 backing/周期不释放 → probe 引用爬。
- 对账：首周期 +34 = 25 + 一次性 boot borrow 9；稳态 +26 = 25 + Δ1（未归属，需活体二分）。
- 修语义（unbind/teardown 或 fini 放行）单独立项：改代码 + 门禁 + 反向验证 + 活体回归。
- 证据：`reports/r137-sealed-vm-ref-leak.md`。遗留：72+ 提交未 push；重建+二分待批准。

---

## 本次会话进展（r136：泄漏 + /tmp 排查，只读；零硬件触碰）

- 驱动不写 /tmp：`kernel/` 下 `filp_open|kernel_write|vfs_write` 零命中；
  `/tmp` 使用率 1%，`/tmp/opencode` 空；写者全是用户态（shim trace 有 256MB 顶）。
- 机器外部重启（uptime 23min），无 `mt_*` 加载、`/dev/dri` 仅 `card0`，
  r125–r134 会话已失；当前无泄漏在发生，MemAvailable 12.8G/16G，dmesg 零 WARN。
- 卡死候选（按证据排）：①live 每周期 +26 probe 引用（r44/r127–r130，`retained` 从未置位属死旗）；
  ②r67 device-mutex owner-death；③Chrome 持 renderD128 钉 2M/fd。
  “写 /tmp 致卡死”已证伪。证据：`reports/r136-leak-tmp-audit.md`。
- 遗留：71+ 提交未 push；重建会话待批准；下次卡死先留现场（lsmod/D 态/dmesg/lsof）再重启。

---

## 本次会话进展（r134：DDK2 门控=DRM version_major==2，离线；零硬件触碰）

- 语料（SHA 已对）：UMD 自 calloc 特性块，`+0x54=(drm major==2)+1`；桥 `.major=0`→1→legacy。
  r131 开关写的是 UMD 不读的内核块，故 r132/r133 无差异（事实仍成立，作用点错）。
- 下一步：桥加 `drm_major` 参数（默认 0），重载 `=2` 跑 r133 同链（需批准）。
- 无需重启：模块干净卸载重载，引用 0/113，无 D 态。证据：`reports/r134-ddk-gate-is-drm-major.md`。
- 遗留：71 提交未 push；`ddk_feature_set` 去留待定。

---

## 本次会话进展（r133：非零 CCB create + ddk_feature_set=2，仍无差异；批准执行）

- 按 r76/r77 还原 r78 命令（pack 0x0733），仅 create+destroy 不 kick；
  `=2` 与默认各重载各跑，桥调用 91=91 逐项一致，无 SyncPrim/SubmissionBuf。
- 模块停在默认参数新桥，引用 0/113，无新 WARN。
- 下一步（离线）：语料核 `RGXCreateKickSyncContextCCB@0x52180` 门控读取点，别再盲重载。
- 证据：`mt-vgpu-guest/reports/r133-ddk2-ccb-create-live.md`。遗留：70 提交未 push。

---

## 本次会话进展（r138：重建会话 + Δ1 二分；批准执行 ①）

- 新会话活：Guest/FW 2/2 pinned（trial `20261004T065633Z-546d5bc5`），probe ref 86，
  桥在载（新鲜构建，ref 0），`card2`/`renderD129`；L3 全绿，L4 未跑。
- 二分：首周期+34（=25+9），空载+25，带帧+26（3/3 零 fault）；Δ1=提交路径残留。
  每次卸载固定 2 条 destroy WARN。证据：`reports/r138-rebuild-bisect.md`。
- Freeze 生效（② 验证除外）。遗留：73+ 提交未 push；Δ1 精确对象待 ② 后猎杀。

---

## 本次会话进展（r140：Δ1 猎杀终结；批准执行 ②-1）

- 根因：`lease_free` 漏 `drm_gem_object_release` + `drm_dev_put`
 （兄弟 `mt_live_drm.c`/`mt_gem.h` 齐全；resv 未 fini 致末 fence 滞留 = Δ1）。
- 修复 2 行，仅 live_3d_drm.ko 重编，会话保留；W=1 零警告 + check-offline 全绿。
- 活体验：单 fill、全 smoke 卸后 ref 均为 8（Δ0），零 WARNING。
  证据：`reports/r140-delta1-gem-release.md`。
- Freeze。遗留：75+ 提交未 push；/tmp 探针不入库。


## 本次会话进展（r141：drm_major=2 首触 DDK2；批准执行，主线）

- =2 同链 render→37（=0 对照全绿）：差值 3 新桥命令全 -ENOTTY
 （0x82:0x12/0x88:0x5 已按名归属为 DDK2 建销，0x2:0x2 待定）+ AlignmentCheck 消失；
  37 系 stub 失败常量；UMD 内 DDK2 桩 18 个。证据：`reports/r141-ddk2-first-contact.md`。
- 桥恢复默认 freeze（ref 8/0，零 WARNING）。下一轮：实现 0x82:0x12/0x13。
- 遗留：76+ 提交未 push；r135 jsonl 已定性为早期跑序，不引用，文件不动。

---

## 本次会话进展（r142：DDK2 render 建销落地；批准执行，主线）

- =2 同链 render 首返 0（0x82:0x12/0x13 + 0x2:0x2 落地）；续堵于 0x88:0x5，
  UMD 随即用户态段错误（内核零异常）。OUT 以活体 12 为准（订正 r141 的 4）。
  证据：`reports/r142-ddk2-render2-live.md`。
- 桥恢复默认 freeze（ref 8/0，L3 全绿）。下一轮：0x88:0x5 及其 destroy 对端。
- 遗留：77+ 提交未 push；r135 jsonl 未动。
---

## 本次会话进展（r143：DDK2 CCB 建销落地；批准执行，主线）

- 0x88:0x5/0x88:0x6 落地（建销，KIND_KICKSYNC 表）；=2 链进分配器后 UMD 空解引用
 （gdb 三帧定位 SubmissionBufAlloctorCreate，内核零异常）。证据：`reports/r143-ddk2-ccb2-live.md`。
- 桥恢复默认 freeze（ref 8/0，L3 全绿）。下一轮：分配器输入只读追踪。
- 遗留：78+ 提交未 push；r135 jsonl 未动。
---

## 本次会话进展（r144：分配器输入追踪；主线诊断）

- 崩溃系 harness 传参差一层间接（b5→b5* 后全绿）；堆表无辜（8 查询全中）。
  0x2:0x8 定名为 SyncFreeEvent（下一实现目标）。证据：`reports/r144-allocator-input-trace.md`。
- 机器重启：无模块在载，/tmp 已清空，会话需重建；易失证据以轮内记录为准。
- 遗留：79+ 提交未 push；r135 jsonl 未动。
---

## 本次会话进展（r145：0x2:0x8 落地，全链首绿；批准执行，主线）

- 0x2:0x8 进 stub-ok；新会话 b5* 全链六符号全 0（122 行零非零）。
  证据：`reports/r145-ddk2-fullchain-green.md` + `r145-major2-fullchain.jsonl`。
- 新会话 freeze（ref 1/0，L3 全绿，零 WARNING）。下一轮：DDK2 kick 语义。
- 遗留：80+ 提交未 push；r135 jsonl 未动。
---

## 本次会话进展（r146：DDK2 kick 语义；批准执行，主线）

- 零 count 同步 kick 仍走 0x88:0x4 原样受理（84B），全链全绿；
  TA/CDM 专属口属 S4 不碰。证据：`reports/r146-ddk2-kick0.md` + 同名 jsonl。
- 桥恢复默认 freeze（ref 1/0，L3 全绿，零 WARNING）。下一轮：Translator T3。
- 遗留：81+ 提交未 push；r135 jsonl 未动。

---

## 本次会话进展（r147：check-only 首帧打通；批准执行，主线）

- UMD check-kick 经真实 DM2 空 marker 回 0（×3，REF 平，零 WARNING）。
  修 5 处：循环丢增量（softlockup→重启）、ops 复本、自饿、RT+CSW、fence 漏 put。
  证据：`reports/r147-first-translated-kick.md`。
- 桥 translate_kick=Y 在载 freeze（ref 28/0，L3 全绿）。遗留：82+ 提交未 push。

---

## 本次会话进展（r148：translator 正常 teardown 活体验证；批准执行，主线）

- retained 会话重建后，check-only marker 成功（tag=1/fence=1）；桥卸载 `unloaded cleanly`，probe 引用 25→1，无新增 WARNING。证据：`reports/r148-translator-teardown.md`。
- 初次错误期望值 1 对着 PMR 实测 0，桥在自身 5 秒预算后 -ETIMEDOUT，未提交 marker；修正期望值后成功。

---

## 本次会话进展（r150：TDM PMR 生命周期与 multicore 回包；live 重验受状态阻断）

- TDM CLI/USC 两个 PMR 分离，PMR import/unref/file close 采用引用计数；`0x89:0x8/9` 只实现 per-file token。真实 trace 两路 release 及 context destroy 均成功，但 `0x89:0xa` 未到达。
- trace 证明 `0x1:0xc` 旧全零 stub 令 UMD 以 `cores=0` 请求零字节 TDM store。新增 DDK2 UMD wrapper 对应的 12/16B wire response，按既有 S3000 topology 实测返回 1 core。反向测试能抓回零值。离线门禁 260 Python（1 skip）+272 C、kernel `W=1` 全绿。
- 第一次重启后按已验证流程 cold-disconnect 成功、fresh trial 复原 retained 2/2。bridge 加载 8 秒后出现 DMA/CPU-only allocation 日志，ChatGPT PID 2704 SIGSEGV 与 kernel NULL dereference 相隔 16 ms；无 Oops 栈，不能定责，`musa_blit_test` 尚未启动。第二次重启后 Guest/FW=2/1、无驱动。证据见 `reports/r150-tdm-core-count.md`。
- 遗留：先查 bridge DRM open/release 路径与 kernel Oops，再恢复设备并验证 `0x1:0xc`/TDM store/`0x89:0xa`；暂停重复加载 bridge。
