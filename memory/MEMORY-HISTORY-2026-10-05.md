# MEMORY HISTORY — 2026-10-05

## 本次会话进展（r155：fabricated PMR shared backing）

- 零硬件触碰。shim 新增 opt-in `UMD_SHARED_BACKING=1`：每个 Services mmap handle 对应独立 memfd，同 handle 映射共享、不同 PMR 隔离；LocalImportPMR 分配递增 handle，只有 AcquireInfoPage 对应映射写合成 info-page 头。新增真实 syscall/ioctl 离线测试；反向注入 `MAP_PRIVATE` 时测试以 13 失败。
- SHA 核验 5.2 UMD 的离屏 blit 到达 `0x89:0x4`（108B/8B），trace 594 行、169 条 bridge ioctl、16 条 PMR snapshot；没有 passthrough。两个 TDM PMR 与三块 TQCB mapping 在 submit 前全零；其他 PMR 有非零数据但 CCB 归属未定。报告与 trace 在 `mt-vgpu-guest/reports/r155-shared-fabricated-pmr.{md,jsonl}`。
- `make -C mt-vgpu-guest check-offline` 全绿：265 Python（1 skip）+272 C；probe shim `-Werror` 编译成功。快照 §6 的 Python 计数仍旧，留待刷新 pass 同步。硬件 bridge 仍不重载。
- 下一步：将 TQCB/池 backing 与 `SubmissionCmdGenerate()`、SubmitTransfer2 嵌套指针逐项对照；保留 fake 执行边界，不推断为真实 CCB。

---

## 本次会话进展（r154：离屏 blit 到达 SubmitTransfer2）

- 零硬件触碰。SHA 核验的 5.2 UMD 在 fabricated shim 下运行 `musa_blit_test -device 0 -f -o`，打印 Submit/Wait OK，最后像素比对失败；578 行 trace、169 次 bridge ioctl 到达 legacy `0x89:0x4`（108B/8B），未见 `0x89:0xa`。报告与完整 trace：`mt-vgpu-guest/reports/r154-musa-blit-offscreen.{md,jsonl}`。
- GDB 核对 SubmitTransfer2 嵌套参数块未发现可识别 CCB；映射日志与 shim 实现表明 UMD fd 的每次 mmap 均为独立匿名假 backing，开头还被写入 info-page 合成字段。此 harness 无法用像素失败或抽样内存证明 UMD 是否生成真实 CCB。
- 下一步：离线改进 shim，使同一 PMR handle/offset 的映射共享 backing，再对比提交前后写入区；在 CCB 格式被执行证实前不接入 Translator。

---

## 本次会话进展（r153：Rogue2D fabricated Context replay）

- 零硬件触碰。复用 harness 调 `R2DCreateContext(out)` 返回 0；95 条 bridge ioctl 中两次 multicore 回包为 1，随后 fabricated TDM shared-memory / TransferContext create 回包均被 UMD 接受。原始 trace 已归档到 `mt-vgpu-guest/reports/r153-rogue2d-context.jsonl`。
- 追加 `R2DCreateSurface` 和直接 Layout 调用均返回 3，bridge 调用数未增加；调用参数 ABI 未确证，不能归因到具体内部条件。未到 SubmitTransfer3，未生成绘制 CCB。
- 遗留：快照 §6 的门禁计数仍是 263 Python；STATUS 已按 r152 门禁结果更新为 264，留到后续快照刷新 pass 同步 §6。
- 更新日期 2026-10-05。Oops 栈仍缺，hardware bridge 不重载。

---

## 本次会话进展（r152：fabricated 单核回包适配）

- 零硬件触碰。shim 对 `0x1:0xc` 回显 caps 并返回单核，补齐与 r150 bridge handler 一致的离线输出；定向测试 3 项通过。
- 反向验证注入 `num_cores=0` 被新测试抓到；完整 `check-offline` 通过（264 Python、1 skip；272 C），shim `-Werror` 构建成功。
- 完整 Rogue2D replay 尚未运行；SubmitTransfer3/真实绘制 CCB 未验证。hardware bridge 因 Oops 根因未定仍不重载。报告：`mt-vgpu-guest/reports/r152-fabricated-multicore.md`。

---

## 本次会话进展（r151：bridge 空指针/泄漏审计与 SubmitTransfer3 契约入口）

- 零硬件触碰。`pvr_mmap()` 改为逐页 vmalloc 转换并判空；`pvr_gpu_vm_ensure()` 复用 DMA
  注册已填的 `arena_gpu_pages`；file close 回收 lazy VM 未就绪时的该表。
- 发现 `pvr_file_release()` 在 VM unbind/fini 错误时保守提前返回，会留住整份 file/PMR/VM
  backing；不改 fail-safe 行为，因释放仍被引用的 backing 更危险。此分支是否关联 Oops 未知。
- 离线门禁 263 Python（1 skip）+272 C 全绿，kernel `W=1` 全模块成功无警告；新增三项
  静态门禁的反向注入均抓到回归。
- UMD SHA 与 5.2 语料一致。增加 `0x89:0xa` 108B/4B packed wire 描述和关键 offsets；check/update
  sync 各最多 32 项、PMR sync 最多 17 项，`SubmissionCmdGenerate()` 的 CCB 地址/长度落在
  `0x58`/`0x68`，`0x60` 槽位用途未定。偏移断言反向注入能失败。handler 仍未接 dispatch。
- 遗留：核实 `0x60`、嵌套指针拷贝/PMR 引用和 CCB 完成语义；真实验证待 Oops 归因并获得
  可安全重建的会话。

---

## 本次会话进展（r156：DDK2 与 legacy producer 路径辨析）

- 零硬件触碰。SHA 匹配的 5.2 UMD 语料把链路分开：`TQJobSubmit`/`TQJobMultiSubmit` → `SubmissionCmdGenerate` → `FUN_0015f890` → `0x89:0xa`；离屏 `musa_blit_test` → `RGXTDMSubmit` → `0x89:0x4`。不能把 legacy SubmitTransfer2 参数当 DDK2 CCB。
- GDB 抽查 `0x89:0x4` ioctl 的 108B 输入及其栈指针块，没有识别出 CCB 字节流；这是有限观察，不能排除其他 PMR 中有 command data。r155 shared-backing snapshot 中 TDM/TQCB 特定区为零，其他非零区域用途仍未证实。
- 更新 r156 报告、索引、STATUS 与快照 §12。下一步先离线寻找可复现 DDK2 producer 输入，再进入 SubmitTransfer3 的 CCB 验证。快照 §5 的旧步骤留待刷新 pass 同步。无代码改动；沿用 r155 的离线门禁结果。

---

---

## 本次会话进展（r157：fabricated DDK2 SubmitTransfer3 producer）

- 零硬件触碰。给 shim 增加 opt-in `UMD_DRM_MAJOR=2`，默认 major 1；`musa_blit_test -device 0 -f -o` 在 shared-backing 模式到达 `0x89:0xa`（108B/4B），trace 527 行、105 bridge ioctl、15 个 submit 前 PMR snapshots、无 passthrough。按 r151 ABI 解码出 CCB GPU VA `0x8000f44000`、长度 `0x1200`，fake ioctl 后终止离线进程，未将等待/完成伪装成执行成功。
- Snapshot 显示 `0x1003000/0x1004000` 与 TQCB `0x5006000`–`0x5008000` 为零；`0x500d000` 有 2,621,440 非零字节，但没有 GPU VA → PMR 证据。修复 snapshot registry 在 munmap 后遗留过期视图并与读取互斥；反向注入时测试进程 SIGSEGV，恢复后通过。`musa_tq_performance_test -n 1` 未到 Submit3，先 SIGABRT。
- 门禁全绿：268 Python（1 skip）+272 C，shim `-Werror` 编译通过；major override 的默认/opt-in/非法值测试通过，反向固定 major=1 可抓回归。下一步映射 CCB GPU VA 到 PMR backing，再验证对应 `0x1200` 字节。快照 §6 计数仍旧，留待刷新 pass；未动硬件。

## 本次会话进展（r158：SubmitTransfer3 CCB VA→PMR 关联与转储）

- 零硬件触碰。shim 新增 VA 台账（`0x6:0x15` reservation 范围 + `0x6:0x13` pmr↔reservation），`0x89:0xa` 提交前按 r151 ABI（ccb@88/bytes@104）解出 VA 并定位 backing，记 `ccb_resolve`（窗内非零数/首非零/FNV-1a/32B 采样；不可达记 `resolved:0`）。
- 重放 `musa_blit_test -device 0 -f -o`（major 2 + shared backing，`timeout -s KILL 15` 终止，UMD 不退出与 r157 相同）：Submit3 解码 `check=0/update=2/pmr_sync=0/ccb=0x8000f44000/0x1200`；`ccb_resolve` 给出 reservation `0x900d`（`0x8000f430fe`+`0xa00fff`）→ PMR `0x500e` → backing `0x500e000`+`0xf02`，窗内 39 非零、首非零 `+0x10`、FNV `0xb9e0f1a18201bf0f`；整块 10MB backing 非零同样 39、首非零 `0xf12`（=`0xf02+0x10`）、采样相同，归属成立。shim 回包仍 fabricated，不证明执行。
- 门禁全绿：269 Python（1 skip，含新增 `test_pvr_shim_ccb_resolve`：合成 reserve→map→mmap→写 pattern→Submit 断言 `resolved/backing_offset/nonzero` 与越界 `resolved:0`，反向关 shared backing 无记录）+272 C，shim `-Werror` 通过。`lsmod` 无 `mt_*`。下一步对照 `SubmissionCmdGenerate` 语料解读 39B 稀疏窗口。

---

## 本次会话进展（r159：update 语义 UMD 侧离线确定）

- 零硬件触碰。`SyncUtilGenerateUpdateData` 确定三要素：布局（IN 36/44/52/60，与头一致）、可见性（`flag&2` 条目经 sync block 句柄+相对偏移，与桥 PMR/SYNC 跟随模型一致）、完成条件（poll 等 fd，先写回后交 fd）。`flag&2` 来源待活体（红线禁重载）。
- 顺手清 r148 调试残留（桥 5 处 DBG）；`make kernel` W=1 零警告，`make check-offline` 全绿。证据：`reports/r159-update-semantics-offline.md`。
- 遗留：工作区有 59 个未提交改动/未入库文件（含本轮，另有历史包袱）；`r135` jsonl 未动。

---

## 本次会话进展（r160：SubmitTransfer3 CCB 窗口字段对照）

- 零硬件触碰。shim `ccb_resolve` 加 `runs`（非零 runs 上限 32，合成门禁断言首 run）；单轮 blit 重放（major 2 + shared backing）27 runs 恰好覆盖窗内 39B。`+0x10`=CCB+`0x58`、`+0x28`=`0x1078` 与语料 `SubmissionCmdGenerate`（SHA 已核）的 `0x58`/`0x1020` 定长拷贝形状吻合；`+0x40` 的 2B 三轮各异（余 37B 一致），来源未定，候选 ASLR/未初始化。
- `FUN_0015f890`（`TQSubmissionSubmit`）确认提交链：check（`flag&1`）/update（`flag&2`，与 r159 一致）编组后进 `BridgeRGXTDMSubmitTransferDDK2`；本轮 `update_count=2` 与之相符，update 数组走 IN 指针、不在 CCB 窗口内。
- 门禁全绿：270 Python（1 skip）+272 C，shim `-Werror`，`lsmod` 无 `mt_*`。证据：`reports/r160-ccb-window-fields.md` + trace。下一步离线 GDB 对 `+0x40` 下写观察点；换 producer 闭合扩展区条目算术。

---

## 本次会话进展（r162：换 producer 探针）

- 零硬件触碰，无代码改动。`-n 2` fill 与基线机器比对：VA/长度/38B 全等，仅 `+0x40` 计数器不同——fill CCB 与源面数无关。copy（去 `-f`）在 major 1/2 下同点 SIGABRT（`0x500d000` 映射后，`TQJobSubmit` 内 copy-setup 分发深处，无信息；rsp 对齐序断帧链，深挖止损），该 producer 当前不可达。
- 证据：`reports/r162-producer-sweep.md` + `r162-n2-fill.jsonl` + `r162-copy-abort.jsonl`。门禁复核全绿（269+272）。结论：扩展区算术缺可复现的新 producer；CCB 侧收敛，候选转向步骤 2（待可重建会话）。

---

## 本次会话进展（r161：CCB +0x40 轮变 2B 写入者落定）

- 零硬件触碰，无代码改动。离线 GDB 对 PMR `0x500e` backing 窗口 `+0x40` 下硬件写观察点：唯一命中为 `SubmissionCmdGenerate` 的 `0x58B` 头拷贝（libc AVX `vmovdqu64`），调用栈 `TQJobSubmit → SubmissionCmdGenerate → PVRSRVMemCopy`（动态符号，非地址推算），活体确认 r156 路径。
- job（`$rsi`，堆地址跨轮稳定）`+0x40` 四轮 `0x288e→0x28ae→0x28bd→0x28d7` 单调递增、步长不等：计数器形态，排除 ASLR 指针；确切命名未定。证据：`reports/r161-plus40-writer.md` + `r161-plus40-watch.txt`。门禁复核全绿（269+272）。下一步换 producer 闭合扩展区条目算术。

---

## 本次会话进展（r163：tq-perf 同倒于同一 abort 点）

- 零硬件触碰，无代码改动。`musa_tq_performance_test -n 1`（64×64，major 2 + shared backing）510 行后 SIGABRT，无 Submit3；GDB 栈与 r162 copy-blit 三重一致（aborter PC、`TQJobSubmit+738` 返回地址、trace 位置）——同一阻塞点，非新 producer。按名断点因符号不可见 pending，止损。
- 证据：`reports/r163-tq-same-abort.md` + `r163-tq-abort.jsonl`。门禁复核全绿（269+272）。producer 线暂止；候选步骤 2（待可重建会话）或 copy-setup 缺口单独立项。

---

## 本次会话进展（r164：快照刷新 pass）

- 纯文档，零硬件触碰。快照 §5→r157–r163 状态、§6 计数→269 Python（1 skip）+272 C 并补 11 个门禁文件行、STATUS L1→269、快照时间→2026-10-05；`对应提交` 先提交后 amend（bA32 做法）。§7 长期项与历史章节未动。
- 门禁实测复核全绿；translator 方法数订正为 6（r159 文“4 项”为口径差）。证据：`reports/r164-snapshot-refresh.md`。

---

## 本次会话进展（r165：复核）

- 零硬件触碰，无代码改动。门禁重跑 269+272 全绿；blit 重放复现 r158/r160（同 VA/PMR，39B/27 runs）；SHA、无模块、r157–r164 文件逐项存在。订正 STATUS 现状两处过期；指针 trailing 属 bA32 惯例不动。
- 证据：`reports/r165-verification.md`。遗留：57 项历史包袱未动。

---

## 本次会话进展（r166：活体会話重建，批准执行）

- 真机调试已批准。`cold_disconnect` 0/1（idle/`guest=0 firmware=1`，双 clean rmmod）→ `fresh-trial --run --runtime-context` rc=0（trial `20261005T161706Z-cf0d876e`，fw sha `35d40f75…`，`guest=2 firmware=2` pinned ref=1）→ 桥默认加载（`card1`/`renderD128`）→ L3 全绿（node 0 failing/0 mismatch；dma smoke PASS，refs 平衡）。dmesg 无新增 WARN/BUG/Oops，r150 Oops 未复现但根因未命名。
- **Freeze**：probe ref 1、bridge ref 0，不 rmmod、不 unbind、不提交额外工作；`make probe`/`make umd` 继续禁用。证据：`reports/r166-session-rebuild.md`。候选下一步：L4 阶梯或 update 活体验证（需批准）。

---

## 本次会话进展（r167：L4 部分通过，rung5 受阻）

- 真机调试已批准（手跑阶梯，桥零重载）。rung1–3 全绿（connect/device/devmemctx）；rung4 先 2 崩后 4 过；rung5 standalone 11/11 SIGSEGV、GDB 2/2 过。core 验尸：`RGXCreateRenderContextCCB+1525` 取 `r12+8==NULL`；148 条 trace 全 ret=0、内核零错误、probe ref 稳 1——桥无罪，UMD 侧时序敏感空指针，发布者未命名。
- 会话健康，freeze 继续；rung6–8 被阻；`translate_kick` 仍 off。证据：`reports/r167-l4-partial-segv.md` + trace + 验尸笔录。候选下一步：trace 加 tid 定位 NULL 发布者。

---

## 本次会话进展（r168：trace 加 tid 猎杀 NULL 发布者）

- 真机已批准（手跑，桥零重载）。shim 25 处记录加 `tid`（门禁断言+反向注零验证；diff 归一化纯净）。活体对比：失败/通过轮均为单 tid——交错假设证伪；桥前缀 65/65 一致全 ret=0。ASLR 关/单 CPU/perturb 0/165 对照仍全崩；GDB 4/4 过。发布者未命名，会话健康 freeze 继续。
- 证据：`reports/r168-tid-hunt.md`。候选下一步：GDB 条件断点比对句柄值。

---

## 本次会话进展（r169：OUT 字节级对比）

- 真机已批准（手跑，桥零重载）。10 组桥命令 OUT 全量对比：18 处差异全为调用方指针回显，其余逐字节一致——桥彻底无罪。另否 argv[0] 与重试（rung5 standalone 累计 0/20，GDB 5/5）。遮罩机制未解释，会话健康 freeze 继续。
- 证据：`reports/r169-byte-exoneration.md` + 失败 trace。候选下一步：GDB 监督下跑梯（非常规但诚实）解 rung6–8。

---

## 本次会话进展（r170：GDB 监督下 L4 八级全绿）

- 真机已批准（手跑，桥零重载；监督非常规，如实记录）。rung5 syncprim、rung6 kicksync 建销、rung7 compute 建销、rung8 `RGXKickSync→0`（inspect，无 GPU 工作）逐级全绿；四轮 trace 全 ret=0；probe ref 稳 1；dmesg 干净。rung5 standalone 崩溃仍在。
- 会话健康 freeze 继续；`translate_kick` 仍 off。证据：`reports/r170-supervised-ladder.md` + rung8 trace。候选下一步：同监督跑 DDK2 全链复验。

---

## 本次会话进展（r171：update 活体注入证伪）

- 真机已批准（手跑+GDB 监督，桥零重载）。rung9（wire IN 摆 update_count=1）`RGXKickSync→1` 且无桥调用；fence 名/PMR 柄变体同败。语料：b26 是 UMD CMD 对象（count 在 +0xD8），84B 错位；该函数无 update 组装（r74 独立 corroborate）。活体验证需 DDK2 重载，被 freeze 挡，单独立项待批。
- 会话健康 freeze 继续；`translate_kick` 仍 off。证据：`reports/r171-update-inject-refuted.md` + trace。

---

## 本次会话进展（r172：DDK2 重载与首个真实 Submit3）

- 真机已批准（两次桥重载均 ref0，probe 零触碰）。`=2` 下 DDK2 全链 123 调用零非零；真实 blit 发出首个真实 `0x89:0xa`（`0/2/0`、`0x8000f44000`/`0x1200`，桥回 `-25` 无 GPU 工作；VA/尺寸与 fabricated 一致）。真实 CCB 字节未捕获（Rss=0 之谜，下一轮）。桥已恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r172-ddk2-reload-real-submit.md` + 双 trace。候选下一步：真实字节捕获或 accept-and-log handler。

---

## 本次会话进展（r173：DDK 门控活体兑现）

- 真机已批准（本轮零重载、只读复盘 + 时间线对齐）。所谓 submit/unwind 双峰 = 桥参数窗口：`=2`→`0x89:0x8`→`0x89:0xa`（`-25`）；默认→legacy `0x89:0x0`（`-25`）→有序自拆。桥 0x89 组仅 `{0x5,0x6,0x8,0x9}`，`0x0/0x4/0xa` 皆 `-ENOTTY`——两路按设计拒收，无 UMD 随机选路。
- 会话健康 freeze 继续。证据：`reports/r173-ddk-gate-live.md` + unwind 全 trace。候选下一步：重开 `=2` 窗口捕获真实字节。

---

## 本次会话进展（r174：SubmitTransfer3 accept-and-log 与真实 CCB）

- 真机已批准（含内核改动+两次 `=2` 重载，均 ref0，probe 零触碰）。桥新增 observe handler（定界/鉴权/零嵌套读/零执行，5 项门禁+反向验证；r150 旧断言改判）；`make kernel` W=1 零警告，274+272 全绿。真实 blit 两轮：39/39 非零字节与 fabricated 逐字节一致（仅 `+0x40` 不同）；`+0x40` 非单调，计数器命名收回。桥恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r174-real-ccb-captured.md` + trace。候选下一步：T3 translator 输入规约重启。

---

## 本次会话进展（r175：扩展区算术闭合）

- 零硬件触碰，无代码改动。离线 GDB 读生成器 job：`c1064=0/c106c=0/c2c=1`，单 type=3 条目；`0x1078+0x18+32+224+40=0x11B8` 内容终点（`0x1200` 系分配器量子）；条目 `+8` = `uVar16` 写回（r160 之谜亦解）；三段源字节与 39B runs 逐项对齐。载荷 B 为传输描述符形态（含 `0xa3xxxx` 小 VA）。
- 证据：`reports/r175-extension-arithmetic.md`。候选下一步：写 T3 translator 输入规约。

---

## 本次会话进展（r176：T3 translator 输入规约 v1）

- 纯文档，零硬件触碰。冻结 envelope、header 字段表、body 拷贝语义、扩展区状态机、fill 实例全账、未知项清单、translator 消费契约；DM 队列格式与完成语义明确在外。门禁复核 274+272 全绿。
- 证据：`reports/r176-t3-input-spec.md`。候选下一步：DM 格式反推（输出侧）。

---

## 本次会话进展（r177：T3 输出侧盘点）

- 纯只读盘点，零硬件触碰，无代码改动，会话未碰（probe 1/bridge 0）。输出侧：DM2 空 marker、TQX fill 矩形、TQX copy 计划均有发射能力；Transfer 归 TQX（DM 只欠 TA/3D）。CCB 几乎全指针/标志，颜色几何不在其中——首要缺口；payload B 的 `0xa3xxxx` 归属未定。
- 证据：`reports/r177-output-inventory.md`。门禁复核 274+272 全绿。候选下一步：追踪 transfer surface 对象。

---

## 本次会话进展（r178：几何/颜色通道落定）

- 零硬件触碰，无代码改动（离线 fabricated + GDB 转储）。排除 CCB/context/注解；5MB 池实转储：3841 零头 + `ff0000ff`×1310720（=1280×1024，行连续）+ 254 零尾；64×64 复核池代数精确成立。颜色即像素字；源池空与 fill 一致。
- 证据：`reports/r178-geometry-channel.md`（二进制未入库，数字即证据）。门禁复核 274+272。候选下一步：T3-transfer 原型（dst VA + 全表面 + 像素字 → TQX fill）。

---

## 本次会话进展（r179：T3-transfer fill-input 构造器）

- 零硬件触碰（离线实现+门禁，会话未碰）。新增 `mt_transfer_fill.h`：池解析（空/短/错位全拒）+ 矩形构造（`w*h` 错配大声拒绝）；C 门禁 16 项（274+288 全绿），反向掐校验可抓。颜色原搬不解释；W/H 由调用方给。未接桥无发射。
- 证据：`reports/r179-fill-builder.md`。候选下一步：活体接线（`=2` 窗口 + 像素回读）。

---

## 本次会话进展（r180：T3-transfer 活体接线规约）

- 只读 recon，零硬件触碰，无代码改动，会话未碰（probe 1）。决定性依据：外页 BO 绑不进会话空间（store/ops 一致性）；scratch 中转只用已验证原语（分配/绑定/fill 构造/submit+fence/CPU 落位）。VA 建议 `0x49000000`；copy 双面；门禁计划已列。
- 证据：`reports/r180-transfer-wiring.md`。候选下一步：实现轮（`=2` 窗口 + 像素回读）。

---
