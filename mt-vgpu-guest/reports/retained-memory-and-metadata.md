# 保留内存、管理库重置与更新元数据核查

2026-09-28，仅操作 Guest。当前没有 MTT 硬件加速，固件仍为 Guest1/FW1、started=0、DM0 5/0。

## 六个已上传范围均未损坏

新增临时只读模块 `kernel/recovery/mt_retained_dump.c`。它在现有主模块的设备锁/会话锁下，
核对设备身份、Guest 状态和新查询的内存布局，临时映射已知六段，读取后立即撤销映射。
没有注册新固件地址，没有写 MMIO/VRAM，没有替换或停止通信模块。
导出完成后已正常卸载，保存 `build/recovery-channel/retained-six-ranges.bin`。

| 范围 | 字节数 | 比较结果 |
| --- | ---: | --- |
| 完整固件 | 8,388,608 | 与失败现场完整镜像一致 |
| GPU 页表 | 24,576 | 与已验证 bootstrap 数据一致 |
| 辅助页 | 4,096 | 与参考 0xba 填充值一致 |
| 默认映射页 | 12,288 | 与已验证 bootstrap 数据一致 |
| YUV 常量及保留填充 | 524,288 | 与参考定义写入及原始备份填充一致 |
| DM Kill 程序及保留填充 | 524,288 | 与参考定义写入及原始备份填充一致 |

合计 9,478,144 字节，无不同字节。对实际读回页表的软件遍历也通过全部 2,048 个页面：
VA 0xe1c0000000 起的 8 MiB 映射到 GPU PA 0x771fef000，root PA 0x605800000，
有效位和参考使用的 bit62 均存在。

这些验证证明保存的字节和软件地址翻译一致，**不证明 Host 内部映射有效或 GPU 已使用这些页表**。
采集不是六段内存的硬件原子快照；采集前后状态均为 Guest1/FW1。

复查：`python3 scripts/verify-retained-memory.py`。
证据：`retained-memory-validation.json` 和 `build/recovery-channel/retained-six-ranges-capture.json`。

## 这份 MTML 的 reset 不是可用设备重置入口

对固定 SHA256 的 Windows `mtml.dll` 解析 RTTI/vtable，而非仅按导出函数名称判断：

- `mtmlDeviceReset` 经 `mtml::Device` vtable +0x2a0 到 `18004c170`，再经
  `GuestDevice` +0xb8 到 `18007e640`。
- `mtmlGpuReset` 经 `mtml::Gpu` +0x48 到 `180055160`。五类 GPU feature vtable 的
  +0x60 均指向 `1800afc60`。
- 上述后端的可继续分支经 `180083ed0` 到 `18008aaa0`；该叶函数仅返回内部错误
  5 或 6，没有设备操作。用原始指令离线执行两个分支，再执行错误映射函数，得到公开
  错误 2（访问驱动失败）或 4（Not Supported）。
- 上层 type=4 的提前拒绝与后端叶函数分别核实；没有把 type=4 未完全解析的语义
  用作本机 Guest 类型的依据。

这只覆盖当前参考库，不能断言其他版本或所有内核重置路径均不存在。
复查：`python3 scripts/audit-mtml-reset.py`；证据：`mtml-reset-audit.json`。

公开 [GMI v2.2.0 发布说明](https://docs.mthreads.com/gmc/gmc-doc-online/gmi/releasenotes/gmi_releasenote/)
描述了 reset 功能调整，但这是另一版本工具的说明，不能据此认定当前 Windows 库已有
可移植实现。本轮检索未取得匹配本机的完整 Linux Guest 包；官方 vGPU 两页直接抓取超时，
不把搜索无结果解释为软件包不存在。

## 更新元数据已实测，未下载/安装

从 `1400266e0` 还原两条查询：type=0/subtype=5 查询更新开关，非零后再以
type=0/subtype=6、value=0x48809490d 查询相对当前参考包的更新。
3 个原始指令模型案例验证了此顺序；参考中的后续下载函数仅记录调用，未执行。

新增 `mt_package_probe` 临时模块复用现有 RPC 页和互斥锁，只发送这两条元数据请求，
期间可以继续回复宿主统计请求。结果：enabled_result=0、enabled_value=1，
package_result=0、package_value=0。按参考控制流，此结果不进入下载分支。
该值不能独立证明 Host 精确版本或全部固件 ABI 兼容。
模块已正常卸载，未下载包、未执行安装程序或更改现有固件。

复查离线语义：`python3 scripts/verify-package-metadata.py`。
证据：`package-metadata-oracle.json`、`build/recovery-channel/package-metadata-hardware.json`。
两个临时模块均 W=1 编译通过，无编译警告。原主模块保持运行，未重置一次性通知标志。

## 后续方向

当前证据不支持再次上传相同数据或重复同一通知来解决问题。仍需要定位固件执行前
未满足的内部条件，或找到能从 Guest 完成的正式映射重建/重置协议。Linux 运行上下文、
DRM 与用户态加速也尚未完成，目标未达成。
