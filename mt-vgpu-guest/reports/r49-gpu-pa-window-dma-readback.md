# r49：system-page GPU-PA 窗口转换与 TQX DMA 回读

在用户授权的重启后完成。旧 one-shot 模块和 retained session 随重启清除；
`mtgpu` 初始化仍在 `PhysHeapsInit [644]` 失败，按既有流程只读验证 idle rings，
再由 `mt_cold_disconnect finish=1` 将 Guest 从 2 置为 0。之后加载新主模块：

- 主模块 SHA-256：
  `52c89e90785495569d4b4b116a5f60e728a4ca89f1941ab0ff45469fd6991049`；
  runtime integration 报告散列匹配，首绑 DMA mask 40。
- trial/runtime-context 建立成功，OSID 3，Guest/FW `2/2`，`event_result=0`。
- 空 marker DM1 `sequence=1 result=0`，`pending=0/completed=1`。

## 地址域修正

r48 证实此前 GPU PTE 写入 DMA IOVA 时，Fence 虽成功，VRAM 目标仍为零。只读
解码当前 OSID 3 信息页和 runtime windows 后，确认系统页 GPU 地址必须用
`mt_system_page_address()`：Guest GPA 加有效窗口 bias。将测试 BO 的 PTE page list
改为该 GPU PA；DMA API IOVA 单独保留作同步/撤销。

运行态数据：Guest GPA `0x15edbf000`，DMA IOVA `0x15edbf000`，窗口转换后的
GPU PA `0x895edbf000`（bias `0x8800000000`）。TQX root `0x60100d000` 的只读页表
回读确认 source leaf 为 `0x895edbf001`，即正确 GPU PA 加 valid bit。

## 实际 GPU 回读

一次 256-byte TQX copy 完成：`sequence=2 result=0 verified=1`。CPU 回读源页与
VRAM 目标均匹配，首字 `0xeea55c13`，目标页剩余 guard 保持 `0xa5`；独立只读
BAR helper 再次确认目标 VRAM 内容及 PTE。最终 `pending=0/completed=2`，无
WARN/BUG/Oops；没有重复提交。

同一 retained session 下，PVR bridge 四页 DMA smoke 也通过：主模块引用数
`17 → 18 → 17`。bridge 首次映射日志显示 DMA IOVA `0x1378d4000`、GPU PA
`0x89378d4000`，两者分属不同地址域；PMR 释放后引用配对恢复。

随后 bridge 源码补上了每文件 CPU-only `mt_gpu_vm` 计划：对齐 PMR 的 MapPMR
把 `gpu_pages` 送入 `mt_gpu_vm_bind`，UnmapPMR/文件关闭按序 unbind/destroy；
未上传根页表、未改变 ioctl 的 UMD 可见返回。mapped PMR 的 PmrUnref 现在拒绝
`-EBUSY`，直到 UMD unmap。该版本 `W=1` 编译及 196 项 Python 测试通过，
但尚未加载或真机验证。

## 当前边界与会话状态

当前主模块和 TQX one-shot 实验均 pinned，Guest/FW `2/2`，BusMaster 开启，
`pending=0/completed=2`；不要卸载、解绑、disconnect 或再次提交工作。验证证明
窗口转换后的 system-page GPU PA 可被 GPU TQX copy 读取。最新 PVR bridge 源码已有
CPU-only PMR→VM 页表计划，但当前加载的仍是旧 bridge（Chrome 持有其 render fd，
模块引用非零）；新计划尚未上机。也没有执行 MUSA UMD kick。下一步需在安全
替换 bridge 后验证 PVR PMR page-list 进入 VM plan，再考虑真实 MUSA submission。
