# Guest Paging Command 的系统 RAM 后备

原始`022f38`内部确认，Guest Paging Command来自系统非分页池，逐页调用
`MmGetPhysicalAddress → 021d90`，不是普通BAR显存池。上一阶段已修正4 MiB大小和
`0x81ff800000` VA，但分配器仍是模型，因此当时新增的BAR后备不能作为Guest正确实现。
本轮已替换为系统RAM，接通地址转换、非连续页面、BO引用和进程VM页表。
没有加载主模块、硬件恢复或Host操作，尚未获得GPU加速。

## 原始指令证据

`verify-system-memory.py` 实际执行：

- `026390`：按保存的设备信息生成Guest窗口，保留原始内外对象相差8字节的关系。
- `022f38`：分配`0x30 + page_count*8`描述符，再分配按页取整的RAM。
  每页调用物理地址查询并执行`021d90`，结果写入描述符`+0x30`开始的页列表；
  描述符`+0x10`指向列表，`+8`保存请求的原始字节数。
- `021d90`：Guest系统页路径使用窗口的系统地址基值（inner`+0x4e0`）。
  info flags bit1已置位时不再加`info+0xca8`；否则bit6已置位时加这个偏移。
  本机保存的信息页flags=`0x3d1`，窗口基值=`0x8000000000`，额外偏移=`0x800000000`，
  因而系统页GPU PA为 **Guest GPA + `0x8800000000`**。
- `023044`：先释放CPU缓冲区，再释放描述符。

模拟边界只替代Windows池分配/释放及物理地址查询。它提供打乱顺序、每页间隔8 KiB
的GPA，实际地址转换循环执行原始指令。12组组合（3种flags×4种页面数）对照3,132页；
另有两处分配失败、非整页请求向上取整和释放顺序检查。原始缓冲区保持模型给出的
`0xa5`内容；Linux清零策略是本地选择，不由此声称Windows会清零。

## Linux 接入

- `mt_system_address.h`：解析已核对的信息页/窗口，只支持无需寄存器往返的系统页转换。
  拒绝BAR2/BAR4地址、未对齐地址、零页和40位溢出。需要原始`+0x80/+0x88`寄存器
  翻译的配置明确返回`-EOPNOTSUPP`，不会擅自操作Host。
- `mt_system_memory.h`：`kvzalloc`提供稳定CPU缓冲区，兼容kmalloc/vmalloc两条后备路径；
  根据每页的真实Guest物理地址构造不可变GPU PA列表。任意分配或页面转换失败完整回收。
  这一步不发布GPA、页表根或任务。
- 主模块`prepare_resources`使用新系统RAM准备接口替代上一轮的4 MiB显存分配。
  所有者仍在外层`mt_guest_device`，不改变共享`struct mt_guest` ABI。
- `mt_bo_system_borrow`与BAR BO使用同一会话锁/引用管理器；映射返回普通RAM，读写使用
  普通`memcpy`，不使用BAR I/O访问。借用视图不释放原后备，最后一个VM/GPU引用消失后
  才允许所有者退出。模块失败清理/正常退出在已确认无视图后释放系统RAM。
- BO可携带不可变4 KiB PA向量；VM与MMU构造器按实际页地址写PTE，支持非零BO偏移。
  完整预检覆盖末页错误、所有页与整块页表后备的重叠；失败不改变已提交镜像、引用或上传标志。
  原连续映射调用接口保留，已有固件页表对照回归通过。

页表本身仍要求物理连续后备；通用复制的连续物理范围解析遇到scatter BO明确拒绝，
避免把首地址加长度当成非连续物理区间。当前系统RAM用于共享Paging Command，
并未宣称所有任务对象、所有内存类型和通用复制后端都已支持scatter。

## 验证

- 原始系统RAM分配/转换：12组、3,132页；Linux页表独立遍历全部页面并测试偏移切片。
- 非法地址8组、非法scatter末页/页表重叠4组；失败输出保持。
- 14组运行集成测试，包括新增系统后备失败回收；ASan/UBSan、W=1编译、共享ABI对照通过。
- 固件连续页表回归：38组稀疏树、129页原始页表对照，3组dummy和14组非法输入通过。
- 进程资源映射回归：16组创建/枚举、30,728次页面调用通过；该枚举测试的后备仍建模，
  系统后备身份以本报告的实际`022f38`执行为证据。
- 真实内核GEM/RAM：**235项检查**。实际分配系统RAM，逐页遍历1,024个PTE，
  跨页写入/读回，GPU持有时CPU读写拒绝；VM关闭后系统BO继续存活，末次引用释放后清空缓存。
  普通RAM对象18分配/18释放，启动测试后备对象9分配/9释放。
- 真实内核RAM fence回归：**321项检查**、2次回调。两种测试模块均卸载。

主模块SHA256：`2958575e626a52bdf6917611b66cb20f9e5755892543bf72228d569987712b07`。
GEM测试模块SHA256：`4b062ae939eeb9b2cc569c5b4208da429dfee0cf86830ce415865f7632748376`。
机器结果：`system-memory-validation.json`、`runtime-integration-build.json`、
`mmu-bootstrap-validation.json`、`gem-kernel-validation.json`、`fence-kernel-validation.json`。

这些证据不证明Host已接受系统页，也不证明GPU能够读写它们。当前仍只有QXL card0，
PCI未绑定、主模块未加载，工作准入仍关闭。下一步继续核实共享PDS/Fence等的初始化与
Paging Command实际写入/提交时序，以及进程/上下文发布。
