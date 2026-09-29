# TQX 软件记录到 DMA 描述符

本机 family2/transfer1 的 CPU 编码已接入 GEM `encode_tqx_dma`。
新增 `kernel/mt_tqx_dma.h`、`kernel/mt_tqx_engine_state.h`。尚无真实 GPU 执行，
DMA/引擎状态 BO 的分配、映射、上传和异步任务引用仍未接入。
本阶段未做恢复、重置或 Host 操作。

## 原始路径

固定 Windows 参考 SHA256：
`0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33`。
地址均属于 `/opt/MTT-driver-only/mtkm64.sys`，伪 C 与汇编已保存在批量反编译库。

`140093d88` 生成长度 `0x50` 的传输提交 metadata：mode=1，`+0x10`
指向命令对象指针数组，`+0x18` 指向 DMA 布局对象，`+0x20` 是独立引擎状态地址。
设备虚表 `1403c4d40+0x1b0 → 14004d7bc` 根据队列 type5 和 metadata 长度
分派到 `14004e924`。该函数使用 `14004d174/14004d188` 读取页列表与软件记录。

模块虚表 `140603ff8+0x98 → 14009c3e8` 计算布局；`14004cc80` 初始化设备
布局常量，`140052020` 把 64 字节 shape 写入布局对象 `+0x18`，对象
`+8/+0x10` 分别为 CPU/GPU 地址。真实分配器为 `140052328`。

`14011d7b8` 导出的最终提交视图为：DMA GPU 地址加 header offset、总长度减
header offset、分配器属性标志。**提交地址是 DMA 分配区，并非 TQX 命令页根地址。**
独立分页函数 `14001a064` 不是这条正常队列提交路径，不能混用其记录格式。

## 当前支持的布局

限制为一个新完成的软件记录、一个命令页、1～4 个复制块、header offset=0，
无额外同步或 instrumentation。未知可选字段拒绝处理，失败不修改输出。
核心数目前是显式参数，范围 1～8，尚无本机拓扑验证。

| DMA 字段 | 值或来源 |
| --- | --- |
| `+0x1c` | 类型 `0x67` |
| `+0x28/+0x2c` | section 偏移 `0x48`、有效 section 数 1 |
| `+0x48` | section 类型 3；布局仍保留三个 section 槽 |
| `+0xa0` | 每条记录跨度 `0xe8`，跨度字段位于 `+0xa8` |
| `+0x148` | 软件记录 `+0x120/+0x124` 的两 DWORD |
| `+0x160` | TQX 命令页 GPU VA |
| `+0x168` | DMA VA + `0x200`，每核心页计数区 |
| `+0x170` | 独立传输引擎状态 VA，不能填 PDS state VA |
| `+0x178` | 软件记录 `+0x30` 的 16 字节硬件参数 |
| `+0x200 + 128*core` | 当前命令页的精确字节数 |

总长度为 `align128(0x200 + cores*128 + 0x1020)`，1～8 核分别为
4864、4992、5120、5248、5376、5504、5632、5760 字节。
Linux 为 DMA 预留 8192 字节；未写入区域清零是 Linux 策略。
`submission_view` 为 24 字节，前两个 qword 是 DMA VA 和有效长度。

输入需满足普通堆范围、整页对齐及 DMA/状态/命令区不重叠。
本接口只验证数值和记录一致性，**不证明这些 VA 已映射或对象已被任务持有**。
现有七对象程序上传接口也不会自动创建这两个新资源。

## 独立引擎状态区

TQX 队列构造 `14009394c` 使用虚表 `1405f7a50`，调用 `140093aa8`。
该分配函数在 family2 请求 `cores*0x180` 字节、`0x80` 对齐、usage0，
属性 `0x10100`；存在物理堆1时优先 `[1,2]`，否则 `[2]`。
`14004c490` 记录资源、offset=0、size 和 alignment 到队列 `+0x148`。
family2 的队列 `+0x168` 额外区域偏移仍为0，不能套用 family>3 的额外 `0x120` 字节。

`140093cc8 → 14004c288 → 14004b2bc` 取资源 VA 加偏移，要求128字节对齐，
最终只保留40位地址。参考函数会截断高位，Linux 规划器先拒绝越界地址。
Linux 侧为 1～8 核预留独立4096字节页，记录所需有效长度和参考对齐要求，
由 DMA 编码器复用此检查。

原始 `140093aa8` 未直接写状态内容；分配边界 `14005aec0 → 14005af2c`
继续经设备虚表 `+0x170` 调用。因此当前验证不能证明初始内容应该全零，
也没有因此上传零页或启用状态区。这个问题需要继续沿分配器和消费者核对。

后续已确认核心数来自 Guest 信息页 `+0xc9c`，并通过原始指令验证，
主模块现在自动安装该值。详见 [核心数来源与接入](tqx-topology-path.md)。
本报告上文关于显式核心数的描述适用于底层CPU编码接口。

## 可复现验证与范围

- `python3 scripts/verify-tqx-dma.py`：144 组完整复制→真实序列化→提交视图
  逐字节一致；8 组原始函数栈污染检查；28 个拒绝案例保持输出不变。
- `python3 scripts/verify-tqx-engine-state.py`：64 组完整分配请求/属性/资源包装
  对照、32 组分配失败、32 组非零偏移 getter、1 组高位截断及11个 Linux 拒绝检查。
  只模拟分配器成功/失败和资源 VA，实际执行分配策略及地址 getter 指令。
- `python3 scripts/verify-runtime-integration.py`：11组回归，相关 ASan/UBSan、
  无警告 W=1 构建和保留模块的 `struct mt_guest` ABI 对照通过。
- `python3 scripts/verify-gem-kernel.py --kind gem --run`：119项真实内核 RAM
  检查通过，临时测试模块卸载；不访问 GPU。

机器可读结果见 `tqx-dma-validation.json`、`tqx-engine-state-validation.json`、
`runtime-integration-build.json` 与 `gem-kernel-validation.json`。
下一步是查明状态初始化，再把两个新 BO 与现有七对象一起做 VM/物理别名
核查和上传，复用已有工作任务引用及 fence 完成释放机制。
