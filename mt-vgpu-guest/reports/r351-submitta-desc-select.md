# r351：T2-d——SubmitTA 内描述子选中步骤定位：`*(rbx+208*i+0x48)`，`i=*(rbx+0x24)`（离线 fabricated，零硬件触碰）

- **结论（实测）**：描述子选中是 `SubmitTA`（`FUN_001796b0`，`libsrv_um_MUSA.so.1.0.0`，SHA `b3058c02…34237b0` 对版）内一段直线指令序列，`SyncPrimRef` 的 `rdi` 来自 `0x79c92: mov 0x48(%rdx),%rdi`，其中 `rdx = rbx + 208*i`：
  - `0x79ace: mov 0x18(%r14),%rdx` —— r14 即 SubmitTA 入口 `rdx`（RGXKickTA 栈帧上的 kick 结构体链；fabricated 下 `r14=0x7fffffffddd0` 栈地址）；
  - `0x79ad2: mov 0x30(%rdx),%rbx` —— `rbx = *(*(r14+0x18)+0x30)`，描述子数组基址对象（堆）；
  - `0x79ad6: mov 0x24(%rbx),%eax` —— `i = *(rbx+0x24)`，槽位索引（存 `rbp-0x628`）；
  - `0x79c53/0x79c62/0x79c71: lea;lea;shl` —— `rdx = 13*i*16 = 208*i`（槽大小 208 字节）；
  - `0x79c75: add -0x608(%rbp),%rdx` —— `rdx = rbx + 208*i`；
  - **`0x79c92: mov 0x48(%rdx),%rdi`** —— 描述子 = `*(rbx + 208*i + 0x48)`；
  - `0x79caa: call 0xa0f70`（`SyncPrimRef`）。
- **活体验证（fabricated GDB，r341 配方的离线版：catch-load 算基址）**：
  - `i=0`（`DESCLOAD_i_rdi=0x0`，`*(rbp-0x628)=0`），`base=0x55555557f2f0`；
  - `*(base+0x48)=NULL`（`0x55555557f338: 0x0`）→ `SYNCREF_rdi=0x0`，与 r350 的"传入 NULL"一致；
  - 链式复核：`*(*(r14+0x18)+0x30)=0x55555557f2f0 == *(rbp-0x608)`，`match=True`；
  - 三个断点（SubmitTA 入口 / 0x79c92 / SyncPrimRef 入口）各命中一次——该序列无循环回边，单次执行。
- **SyncPrimRef @ 0xa0f70 反汇编复核（r350 结论成立）**：入口 `movq $0x0,(%rsi)`；`test %rdi,%rdi; je 0xa0fb0`；`0xa0fb0` 为 debug-print 后 `mov $0x3,%eax; ret`；非空则 `8(%rdi)` 须为 1（走 `0x18/0x20` 取值返回 0）或 2，否则返回 `0x14/0xf7`。
- **NULL 条件（实测）**：fabricated 全零输入下，rbx 指向零填充堆对象 ⇒ `i = *(rbx+0x24) = 0` ⇒ 取槽 0 的 `+0x48` qword = NULL ⇒ `SyncPrimRef` 返回 3（`INVALID_PARAMS`）。
- **推断（非实测）**：要让校验通过，需把 `CreateSyncPrim` 产物描述子填入槽 0 `+0x48`（即 `rbx+0x48`）；`*(rbx+0x24)` 疑似 pending-sync 计数/索引，真实 kick 下未必为 0。另：r349 记的"`0x79c80–0x79c96` 线性解码错位"经本轮核对为起始边界问题，该区实际解码干净——`0x79caa` 上游无需绕行。

## 实测与边界

1. 全程 fabricated（harness + shim 默认模式，UMD SHA 对版，`musa.ini` 工作目录）；断点地址全部实测命中，无模块、无 DRM、无 PCI、无 GPU，会话 freeze 继续。
2. 语料用法：`FUN_001796b0` 伪 C 共 709 行未通读（§8 防迷失），按地址直读 objdump；`SyncPrimRef` 有动态符号但不在 Ghidra 函数表——以反汇编为准。
3. 证据：`r351-descload-gdb.txt`（0600：断点快照 + CHAIN 复核）+ `r351-descload-disasm.txt`（0600：`0x79ace–0x79caa` 反汇编）；`build/traces/r351/`、`build/r351-replay/` 已清空。
4. 未断言：rbx 链对象在 kick 结构体中的上层语义（对应哪个 buffer/字段）；i 在真实 kick 下的取值；描述子 `8/0x18/0x20` 字段的填充配方（T2-e）。

## 下一步

1. T2-e：定位 `r14+0x18` 所指堆对象在 kick 结构体中的来源（哪个 buffer、偏移），把 b10 描述子填到槽 0 `+0x48` 后复测 `SyncPrimRef` 返回值；仍 fabricated、零硬件触碰。
