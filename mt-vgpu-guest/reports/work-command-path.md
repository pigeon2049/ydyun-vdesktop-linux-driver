# 工作提交包与 VM 范围检查

2026-09-28。按继续适配的要求推进，未恢复设备或操作宿主。主模块未加载；本轮
仅运行了普通 RAM 后备的临时 GEM 内核测试，随后正常卸载。

## 字段来源

本轮追踪 `mtkm64.sys` 的 `140014344`（SIMSubmitCommandVirtual）、`14001475c`
（包构造）、`14000a750`（设置上下文 root PC）与 `140015048`（SIMCreateProcess）。
确认以下来源，避免把所有 64 位字段都解释为地址：

| 包偏移 | 字节数 | 来源和意义 |
| --- | --- | --- |
| +0x08 | 4 | 提交 flags 的 bit7 转为包 bit2，其余位为 0 |
| +0x0c | 4 | 由描述符任务类型选择的 opcode |
| +0x18 | 8 | 描述符 +0xc0；虚拟提交路径写入 Context+0x30 的 root PC |
| +0x20 | 8 | Process+0xa0 的进程 token，由 adapter 计数器分配；不是 GPU 地址或 PID |
| +0x28 | 8 | 描述符 +0x38，虚拟提交路径写入传入的命令 GPU VA |
| +0x30 | 4 | 命令字节数 |
| +0x48 | 4 | 本次提交的 fence 编号 |
| +0x4c | 4 | 描述符 +0x1c；描述符准备路径可追到 PsGetCurrentProcessId |

`14000a750` 将地址转换 helper 的输出同时写入 Context+0x30 与 Process+0xa8，
诊断字符串明确标为 root PC。`140015048` 将 adapter+0x1618 递增值写入进程 +0xa0，
这与 Linux 用户空间的 PID 是两种字段。地址转换 helper 的完整多平台行为本轮未移植。

任务类型名字来自 `1400136dc`。type1→0x67，type3/11→0x66，type5→0x68，
type6→0x65，type9→0x69，其余原始类型走 0x64。NULL 描述符分支同样用 0x64，
但除 opcode/fence 外全部清零。原始包构造会保留任意宽度的 token/输入字段；
这不代表这些输入都可用于实际工作负载。

## 节点与队列不是相同编号

`14000c1e4` 初始化逻辑节点，`1400229d4` 提供平台节点类型列表。完整执行初始化
指令，核对类型、节点索引、DM、标志、能力、调度分类、engine 数和分组后，得到：

| 节点类型 | 参考名字 | 固件 DM |
| --- | --- | --- |
| 1 | TQX | 1 |
| 2 | COMPUTE | 3 |
| 3 | COPY | 4 |
| 4 | KMD_COPY | 5 |
| 5 | UQ | 2 |
| 6 | PCIEDMA | 6，非六个固件 DM 内的索引 |
| 7 | TIMER | 6，非六个固件 DM 内的索引 |
| 8 | SMQ1 | 2，分组 1 |

本机型号选择的参考构造器 `14002d73c` 包含 `[1,5,2,6,7]` 与
`[1,5,2,6,7,8]` 两个表，选择受对象 +0x5d 控制。本轮没有将未追通的选择条件
猜测成硬件能力，也没有在 Linux 自动启用某个表。node_route_build 只构造路由信息，
不是节点资源分配或向固件注册上下文。type0 的参考 GP 路由也测试为 DM6。

## 新增实现

`kernel/mt_work_command.h` 分离精确序列化和 Linux 暂存检查。encode 是 CPU 纯函数，
完整填充 80 字节，保留输出后的空间，错误时不修改输出；不代表输入可执行。

prepare 要求同一 BO/VM 锁，接受已映射的 GEM 命令对象；验证目标 VA 的完整命令
范围位于该 BO 的一个绑定内，检查 40 位地址范围及后备长度。页表根从 VM 的页表 BO
取得，覆盖请求里任意提供的 root 值。进程 token 全 64 位保留，不按 GPU 地址截断。
当前仅允许 type1/3/5/9/11 和已确认的 bit7，拒绝软件操作、抢占、未知类型及尚未
支持的外层 WDDM 提交 flags。不能跨多个绑定构造一个命令范围。

`kernel/mt_gem.h` 的 prepare_work 内部接口先用文件私有句柄取得 GEM 引用，在
会话锁内执行上述检查，退出锁后归还引用。拒绝其他文件、设备/管理器和错误对象。
输出仍只是暂存包：没有调用固件队列，没有增加 GPU 使用引用、上传/封存 VM 或
验证命令流内部引用。真实发布前必须重新验证并持有所有实际资源，不能直接把暂存包
当作允许用户提供任意 GPU 命令的 ioctl。

## 验证

`scripts/verify-work-command.py` 执行原始 `14001475c` 指令，在入队之前捕获包，
120 个完整包和 3 个空包逐字节一致，覆盖所有 type 分支、不同 DM、flags 和宽字段。
另执行 `14000c1e4`，20 个节点路由一致，覆盖全部 0..8 类型与两种 S3000 参考表。
pool 分配、timer 设置、统计/等待与 stack cookie 均建模，不运行 Windows OS 或 MMIO。
结果见 [work-command-validation.json](work-command-validation.json)。

扩展 GEM 模型测试：正确命令范围、错误句柄/对象、错误 manager、输出过短、未支持
类型/flags、跨绑定末端和 40 位溢出；全部错误保持输出不变。检查实际 VM 根替换、
宽 token 不被误截断、尾部保护字节和“只暂存不改变 VM/BO 引用”行为。ASan/UBSan
与既有回归通过。

真实 Linux GEM/RAM 内核测试增加同样的地址来源和越界拒绝检查，共 18 项通过，
分配/释放为 2/2；临时模块已卸载、无新增内核 WARNING/BUG/Oops。
[内核测试记录](gem-kernel-validation.json)与 [集成构建记录](runtime-integration-build.json)。

```sh
python3 scripts/verify-work-command.py
python3 scripts/verify-runtime-integration.py
python3 scripts/verify-gem-kernel.py --run
```

实际节点/任务上下文、命令流载荷、BO/VM 的任务持有、发布与完成回收、用户 ioctl
和用户态渲染仍需继续接通。主模块尚未加载，GPU 执行和硬件加速尚未验证。
