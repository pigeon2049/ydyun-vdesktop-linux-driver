# AGENTS.md — 本仓库 agent 工作约束（2026-10-03）

> 本文件是给 agent 的指令，不是给人看的散文。规则按编号执行，
> MUST = 必须，NEVER = 禁止。违反红线（§5）的工作无论结果一律视为失败。

## 1. 开工顺序（按序读，读完再动手；总预算约 200 行）

1. `STATUS.md`——全文读。这是唯一入口和目标清单。
2. `PROGRESS-SNAPSHOT.md` §12（活页运行态）——确认模块/会话是不是 freeze 的。
3. 本轮任务对应的快照章节（如 §5 下一步、§10 设计）——只读相关节，不通读全文。
4. 要接上轮工作时再读 `MEMORY.md`（只留最新两节，短）。

NEVER 为“了解背景”打开以下文件（token 黑洞，内容已过期或被取代）：
`MEMORY-HISTORY-*.md`、`mt-vgpu-guest/HISTORY-2026-09.md`、
`docs/PROGRESS-HISTORY.md`、`docs/MTT-VGPU-2026-09-22.md`、
`mt-vgpu-guest/PROTOCOL-NOTES.md`、`FIRMWARE-NOTES.md`。
只有追溯某个具体历史结论的出处时，才按 `reports/README.md` 索引打开单篇报告。

## 2. 目标锁定（防偏离）

1. 每轮工作 MUST 是 `STATUS.md`“下一步”中的一项，或用户本轮明确下达的指令。
   两者都没有时，停下来问用户，NEVER 自选题目。
2. 动硬件（加载/卸载模块、绑/解绑 PCI、提交 GPU 工作、跑 L3/L4）MUST 先拿到
   用户明确批准。只读观察（sysfs、dmesg、`pvr_*_probe` 的只读路径、
   fabricated 重放、离线测试）不需要批准，但 MUST 在开工时声明“零硬件触碰”。
3. 一轮只做一件事。做完 → 写文档（§3）→ 跑门禁（§6）→ 提交推送，
   再谈下一件事。NEVER 把两轮工作塞进一次提交。

## 3. 写到哪里（落点约束）

| 内容 | 落点 | 格式 |
|---|---|---|
| 本轮做了什么、证据、教训 | `mt-vgpu-guest/reports/rNN-<topic>.md`，NN = 当前最大号 + 1（现为 r77 起） | 标题即结论；区分“实测”与“推断”；失败/证伪也如实记 |
| 报告索引 | `mt-vgpu-guest/reports/README.md` 主线表加一行 | 一行，不展开 |
| 最新过程记录 | `MEMORY.md` 顶部插入一节（最新在最上），并更新“最后更新”行 | 见 §4 清理规则 |
| 活页运行态变化 | `PROGRESS-SNAPSHOT.md` §12 随手更新（模块/引用数/会话/节点） | 只改 §12，不碰其余章节 |
| 快照其余章节过期 | 不要顺手改全文；记到 MEMORY 本节“遗留”里，攒到§7 的刷新 pass 一起做 | 防半截重写 |
| USB/画面线进展 | `docs/PROGRESS-HISTORY.md` 末尾追加 Step（保持 Step N 连续编号）+ 同步 `docs/PROGRESS.md` 现状短页 | 短页不超过 60 行 |
| 总入口/下一步变化 | `STATUS.md` 对应行 | 只有目标、红线、门禁变化时才动 |

NEVER 把过程记录写进 `STATUS.md`（它是入口，不是日志），
NEVER 改文件名（`reports/r*.md` 被正文和门禁引用文件名），
NEVER 回改归档文件（`*HISTORY*`、`*2026-09-22*` 只读）。

## 4. MEMORY.md 清理周期（定量规则，不凭感觉）

- 触发条件（任一即清理）：节数 > 2，或全文 > 150 行。
- 清理动作：只保留最新的 2 节，其余原样移入
  `MEMORY-HISTORY-<当天日期>.md`（已存在则追加到同一文件，不新建）；
  同步更新 MEMORY 头部的归档指向行与“最后更新”行。
- 快照刷新 pass（每 4–6 轮或 §12 与头冲突时）：把 MEMORY 沉淀的结论
  合入 `PROGRESS-SNAPSHOT.md` 对应章节，并把本轮提交哈希填进头部
  `对应提交`（先提交再 amend，参考 bA32 做法）。

## 5. 红线（硬件，违反即失败；全文见 STATUS.md）

1. 活会话 freeze：`mt_guest_probe`、`mt_pvr_bridge`、`mt_live_3d_drm`
   不 rmmod、不 unbind、不提交额外工作。
2. `make probe` / `make umd` 会先 rmmod——活会话上禁用，只在可重建会话上跑。
3. `timeout` NEVER 落在 bridge ioctl 临界区内；DMA 路径命令只用短超时做挂起
   探测，超时即停手、不堆任务（r67 device-mutex 泄漏只能重启恢复）。
4. 一次只跑一个 live 实验模块，做完即卸。
5. 大体积易失产物（bridge trace、CCB dump、GDB 工作区、blit 中间输出）
   MUST 写硬盘暂存区 `mt-vgpu-guest/build/traces/`（gitignore，硬盘；
   按轮建子目录，用完即清），NEVER 写 `/tmp`（tmpfs 仅 8G 易灌满，
   且重启丢失——r148/r149 trace 前车之鉴）。精选证据拷贝入库
   （`reports/*.jsonl` 0600、`*.bin`）后再清暂存。`UMD_TRACE` 等环境
   变量每轮显式指向暂存区，不依赖 shim 的 `/tmp` 默认。

## 6. 门禁与验证（提交前必须全绿）

1. 纯文档改动：核对所有引用路径存在（`for f in ...; do [ -e $f ]`），
   确认活跃短页无过期“当前/最新”表述。
2. 含代码改动：`make -C mt-vgpu-guest check-offline`（221 Python + 268 C）
   MUST 全绿；改内核再加 `make kernel`（`W=1` 零警告）。
   新增行为 MUST 配门禁测试并做**反向验证**（注入 bug 确认能抓到，再还原）。
3. 验证靠执行，不靠推断：二进制结论读重定位表/notes 段，不读注释；
   断点地址实测，不从反编译抄；“没匹配上”不等于阴性结论。
   详见快照 §8，四条教训仍然有效。

## 7. 提交推送规范

- 文档与代码分开提交；一轮一提交；信息格式：
  `docs: ...` / `feat(mt-vgpu-guest): ... (rNN)` / `fix/test/probe(...)`，
  正文写清证据与边界（参考既往提交）。
- 用户说过“提交并 push”后，后续文档整理类工作默认提交并推送，
  推送后贴 commit hash 与工作区状态。含硬件影响的工作除外，
  永远等明确指令。
- NEVER 提交 `build/`、`decompiled/`、`downloads/` 下的产物（gitignore），
  提交前 `git status --short` 确认无多余文件。

## 8. 防迷失检查单（每轮开工前逐项过一遍）

- [ ] 已读 `STATUS.md` + 快照 §12，知道会话是不是 freeze 的？
- [ ] 本轮目标是 STATUS 下一步之一或用户原话？（都不是 → 先问）
- [ ] 需要动硬件吗？（需要 → 有明确批准吗？没有 → 只做只读部分）
- [ ] 涉及驱动行为/结构映射吗？（先查 §9 语料 + Win 侧实现，对过 SHA 吗？）
- [ ] 知道写完后 rNN 编号、MEMORY 节标题、门禁命令分别是什么？
- [ ] 预计读的文件超过 300 行吗？（超过 → 改查索引/单节，不要通读）
- [ ] 动的是 USB/画面线吗？（Step 追加后同步了 `docs/PROGRESS.md` 短页吗？仍 ≤60 行吗？）

## 9. 参考实现优先序（RE/结构映射时；r76 教训沉淀）

1. 凡涉及 UMD/驱动行为、结构体布局、调用链，先查现成实现与语料，
   再动手反汇编/重放。顺序：
   - 首选 `mt-vgpu-guest/decompiled/<二进制>/` 反编译语料
     （`functions.jsonl` 按名取地址 + `decompiled.c` 按 `c_line_start`
     读伪 C；`calls.jsonl` 查调用；只按名查询，NEVER 通读）。
   - 次选 `/opt/MTT-driver-only/` 官方 Windows 驱动包
     （`mtkm64.sys` 等 Win 侧实现对照；只读，不执行）。
   - 可重开的 Ghidra 工程在 `mt-vgpu-guest/ghidra-projects/<原文件名>/`，
     但 NEVER 重跑全量分析（复用 `scripts/corpus.py` /
     `scripts/decompile-drivers.py` 的查询结果）。
2. 应用语料地址前 MUST 核对二进制 SHA-256（`sha256sum` 实测）与
   `DECOMPILATION.md` 或各脚本头部的期望值一致（在用 UMD：`b3058c02…`），
   不一致时停下来问，NEVER 套用错版地址。
3. 语料给出的是假设，不是证据：字段/调用结论仍按 §6.3 靠执行验证
   （fabricated 重放、trace、gdb），伪 C 不得直接当事实写进报告；
   伪 C 与实测冲突时以实测为准，并如实记录差异。
