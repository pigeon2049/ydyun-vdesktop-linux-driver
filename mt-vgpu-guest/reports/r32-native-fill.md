# r32：Guest 原生 GPU 矩形填充与显存读回

2026-09-30，在 r28 恢复成功后的同一设备会话继续适配。当前已经有真正的原生
GPU 2D 填充：62 次矩形填充、10 次新前端显存复制、1 次旧前端复制全部完成，
驱动累计 completed=231、pending=0，Guest2/FW2/started1/event_result0。
仍未提供 OpenGL/Vulkan、Mesa 接入、显示扫描输出或桌面加速。

![全部像素由 GPU 原生填充产生的读回图](../build/r32-live/gpu-native-fill.png)

这张 128×128 图由 21 个原生颜色填充命令绘制，再经 GPU 复制到第二个 GEM，
从第二块显存读回导出。工具逐字节核对整块 64 KiB，不是把 CPU 绘好的图片上传后截图。
图像显示在文档中也不代表 MTT 已经驱动桌面显示。

## 反编译与原始指令对照

Windows `mtkm64.sys` SHA-256 为
`0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33`。
沿原生 clear 路径执行 `14010ae38`、`14011e4c4` 的 vtable+0x568，
进入 `1400dc430`；复制入口是另一个回调 `1400dc638`。

填充命令长 76 字节，内含 4 个已打包的颜色 DWORD；不分配源纹理和复制着色器参数。
只需要初始 PDS 状态，最后控制字在流结束时由 0x25 改成 0x2d。
矩形裁剪与表面行宽独立，最终记录的精确长度为 76、32 字节对齐长度为 96。
原始分配器仍预留 20 DWORD 的窗口，不能把游标返回值错误地减成 76 字节。

`verify-tqx-fill.py` 在受限 RAM 中执行原始 x64 指令：220 组填充命令、初始状态、
记录和完整 DMA 均逐字节相同，覆盖 1/2/4/8/16 字节元素、边界和随机矩形，
15 项无效输入被拒绝。公开接口目前仅开放实机验证过的 32 位像素。
原有复制流与 DMA 对照回归、15 项运行时集成检查及 W=1 构建通过。

## DRM 接入

新增 `DRM_IOCTL_MT_FILL`（私有编号 5），查询结构原保留 DWORD 返回能力位：
COPY=1，FILL=2。填充使用 GEM 句柄和已有 binary syncobj，内核选择地址和命令。
支持严格位于表面范围内的矩形、4 字节对齐的表面起点和紧密排列的 32 位像素。
没有用户原始命令或 GPU VA。全表面必须落在对象内，每槽最多 64 KiB。

目标 reservation 增加真实 WRITE fence，syncobj 引用原主模块固件事件生成的 fence。
提交仍串行等待，超时最多 5 秒；故障后锁存，不自动复位。
打开和私有 ioctl 要求 CAP_SYS_RAWIO。填充上传路径只写命令、程序、PDS 和 DMA，
不写目标像素；任务准备时持有完整 VM 的 GPU pin，发动机状态不重新清零。

已加载的 r31 模块持有封存根，不能替换。因此 r32 使用独立
`mt_live_graphics.ko` 注册 `mtvgpu 0.2.0`，当前为 `card2/renderD129`，进程 token=2。
r31 的 `card1/renderD128` 和原复制 bridge 保留，QXL `card0` 仍负责显示。
新模块和主模块一样保留到真正的根撤销机制实现或设备会话结束。

实际加载模块 SHA-256：
`f37057d399405e7ee8b9bdc2c5320bf10027aee947227e32d27618c934b22ef5`。
本轮没有安装启动项、覆盖系统驱动、访问宿主或执行 MPC HWR。
多个 DRM 前端挂在同一个 PCI 设备上，注册时出现一次 debugfs 目录重名提示；
设备节点及 ioctl 正常，当前前端并存属于保留旧根的实验安排。

## 真机验证与页表

- smoke：1 次 16×16 填充 + 1 次显存复制，sequence 159–160。
- exercise：40 次填充 + 1 次复制，sequence 161–201。覆盖满 128×128、裁剪、右下角
  单像素、67×109/offset=4、最后一行/列、页尾以及 16384×1/1×16384 跨页。
- demo：21 次填充 + 1 次复制，sequence 202–223；所有像素与记录中的矩形和颜色一致。
- 切换回 r31 完成一次复制 sequence 224，再切回 r32 完成 7 次复制 sequence 225–231。
- 3 轮共拒绝 39 个无效填充请求，拒绝前后提交计数一致。每次成功均核对整个对象和保护区，
  通过 native syncobj 等待、sync_file 导出/poll 和固件 fence 名称、状态、非零时间戳检查。

只读快照 helper 已卸载。新根 PA=0x6060d7000，18 个页表页、20 个绑定、3461 个叶页；
132 个私有页逐页匹配实际物理后备，3329 个共享页与旧根一致，全部标志 1。
r28 和 r31 完整根页表逐字节未变。新根 SHA-256：
`2d260f92fdb369c814d93ac697e1493e3b267d8566166910f7c722ad1f10125f`。

## 复核与后续

```sh
sudo python3 scripts/check-graphics-session.py  # 身份/能力/空闲状态检查，不执行任务
sudo python3 scripts/verify-r32-native-fill.py # 只读保存的证据
python3 scripts/verify-tqx-fill.py              # 只在 RAM 中运行原始指令
```

数据在 `build/r32-live/`，完整结果在 `r32-native-fill-validation.json`。
原主模块的 `drm_registered=0` 和 `gem_objects=0` 没有统计新增前端，
`render_ready=0` 仍是旧通用渲染入口的状态。当前原生填充能力应通过 DRM QUERY 的
FILL 位及真实 fence/像素结果判断。

下一步需要扩大显存对象/表面规模，继续还原图形上下文和用户态接口，完成 Mesa 或其它
可用用户态接入；原生 fill 通过不能代表三角形、着色、OpenGL/Vulkan 或桌面已经可用。
