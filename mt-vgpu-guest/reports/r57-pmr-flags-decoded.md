# r57：PMR/map flags 解码与记录（PTE 策略的输入）

承接 r54 清单。`map_flags` 与 alloc `flags` 逐项一致不是巧合——对照
2.7.1 `pvrsrv_memallocflags.h` 解码如下（测试逐位对照头文件，改位即挂）：

| 值 | 出现 | GPU R/W | CPU R/W | 缓存 |
|---|---|---|---|---|
| 0x333 | 3 static + 8 KiB PMR | ✓ | ✓ | GPU incoherent |
| 0x1233 | 2 个大 heap PMR（0x9bff/0x2a7ff） | ✓ | ✓ + CPU coherent | GPU incoherent |
| 0x303 | 7 个小 PMR | ✓ | ✗（GPU-only） | GPU incoherent |

## 改动（零线格式变化，零行为变化）

- `mt_pvr_pmr` 新增 `alloc_flags`（0x6:0x9 `in.flags`），
  `mt_pvr_binding` 新增 `map_flags`（0x6:0x13 `in.map_flags`）。
- bridge 仍不执行它们：CPU-only plan 保持 DEFAULT——清单里全是 GPU
  可读写的，这与现状一致；mmap 也不按 CPU 位收紧（UMD 实际映射行为优先
  于纸面权限，动它会破坏 ladder）。
- 翻译器届时必须由这两处派生 PTE 只读/coherent 位（r24 教训：错标志位
  直接生成错页）。

## 验证

- 新增 `MemAllocFlags` 3 项（逐位对照 vendor 头文件）+
  `test_flags_are_recorded_for_the_translator`；206 项 Python 全过。
- bridge `W=1` 零警告构建通过；**未重载**（运行中仍是已验证构建）。
- 会话 `Guest/FW 2/2`、`pending=0`；shim 转储工具（多目标+annotation+
  out_hex）是本轮顺手补完的永久调试能力。
