# r351→r352：T2-e——b10 描述子直接验证通过（`SyncPrimRef` 返回 0），`r14+0x18` 链静态定位（离线 fabricated，零硬件触碰）

- **结论（实测）**：`CreateSyncPrim` 产物（b10 首 qword 所指描述子）直接传给 `SyncPrimRef` 时返回 **0**（成功），而传 NULL 返回 3（`INVALID_PARAMS`）。描述子内存布局：`+0x08=1`、`+0x10=1`、`+0x18=<ptr>`、`+0x20=0`，满足 `SyncPrimRef` 的 `8(rdi)=1` 分支条件。结合 r351 已实测的选中逻辑（`rdi=*(rbx+208*i+0x48)`），把 b10 描述子填入槽 0 `+0x48` 后 `SyncPrimRef` 应返回 0。
- **结论（实测，静态）**：`r14`（SubmitTA 入口 rdx）= RGXKickTA 栈帧 `rbp-0x170` 处结构体，经 `0x7b03c: lea -0x170(%rbp),%rdx` 传入 PrepareTA（0x78800），再经 `0x7b055: mov -0x198(%rbp),%rdx` 原样传给 SubmitTA（0x7b08f）。`*(r14+0x18)` 堆对象由 PrepareTA 在执行期间写入（RGXKickTA 仅分配栈槽，不写 `+0x18`）。
- **推断（非实测）**：`*(r14+0x18)` 的具体来源 buffer（候选：b20 的某字段如 `+0x30`=b21，或 PrepareTA 内部分配）未能实测——完整 mapA 在 GDB 下触发 `RGXCreateRenderContextCCB+1525` 处 SIGSEGV（堆布局敏感，GDB 引入；脱离 GDB 则正常）。已穷尽 catch-load、dlopen-finish、pending 符号断点、`setarch -R` 等方案，均复现崩溃。故来源定位停留在静态链，记为推断。

## 实测与边界

1. 全程 fabricated（harness + shim 默认模式，UMD SHA `b3058c02…34237b0` 对版）；无模块、无 DRM、无 PCI、无 GPU，会话 freeze 继续。
2. 直接验证命令（`t2e-syncref-direct.txt`，0600）：
   - `call CreateSyncPrim b14* b10 b11` → 0；`dumpat b10@0 48` 显示描述子 `+8=1`；
   - `call SyncPrimRef b10@0 b30` → **0**；`call SyncPrimRef u0 b30` → **3**（基线）。
3. 静态证据：`objdump` 反汇编 `RGXKickTA@0x7afd0`（`0x7b03c/0x7b050/0x7b055/0x7b08f`）与 PrepareTA（0x78800）调用关系；`r14+0x18` 写入点未在 RGXKickTA 内（仅 `lea` 分配）。
4. GDB 尝试记录（`r352-gdb-attempt.txt`，0600）：dlopen-finish 方案可武装断点，但完整 mapA 在 `RGXCreateRenderContext` 阶段崩溃；plain GDB + 部分命令可用，完整命令必现。结论：GDB 路径当前不可用，改用直接调用验证。
5. 未断言：`*(r14+0x18)` 的确切来源 buffer/偏移；`i=*(rbx+0x24)` 在真实 kick 下的取值；回填后经完整 SubmitTA 路径的端到端返回值（直接调用已证描述子有效，选中逻辑 r351 已证）。

## 下一步

1. T2-f：解决 GDB 堆布局敏感问题（或改用插桩 shim 记录 `*(r14+0x18)`），完成 `r14+0x18` 来源的实测定位；或直接在 harness 层用 `poke` 按 r351 地址回填后跑完整 `RGXKickTA`（需先解决 GDB/布局问题，或等可用的 live 窗口）。
