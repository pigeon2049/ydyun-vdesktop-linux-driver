# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](MEMORY-HISTORY-2026-10-07.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-07（r209 fabricated GFX CCB 捕获能力）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r209：fabricated GFX CCB 捕获能力）

- 零硬件触碰。扩展 `umd_bridge_shim`：`0x82:0x14` 按 5.2 schema 的 submission VA/size 找 shared PMR backing；显式设置 `UMD_CCB_DUMP_DIR` 后才将原始 CCB 落盘，16 MiB 上限、0600、独占创建。`0x89:0xa` 复用同一逻辑。
- 新增合成门禁覆盖偏移、PMR 解析、raw bytes、不可解析 VA、关闭 shared backing 与关闭 dump 目录。故意把 size offset 84 改成 80 时测试失败（128 vs 256），复原后通过。
- `make check-offline`：295 Python（1 skip）+292 C 全绿；`git diff --check` 通过。未改内核、未运行 `make kernel`、未操作硬件。
- r203 现存请求可解出 VA=`0x8000023000`、size=`0x4700`、flags=0、submissionID=1；本轮没有重放 GFX producer，因此真实 UMD CCB backing bytes 尚未捕获。见 `reports/r209-kickgfx-ccb-capture.md`。
- 遗留：使用可复现的 fabricated GFX producer 配方捕获真实 UMD 生成 CCB，并据其字节结构推进 TA/3D 包映射；捕获/解码仍不等于执行。

---

## 本次会话进展（r208：PMR 到真实 GPU VM 的后端边界）

- 零硬件触碰。只读确认 `mt_bo_system_borrow()` 能将稳定 `mt_system_memory` 的逐页 GPA 包成设备 session BO；VM 绑定要求 BO 与页表 BO 的 store/ops 一致。现有 PVR PMR `gpu_bo` 是 CPU-only planning facade，不能进入真实 VM。
- 真实接线仍需 per-file 上传 VM、process/render context、PMR borrowed BO 生命周期、nested sync/PMR 解引用、CCB 资源闭包和 fence 完成。当前 translator 全局共享、context 只是 token；marker/TDM observer 不执行真实 CCB。
- 未改代码、未跑门禁、未动硬件。设计路线和证据边界见 `reports/r208-ddk2-render-backend-boundary.md`。STATUS 下一步不变。
- 遗留：真实 CCB 活体验证仍须用户明确批准；执行包格式/资源闭包尚未证实。

---
