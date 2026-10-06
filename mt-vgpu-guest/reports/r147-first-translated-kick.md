# r147：check-only 首帧翻译打通——UMD check-kick 经真实 DM2 空 marker 回 0（批准执行）

- **结论**：首个厂商 UMD check-kick（`0x88:0x4 check=1 update=0`）经 translator
  完整走通：UFO 跟随 → PMR 等待 → 空 DM2 marker 真实提交 → 真 fence fd →
  UMD 返回 0。3 次连通（tag 1/2/3，fence 9/10/11），引用数纹丝不动，
  dmesg 零 WARNING。这是翻译器从 0 到 1 的最小闭环（r113 设计执行）。
- 桥带 `translate_kick=1` 在载 freeze（默认关 = legacy 原样；零 count kick
  仍走 inspect，不受影响）。会话为重启后重建（trial `20261004T110947Z-7b81f015`）。

## 实测（执行过）

1. 重建（重启后）：cold 跳过（设备已 0/1，precheck 要 guest=2，见下）→
   fresh-trial rc=0（connected/published，ref=1）→ UMD 恢复（sha `b3058c02` ✓）。
2. fabricated 翻译 kick（r73 配方 + `*b10` 真句柄）：6 符号全 0，
   `0x88:0x4 ret=0`，`translated kick: check=1 tag=N fence=M` ×3，
   completions 递增，REF 28/28/28（修复后；修复前 +1/次，见下）。
   证据：`/tmp/opencode/umda/r147t.jsonl`（易失；116+ 调用）。
3. L3：node probe 0 failing + dma smoke PASS；ref 28/0；零 WARNING。

## 修的 4 个 bug（按发现序；全有活体证据）

1. **模板循环丢增量致 soft lockup**：`off += chunk` 在清理编辑中丢失 →
   同一 4KB 无限重写 → CPU#3 soft lockup（watchdog 报，任务 R 态免疫
   SIGKILL/gdb）。教训：循环改形后必须重读循环体；`make kernel` 不报错
   此类逻辑错。恢复靠重启（r67 结论复用）；修复版在盘，`make kernel` W=1
   零警告后重载验证。
2. **跨模块 ops 复本**：`mt_bo_vram_ops` 是 header 静态常量，probe 与桥各一份；
   桥内直调 `mt_bo_vram_write` 必 `-EINVAL`（dmesg 实测两地址）。
   修：桥内 `pvr_translator_bo_write`，照抄 live_3d_drm `transfer()` 模型
   （对 store 指针验 ops + cpu_begin/end 经 bo 自带指针分发到正确复本）。
3. **持 trial_lock 等 fence 自饿**：完成事件 drain 要 trial_lock（trylock），
   submit 内等待必 5s 超时。修：锁内提交、锁外等（与 live submit_3d 同纪律）。
4. **裸模板无有效 RT/CSW**：RT 全零 + CSW 野指针 → FW 不回完成。
   修：prepare 绑 64KB scratch RT（0x48100000，沿用 live 几何/stride/extent）
   + 复刻 live 的 11 context BO + `mt_gfx_context_build_csw` + 打 6 处补丁
   （csw_va/csw/4×RT）。
5. **fence caller 引用漏 put**：成功路径未放 submit 引用 → +1 probe ref/kick
   （默认桥对照完全平衡，排除法定位）。修：sync_file 接管后 put（live
   put_sync 同纪律）；验证 28/28/28。

## 门禁（执行过）

- `make kernel` W=1 零警告；`make check-offline` 全绿（含 translator_packet_test，
  已扩展 RT 补丁断言：4 槽值正确 + 槽外模板一致）；
  `test_pvr_translator.py` OK（含 SYNC-link 门禁；反向单行掐断即红，已还原核对）。
- `test_pvr_pmr_lifetime.py` 的 `ops->` 全文件禁令已收窄为 handoff 函数域
  （translator/live 的 drvdata 间接 ops 调用本就无需符号解析；反向注入即红）。

## 遗留

- 82+ 提交未 push；`r135` jsonl 未动。
- translator 卸载路径（`pvr_translator_exit`）尚未活体走过——下次自然重载时验证。
- bridge 以 `translate_kick=Y` 在载：零 count 路径不受影响（仍 inspect）；
  非零 check 在此桥上会被真实翻译（符合开关语义）。
