# r362：r361 描述子"不匹配"系 GDB 脚本读错位置——CreateSyncPrim 输出始终合法，无需参数调整

**结论**：r361 报告的 b10 描述子 `+0x18=NULL` 不是 `CreateSyncPrim` 的输出问题，而是 r361 GDB 驱动脚本的测量 bug——它在 `CreateSyncPrim` 入口捕获 `$rdi`（param_1），返回时从 `*(param_1)` 读"描述子"；但 `CreateSyncPrim` 按 ABI 把描述子写到 `*param_2`（`b10` 缓冲）。本轮用同一 harness 命令行复现：`*(param_2)` 处描述子 `+0x18=<ptr>、+0x20=0`（与 r352/r353 完全一致），`*(param_1)` 处"描述子" `+0x18=NULL、+0x20=<heap ptr>`（与 r361 dump 逐项一致）。r352 式直接调用 `SyncPrimRef(*b10)` 返回 0。**无需 fabricated 参数调整；r363 重跑 r361 观察计划的唯一前置条件是修正 GDB 脚本从 `$rsi` 取 param_2。**

## 1. `+0x18` 的设置逻辑（反汇编实测）

`CreateSyncPrim @ 0x1782a0`（即 `_SyncPrimAlloc`；UMD ELF SHA-256 `b3058c02…34237b0` 与 `DECOMPILATION.md` 对版，实测核对）：

```c
__ptr = (long *)FUN_0018d570(0x30);          // 48B 描述子
lVar3 = GetFeatures(*param_1);
lVar3 = (-(ulong)(*(uint *)(lVar3 + 0x54) < 2) & 0xfffffffffffffff8) + 0x10;
iVar1 = FUN_0019e0c0(param_1[8],lVar3,1,0x100000000,lVar3,"Sync_Prim",
                     &lStack_68,0,&puStack_70,0);   // RA_Alloc
if (iVar1 == 0) {
    *(undefined4 *)(__ptr + 1) = 1;           // desc+0x08 = 1
    *(undefined4 *)(__ptr + 2) = 1;           // desc+0x10 = 1
    __ptr[4] = lStack_68;                    // desc+0x20 = RA_Alloc 输出 lStack_68
    __ptr[3] = (long)puStack_70;              // desc+0x18 = RA_Alloc 输出 puStack_70
    ...
    *param_2 = __ptr;                        // 描述子写入 *param_2
}
```

- **desc+0x18 的来源**：`RA_Alloc`（`FUN_0019e0c0`）的 `puStack_70` 输出指针。
- **NULL 条件**：仅当 `RA_Alloc` 返回的 `puStack_70` 为 NULL。但本轮实测 `RA_Alloc` 正常返回 0，`puStack_70` 为有效堆指针——**r361 的 NULL 并非来自此路径**。
- **描述子落点**：`*param_2`（ABI 第二参数），不是 `*param_1`。

## 2. 根因：r361 GDB 脚本读错位置（实测证实）

r361 `build/traces/r361/gdb_drive3.py`：

```python
class CreateSyncPrimBP(gdb.Breakpoint):
    def stop(self):
        b10_buf = int(gdb.parse_and_eval("$rdi"))   # param_1 ← 取错了
        ...
class CreateSyncPrimRetBP(gdb.Breakpoint):
    def stop(self):
        b10_desc = int(gdb.parse_and_eval("*(uint64_t*)%d" % b10_buf))  # *(param_1) ← 读错了
```

harness 调用为 `call CreateSyncPrim b14* b10 b11`（见 `build/traces/r361/run_r361.sh`）：
- param_1（`$rdi`）= `b14*` = `*(*(uint64_t*)b7 + 176)`（某 UMD 内部指针）
- param_2（`$rsi`）= `b10`（64B 缓冲地址）← 描述子实际写入 `*(b10)`

本轮复现实测（同一 harness 命令行，GDB 开 ASLR，两次独立运行，结果一致）：

| 读取位置 | desc | +0x08 | +0x18 | +0x20 | 对应 |
|---|---|---|---|---|---|
| `*(param_2)`（正确，`$rsi`） | 0x55… | 1 | 0x55…（ptr） | 0x0 | r352/r353 ✓ |
| `*(param_1)`（r361 读法，`$rdi`） | 0x55… | 1 | 0x0（NULL） | 0x55…（ptr） | r361 dump ✓ |

`*(param_1)` 处是一个碰巧有描述子头（+0x08=1）的其他堆对象，不是 b10 描述子。r361 的 poke 把这个错误地址写入 TA 槽位，导致 `SyncPrimRef` 在 `0xa0fa0: sub 0x30(%rdx),%eax` 解引用 `rdx=NULL` 崩溃——崩溃的是**错误的描述子**，不是 `CreateSyncPrim` 的输出。

## 3. 差异来源假设逐个证伪（实测，不猜）

- **输入不一致？** → 证伪。本轮用与 r361 **完全相同**的 harness 命令行复现，`*(param_2)` 输出与 r352/r353 一致（`+0x18=<ptr>、+0x20=0`）。输入相同则输出相同。
- **堆布局/ASLR 差异？** → 证伪。两次独立运行（不同 ASLR 基址）结果一致；r361 同样开了 ASLR（`set disable-randomization off`）。
- **UMD 全局/线程状态？** → 证伪。`RA_Alloc` 的行为仅取决于输入 arena（`param_1[8]`）与请求尺寸；直接调用可稳定复现合法描述子，无状态依赖迹象。
- **` *b7*+176` 内容变化？** → 不适用。该值只决定 param_1（`CreateSyncPrim` 的输入上下文指针），不决定描述子 `+0x18`（后者来自 `RA_Alloc` 输出）。r361 的怀疑建立在"描述子真的坏了"的前提上，而该前提不成立。

**定性**：测量方法 bug。非输入问题、非状态问题、非布局问题。

## 4. 合法化方案与验证

**无需参数调整**——描述子一直是合法的。修正测量方法即可：

- GDB 脚本入口捕获 `$rsi`（param_2）而非 `$rdi`；
- 返回时从 `*(uint64_t*)$rsi` 读描述子；
- poke 时使用此正确地址。

r352 式直接调用验证（本轮实测，见 `r362-direct-test.txt`）：

```
SYMBOL CreateSyncPrim(...) -> 0
SYMBOL SyncPrimRef(...) -> 0        # 用 *b10 描述子，返回 0（成功）
```

## 5. r363 前置条件（如实记录）

重跑 r361 的 0x82:0xC 活体 IN 观察计划，**唯一**前置条件：

- 修正 GDB 驱动脚本：`CreateSyncPrim` 断点捕获 `$rsi` 为 b10 缓冲，返回时读 `*(uint64_t*)$rsi` 得描述子；后续 poke 使用此地址。
- 其余与 r361 相同：真实建连（`connect 0` + `PVRSRVConnectionCreateDevice b7 u1 u0`）、b10 描述子 poke 到 `0x79c92` 槽位（用已算好的 `$rdx+0x48`，`$rbx` 已被改写不可用）、GDB 开 ASLR（`set disable-randomization off`）、在载桥为 r356 构建（含 observer，r360 已重载）。

## 证据

- `reports/r362-probe-output.txt`（0600）：GDB 实测 `*(param_2)` vs `*(param_1)` 对照（两次独立运行）
- `reports/r362-direct-test.txt`（0600）：`CreateSyncPrim` + `SyncPrimRef(*b10)` → 0 直接调用
