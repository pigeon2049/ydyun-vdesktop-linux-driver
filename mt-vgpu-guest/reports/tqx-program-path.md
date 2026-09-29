# TQX 程序库与复制 job 参数

本轮把 TQX 的复制几何推进到预编译程序和 job 状态构造。新增
`kernel/mt_tqx_program.h`、可重复提取的 `kernel/mt_tqx_program_data.h`，
以及 GEM 内部 CPU 程序库暂存接口 `prepare_tqx_programs`。
**仍未上传这些程序或向 GPU 提交 job，硬件加速没有完成。**

后续已补齐 [源纹理描述符与 sampler](tqx-texture-path.md)。本报告对应的 192 组
job 验证现已执行真实源编码回调，不再用固定返回值替代源描述符索引。

## 本机模板来源与程序库

传输 handler 初始化 `1400d9340` 调用 `14004c2fc`，检查库 family 是否大于 3。
本机 family2 选择 PDS 表 `140b9e850` 和传输程序表 `140b9ea30`；另一个
`140ba00e0` 表属于不同分支，未混用。

`140119250` 拷贝 121 个传输模板和 10 个 PDS 模板，分别按 32 和 16 字节
对齐每段程序，并保存各段在所属库内的偏移。CPU 暂存库如下：

| 内容 | 字节数 | CPU 镜像偏移 |
| --- | --- | --- |
| 121 项传输程序表对应的程序数据 | 0x4e80（20096） | 0 |
| 10 项 PDS 程序表对应的程序数据 | 0xb0（176） | 0x4e80 |
| 合计 | 0x4f30（20272） | — |

提取脚本校验参考 PE 固定哈希，保存每条元数据、参数重排表、PDS 初始状态与程序哈希。
对齐空隙在 Linux 镜像中清零。镜像 SHA-256：
`3ba48c581e69d0732ef83ea217aa0c2d6ecc53e3b45909caf7ec61a6756fe925`。
这些是从本地原厂二进制提取的机器码，不是恢复出的源代码，也不宣称所有表项都是
已验证可执行的工作负载。当前 Linux 程序库入口仅允许已追踪的 family2/transfer1。

**CPU 连续镜像不等于一个 GPU 程序分配。** 后续 `14012369c` 将 shader 和 PDS
分别分配到不同用途的堆，`140123a70` 分开映射、复制、解除映射。family2 的 shader
分配还包含设备信息 `+0x100`（设备对象 +0x470，为 0x20）的额外空间，并向 0x80
对齐；这一步和地址域对应关系尚未接入 Linux，不能把 CPU 镜像作为单块直接上传。

## 三种复制操作的 job 状态

`140113c08` 选择程序并组合参数。三个已还原的复制操作如下：

| operation | 程序库内偏移 | 程序字节数 | 输出字段 | 常量 DWORD 数 |
| --- | --- | --- | --- | --- |
| 8 | 0x1c0 | 56 | 2 | 5 |
| 0x10 | 0x4c0 | 56 | 2 | 5 |
| 0x15 | 0x620 | 60 | 4 | 5 |

同尺寸、无变换复制的 `1401169fc` 先形成 `{source_index, 1.0, 1.0, 0, 0}`，
`140114290` 根据模板 `{1,3,2,4,0}` 重排为 `{1.0,0,1.0,0,source_index}`。
Linux 用 `0x3f800000` 位模式表示 1.0，没有在内核使用浮点运算。

三种操作都选 PDS 模板 4（代码偏移 0x50）装载常量、模板 9（代码偏移 0xa0）
执行程序。已还原 `14011ccdc/14011d0f8/14011b7f0` 的 16 字节常量装载状态、
`14011e5bc/1400fbab8` 的 36 字节执行状态，连同 20 字节重排常量形成 CPU 输出。
常量装载状态含常量 GPU VA 和 `0x98000005`；执行状态含 shader 专用堆内地址和
输出字段。地址加法、40 位常量 VA 范围、容量与对齐均检查，失败不改写输出。

三个输入地址量意义不同，API 显式区分：

- `constants_va`：常量数据的 GPU 虚拟地址。
- `shader_heap_base`：shader 分配在专用堆内的 32 位相对地址，不是 BO 物理地址。
- `source_descriptor_index`：源纹理描述符的编码索引，不是源数据 VA。

`1400a2cb4` 已确认分配器分别返回 CPU 指针、GPU VA 和堆相对值；后者来自
`1400a21f8` 的资源 VA 减专用堆基址。Linux 尚未构造该堆映射，因此状态构造函数
仍是内部 CPU 组件；没有把普通 BO 地址假装成这些地址，也没有开放该接口给用户态。

## 验证

`scripts/verify-tqx-programs.py` 在限定函数范围的 RAM 模型中执行：

- 真实 family 选择和 `140119250`，131 项库内偏移及完整 20272 字节与 Linux 一致。
- 192 组随机地址、描述符索引及矩形尺寸，执行真实 `140113c08`、参数构造/重排、
  PDS 状态拷贝与补丁；逐字节对照全部常量和状态，执行真实目标发射，并对照 80 字节目标命令。
- 9 个无效状态输入拒绝案例，以及程序库平台、容量和输出尾部保护。

分配器、地址查询、初始命令状态及目标发射之后的收尾仍由模型提供，
它们没有被本测试证明正确。程序库分配与 memcpy/栈 cookie 也为普通 CPU 模型。
源码的 ASan/UBSan 检查、W=1 主模块构建和既有 `mt_guest` ABI 对照通过。
真实内核 GEM/RAM 测试随后增至 63 项，临时模块已卸载。主模块仍未加载，无 GPU 执行。

复现：

```sh
python3 scripts/extract-tqx-programs.py
python3 scripts/verify-tqx-programs.py
python3 scripts/verify-runtime-integration.py
python3 scripts/verify-gem-kernel.py --kind gem --run
```

证据：[提取索引](tqx-program-extraction.json)、[原始指令对照](tqx-program-validation.json)、
[集成构建](runtime-integration-build.json)、[本机进度](progress-2026-09-28.md)。

## 下一段

源纹理、sampler 及目标命令已经补齐，见 [组合 job 与目标发射](tqx-destination-path.md)。
剩余为专用堆地址分配/映射、初始状态、收尾记录和完整命令流。
当前 CPU 对照不能代替这些步骤或真实 GPU 执行。

后续 [真实构造至收尾](tqx-stream-path.md) 已覆盖新建对象的常量 type4 分配，
这里的零初态/type1 编码夹具不用于推断真实分配类型。
