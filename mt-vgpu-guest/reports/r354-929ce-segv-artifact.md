# r354：`0x929ce` SIGSEGV 是 fabricated artifact——`GetSrvHandle` 返回的 `0x6000` 句柄值被当作指针解引用（离线 fabricated，零硬件触碰）

- **结论（实测）**：`0x929ce` 处 `mov (%rax),%edi` 以 `rax=0x6000` 触发 SIGSEGV（fault_addr=0x6000，未映射）。`0x6000` 经完整链条实测溯源：`ZeusSyncPrimImportFD` 内 `0x79733: mov %rax,-0x5b0(%rbp)` 存的是 `0x79718: call GetSrvHandle` 的返回值；而 `GetSrvHandle @ 0x3c1c0` 反汇编实测为 `rdi ? *(uint64_t*)rdi : 0`——即它从某结构体首 qword 读出 `0x6000` 并原样返回。该值随后经 `0x7a30b → 0x36ec0 (rdi)` → `0x37111 → 0x92930 (rdi=0x6000, rsi=0x82, rdx=0xc)`，在 `0x929ce` 被当作指针解引用以取 ioctl fd（`ioctl([rax], 0xc0206440, r15)`，其中 `0xc0206440=_IOWR('d',64,32)` 为二进制内常量，`0x92955` 处 `mov $0xc0206440,%r14d` 实测）。
- **结论（实测）**：`0x6000` 不是合法用户态指针（fault_addr 即其本身），不是二进制内常量（全二进制无 `$0x6000` 立即数），不是 harness 缓冲地址（堆地址为 `0x55…`/`0x7f…` 形态）。它是句柄/小整数值被写入了期望指针的结构体字段。
- **定性（推断，有实测支撑）**：**fabricated artifact**，非真实执行链问题。依据：(1) 任何真实执行以 `rdi=0x6000` 进入 `0x92930` 都会同样崩溃，而官方驱动在真机工作正常，故真实路径下该字段必为有效指针；(2) fabricated harness 手工拼装 TA kick 结构，未经过完整 PVRSRV 连接/句柄表初始化，`GetSrvHandle` 读到的首 qword 是句柄值而非指针；(3) 崩溃点位于设备 ioctl 建连路径（`_IOWR('d',64,32)`），该路径本质上需要真实内核驱动连接，fabricated 环境无法提供——此前被 `SyncPrimRef→3` 挡住从未到达，属新领地输入不完备。
- **证伪记录（实测）**："0x6000 是 GPU 虚地址"的假设被证伪——fault_addr=0x6000 且 GDB `info proc mappings` 无此映射，为纯粹未映射地址；"0x929ce 非指令边界"的 r353 存疑被澄清——GDB `x/i` 反汇编显示 `0x929ce: mov (%rax),%edi` 为合法指令，崩溃地址精确。
- **方法（实测）**：沿用 r353 的 GDB 驱动（`set disable-randomization off`，CreateSyncPrim/RGXKickTA pending 断点，`0x79c92` 处回填 b10 真描述子），新增 `0x92930` 入口断点抓 `rdi/rsi/rdx` 与调用者反汇编；base 改由 `info proc mappings` 的 `r--p` 映射解析（修正 r354 首轮 `pc-0x7afd0` 误算——pending 断点实际落在 `RGXKickTA+17`，导致 base 差 `0x11`、断点错位、行为回退到 `→3` 的教训）。

## 实测与边界

1. 全程 fabricated（harness + GDB Python，无模块、无 DRM、无 PCI、无 GPU）；UMD SHA `b3058c02…34237b0` 对版。会话 freeze 继续。
2. 证据（0600）：`r354-crash-summary.txt`（POKE/RET/崩溃关键行）、`r354-crash-dump.txt`（完整 crash dump：pc/fault_addr/寄存器/backtrace）、`r354-disasm-evidence.txt`（0x929ce/GetSrvHandle/0xc0206440 常量/0x79733 四处反汇编）。
   - 调用链（实测）：`RGXKickTA+0xc4 → ZeusSyncPrimImportFD+0x204a → [0x36ec0] → 0x92930 → 0x929ce SIGSEGV`
   - `SyncPrimRef → 0` ×2 复现（r353 结论保持）
3. 未断言：`0x6000` 所在结构体字段的具体语义（哪个 buffer 的哪一偏移写入了该句柄值）；真实 `PrepareTA` 输出中该字段的期望内容。补齐需真实 DDK2 render backend（STATUS.md 下一步 #1）。
4. 本轮未改驱动代码，`check-offline` 门禁沿用（见门禁节）。

## 下一步

1. 真实 DDK2 render backend（STATUS.md #1）：本轮结论意味着 fabricated TA 路径的天花板已到——`SyncPrimRef` 之后是设备 ioctl 建连路径，harness 无法仿真。后续应转向真实 render backend，使 `GetSrvHandle` 链返回有效指针后再推进。
2. 若继续深挖 fabricated：可尝试在 harness 层将 `GetSrvHandle` 所读结构体首 qword 指向一个伪造的含有效 fd 的结构，观察是否越过 `0x929ce`——但收益递减，建议仅在 render backend 之前做时间盒 spike。
