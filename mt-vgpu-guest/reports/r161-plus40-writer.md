# r161：CCB `+0x40` 轮变 2B 的写入者落定——生成器头拷贝逐字搬运 job 计数器

- **结论**：`+0x40` 的 2B 由 `SubmissionCmdGenerate` 的 `0x58B` 头拷贝（`PVRSRVMemCopy(lVar9, param_2, 0x58)`，经 libc AVX `vmovdqu64`）从 job 结构体 `+0x40` 逐字搬运而来；该欄位四轮实测 `0x288e → 0x28ae → 0x28bd → 0x28d7` 单调递增，步长不等，是计数器形态（确切来源仍未命名，不断言）。写入栈同时以活体执行确认了 r156 的路径：`musa_blit_test → TQJobSubmit → SubmissionCmdGenerate → 0x89:0xa`。本轮零硬件触碰、无代码改动。

## 实测

1. 离线 GDB（`gdb -batch`，`LD_PRELOAD` 带 `-g` 重编的同源 shim，`UMD_DRM_MAJOR=2` + shared backing；gdb 默认关 ASLR 故地址跨轮稳定）。先断 `map_shared_pmr` 取 `0x500e000` backing CPU 地址（`0x7ffff5703000`），再下硬件写观察点 `*(short*)(backing+0xf02+0x40)`。完整记录见 [`r161-plus40-watch.txt`](r161-plus40-watch.txt)。
2. 观察点命中唯一一次写：`Old value = 0, New value = 10382`（=`0x288e`），PC 在 libc memcpy 的 `vmovdqu64 %ymm19,-0x40(%rdi,%rdx,1)`（64B 向量存，覆盖 `+0x40` 只是其 64B 跨度的一部分）。调用栈：`PVRSRVMemCopy ← SubmissionCmdGenerate ← TQJobSubmit ← musa_blit_test`（动态符号解析，非反编译地址推算）。
3. 另断 `SubmissionCmdGenerate` 入口（按名，非地址）读 job（`$rsi`）三轮：job 地址稳定 `0x55555558a770`（堆），`job+0x40` = `0x28ae / 0x28bd / 0x28d7`；连同观察点轮的 `0x288e`，四轮单调递增、步长 15–38 不等。堆地址稳定而值变 → 排除 ASLR 指针；小量值 + 单调 → 计数器形态。确切是何种序列号（提交序号/job ID/时间低位）未命名。
4. 门禁：本轮无代码改动，`make -C mt-vgpu-guest check-offline` 复核全绿（269 Python，1 skip + 272 C）；`lsmod` 无 `mt_*`。

## 边界

- 观察点是“值变化即停”，64B 向量存内其余 62B 本轮全零故只此一停；若某轮 `+0x40` 恰与旧值相同会漏停——四轮值各异，此风险未触发。
- 计数器命名与扩展区（`+0x1078–0x11B8`）条目算术仍未闭合；shim 回包仍 fabricated，不证明执行；不重载硬件 bridge。

## 下一步

- 换 producer（不同 blit 参数/尺寸）看窗口长度、扩展区与该计数器是否跟随变化，闭合条目算术（STATUS 步骤 1 收尾）。
