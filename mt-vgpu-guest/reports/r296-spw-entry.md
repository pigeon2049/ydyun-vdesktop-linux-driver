# r296：`SyncPrimWait` 入口三元组落定（批准执行）——100 秒双编码铁证

- **结论**：GDB `dprintf`（`commands` 在 batch 下不可用，`silent` 非法——换 dprintf 不停机打印）在 `SyncPrimWait` 入口抓到全进程唯一一次调用：`rdi=stack表项`、`rsi=0x174876e800`、`rdx=0x186a0`。`0x174876e800 == 100000×1000000` **精确相等**（`SemWaitI` 内 `imul $0xf4240`），与 `rdx=100000`（ms）双编码互相印证：**等待上限 = 100 秒**，与 r294 实测（submit3→自杀约 100s）闭环。全进程只调用一次（单表项）。L3 双绿，refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**

## 实测（执行过）

1. 批准：用户 standing 授权。`=2` 窗口：停桌面 → ref 0 → 拆桥 → `=2` → dprintf 版 GDB 下 blit（首轮 `commands` 版断点停机致 blit 冻住到 timeout——`commands 1` 在 pending 断点上不生效，教训记下）。
2. `SPW-ENTRY rdi=0x7fffffffd628 rsi=0x174876e800 rdx=0x186a0`（唯一下发，全进程一次）：rdi 落 blit 栈（`SemWaitI` 传 `&table[i]`，32B 步进）；rsi 经 `python3` 验证 `== 100000*1000000`（ns）；rdx=100000（ms）。两处独立编码同一 100s，与活体计时三方一致。
3. 恢复：用户 02:30 重开致一次 `rmmod` 被拒 → 二次停→10 秒内拆→默认桥 → L3 双绿 → 拉桌面。终态 refs 1/1。
4. 证据：`r296-spw-entry.txt`（0600，dprintf 全文，入库紧随采集）+ `r296-entry-trace.jsonl`（0600，8633 行：8201 blit + 431 GDB 自身 `pread`/`open`/`mmap` 调试杂波—— bridge 调用形状不变，单 submit3@8201）+ `r296-window.dmesg`（0600，33 行）；暂存区已清空。无代码改动，门禁沿用 r295（366+292）。

## 边界与下一步

- 入口 `rdi`（栈表项 `+0x8` 指针链 → 轮询地址）未解引用读出——表在 blit 栈上，`x/gx $rdi+8` 一次 attach 即得；但结合 fabricated 真 IN（`0x6005/0x6009@0`）已足够设计满足实验：**submit3 后由桥写目标同步值**（`0x2:0xa` 真写路径现成），awaited value 首候选 1（fence 语义）。
- 下一步（需离线改代码 + 门禁 + 窗口验）：submit3-observe 后对 UMD 同步写值（handle 取自 update 数组/0x2:0x2 记录），看 UMD 是否越过 submit3——STATUS #2 的 UMD 驱动验证即从这里开始。
