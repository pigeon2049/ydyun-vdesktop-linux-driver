# Paging Command 创建来源与映射纠正

本轮修复进程固定共享资源中一项实质错误：`platform+0x138` 指向的是 **4 MiB
Paging Command**，其 VA 来自资源3，不能使用资源7的8 KiB Paging Context代替。
当前代码、原始指令验证和内核 RAM 测试均已修正。未加载主驱动，没有硬件恢复或 Host 操作。

**后续分配器纠正**：`022f38`已进一步执行到系统池和逐页GPA转换。
原先普通BAR显存后备已替换为系统RAM及scatter页表，见 [系统RAM后备证据](system-memory-path.md)。
下文保留上一阶段的大小/VA纠正及当时验证记录，不能把当时的后备模型视为真实分配器证据。

## 原始路径证据

- `00de94` 的反编译调用传入 `adapter+0x790` 作为 `01a558` 的资源计划；
  `01ccd8` 的原始指令生成该计划。
- `01a558` 实际执行 `019cc4 → 019c5c/01e1e4` 创建PDS、USC、YUV、Kill，
  随后创建Fence及Paging Command。后者分配参数固定为`0x400000`、tag`0x49474150`、flags`1`。
- Guest配置字节为1时调用`022f38`；字节为0且`0230b4`返回0时调用`022218`。
  两条分支均在模拟器中执行。分配函数本身为OS/物理页模型，未据此确定实际OS后备策略。
- `01a558` 将分配描述符存入`platform+0x138`，将`plan+0x70`存入`platform+0x140`。
  后者等于`adapter+0x800`，对应资源3的VA **`0x81ff800000`**。
- `014d58 → 00a630 → 009258` 从这些真实创建结果取得大小/VA/页列表，
  逐页发出flags0的映射，`018bd0`在页表插入边界截获。

此前7,188次页面调用的报告只验证了枚举与所提供描述符一致；测试输入错误地用资源7
构造了末项描述符。该报告不能证明资源创建来源，现已由更完整的创建→映射验证替代。
当前共16种组合（2条分配分支×8种可选资源组合）、30,728次页面调用；其中8组与Linux
绑定器逐范围、逐页面比较。PB创建、USC延后放置和可选缺失状态仍由模型提供。

另外，创建函数在模型提供的`0x5a`后备上，保持PDS/USC/Fence/Paging Command全部字节。
这只能说明本次执行路径没有改变这些内容；不能推导真实OS初始内容、后续填充或GPU准备完成。

## Guest 代码修正

`mt_process_resources.h` 使用资源3的VA和完整4 MiB长度。`mt_boot_bo.h` 接受独立的
Paging Command块，初始化时拒绝8 KiB替代品，仍通过共享BO视图管理多个VM的引用。

当时主模块从普通显存池另行保留4 MiB；后续查明Guest使用系统RAM，该实现已被
`mt_system_memory_prepare/fini`替换。对象仍存于外层`mt_guest_device`。
现有8 KiB Paging Context继续独立保留；没有扩大ABI敏感的`struct mt_guest`或启动数组。
分配/映射不代表已清零、上传、发布或完成初始化。

完整九个TQX任务对象+六个共享对象需要**18页**页表。内核RAM测试曾因旧16页容量
正确返回`-ENOSPC`，随后补入明确的容量不足回归，再使用32页容量验证完整组合。
测试同时保留独立8 KiB块，验证它不被当作Paging Command导入且内容不变。

## 验证结果

- `python3 scripts/verify-process-resources.py`：16组原始创建/映射，8组Linux对照，全部通过。
- `python3 scripts/verify-runtime-integration.py`：13组运行集成检查、ASan/UBSan、W=1编译及共享ABI对照通过。
  主模块SHA256：`c3104947ff8b4c182a39b29e072326b4f609ca50f44c808af85da4b430b70c28`。
- `python3 scripts/verify-gem-kernel.py --kind gem --run`：**227项**真实内核RAM检查通过，
  普通后备18分配/18释放，启动测试后备9分配/9释放。测试模块已卸载。
  测试模块SHA256：`2c3cdccfb0dd240bed71ad58fd68e3e3191b3b9989c585fcc3085557099c4c0e`。
- 测试前后PCI未绑定、主模块未加载；仍只有QXL card0，没有MTT render节点。

机器结果见`process-resources-validation.json`、`runtime-integration-build.json`和
`gem-kernel-validation.json`。当前仍未获得硬件加速。下一步需要继续追共享资源实际填充、
上传及准备状态，再衔接进程/上下文发布；本轮不改变工作准入。
