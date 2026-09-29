# 静态 PDS/USC 程序初始化与动态池区分

本轮将本机 TQX 的两份静态程序镜像接入 Linux 启动准备，并执行 Windows 原始
生成、复制和释放路径作逐字节对照。验证限于 CPU/RAM；主模块未加载，没有恢复、
Host 操作或 GPU 执行。尚未启用硬件加速。

后续已完成[独立动态池BO/VM接入](reserved-pools-integration.md)，以下保留静态程序
初始化阶段的验证记录与当时限制。程序上传和池内子分配仍未完成。

## 原始程序生成与复制

参考文件 `/opt/MTT-driver-only/mtkm64.sys` 的 SHA256 为
`0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33`。
以下省略函数地址共同前缀 `140`。

分页上下文创建的静态调用关系指向 `01a898(platform+0x30, helper+0x30)`。
本轮从该入口执行原始指令，使用已选定的 family2 设备和 TQX 模块对象：

1. `021888 → 04d758 → 09c7a8` 分派至 TQX 程序获取接口。
2. `0d9518/0fbca4` 实际执行内部静态表自检；未用成功返回钩子替代。
3. `0d93ac → 119250` 生成 20,272 字节临时程序库。
4. `01a898` 向两个不同的共享描述符复制 PDS 和 USC 镜像。
5. `02186c → 04d708 → 09c788 → 0d94d0 → 04567c` 释放临时程序库。

| 目标 | 来源偏移 | 定义字节数 | 镜像 SHA256 |
| --- | --- | --- | --- |
| PDS | `0x4e80` | 176 | `cb73470a4b41652c3c5ad479d794188ff161293df37a86f6a4230c5d01071d01` |
| USC | `0` | 20,096 | `cc7445520a1c297b2d3e5f563251dc77d39a41da9653cb25ebdbfa88583cc2d1` |

这两个目标各有 1 MiB 容量，但本入口只写表中的前缀，其余字节保持原状。
CPU 分配、映射、释放、memcpy/memcmp 边界采用模型。完整 SDK/分页上下文的创建
尚未执行，不能由本入口成功推断其已可发布。

## Linux 接入和验证

`kernel/mt_static_programs.h` 复用已有提取程序库，生成两份独立镜像，限定
family2/transfer1，并拒绝短输出和两个写入区间重叠。不清零目标未定义尾部。
`mt_boot_resources_prepare` 接收实际设备 profile，在原启动暂存数据尾部增加
20,272 字节；原有暂存偏移、共享结构和分配枚举保持不变。失败沿既有路径回收。

`scripts/verify-static-programs.py` 在全零、固定非零和随机三种基线上运行原始
初始化链，与 Linux 的两个完整 1 MiB 输出对照，共比较 6,291,456 字节。
六个无效输入检查通过。独立镜像保存在：

- `build/firmware/static-pds-program.bin`
- `build/firmware/static-usc-program.bin`

新增 `boot_resource_stage_test` 调用真实启动准备函数，覆盖十处分配失败、平台拒绝、
重复准备拒绝、镜像位置、释放平衡及原后备内容保持不变。15组集成检查、ASan/UBSan、
W=1 模块编译和 `mt_guest` ABI 对照通过，结果见 `runtime-integration-build.json`。
本轮模块 SHA256 为 `b36ab8fabb5436fa6203abd100d64f84719ee46b41ca7cb1396490fb3c93c50d`。
本轮未重新运行此前的真实内核 GEM/fence 测试。

## 动态池核查：不能把静态 USC 后备直接当作池后备

继续执行 `01dafc → 01d98c → 01d3c8`，确认适配器 `+0x1060` 指向一个
22项资源池管理器。它在 `01a558` 创建四份静态资源之前，独立分配如下后备：

| 堆编号 | 动态池 VA | 字节数 |
| --- | --- | --- |
| 1 | `0x81ffd03000` | `0x200000` |
| 2（USC） | `0x84fff00000` | `0x100000` |
| 10 | `0xf0ffe00000` | `0x200000` |

地址选择执行了原始 heap plan 和范围分配器；不再手填 USC 池的 VA。
`01dbdc/01dc9c` 的5001字节请求舍入、池填满、额外请求失败、释放合并和整池复用
均执行通过；`01daa8/01d938` 最后释放全部三个后备。OS后备和锁仍是 RAM 模型，
没有构造物理页列表或调用硬件映射。见 `reserved-pools-validation.json`。

此前 `process_resources_reference.py` 的可选列表案例把静态资源9的描述符放到
候选 VA 上，属于人工枚举夹具，**不能证明生产中的 USC 对象身份或完整动态列表**。
本轮确认生产池后备是独立对象；现有 Linux 六项固定共享映射尚未接入这三个池。
静态 USC 模板如何进入动态池及其分配后的地址传递，仍需继续追踪。

## 后续接入边界

新 PDS/USC 数据目前只在 CPU 暂存区，既有硬件 trial 上传列表没有自动增加它们。
需要继续连接三个动态池的 BO/VM 生命周期、静态镜像使用和实际上传读回、上下文
发布及完成事件。生成镜像、建立 CPU 页表和成功编译均不等于 GPU 已执行。

复现命令：

```sh
python3 scripts/verify-static-programs.py
python3 scripts/verify-runtime-integration.py
python3 scripts/verify-reserved-pools.py
```
