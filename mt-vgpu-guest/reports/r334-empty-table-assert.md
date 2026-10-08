# r334：abort 是空表断言——首轮 `ebx=0 >= edx=0` 即自杀（批准执行）

- **结论**：GDB 文件脚本单发真实 copy tq-perf（`=2` 窗口）：`RGXReleaseCPUMappingZSBuffer` 内的断言分支（file `0x87a82`：`cmp %edx,%ebx; jae abort`）**首轮即中**——`ebx=0, edx=0`，且 `*(rax+0xc)=0` 证实 `edx` 取自结构体计数槽而非残留寄存器。语义：release 循环要求首个下标 `0 < 容量`，而描述符表容量槽为 0——setup 搭了 job 骨架（r320 的 destination-magic/r333 的 `[r8]=1` 皆成立），但 release 列表是空的。这是“结构性前置缺失”的第一条命名条件。`=2` 拆 → 默认回（`drm_major=0` 已验）→ L3 双绿；refs 1/0，窗口零新增 WARN。**Freeze 已恢复。**无内核代码改动（窗口脚本 `scripts/release-assert-window.sh` 落库）。
- **静态锚点（离线核对）**：`nm -D` 定锤 `RGXReleaseCPUMappingZSBuffer @ 0x856a0`（长 `0x54d0`），`0x87a82` 与 abort 跳板 `0x88650→0x2c8cc`（r321）皆在其内；同函数另有直跳 abort（`0x87d4c: jae 0x2c8cc`，该处线性解码错位，只认前者）。

## 实测（执行过）

1. 批准：用户“继续推进”（接 r333 既定下一刀）+ 先前真机/重载授权。预检 ref 0、无持有 → `drm_major=2` 重载 → GDB 单发（`timeout -s KILL 120`，即时 abort）→ 读分支寄存器 + abort 点寄存器（`rbx=rdx=rsi=0`，`rcx=0x160000`）→ 恢复默认 + L3。
2. 证据：`r334-release-assert.txt`（0600：ARMED/CMP/ABORTSITE + 寄存器）+ `r334-tqperf.jsonl`（0600，8626 行）；暂存区已清空。门禁沿用（394+299）。
3. **两个未解如实记**：① entry 断点（`0x856a0`）已设但零命中，执行直达函数内 `0x87a82`——中段进入或 GDB  quirks，待查，不影响分支读数本身；② abort 点 `bt` 连续两轮（r333/r334）零输出——文件脚本 batch 下 `bt` 疑似静默，与栈无关（r317 core 有栈）。③ r322 曾以 `info symbol` 把 abort helper 归于 `RGXTDMSubmit`，与本轮静态区间 math（`0x856a0–0x8ab70`）矛盾——以后者为准，前者收回。

## 边界与下一步

1. 下一刀：谁把计数槽清零/没填——`rdi`（`0x…1380`）指向的 job 区在 BlitInit→CheckFences→LookUpEOT 链中的写入史；或换 producer 差分（r322 候选）；另行开轮。
2. 本轮未停桌面（无持有即不停）；teardown 前复验 ref 0（未触发挡回）。
