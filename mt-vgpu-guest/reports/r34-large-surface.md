# r34：1080p 原生 GPU 填充与 MiB 级显存复制

2026-09-30，本机已完成 1920×1080 原生 GPU 矩形绘制。新前端新增 25 次填充、
4 次复制，旧前端回归 1 次填充和 1 次复制，共 31 个任务全部完成；累计
completed=262、pending=0，Guest2/FW2/started1/event_result0。
仍未实现 OpenGL/Vulkan、PVR bridge、MTT 显示扫描输出或桌面加速。

![GPU 原生绘制后从显存读回的 1920×1080 图像](../build/r34-live/gpu-native-1080p.png)

这张图由 23 次 GPU 原生颜色填充生成。随后分两次 GPU 复制覆盖全部 8,294,400
字节画面，并逐字节检查源、目标及保护区；图像从最终实际显存读回数据导出。
测试程序不使用 WRITE ioctl 上传像素。每次填充均读回整个 8 MiB 对象并验证真实
syncobj/sync_file，覆盖画面尾部未使用区域；没有用 CPU 绘图冒充 GPU 绘制。

## 显存扩容与运行态

加载前普通堆剩余 15,224,832 字节，无法同时容纳两个 8 MiB 画面。新前端采用
一个 8 MiB 槽、一个 4 MiB 复制槽、六个 64 KiB 槽，并为槽分配不重叠的 16 MiB
间隔 GPU VA。原普通堆未扩容，固件堆未借作画面存储。加载后剩余 2,101,248 字节。

QUERY.slot_bytes 返回当前可支持的最大对象 8 MiB。CREATE 根据请求选择最小的
可用合适槽；先申请六个小对象后仍能创建两个大对象。没有空闲合适槽时返回 ENOSPC。
成功创建的 GEM 大小决定 ioctl 的访问边界，关闭再租用时清零整个物理槽。
公开像素格式仍是紧密排列的 32 位线性像素。

`mt_tqx_fill_work.h` 将完整表面上限从 64 KiB 提至 8 MiB；原有 BO 范围、页表权限、
物理地址别名、工作区和输出隔离检查继续执行。上传阶段仍只写命令/程序/描述符，
不写目标像素。COPY/FILL 继续同步等待真实硬件 fence，失败锁存、不自动复位。

新模块 `mt_live_surface.ko` 使用独立名字保留旧自持模块，当前注册在
`card3/renderD130`，GPU process token=3。原 r31/r32 节点及 QXL 显示继续存在。
所有接口均要求 CAP_SYS_RAWIO；没有 mmap/PRIME、原厂用户态 ABI、异步调度器
或根撤销协议。模块在首次封存根时自持引用，不能直接卸载。

实际模块 SHA-256：
`fefc8b5957eb6a9999d74c82aa667bac00647af1d25af95990dddbe204ae0d6d`。
Build ID：`3962d291ed95b17df92f870d89315cf6c1776612`。
启动 ID：`fe18deeb-d431-4c03-ac88-34235e9849c5`。

## 真机核验

新根 PA=`0x60617b000`，容量 32 页、实际使用 26 页，20 个映射范围。
首次提交前读取实际页表，6501 个叶页全部核对：3172 个私有页匹配 BO 物理后备，
3329 个共享页与既有上下文一致，PTE flags 均为可写默认值 1。

- sequence 232–234：跨 2 MiB 页表边界填充、8 MiB 缓冲最后一个像素、4 MiB 复制。
- sequence 235–259：23 次完整/局部 1080p 填充、两次画面复制，包含最右下角单像素。
- sequence 260–261：切回 r32 完成原填充与复制。
- sequence 262：返回新上下文完成小对象复制，核验文件隔离、参数边界和槽释放。
- 两轮大表面测试验证无可用槽与越界填充拒绝，均未增加提交计数；关闭重建后完整
  8 MiB 清零检查通过。旧填充工具另验证 13 个无效请求拒绝。

提交前后新根逐字节相同，旧 r28/r31/r32 三个完整根也均未变化。新根 SHA-256：
`d0dda36728a629e4ff7e6c69ce3ff184feff0ba61f445874f9790badb9296e0a`。
只读快照模块已卸载。普通运行模块和上下文保留；没有访问宿主、执行 MPC HWR
或添加开机自动加载项。

代码通过 W=1 编译、运行时集成检查与保留主模块 ABI 对照。颜色/几何编码沿用
r32 原始 Windows 指令验证过的实现；r33 又完成 80 例 Linux/Windows 描述符转换
对照，但该转换器尚未接入这个真机前端。

## 复现与保存

```sh
cd /opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest
sudo python3 scripts/check-graphics-session.py       # 只读当前身份/能力/空闲状态
sudo python3 scripts/verify-r34-large-surface.py     # 只核对保存的证据
# 下列命令会实际提交 GPU 任务，输出文件必须不存在：
sudo build/r34-live/mt-surface-check /dev/dri/renderD130 smoke
sudo build/r34-live/mt-surface-check /dev/dri/renderD130 exercise /path/new-image.ppm
```

二进制、原始读回 PPM/PNG、每次任务记录、四个根、内核日志、源文件散列和源码
快照位于 `build/r34-live/`；汇总为 `reports/r34-large-surface-validation.json`。
检查程序不保证重启后节点名相同，必须先核对真实会话和能力。

后续重点是图形上下文/着色状态、标准用户态接口和可用应用接入。当前真机路径证明
大表面填充/复制与真实同步已可用，不能等同于完整 Linux 图形驱动已经完成。
