# STATUS — 仓库当前状态总入口（2026-10-03）

> 新 agent 先读完本文件再动手。历史文件都有归档头；状态冲突时裁决顺序为
> `STATUS.md`（本文件）→ `PROGRESS-SNAPSHOT.md`（细节）→ `MEMORY.md`（过程）。
> 不要为“了解背景”通读归档文件——那是 token 黑洞。
> 机器可执行的完整约束见 [`AGENTS.md`](AGENTS.md)（落点、清理周期、红线、门禁）。

## 这是什么仓库（两条互不依赖的线）

| 线 | 目录 | 做什么 | 当前结论 |
|---|---|---|---|
| 云电脑 USB/画面 | `linux/`、`wayland/`、`docs/` | Debian 云电脑 USB 转发 + KDE Wayland 分辨率 | v0.2.52 可安装（见根 `README.md` 教程）；云端联调待真实会话 |
| S3000 vGPU Guest | `mt-vgpu-guest/` | 自研内核栈点亮 Moore Threads S3000 vGPU | 会话已连通（Guest/FW 2/2）；厂商 UMD 桥 8 符号全绿；RGX 已真实执行并读回像素 |

## vGPU 一句话现状

`mt_guest_probe` 绑定 `00:0e.0` 并连通固件；`mt_pvr_bridge`（`894faf50`，
arena backing + cover-page plan + kick T1/T2 只读观察）在载；
厂商 MASA UMD 走完全链路符号全部返回 0（kick 提交是 accept-and-inspect
即时 fence，真提交入口仍拒绝，那是 S4 边界）；`live_3d_drm` 已做单帧
DM2 + 64 KiB 像素读回 + 20 帧批量，21 次执行零 fault。
细节见 `PROGRESS-SNAPSHOT.md`，逐轮记录见 `MEMORY.md`（只留最新两节），
证据在 `mt-vgpu-guest/reports/r*.md`（索引见该目录 `reports/README.md`）。

## 活会话红线（先读这段，违反会毁掉数小时工作）

- `mt_guest_probe`、`mt_pvr_bridge`、`mt_live_3d_drm` 保持加载，
  不 rmmod、不 unbind、不提交额外工作。
- 顶层 `make probe` / `make umd` 会先 rmmod 再 insmod，
  **在活会话上禁止直接使用**；只在可重建会话上跑。
- `timeout` 不得落在 bridge ioctl 临界区内（r67：device-mutex
  owner-death 泄漏只能靠重启恢复）。DMA 路径命令超时只做挂起探测，
  超时即停手、不堆任务。
- 一次只跑一个 live 实验模块，做完即卸（占着 trial_lock 会卡死下一个）。

## 门禁（`mt-vgpu-guest/` 下执行）

| 命令 | 含义 | 动硬件 |
|---|---|---|
| `make check-offline` | L1：226 Python + 268 C RAM checks | 否 |
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

1. **真实绘制 kick 观察**：合成零 count 与非零 check 均已复现
   （fabricated r72 + 活体 r73，T2 `ufo_known=1/1`）；update 侧需
   DDK2，而 DDK2 需桥特性开关（改代码 + 重编 + 重载，单独立项，r78）；
   非零 CCB 内容仍只能来自完整绘制路径（Rogue2D 推进到
   TransferContext 创建，legacy-TDM 疑死代码，r86–r112）。
2. **Translator T3**：DM 队列格式改从 UMD 侧反推（r82–r84、r114；
   `mtkm64.sys` 已排除）；check-only 首帧翻译设计已完成，待新会话
   执行（r113）；在拿到真实 CCB 内容之前不写翻译器骨架——输入规约
   先行，代码随后。
3. 长期：快照 §7 的门禁可复现（`build/` 产物入 git）与 in-tree 构建外移。
