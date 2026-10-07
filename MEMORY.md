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

最后更新：2026-10-07（r210 fabricated GFX 原始 CCB 捕获）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r210：fabricated GFX 原始 CCB 捕获）

- 零硬件触碰。恢复重放工作目录与 sync tuple 后，GDB 实测 check/update helper 均返回 0，`RGXKickGfx` 返回 0；trace seq 125 发出 `0x82:0x14`，VA=`0x8000023000`、size=`0x4700`、check/update counts=1。
- r209 shim 落盘 18,176 字节原始 UMD CCB，107 字节非零，SHA-256=`faa93985aa6b3f65df66641ac73f25020a66fca7af6cdece3b9e88c3a315dae7`。保存于 `reports/r210-gfx-ccb-capture.bin`，桥 trace 在同名 `.jsonl`。
- 不证明 Guest handler 或 GPU 执行；STATUS 下一步仍是补齐真实 DDK2 render backend 和 TA/3D CCB 执行链。详见 `reports/r210-gfx-ccb-capture.md`。
- 遗留：USB/画面线短页 `docs/PROGRESS.md` 标题日期仍为 2026-10-03；本轮是 vGPU 任务，留待对应短页刷新轮处理。


## 本次会话进展（r209：fabricated GFX CCB 捕获能力）

- 零硬件触碰。扩展 `umd_bridge_shim`：`0x82:0x14` 按 5.2 schema 的 submission VA/size 找 shared PMR backing；显式设置 `UMD_CCB_DUMP_DIR` 后才将原始 CCB 落盘，16 MiB 上限、0600、独占创建。`0x89:0xa` 复用同一逻辑。
- 新增合成门禁覆盖偏移、PMR 解析、raw bytes、不可解析 VA、关闭 shared backing 与关闭 dump 目录。故意把 size offset 84 改成 80 时测试失败（128 vs 256），复原后通过。
- `make check-offline`：295 Python（1 skip）+292 C 全绿；`git diff --check` 通过。未改内核、未运行 `make kernel`、未操作硬件。
- r203 现存请求可解出 VA=`0x8000023000`、size=`0x4700`、flags=0、submissionID=1；本轮没有重放 GFX producer，因此真实 UMD CCB backing bytes 尚未捕获。见 `reports/r209-kickgfx-ccb-capture.md`。
- 遗留：使用可复现的 fabricated GFX producer 配方捕获真实 UMD 生成 CCB，并据其字节结构推进 TA/3D 包映射；捕获/解码仍不等于执行。

---
