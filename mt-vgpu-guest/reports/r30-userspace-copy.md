# r30：用户态到真实 GPU 的复制链路

2026-09-30；boot ID `fe18deeb-d431-4c03-ac88-34235e9849c5`，继续同一正常会话。
新增内核 bridge、固定宽度 ioctl ABI、用户态文件复制工具及调用边界检查程序。
没有重启、重置 GPU、操作宿主或替换当前主模块。

## 实测

`/dev/mt-vgpu-copy` 已注册为 root:root 0600。通过 root 用户态程序调用：

1. 10 种无效版本/flags/reserved/sequence/长度/偏移请求返回 EINVAL；错误 ioctl
   大小返回 ENOTTY，无效用户地址返回 EFAULT。提交和完成计数保持不变。
2. 子进程继承有效 fd 后降为 UID/GID 65534，QUERY 与 EXEC 均返回 EPERM。
3. 4 个进程各打开自己的 fd，同步起跑，共执行 32 次复制。服务按锁串行处理；
   每次由应用检查完整源页、目标页和保护区，32 个真实 fence sequence 唯一且连续。
4. 可读但不可写的请求缓冲区触发 copyout EFAULT；GPU 已完成的一个任务仍被正确
   计入 submitted/completed，服务没有擅自重试。
5. 文件工具复制 65,659 字节，17 个真实 GPU 任务完成。输出与输入逐字节相同，
   SHA-256 均为 `6679f4178f43eb465ee2c5e013f4b8000140eb19ce2845a27ba68395237e7f35`。

bridge 合计 submitted=50、completed=50、faulted=0、last_sequence=132。
整个驱动累计完成数从 82 增至 132，pending=0。
Guest2/FW2/started1/event_result0；DM1 命令和事件 head/tail 均为 4/4。

独立只读快照验证最终文件块：源页为最后 123 字节加零，目标页为同 123 字节加原
0xa5 保护区；128 KiB 根页表与 r28 一致，没有修改映射。
测试程序关闭 fd 后 bridge refcnt=0，15 个 BO、一个 VM/进程/上下文继续保留。
快照 helper 已卸载；复制 bridge 保持加载，供后续用户态适配使用。

## 实现

- `include/mt_copy_uapi.h`：无裸指针/裸 GPU 地址的两个 ioctl，结构为 48/8232 字节。
- `kernel/recovery/mt_live_copy_bridge.c`：验证原上下文身份与绑定，通过原 BO ops 和
  marker ops 提交；当前调用者必须有 CAP_SYS_RAWIO；整次操作跨文件串行化；
  所有参数使用一次内核副本，命令由驱动构造，真实 fence 完成后读回并验证显存。
- `userspace/mt-copy.c`：query 与分块文件复制；拒绝覆盖已有输出。
- `userspace/mt-copy-check.c`：多进程调用、非法参数、权限和 copyout 错误路径检查。
- `scripts/load-copy-bridge.py`：默认只读，核对精确主模块/保留 helper 身份、健康空闲
  会话和固定 bridge 二进制；只有显式 --load 才加载，不初始化或恢复会话。

返回结果从 destination BO 读取，内核计算的 expected 数组只用于比对；不会用 CPU
计算结果替换 GPU 结果。CPU 暂存、每页校验和同步 fence 是目前适配阶段的限制，
不声称性能收益或零拷贝。多进程测试证明访问串行化，不证明 GPU 并发调度能力。

内核初次构建发现 Linux 6.12 已无 no_llseek；删除该成员后由 nonseekable_open
禁止 seek，W=1 编译无警告。两个用户程序以 C11/Wall/Wextra/Werror 构建通过。
32 位结构大小、关键偏移及 ioctl 常量静态编译通过；compat 入口尚未执行 32 位实测。

已加载 bridge SHA-256：
`6a71ba1dca9223f5581009541e281dea93844ad93a66ae671e5881a67dca1cd1`。
主模块及原 TQX helper 的 build-id 分别为
`9d0f0ce06c13bf64d4b24d5789158080e280e5e3`、
`9763bf6502e9a80ce3b4da4e34afb07ddfd0d8bd`，与固定 r28 包一致。

## 证据与剩余工作

`build/r30-live/` 保存实际模块/用户程序、输入输出文件、状态、8 MiB 固件和 155648
字节显存快照。`reports/r30-user-check.json` 为用户程序执行结果；
`reports/r30-copy-validation.json` 为独立离线核验；编译日志、loader 身份检查分别保存。
`python3 scripts/verify-r30-userspace-copy.py` 可复核保存结果，需要有输出文件读取权限。

尚无 MTT DRM/render 节点、GEM 用户接口、PVR/MUSA ABI、OpenGL/Vulkan 或桌面加速。
本轮接口使用固定保留上下文和既有 BO，不能替代一般进程 VM、页表扩容/撤销、GPU
挂起恢复及绘制路径。目标仍在进行中。后续应接对象/同步生命周期与 DRM/用户态渲染，
并继续对照已有完整反编译和原厂 Linux ABI；不把复制测试通过视为显卡适配完成。
