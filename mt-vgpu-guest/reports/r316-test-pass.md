# r316：真实绘制像素闭环 `Test PASS`（批准执行）——STATUS #1 落定

- **结论**：五开 + `+0` 落池构建窗口：`fired=1 chunks=21 verified=1 todst=1` → bump → UMD `Submit transfer command OK → Wait OK` → **`*** Output matches source ***` / `*** Test PASS ***`（exit=0）**。真实 UMD 绘制（建链→提交→同步→执行→回读→比对）全链条首次打通：GPU 执行 fill 程序（21 fence 全 signal），像素落 UMD 目的池正确偏移，同步满足，UMD 像素比对通过。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**

## 实测（执行过）

1. 批准：standing 授权。停桌面（本次用户侧似已提前关闭，`stop` 报 mangle 警告但 ref 已 0——窗口照常）→ ref 0 → 五开（r315 构建，vermagic 对版）→ 真实 blit（exit=0）→ 读 fired/bump/判决 → 拆桥 → 默认 → L3 双绿 → 拉桌面。
2. dmesg + stdout 双收：`scheduled todst=1` → `fired=1 ... todst=1` → bump → `Output matches source`。trace 8290 行/178 调用，全序拆除至 `0x1:0x1`。
3. 证据：`r316-pass.jsonl`（0600）+ `r316-blit-stdout.txt`（0600，判决原文）；暂存区已清空。门禁沿用 r315（386+299）。

## 边界与下一步

- 本轮是 transfer-fill 单路径；TA/3D（`0x82:0xC`/`0x81:0x5`）、copy 路径、更大几何仍未覆盖——但“真实绘制”从 0 到 1 已经打通，余量是扩展不是证伪。
- 建议：push 积压提交（含 10+ 本地提交）；快照刷新 pass（§12 与头、`对应提交`指针已落后多轮，按 bA32 做法）。
