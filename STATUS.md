# STATUS — 仓库当前状态总入口（2026-10-07）

> 新 agent 先读完本文件再动手。历史文件都有归档头；状态冲突时裁决顺序为
> `STATUS.md`（本文件）→ `PROGRESS-SNAPSHOT.md`（细节）→ `MEMORY.md`（过程）。
> 不要为“了解背景”通读归档文件——那是 token 黑洞。
> 机器可执行的完整约束见 [`AGENTS.md`](AGENTS.md)（落点、清理周期、红线、门禁）。

## 这是什么仓库（两条互不依赖的线）

| 线 | 目录 | 做什么 | 当前结论 |
|---|---|---|---|
| 云电脑 USB/画面 | `linux/`、`wayland/`、`docs/` | Debian 云电脑 USB 转发 + KDE Wayland 分辨率 | v0.2.52 可安装（见根 `README.md` 教程）；云端联调待真实会话 |
| S3000 vGPU Guest | `mt-vgpu-guest/` | 自研内核栈点亮 Moore Threads S3000 vGPU | **机器已重启（r263 死锁恢复）：无模块在载，`00:0e.0` 未绑定，会话待重建**；r263 代码（含死锁）仍在树内，未加载前无影响 |

## vGPU 一句话现状

`mt_guest_probe` 已绑定 `00:0e.0`（trial `20261007T040408Z-f3fb55af`，Guest/FW `2/2` pinned，ref 1），`mt_pvr_bridge` 默认参数在载（`card1`/`renderD128`，ref 0），L3 全绿（node 0 failing/0 mismatch，dma smoke PASS），dmesg 无新增 WARN/BUG/Oops。**会话 freeze 中：不 rmmod、不 unbind、不提交额外工作。**r151 静态修正（`pvr_mmap`、arena 页表、close 泄漏）与 SubmitTransfer3 ABI 描述已随本次构建上机但 handler 仍未接入。r157–r160 的 fabricated CCB 结论（VA `0x8000f44000`/`0x1200`→PMR `0x500e`，39B 全定位）不受影响。r150 Oops 未复现，但根因仍未命名——后续 live 模块一次一个、做完即卸。UMD check-only kick 曾在旧会话经真实 DM2 空 marker 完成（r148–r149）；legacy（r212，tag=1 fence=1）与 DDK2（r213，tag=1 fence=2）均已在新会话复验通过；update 数组语义已离线确定（r159，活体待定），真实绘制第二样本已在新会话落定（r214：39B 中 37 与 r174 跨会话一致，仅 `+0x40` 轮变），TA/CDM 专属提交仍未验证。RGX 像素读回与 20 帧批量是已完成结果，不代表 `mt_live_3d_drm` 当前加载。
细节见 `PROGRESS-SNAPSHOT.md`，逐轮记录见 `MEMORY.md`（只留最新两节），
证据在 `mt-vgpu-guest/reports/r*.md`（索引见该目录 `reports/README.md`）。

## 活会话红线（先读这段；r211 起新会话 freeze 中）

- 当前 `mt_guest_probe` 绑定 `00:0e.0`（Guest/FW `2/2` pinned，ref 1），
  `mt_pvr_bridge` 默认参数在载（ref 0）：**不 rmmod、不 unbind、不提交额外工作**。
  r150 Oops 未复现但根因未命名，后续 live 模块一次一个、做完即卸。
- 顶层 `make probe` / `make umd` 会先 rmmod 再 insmod，
  **在活会话上禁止直接使用**；只在可重建会话上跑。
- `timeout` 不得落在 bridge ioctl 临界区内（r67：device-mutex
  owner-death 泄漏只能靠重启恢复）。DMA 路径命令超时只做挂起探测，
  超时即停手、不堆任务。
- 一次只跑一个 live 实验模块，做完即卸（占着 trial_lock 会卡死下一个）。

## 门禁（`mt-vgpu-guest/` 下执行）

| 命令 | 含义 | 动硬件 |
|---|---|---|
| `make check-offline` | L1：269 Python（1 skip）+ 272 C RAM checks | 否 |
| `make check` | L1+L2：+ 内核 `W=1` 构建 + ABI 门禁 | 否（但依赖 gitignore 的 `build/` 产物，新 clone 会失败，见快照 §7） |
| `make kernel` | 全模块 `W=1` 构建 | 否 |
| `make probe` / `make umd` | L3/L4：加载模块跑探针 / 真实 UMD 八级阶梯 | **是**，且会重载模块——活会话上禁用 |

## 文档地图（只列活页；归档不在此列）

| 文件 | 性质 | 何时读 |
|---|---|---|
| `STATUS.md`（本文件） | 总入口 | 每次开工先读 |
| `PROGRESS-SNAPSHOT.md` | vGPU 权威快照（缺陷表、门禁、待办、教训） | 要动手前读对应章节 |
| `MEMORY.md` | 最新两节过程记录 | 要接上轮工作时读 |
| `mt-vgpu-guest/README.md` | 该目录入口：红线、目录、门禁命令 | 进该目录前读 |
| `mt-vgpu-guest/reports/README.md` | r 系列证据索引 | 找某轮证据时先查索引 |
| `docs/PROGRESS.md` | USB 线现状（短） | 动 USB/画面时读 |
| `docs/README.md` | 文档地图（含时效列） | 找文档时读 |
| 根 `README.md` | v0.2.52 安装教程 | 装机时读 |

归档（按需才读）：`MEMORY-HISTORY-2026-10-01.md`、
`mt-vgpu-guest/HISTORY-2026-09.md`、`docs/PROGRESS-HISTORY.md`、
`docs/MTT-VGPU-2026-09-22.md`，以及 `PROTOCOL/FIRMWARE-NOTES.md`
（顶层已有归档头）。

## 下一步（vGPU，按序）

1. **真实绘制 CCB**：check-only kick 曾在旧会话经真实 DM2 空 marker 完成（r148–r149）；离屏 fill 的非零 CCB 窗口已在 fabricated major 2 blit 中取得并归属（r157–r161）；r172–r182 的 DDK2 dry-run/TQX bring-up 与 r191 kill-while-busy ref 关账均已完成，但真实绘制 CCB 仍未执行。r201/r203 fabricated 重放动态闭合 GFX allocator 链、生成 flag=2 update 并发出 `0x82:0x14`；r203 修正两个 harness 缓冲尺寸后 `RGXKickGfx` 返回 0，watchpoint 证明此前 abort 是 0x408 字节输出复制越界破坏相邻 chunk。r204–r207 已用 5.2 Host `MUSAKICKGFX5` schema/API 对齐 108B 请求并把 wire struct 与偏移钉入门禁。r207 确认当前 context 只是 token、已有 kick translator 只发固定 marker、TDM 路径只观察 CCB。`0x82:0x14` observer 已落桥上机，全路径 envelope（r218）、手塑非零窗口（r224）与 UMD 生成字节重放（r225：nonzero=107/FNV 全命中）均已活体验证；TQX bring-up（r219）与 transfer dry-run（r226：digest 与预言逐位一致）均已在新会话复验。下一步补齐真实 DDK2 render backend 和 TA/3D CCB 执行链；不得用 accept-and-log 代替执行。活会话继续 freeze。
2. **同步 update 语义**：离线已确定（r159）；legacy 活体注入已证伪（r171）；非零 update 路径已定位到 `0x82:0x14`（r190）；r196/r201/r203 fabricated RGXKickGfx trace 发出 `0x82:0x14`，r203 见 count=1、首项 flag=2、非空 sync handle，且在缓冲尺寸修正后干净返回。r201 abort 原因已在 r203 定位为 harness 输出复制越界。`0x82:0x14` accept-and-log observer 已离线落桥并钉入门禁（r215），新构建已上机复绿（r216），分发（r217）与全路径 envelope（r218，阵列 NULL）已在活体验证到达；fake shim 不执行真实 bridge handler；translator 混合语义（等→标→写）已活体验证（r227：预置 V7 → 混合 fire 44.8ms 即过 → 写回 probe 0.11ms 即过）；check 值语义（r222）、update 写回（r223）、observer 全路径/非零/CCB 重放（r218/r224/r225）、dry-run（r226）均已活体验证；UMD 生成的真实数组与 GPU 执行仍未验证。
3. **DDK2 TA/CDM 专属提交**：`0x82:0xC` / `0x81:0x5` 仍属 S4 真提交边界，空 marker 结果不能外推；待取得真实工作包与输入规约后再推进。
4. 长期：快照 §7 的门禁可复现（`build/` 产物入 git）与 in-tree 构建外移。
