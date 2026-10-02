# MT vGPU Guest：本机适配与实验

> **当前状态（2026-10-03）：本文件是 `mt-vgpu-guest/` 的入口。**
> 当前权威快照是仓库根目录 `PROGRESS-SNAPSHOT.md`（逐轮过程记录在 `MEMORY.md`），
> 两者冲突时以快照为准。
>
> 一句话现状：厂商 MUSA UMD 在自研内核桥上走完 8 个符号（connect → device →
> devmemctx → render → syncprim → kicksync → compute → kicksubmit
> accept-and-inspect），全部返回 0；S4-3 交接第一步（DMA + VM plan +
> kick T1+T2 只读观察）已落桥；首次 RGX 真实执行 + 像素读回 + 20 帧批量
> 已完成（r66/r70/r71）。真 kick 提交入口仍拒绝，那是 S4 边界。
> 运行中会话（`mt_guest_probe` 绑定 `00:0e.0`，Guest/FW 2/2 pinned；
> `mt_pvr_bridge` 在载；`/dev/dri` 有 `card1`/`renderD128`）**不要卸载模块、
> 解绑设备或提交额外工作**。

## 运行态红线（先读这段再动手）

- 活会话上的模块、设备绑定、固件提交全部 freeze：`mt_guest_probe`、
  `mt_pvr_bridge`、`mt_live_3d_drm` 保持加载，不 rmmod、不 unbind。
  顶层 `Makefile` 的 `probe`/`umd` 目标会先 rmmod 再 insmod，
  **在活会话上禁止直接使用**；如需验证，先确认目标会话可以重建。
- `timeout` 不得落在 bridge ioctl 临界区内（r67：device-mutex owner-death
  泄漏只能靠重启恢复）。DMA 路径命令超时只做挂起探测，超时即停手、不堆任务。
- 一次只跑一个 live 实验模块，做完即卸（S4-2 教训：占着 trial_lock 会卡死
  下一个）。

## 入口

| 目录 | 内容 |
|---|---|
| `kernel/` | `mt_guest_probe` 主会话模块 + 共享头（`mt_pvr_session.h` 为桥—会话契约） |
| `kernel/recovery/` | `mt_pvr_bridge`（Stage B S1，UMD 桥）与 live 实验模块 |
| `probe/` | 用户态探针与 harness（`pvr_node_probe`、`pvr_dma_smoke`、`pvr_cover_probe`、`pvr_kick_probe`、`umd_connect_harness` + `umd_bridge_shim.so`） |
| `tests/` | Python 门禁 + C RAM 模型测试 |
| `userspace/` | `mt-3d-check` 等用户态验证程序 |
| `reports/r*.md` | 逐轮实测证据链（r66 首次 RGX 执行、r70 像素读回、r71 批量压测；不要改名） |
| `HISTORY-2026-09.md` | 09-22–09-30 横幅堆栈归档，只读 |

## 门禁

```sh
make check-offline  # L1：Python 套件 + 桥核心 RAM 测试，不碰硬件
make check          # L1+L2：+ 内核 W=1 构建 + ABI 门禁，仍不加载模块
make kernel         # 全模块 W=1 构建，不加载
```

`make probe`（L3）、`make umd`（L4 八级阶梯）会加载/卸载模块，需要 root
且只在可重建会话上跑。L4 止于 kick-submit 形状流量（S4-1）；
真实 GPU 执行（S4-2/RGX）是单独批准的步骤，不属于任何默认目标。
