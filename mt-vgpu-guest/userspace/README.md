# 实验性 GPU 3D 渲染、填充与复制 DRM 接口

当前最新的统一全功能前端是 r40 的 `/dev/dri/renderD128`（`mtvgpu 0.3.0`），同时具备：
- `MT_DRM_CAP_COPY` (1): 直接显存复制
- `MT_DRM_CAP_FILL` (2): 原生 GPU 颜色填充
- `MT_DRM_CAP_3D` (4): Universal Queue 3D 渲染执行

用户态 3D 渲染测试工具：
```sh
cd /opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest
make -C userspace
sudo build/userspace/mt-3d-check 10
```
该工具通过标准 `DRM_IOCTL_MT_SUBMIT_3D` 直接打开 `/dev/dri/renderD128`，向 DM 2 Universal 硬件队列提交 3D 渲染工作包，并绑定与验证 Linux 原生 DRM syncobj 及 sync_file 异步栅栏。


```sh
sudo python3 scripts/check-graphics-session.py
sudo build/r34-live/mt-surface-check /dev/dri/renderD130 smoke
sudo build/r34-live/mt-surface-check /dev/dri/renderD130 exercise /path/new-output.ppm
```

第一条只读，后两条会提交 GPU 任务。输出文件必须不存在。exercise 以 23 个 GPU
填充绘制 1920×1080 图像，再复制/读回并核验；程序没有上传像素的 WRITE 调用。
目前仍是 root/CAP_SYS_RAWIO 私有实验接口，尚无标准 OpenGL/Vulkan 或桌面加速。
详见 [r34 实测与限制](../reports/r34-large-surface.md)。以下 r32/r31 接口仍保留。

保留的 r32 的 `mtvgpu 0.2.0` 节点是 `/dev/dri/renderD129`，支持真实 GPU 32 位矩形填充、
直接显存复制及 native syncobj/sync_file。尚未接入 Mesa/OpenGL/Vulkan 或桌面显示。
旧 r31 `renderD128` 仅有复制；请先用能力检查确认节点，不要把存在 render 节点视为完整驱动。

```sh
cd /opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest
sudo python3 scripts/check-graphics-session.py
sudo build/r32-live/mt-fill-check /dev/dri/renderD129 smoke
sudo build/r32-live/mt-fill-check /dev/dri/renderD129 exercise
sudo build/r32-live/mt-fill-check /dev/dri/renderD129 demo /path/new-output.ppm
```

检查脚本只读；三个测试命令会实际提交 GPU 任务。demo 输出必须不存在，图像每个像素
由 GPU 原生填充生成，再经 GPU 复制和显存读回导出。所有填充都要求 root/CAP_SYS_RAWIO，
每槽最多 64 KiB，COPY/FILL 为同步 ioctl。`mt_drm_uapi.h` 定义结构；QUERY 的 capabilities
返回 COPY=1、FILL=2。根和上下文暂时保留，不要卸载已自持引用的模块。
重启后需重新核对设备会话，未配置开机加载。实测与限制见 [r32 记录](../reports/r32-native-fill.md)。

以下是独立的旧文件复制 bridge（r30）及 DRM 复制阶段（r31）的说明。

当前启动会话已提供 `/dev/mt-vgpu-copy`，由 `mt_live_copy_bridge` 挂接到已验证的
r28 固定 GPU 上下文。真实复制使用 TQX 和固件完成事件。该独立 bridge 是早期用户态适配步骤，不实现原厂 MUSA/PVR ABI；
当前 DRM 和原生填充由上文的新前端提供。

## 当前会话使用

在 `/opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest` 下：

```sh
make -C userspace
python3 scripts/load-copy-bridge.py
sudo build/userspace/mt-copy query
sudo build/userspace/mt-copy copy /path/input.bin /path/new-output.bin
```

输出路径必须不存在，工具不会覆盖原文件。文件按最多 4096 字节分块，经 CPU 上传、
GPU 复制、CPU 读回后写入输出文件，并核对每块结果。它用于验证完整调用链，
不具备零拷贝能力，也不保证比 CPU 文件复制更快。失败时可能留下部分输出文件。

加载脚本默认只检查。仅当相同的 r28 主模块和自持引用的上下文已经存在、固件正常、
队列空闲时，`sudo python3 scripts/load-copy-bridge.py --load` 才能加载固定且经过
验证的 r30 bridge。它核对保存二进制 SHA-256 与加载模块的 build-id，不执行 GPU
任务、不创建新设备会话，也不自动恢复故障。启动后缺少这些条件会明确拒绝。
未配置开机加载；重启后不能假设此端点仍可用。

## ABI 和资源规则

`../include/mt_copy_uapi.h` 定义实验 ABI 1：

- `MT_COPY_QUERY` 返回最大块长、提交/完成计数、最近 fence sequence 和错误锁存状态。
- `MT_COPY_EXEC` 接收完整 4096 字节源页、完整 4096 字节目标页、两侧偏移和复制长度。
  返回的目标页来自实际显存读取，范围外内容应保持输入值。
- 两个结构没有用户指针或 GPU 地址。序列号输入、flags 和 reserved 必须为零。
  参数先复制到内核并检查；整数越界、空长度和超页范围均在提交前拒绝。
- `/dev/mt-vgpu-copy` 为 root:root 0600；打开及每次 ioctl 都检查当前调用者的
  `CAP_SYS_RAWIO`，继承文件描述符不能绕过权限检查。
- 多进程调用由一个提交锁串行化。等待真实 fence 时释放驱动事件锁；最多等待 5 秒。
  发布后发生 GPU/fence/数据检查错误会锁存 faulted，后续执行返回错误，不自动重置。
- 内核复制并持有用户数据，GPU 不直接访问用户地址。文件关闭不释放已发布的根和
  原上下文；它们继续由原始自持模块拥有。此设计不代表已经完成一般 GPU VM 的销毁。

成功只在 fence 完成、完整源页和完整目标页校验后返回。若 GPU 已完成，但返回用户
缓冲区时发生 EFAULT，ioctl 返回错误，QUERY 的 completed 仍会增加；不要将所有
系统调用错误理解为 GPU 未执行。当前测试已覆盖这一分支。

原始主模块 sysfs 的 `submit_enabled=0 / workload_submit=0` 表示通用提交入口关闭；
bridge 仅在持有驱动锁的提交临界区临时开放它们，随后关闭。接口没有登记 DRM 节点，
新增 DRM 前端的状态不能由原模块这两个字段判断，请读取 DRM QUERY。

## 验证

`mt-copy-check` 是会实际提交 GPU 任务的 root 测试工具。r30 已验证 14 项非法/越权
请求拒绝、4 个独立进程共 32 次正确复制，以及 1 次 GPU 完成后的 copyout EFAULT。
另用 `mt-copy` 完成 65,659 字节文件的 17 个任务，文件散列相同。所有进程退出后
bridge refcnt=0，原 GPU 上下文仍正常。

64 位原生用户态已实测。32 位结构大小、偏移和 ioctl 编号编译验证通过；尚未运行
32 位用户程序验证 compat ioctl。

证据见 `../reports/r30-userspace-copy.md`。对保存证据离线重验：
`sudo python3 scripts/verify-r30-userspace-copy.py`（sudo 仅用于读取 root 创建的 0600 输出文件）。

## r31：直接显存 DRM 接口

当前另有 DRM 驱动 `mtvgpu`，节点 `/dev/dri/renderD128`。它用独立 GPU 上下文和
8 个预映射的 64 KiB 显存槽提供 GEM 句柄、直接 GPU 复制及原生 DRM syncobj。
该接口不通过上面的 `/dev/mt-vgpu-copy` 中转源/目标数据。

```sh
make -C userspace
sudo build/userspace/mt-drm-check /dev/dri/renderD128 query
```

`smoke` 与 `exercise` 模式会实际提交 GPU 任务；本轮已运行并保存结果，无需为查看
状态再次执行。ABI 位于 `include/mt_drm_uapi.h`，支持 QUERY、CREATE、READ、WRITE、COPY，
以及标准 GEM_CLOSE 和 binary SYNCOBJ 操作。COPY 接受本 fd 的两个不同 GEM 句柄，
可执行单次最多 64 KiB 的 GPU 复制，并把真实完成 fence 关联到已创建的 syncobj。
READ/WRITE 每次显式传输最多 4 KiB。句柄关闭回收槽位，下次分配清零整个槽。

当前要求 CAP_SYS_RAWIO，根和上下文在首次提交后长期保留；没有 mmap/PRIME、
OpenGL/Vulkan、原厂 MUSA/PVR ABI 或桌面绘制。DRM ioctl 同步等待完成，尚非异步调度器。
不能在此状态下卸载已自持引用的 DRM 模块；本轮未设置自动加载，重启后需重新核查会话。
详情见 `reports/r31-drm-gem-syncobj.md`。
