# r29：固定上下文连续 GPU 复制通过

日期：2026-09-30。boot ID `fe18deeb-d431-4c03-ac88-34235e9849c5`。
沿用 r28 的有效会话，没有重新加载主模块、修改页表、清零 engine-state、重启或操作宿主。

## 真机结果

80 次顺序 TQX 复制全部成功，使用 15 种长度/偏移组合，覆盖 1～4096 字节、
非对齐源/目标、页尾边界及整页。每次改变源数据和目标哨兵，在提交前读回检查上传内容，
由真实完成事件触发 Linux fence 后再校验完整源页、目标复制范围以及范围外的全部字节。
任何错误都会停止本轮，未执行错误恢复或人工成功通知。

- `submitted=80 / passed=80 / result=0`。
- fence 从 3 到 82；原有累计完成数由 2 增至 82，pending=0。
- DM1 命令环和完成事件环的 head/tail 从 2/2 变为 18/18；
  与 80 次提交越过 64 槽环形回绕一致，两环均已排空。
- `Guest=2 / FW=2 / started=1 / event_result=0`，固件仍正常。
- 128 KiB 根页表快照与 r28 逐字节一致；SHA-256 为
  `b07abac851a3bdcdc49c712301a79ddababb791bdeac8f601d2d6153e32f938c`。
- BO 对象数仍为 15，allocated_bytes=13791232，VM/process/context 仍各 1。
  验证 helper 持有的额外 BO 引用在卸载时释放，原上下文和已发布根继续保留。

最终一次是 iteration=79 的 31 字节复制。额外只读快照读取 155648 字节，
独立 Python 检查确认完整 4096 字节源页、目标前 31 字节与其余保护区正确。
最终目标 SHA-256 为 `455b388bcfd9c985131e01d3542d9c1109a0eef5170b6d1cb624a3a90688e480`。
前 79 次的数据校验依据内核 helper 日志，没有声称逐次独立快照。

## 实现与复现证据

新增 `kernel/recovery/mt_live_tqx_repeat.c`，使用已自持模块引用的 r28 `mt_live_tqx`
上下文。加载前后核对现有模块参数，实际运行前比对主模块和原 TQX helper 的 build-id
与保存的 r28 二进制相同。挂接阶段不提交任务。

挂接检查唯一进程/上下文、三个池切片、封存页表、14 个绑定，以及五个普通 BO 的
精确 VA/大小/默认可写权限。通过原驱动的 BO ops 和 `submit_tqx_work` 调用，
避免跨模块静态 ops 身份和回调归属问题。命令发布后的 job、fence、完成处理和原
上下文都属于保留模块，验证 helper 不接管它们的生命周期。

每次准备和提交由原 session mutex 保护，等待 fence 时释放锁；每轮仅在提交临界区
开放工作入口，之后立即关闭。页表始终固定，缓存的上下文池切片复用；没有再次分配
GPU BO 或上传根页表。该模块限 root 显式触发一次 80 项任务，不是生产用户 ABI。

`W=1` 编译无警告。测试后 `mt_live_tqx_repeat` 与 `mt_tqx_snapshot` 已正常卸载，
主模块和原 `mt_live_tqx` 保留。没有配置开机加载或更改桌面驱动。

保存内容：

- `reports/r29-repeat-build.log`：模块编译输出。
- `build/r29-live/mt_live_tqx_repeat.ko`：实际测试二进制。
- `build/r29-live/dmesg.log`：80 项逐次参数、sequence 和结果。
- `build/r29-live/repeat-status.json`：卸载前 helper 状态。
- `build/r29-live/after-trial_firmware`：8 MiB 固件快照及队列游标。
- `build/r29-live/tqx-memory.bin`：独立读取的根、command、source、destination、DMA、engine-state。
- `reports/r29-copy-validation.json`：离线核验结果。

离线重验：`python3 scripts/verify-r29-live-repeat.py`。
此脚本不访问硬件；它核对全部 80 项日志、实际页表/最终数据、队列回绕和运行状态。
不要自动重新加载本轮 helper 或反复写 run；复现硬件测试前必须重新核查活动会话与
保留 helper 的身份、唯一性及资源归属。

## 用户态接口仍缺的条件

本轮证明固定根、固定 BO、单上下文顺序复制可复用，尚不覆盖并发任务、跨页大传输、
根替换/撤销、超时恢复或桌面渲染。

`kernel/mt_gem.h` 目前只有内部 create/handle/bind/prepare 接口，没有注册 DRM 设备或
公开 ioctl；CPU mmap 与 PRIME export 明确返回 EOPNOTSUPP。直接创建 render 节点
不会让现有 Linux UMD 工作：原厂用户态依赖完整 PVR bridge ABI，既有审计也发现版本差异。

下一步可先为已验证的固定上下文实现受限、串行、带真实 fence 结果的用户态复制入口，
不允许用户传裸 GPU 地址或任意命令流，再逐步接 GEM 对象和进程资源生命周期。
公开一般 VM/DRM 提交之前，仍需明确处理进程退出、挂起任务及封存页表撤销，不能仅因
pending=0 就释放已经发布的根。当前 `render_ready=0 / drm_registered=0`；尚未完成桌面加速。
