# r151：修正 mmap 页解析与 arena 页表生命周期，Oops 根因仍未定

## 结论

只读检查发现并修正两个 bridge 生命周期缺陷：`pvr_mmap()` 不再假定 vmalloc 页的
`struct page` 连续，并在逐页转换失败时安全退出；VM 首次初始化会复用 DMA 注册已填充的
arena GPU 页表，文件关闭也会回收 lazy VM 未成功初始化时留下的该表。它们是确证的缺陷，
但因上一启动 Oops 缺少 RIP/调用栈，不能断言其中任何一个就是重启根因。继续追 UMD 调用
链后，已把 `0x89:0xa` 的 108B/4B 包形状和已证实的指针组写成 packed wire 描述及 offset
断言；提交 handler 仍未启用。

## 本轮动作与证据

- 零硬件触碰：没有加载/卸载模块、绑定设备或提交 GPU 工作。
- 上一启动日志只保留 `BUG: kernel NULL pointer dereference, address: 0` 首行；没有可读
  的故障指令、调用栈、进程或模块信息。故障附近的 DMA/VM plan 日志只用于缩小审计范围，
  不足以定责。
- **实测源码缺陷（mmap）**：`pvr_mmap()` 对 `vmalloc_to_page(pmr->host)` 只解析一次，
  再用 `page + i` 访问其余页。vmalloc 保证虚拟连续，不保证 `struct page` 数组可这样递增；
  原实现也未检查首次转换是否返回 NULL。现改为对每个虚拟页调用 `vmalloc_to_page()`，
  空页返回 `-EFAULT` 并经统一出口释放 PMR 引用。
- **实测源码缺陷（arena 表）**：DMA 注册先调用 `pvr_arena_pages_ensure()`，并将翻译后的
  GPU PA 写入 `file->arena_gpu_pages`；此前第一次 `pvr_gpu_vm_ensure()` 又无条件分配并覆盖
  该指针，泄漏原数组并清除先前地址。现改为复用 ensure helper 保留已填地址。若 DMA 注册
  已分配该表但 lazy VM 未就绪，文件关闭现在也会释放它。
- `pvr_file_release()` 遇到 VM unbind/fini 错误会提前返回，保留整份 file/PMR/VM backing，
  防止释放仍可能被 VM 引用的内存。这属于异常 teardown 下的 fail-safe 泄漏，当前未改变；
  Oops 栈缺失，无法确认该分支是否曾执行。
- **适配线索（反编译假设，不是 Linux 实测）**：本地厂商 UMD SHA-256 实测为
  `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与
  `DECOMPILATION.md` 语料版本一致。`FUN_001383d0` 的调用包装将 bridge `0x89:0xa`
  的 IN 指针/长度传为 108 字节，OUT 为 4 字节；调用者 `FUN_0015f890` 先构建
  check/update sync 数组，并可附加 PMR sync。该伪 C 还不足以证实每个字段的线格式/语义，
  因此 SubmitTransfer3 仍保持拒绝，未做真实绘制提交。

  包装函数按栈偏移构造出的 108 字节请求槽位顺序如下；`pN` 是伪 C 参数编号，组名只表示
  caller 传入的临时数组，不能代替厂商字段定义：

  | offset | type | wrapper value | caller-side source |
  |---:|---:|---|---|
  | `0x00` | u64 | p2 | `puVar4[0x25]`（transfer-context handle 候选） |
  | `0x08` | u32 | p3 | check-group block (`local_560`) |
  | `0x0c` | u64 | p4 | check sync handles (`local_558`) |
  | `0x14` | u64 | p5 | check sync offsets (`local_458`) |
  | `0x1c` | u64 | p6 | check values (`local_3d8`) |
  | `0x24` | u32 | p7 | update-group block (`local_2d0`) |
  | `0x28` | u64 | p8 | update sync handles (`local_2c8`) |
  | `0x30` | u64 | p9 | update sync offsets (`local_1c8`) |
  | `0x38` | u64 | p10 | update values (`local_148`) |
  | `0x40` | u32 | p11 | PMR-sync count (`local_640[0]`) |
  | `0x44` | u64 | p12 | PMR-sync access flags (`local_5b0`) |
  | `0x4c` | u64 | p13 | PMR-sync handles (`local_638`) |
  | `0x54` | u32 | p16 | submit flags (`local_644`) |
  | `0x58` | u64 | p14 | generated CCB data (`local_e48`) |
  | `0x60` | u64 | p17 | opaque caller value (`param2[0x61]`) |
  | `0x68` | u32 | p15 | generated CCB size (`local_e50`) |

  `SubmissionSetCheckSyncPrim` / `SubmissionSetUpdateSyncPrim` 伪 C 还显示每组最多 32 项，
  每项由 handle、offset、value 三个并行数组构成；handle/offset 来自
  `SyncPrimLocalGetHandleAndOffset`。`TQ_SubmitPMRSyncs` 将 PMR sync 聚合到 handle 与
  access-flag 并行数组，并在到达 17 项时拒绝继续追加。`SubmissionCmdGenerate()` 的返回
  地址/长度分别流入 `0x58` CCB 指针和 `0x68` CCB 字节数；`0x60` 尾部 u64 用途未定。
  以上依据 5.2 UMD wrapper/callsite 静态恢复；kernel 对这些用户指针的复制、PMR sync
  ownership 和完成语义仍待核实。新增 `mt_pvr_tdm_submit3_in/out` packed 描述、108B/4B 和
  关键偏移编译期断言；仍不接入 dispatch。
- 已按 `scripts/pe_image.py` 固定值核验 `/opt/MTT-driver-only/mtkm64.sys` 的 SHA-256 为
  `0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33`。该二进制字符串索引
  未找到 `SubmitTransfer3` / `BridgeRGXTDM` 字面项，不能据此确认或否定其内部实现；没有套用
  未核验地址或将 Linux UMD 伪 C 当作 Windows 结构证据。

## 验证

- `make -C mt-vgpu-guest check-offline`：263 Python tests（1 skip），C RAM 272 checks，
  全绿。
- `make -C mt-vgpu-guest kernel`：全模块 `W=1` 构建成功，无警告。
- 新增源码门禁分别检查 mmap 逐页解析/判空、VM init 复用 arena 页表、close 回收 lazy VM
  未启动时的页表；三项目标用例通过。反向注入旧 mmap 页算法、覆盖式重复分配及去掉 close
  回收后，对应测试均按预期失败；已恢复修复。
- `test_pvr_wire_sizes` 编译并核对新 SubmitTransfer3 wire size/offset 断言通过；将 check
  handle 偏移注入为错误值时，测试编译阶段失败，已还原。

## 边界与后续

当前启动状态仍是无 `mt_*` 驱动模块、Guest/FW=`2/1`。两个源码修复不能替代 Oops 调用栈，
不据此解除“不要直接重复加载 bridge”的状态约束。继续适配前，先核实 `0x60` 槽位、PMR
sync 用户指针拷贝/引用规则和 CCB 完成语义；真实硬件验证需另等可重建且安全的会话窗口。
