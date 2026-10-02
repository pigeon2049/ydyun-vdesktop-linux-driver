# 文档地图（2026-10-03）

本目录有两条**互不依赖**的适配线，文档容易互相误读，先分清：

| 线 | 目录 | 当前状态文档 |
|---|---|---|
| 云电脑 USB / 画面 / Wayland | `linux/`、`wayland/`、`docs/` | `docs/PROGRESS.md`（现状短页） |
| 摩尔线程 S3000 vGPU Guest 适配 | `mt-vgpu-guest/` | 仓库根 `STATUS.md` → `PROGRESS-SNAPSHOT.md` |

**状态冲突时裁决顺序**：仓库根 `STATUS.md` → `PROGRESS-SNAPSHOT.md`（细节）
→ `MEMORY.md`（过程）。本目录任何“当前/最新/下一步”
的表述只代表其所属线的历史阶段。

## 云电脑线（USB / 画面 / Wayland）

| 文件 | 内容 | 时效 |
|---|---|---|
| `PROGRESS.md` | USB 线现状短页（可交付物、已验证、未完成、证据表） | 活跃 |
| `PROGRESS-HISTORY.md` | Step 0–140 全量进度日志（原 `PROGRESS.md` 原样更名） | **归档** |
| `ANALYSIS.md` | Windows 云电脑安装包静态分析（`/opt/code/ydyun/driver/`），Step 0–40 | 历史证据 |
| `CLIENT-ANALYSIS.md` / `CLIENT-ABI.md` | 官方 Linux 客户端（UOS/麒麟）DWARF 与符号证据：display/input/USB 三条数据面分层、JWAE/SCG/ZIME 边界 | 历史证据，结论仍有效 |
| `ICE-PROTOCOL.md` | Windows ICE display channel 静态调查（TLS/TCP/UDP/KCP、dirty rectangle、无损区域） | 历史证据 |
| `USB-FRONTEND-ANALYSIS.md` | 官方 `chuanyun-redirect` / `libusbipd.so` 本地策略层与 SysV 队列 | 历史证据 |
| `WINDOWS-DRIVER-ANALYSIS.md`、`WINDOWS-OPEN-SOURCE-AUDIT.md` | Windows 侧驱动与开源替代审计 | 历史证据 |
| `WAYLAND-RESOLUTION.md` | KDE Plasma Wayland 下 spice-vdagent KScreen 适配设计与验证 | 活跃设计 |
| `SECURITY-SCOPE.md` | 明确排除的安全/监控/遥测组件与最小权限约束 | 约束文件，长期有效 |
| `UPDATES.md` | 升级与下游更新策略（`+ydyun` 独立版本、APT 保护规则） | 活跃 |
| `RELEASE-v0.2.52.md` | v0.2.52 发布内容与校验值 | 版本快照 |
| `CHROME-VERIFICATION.md`、`PORT-PROBE.md` | 官方客户端来源核验、端口探测记录 | 历史证据 |

## vGPU 线（`mt-vgpu-guest/`）

| 文件 | 内容 | 时效 |
|---|---|---|
| `../PROGRESS-SNAPSHOT.md` | **权威快照**：真机阶梯、修掉的缺陷、门禁现状、下一步 | 活跃 |
| `../MEMORY.md` | 最新两节过程记录（旧文在 `MEMORY-HISTORY-2026-10-01.md`） | 活跃 |
| `../mt-vgpu-guest/README.md` | 该目录入口：当前状态、运行态红线、门禁命令 | 活跃 |
| `../mt-vgpu-guest/reports/README.md` | r 系列证据索引（先查索引再开报告） | 活跃 |
| `MTT-VGPU.md` | stub；全文在 `MTT-VGPU-2026-09-22.md`（只读） | **归档** |
| `../mt-vgpu-guest/HISTORY-2026-09.md` | 09-22–09-30 横幅堆栈、旧状态表、社区构建实验 | **归档** |
| `../mt-vgpu-guest/PROTOCOL-NOTES.md`、`FIRMWARE-NOTES.md` | stub；全文在同目录 `*-2026-09.md`（只读） | **归档** |
| `../mt-vgpu-guest/HOST-REQUEST.md` | 对外请求记录（已不再执行），顶部有归档注记 | **归档** |
| `../mt-vgpu-guest/DECOMPILATION.md` | 反编译库使用说明（工具与索引） | 工具说明 |
| `../mt-vgpu-guest/reports/r*.md` | 逐轮实测证据链（r66 首次 RGX 执行、r70 像素读回、r71 批量压测） | 证据链，勿改名 |

## 动手前必读

1. 仓库根 `PROGRESS-SNAPSHOT.md` §12 运行态：活会话的模块与设备是 freeze 的，
   不要卸载、解绑或提交额外工作。
2. `mt-vgpu-guest/README.md` 的“运行态红线”：`make probe`/`make umd` 会
   rmmod/insmod，只在可重建会话上使用。
3. 门禁分层：`make check-offline`（L1，不碰硬件）、`make check`（L1+L2）、
   `make kernel`（只构建）、`make probe`/`make umd`（L3/L4，需 root 且动模块）。