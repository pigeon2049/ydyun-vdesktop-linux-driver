# r28：关机重启后首次 TQX 真机复制成功

2026-09-30，北京时间。boot ID `fe18deeb-d431-4c03-ac88-34235e9849c5`，
内核 `6.12.107+deb13-amd64`。全部操作在 Guest 完成，没有操作宿主或发出 MPC HWR。

## 实测结果

r24 权限修正版在本机完成一次 256 字节 GPU TQX 复制。
helper 返回 `result=0 / verified=1 / sequence=2`；原驱动 fence 为
`pending=0 / completed=2`（先前空命令一次、实际复制一次）。
固件保持 `Guest=2 / FW=2 / started=1 / event_result=0`。

已独立只读提取真实分配的 155648 字节，再核验：

- 源页全部 4096 字节仍符合原始模式 `((i*73+19)^(i>>3))&255`。
- 目标前 256 字节与源完全相同。
- 目标后 3840 字节均为原始 `0xa5`，未越界改写。
- 五个普通 BO 共六页的实际 PTE 均为 `PA|1`，可写默认权限。
- GPU 完成后 DMA 和独立 engine-state 区有变化，保存了原始结果；不将它们当作上传前镜像。

这证明本机的固件连接、任务提交、GPU 地址翻译、该复制命令和 Linux fence 往返可用。
只覆盖一个 256 字节任务，不能据此宣称连续任务、多尺寸复制、渲染、DRM/UMD ABI 或桌面加速完成。
当前仍没有 MTT DRM/render 节点；目标保持进行中。

## 新会话与断开收尾

关机重启使分配的 OSID 从 6 变为 4，正常显存段 PA 变为 `0x605000000`，
固件段 PA 为 `0x771fef000`。旧自动加载的 `mtgpu` 仍在 PhysHeapsInit [644] 失败，
引用数为零、没有 MTT DRM 节点，正常卸载后读到 Guest2/FW1。
其卸载仍有已知 workqueue flush 警告。

新信息页查询和只读采集发现，实际发布的固件描述符为
PA `0x605000000`、8 MiB，位于普通段的 BAR2 offset `0x200000`；它与固件段
offset `0x3f000000` 不同，因此分别采集两个窗口，没有照用 r23/r25 地址。

实际发布区的完整 SHA-256：
`d48ab779337873246000003b1e2332f3161bd5346bb817d61312065d380a4975`。
其中 started=0、DM0 首两条为 CONNECT/DISCONNECT（0x46/0x47），命令及事件均为 2/2，
所有六个 DM 的三个环都已排空。FW 段窗口也为 started=0、队列空。
不能断言这些历史命令由本次 Linux 启动产生，但当前没有未消费的命令或事件。

`mt_idle_disconnect` 先只读 dry-run，核对精确 PCI/BAR、BusMaster 关闭、发布 PA/长度、
完整 8 MiB 散列、CONNECT/DISCONNECT、所有环 head=tail、Guest2/FW1/started0。
再次加载 `finish=1` 后只执行已完成断开的 Guest OFF 最终写入；不写固件、事件 tail 或 doorbell。
读回 Guest0/FW1 后卸载 helper。不是强改 FW READY，也不是触发重置。

## 加载与执行

`fresh-trial.py --runtime-context --run` 使用已验证的 r24 主模块，首次 CONNECT 成功，
运行描述符发布成功。证据目录：
`build/fresh-trials/20260929T164002Z-8a4b0f0d/`（目录时间为 UTC）。

随后加载保存的 r24 marker helper，DM1 fence=1 正常完成；卸载 marker helper。
加载保存的 r24 TQX helper，准备 15 个页表页、14 个映射、15 个资源 pin，
根 GPU PA=`0x60600d000`，上下文 token=0，cores=1。
仅一次写 `run=1`，fence=2 成功返回；没有绕过一次性保护重跑任务。

| BO | GPU VA | GPU PA | 大小 |
|---|---|---|---:|
| command | 0x40000000 | 0x60602d000 | 4096 |
| source | 0x40100000 | 0x60602e000 | 4096 |
| destination | 0x40200000 | 0x60602f000 | 4096 |
| DMA | 0x40010000 | 0x606030000 | 8192 |
| engine-state | 0x40020000 | 0x606032000 | 4096 |

实际加载的模块散列与 r24 固定候选包一致：

- 主模块：`c1c97955663a86d3f56703e69cfcfb799f7c1bbdd1552544c8238a9902b1a662`
- marker：`54c45e89cffa6d1ed98d8b921163c420dbfcbaabe3c96c80bfa0059952cb1f77`
- TQX：`3d2f118a168db68bc4a7edd9276fae6e0cfd1e7deecc96d30045f3343e4dc87d`

`mt_tqx_snapshot` 的可选 r28 只读模式要求新 OSID=4、普通段 PA 匹配，并在原模块已持有
的 allocation list 中精确查找六段；通过原 CPU 映射读取，没有新 MMIO 写入。
提取后该 helper 已卸载。信息采集和断开收尾 helper 也已卸载。

主模块与 TQX 上下文仍保留：任务虽完成，根页表撤销协议尚未验证，不能因为 pending=0
就释放这些已发布资源。此举保留当前工作的硬件会话供后续连续提交适配。

## 产物与下一步

`reports/r28-copy-validation.json` 包含数据完整性、目标保护区、PTE 和运行状态。
`build/r28-live/` 保存 marker/TQX 前后 sysfs、日志、根页表、命令、源、目标、DMA、状态区。
`reports/r28-fw-slot.bin` 与 `r28-published-window.bin` 保存初始化前两个不同区域。
诊断/收尾/快照 helper 均 W=1 编译无警告。

下一步需要在保留会话中验证可重复、多尺寸提交及错误处理，再推进可用的 DRM/用户态接口。
r27 的阻塞条件已由本次新会话和成功执行解除，不再作为当前状态。
