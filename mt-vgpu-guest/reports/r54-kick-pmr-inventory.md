# r54：一次真实 kick 的 PMR/VA 全清单（翻译步骤的输入规约）

承接 r53（kick 包本身无 work）。用 shim 新转储（IN+OUT+PMR annotation，
`UMD_DUMP_BRIDGE="0x6:0x9,0x6:0x13,0x6:0x15"`）重跑 rung8，
解出一次 `RGXKickSync` 背后 UMD 建立的全部映射。零内核改动、零 GPU 执行。

## 清单（12 PMR，共 0x49c93 ≈ 295 KiB）

| PMR | 大小 | 命名 | VA | 对齐 |
|---|---|---|---|---|
| 0x1013 | 0x1000 | PDS Static Memory | 0xda00000000 | ✓ |
| 0x1014 | 0x1000 | General Static Memory | 0x8000000000 | ✓ |
| 0x1015 | 0x1000 | USC Static Memory | 0xe000000000 | ✓ |
| 0x1019 | 0x9bff | PMR sub-allocated | 0xda00010000 | 大小未对齐 |
| 0x101b | 0x2a7ff | PMR sub-allocated | 0xe000010000 | 大小未对齐 |
| 0x101d | 0x387 | PMR sub-allocated | 0xeb00000000 | 大小未对齐 |
| 0x101f | 0x253 | PMR sub-allocated | 0x8000010000 | 大小未对齐 |
| 0x1021/23/25/27 | 0x408f ×4 | PMR sub-allocated | 0x8000010253…（byte-tight 续接） | VA+大小未对齐 |
| 0x1029 | 0x207f | PMR sub-allocated | 0x800002048f | VA+大小未对齐 |

另有：map_flags 与 PMR flags 逐项一致（0x333/0x1233/0x303）；
mapping == pmr（OUT 回传句柄）；server_heap 为 heap 对象句柄（如 0x1007）；
15× HeapCreate、12× reserve/map 全部 `ret=0`。

## 对翻译步骤的含义

1. **work = 12 PMR + 12 VA range + kicksync context 句柄**，三者都在
   bridge 的 per-file 表里（`pmrs`/`bindings`/KICKSYNC 对象）。翻译器输入
   完备，无需再抓包。
2. **只有 3 个 4 KiB 对齐 PMR（12 KiB）能进当前 CPU-only VM plan**，
   其余 9 个因 byte-tight 未对齐而降级（`pvr_gpu_vm_bind` 回 `-EOPNOTSUPP`，
   线格式不变）。这是已知边界（§10 item 2）：bind 侧 round-down 取整规则
   是下一步要定的，现在两侧都还没动。
3. 总量 295 KiB 远小于 32-table-page plan 上限；容量不是问题，对齐才是。
4. 测试：`test_byte_tight_pmr_degrades_from_gpu_plan` 把该降级钉住。
   201 项 Python 全过；会话 `Guest/FW 2/2`、`pending=0`，无 WARN/Oops。
