# r172：DDK2 桥重载 + 全链全绿 + 首个真实 SubmitTransfer3（批准执行）

- **结论**：桥经 `drm_major=2` 重载（rmmod ref0 → insmod，节点仍 `renderD128`）后，DDK2 全链 123 调用**零非零返回**（`0x82:0x12` render2、`0x88:0x5/0x88:0x6` CCB2 建销、`0x2:0x2`×3、`0x2:0x8`，优于 r145）；真实 `musa_blit_test -device 0 -f -o`（无 shim fabrication，直连真桥）发出**首个真实 `0x89:0xa`**：108B / `check=0/update=2/pmr_sync=0` / `ccb=0x8000f44000/0x1200` / opaque 0，桥按设计回 `-25`（handler 未接入），无 GPU 工作；VA 与尺寸与 fabricated 六轮完全一致。随后桥恢复默认并 L3 复绿，freeze 继续。

## 实测

1. 重载：`rmmod`（bridge ref 0）→ `insmod drm_major=2`（`Initialized pvr 2.1.0`，param 读回 2，节点仍 `renderD128`）；probe 未动（ref 稳 1）。
2. DDK2 链（GDB 监督，同符号 + CCB 传 `b5*`，r144 配方）：connect/devmemctx/render/syncprim/CCB 建销全 0，exit 0；trace 123 行零非零。证据 [`r172-ddk2-chain.jsonl`](r172-ddk2-chain.jsonl)。
3. 真实 blit（standalone，passthrough 记录，`UMD_DUMP_BRIDGE=0x89:0xa`）：8198 行，1 次 `0x89:0xa`，`in 108/out 4/ret -25`；解码值见上。UMD 随后在错误路径以 SIGABRT 退出（预期内：提交被拒后的用户态处理），内核侧干净。证据 [`r172-real-submit3.jsonl`](r172-real-submit3.jsonl)。
4. 真实 CCB 字节**未捕获**（诚实边界）：VA→PMR→mmap(CPU) 关联在 trace 上成立（res `0x8000f430fe`+`0xa00fff` → PMR → `handle<<12` 映射，live 复核了 `off>>12==pmr` 约定），但 GDB 停住时窗口不可读；smaps 显示该映射 `Rss: 0`；失败轮无 munmap 记录。候选解释：UMD 经另一映射写入、或该轮未及写入、或按轮变化的句柄归属——trace 按轮解码尚未做。UMD 启动期抖动大（GDB 下多次开局即停），多次捕获尝试未果。
5. 恢复：桥 rmmod → 默认 insmod（param 读回 0，节点仍 `renderD128`）→ L3（node 0 failing，dma smoke PASS）→ dmesg 无模块相关 WARN（仅已知用户态 segfault 行与启动期通告）。probe ref 1、bridge ref 0，freeze 继续。

## 边界

- `-25` 是“无 handler”的正确拒绝，不是缺陷；不证明 CCB 有效，只证明真实 UMD 生成了该输入。
- 真实 CCB 字节内容仍未知；fabricated 39B 窗口不能直接套用（同 VA/尺寸是布局确定性，不是内容一致性）。
- 本轮两次 bridge 重载均在 ref0 下执行，probe 会话零触碰。

## 下一步（候选）

- 真实 CCB 字节捕获：按轮解码归属（句柄可能轮变）+ 提交时刻内存快照；或实现 `0x89:0xa` accept-and-log handler（r151 ABI 已备）让 UMD 走完提交、字节落桥日志。
