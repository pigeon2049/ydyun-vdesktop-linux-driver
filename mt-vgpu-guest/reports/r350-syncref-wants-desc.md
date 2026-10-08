# r350：T2-c——`SyncPrimRef` 要的是非空描述子（`8(desc)∈{1,2}`），传入为 NULL（离线 fabricated，零硬件触碰）

- **结论**：`SyncPrimRef @ 0xa0f70` 入口即 `*(rsi)=0` 后判 `rdi`：NULL→`0xa0fb0`（即 3 的出口）；非空则读 `8(rdi)` 须为 1 或 2，再取 `0x18/0x20(rdi)`。活体（fabricated GDB）`SPCALL rdi=NULL`——`SubmitTA` 传的就是 NULL，故 `INVALID_PARAMS`。`SubmitTA` 入口快照（`SUENT`）显示其 `rsi`（PrepareTA-out 局部）含一串堆指针（`…82460/…fc00/…80c10/…7bbe0`），`rdx`（`0x…d6b0`）亦栈结构——描述子候选众多，但无一被选为 `rdi` 传入，说明 `SubmitTA` 内部按某字段选中描述子而全零输入下选中了空。
- **整形进展**：`CreateSyncPrim` 真 handle 已落 `b10[0]`（`SYMBOL -> 0`，verbatim L4 形式；裸写形式会 segfault/返回 3——引号转义教训）；按 GFX 类比回填 `b22+0x48/+0x50` 后仍 `-> 3`（handle 放错了位置，或描述子另有所指）。`mapB` 一度漏 `$SYNC`（替换未命中），已补齐，未污染结论（mapA 为准）。
- **T2-c 未闭合**：还差“描述子 selected from where”——`SubmitTA` 内从 `rsi` 链选 `rdi` 的那一步（`0x79caa` 上游误 decode 区，需 GDB 单步或返回地址再探）。

## 实测与边界

1. 全程 fabricated；`build/r350-replay` 系 GDB 单行工作区，用完即删。无模块、无 DRM、无 PCI、无 GPU。门禁沿用（394+299）。
2. 证据：`r350-ta-syncref-null.txt`（0600：`CreateSyncPrim->0` + `RGXKickTA->3`）+ `r350-ta-attempt2.jsonl`（0600）；`ta-kick-attempt1.sh` 增量已单提交（`40403bb`）。暂存区已清空。
3. 未断言：`0xa0fb0` 是否确为 `mov $0x3` 出口（读到 `je` 即收，未逐字节核对）；`UNK1/2` 身份仍未命名。

## 下一步

1. T2-d：`SubmitTA` 内描述子选中步骤（`0x79caa` 上游），GDB 返回地址 + 读 `rsi` 链候选。
