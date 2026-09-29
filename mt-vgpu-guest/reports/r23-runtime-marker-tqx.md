# r23：固件连接、真实 fence 和首个 TQX 故障现场

记录日期：北京时间 2026-09-30。设备 `0000:00:0e.0`，`1ed5:0222 / 1ed5:1101`，
内核 `6.12.107+deb13-amd64`，boot ID `9635196d-8d84-4701-b9bc-9ff6e2bb8e24`。
本轮全部操作在 Guest 完成，没有宿主操作。

## 结果与当前状态

固件连接成功、运行描述符已发布，空命令产生真实完成事件并触发 Linux `dma_fence`。
随后一次 256 字节 TQX 复制返回 `0x101` 故障事件，复制未完成。没有 MTT DRM/render 节点，
尚未启用硬件加速。当前 `Guest=0 / FW=0 / started=1`，事件处理结果 `-95`，
`pending=1 / completed=1`。`mt_guest_probe` 与 `mt_live_tqx` 保留并固定全部已发布资源。
不要卸载、unbind、释放或恢复覆盖这些资源；也不要重跑同一任务。当前没有执行重启。

## 连接与断开竞态

旧 trial 已消费 DISCONNECT，FW 进入 READY，但最后一个空事件尚未由 Guest drain。
`mt_fw_disconnect` 的“先看队列、再看 FW 状态”会将这个窗口判成失败。
修正仅在已成功提交 DISCONNECT，且发现 FW=1、started=0 时，再 drain/recheck 一次；
只有队列空且状态仍一致才能发布 Guest=0。不重发命令，不绕过超时或健康检查。

`mt_retired_disconnect` 针对已保存的完整 8 MiB SHA-256 和唯一剩余事件核对，
先只读 dry-run，再仅确认那个事件并发布 Guest OFF。它没有改写 FW 状态、触发 reset、
重新上传镜像或引用旧 GPA。旧快照散列：
`46c2843f5a3b519104edc271b564b2d21fdec4f4c3254f50fc5f63213f91a4df`。
随后 fresh runtime trial 成功，`connect_result=0`，维持 FW=2、started=1。
BAR1 描述符发布和通知成功不等于对端独立确认了描述符全部字段。

## 空命令：真机成功

`mt_live_marker` 在原模块持有的 session 锁下调用原模块自己的 submit ops，
仅临时开启 marker 门；软件 pending owner 在队列 head 更新前建立。
DM1 opcode `0x64`、wire fence ID 1，返回事件 `[0,0,1,0,0,0]`，
命令及事件 head/tail 都变成 1/1，`completed=1 / pending=0`，等待返回成功。
helper 已卸载，原模块未替换。这个结果证明该固件队列与 Linux fence 往返，不能证明渲染。

证据：[marker 结果](r23-marker-live.json)、[7 种 ABI 布局对照](r23-marker-abi.json)。

## 256 字节 TQX：已提交但失败

`mt_live_tqx` 首先通过原模块 BO/VM ops 分配并映射 5 个普通 BO 和 32 页页表空间，
绑定已有共享资源、分配上下文池切片，上传读回命令/DMA/页表。当前 topology cores=1。
15 页实际页表、14 条映射、15 个 GPU pin；固定根 `0x61000d000`，进程 token=0。
源页使用确定模式，目标整页预填 `0xa5`。只允许通过 root-only 参数单次提交 256 字节，
成功需同时验证复制内容、目标剩余 3840 字节以及源整页未被修改。

| BO | GPU VA | GPU PA | BAR2 offset | 大小 |
|---|---|---|---|---|
| 命令流 | 0x40000000 | 0x61002d000 | 0x122d000 | 4096 |
| 源 | 0x40100000 | 0x61002e000 | 0x122e000 | 4096 |
| 目标 | 0x40200000 | 0x61002f000 | 0x122f000 | 4096 |
| DMA | 0x40010000 | 0x610030000 | 0x1230000 | 8192 |
| 引擎状态 | 0x40020000 | 0x610032000 | 0x1232000 | 4096 |

根页表 BAR2 offset=0x120d000，保留 131072 字节。提交包 opcode=0x67，
DMA 长度=0x1300，fence=2。只发一次；5 秒等待超时 `-110`。
DM1 cmd head/tail=2/1，event head/tail=2/1，待处理事件 `[0,0x101,2,0,0,0]`。
Windows `14000be34` 将这类事件送入 `14000e5c8`，与普通完成处理分支不同。
当前实现拒绝将其视为成功，不推进故障事件 tail，不 signal 成功，不释放 GPU pin。

故障后观察到 Guest/FW 均为 0，started 仍为 1；这不是本 helper 主动写状态或重置的结果。
固件队列起点还出现 `0x4d545946` 及若干字段变化，含义未确认，保留原始数据。

## 现场核对

用户态 BAR2 mmap 返回 EINVAL，/dev/mem 读取被 EPERM 拒绝。
只读 `mt_tqx_snapshot` 按原驱动已保留的 allocation list 精确匹配六段地址/长度，
从已有映射提取 155648 字节，没有新建 MMIO 映射或硬件写入；提取后已卸载。
快照不是 GPU 已静止的保证。

- 源整页仍符合初始模式；目标整页仍是 `0xa5`；引擎状态页全零。
- 独立遍历 PC/PD/PT，6 个普通映射页解析到分配的 GPU PA。叶条目低位为 `0x7`。
  这确认软件表内容，不证明硬件权限、缓存属性或地址域选择正确。
- `verify-r23-live-tqx.py` 在 Unicorn 中执行 Windows 原始命令生成、finalize 和 DMA 序列化，
  使用本次 Linux 池放置。实际上传的 4096 字节命令页和 8192 字节 DMA 全部逐字节一致。
  分配器和 OS/平台接口仍为模型，不能据此排除上下文初始化或固件兼容问题。

证据：[TQX 状态](r23-tqx-live.json)、[内存与页表](r23-tqx-memory.json)、
[实际上传字节与 Windows 指令对照](r23-tqx-reference.json)、[19 种 ABI 对照](r23-tqx-abi.json)。
原始二进制与日志在 `build/r23-live/`；初次运行快照在
`build/fresh-trials/20260929T155441Z-36291a44/`。

## 模块身份与验证

实际保持加载的主模块备份为 `build/r23-live/mt_guest_probe.ko`，SHA-256：
`cbca29c5ad41343b4a0fb16286ff5e8a1d7fa1ead1b29327bfdfbcbff1aafce4`。
加载前已比对 sysfs build-id。TQX helper 备份 SHA-256：
`87b7497b7c359feeca26ee6cefec1bd22c8878f005b84bf0b3ad344132668efa`。

公共 `mt_guest_device` 声明移动到共享头，未改变布局。
之后重新编译的主模块 `kernel/mt_guest_probe.ko` SHA-256 为
`826200164592ad784dec875eda10657f99b76a71c2087db8a18b65145ddc7199`，它没有加载。
不能用这个新文件散列替代当前运行态身份。

99 项 Python 单测（含 3 项断开竞态测试）、15 项 RAM/生命周期/ASan/UBSan 检查、
主模块 W=1 构建及原保留 ABI 检查通过。marker/TQX/snapshot helper 的 W=1 构建通过。
连接状态机仍对照 36 个原指令场景及 2 个发送错误；这些离线验证不将失败的 TQX 实测变为成功。

下一步基于保留现场核对硬件 PTE 权限与缓存属性、进程上下文首次使用、固件版本及故障记录格式。
当前故障会话不适合继续提交；在有依据的修订前保持现场，继续离线分析，不要求操作宿主机。
