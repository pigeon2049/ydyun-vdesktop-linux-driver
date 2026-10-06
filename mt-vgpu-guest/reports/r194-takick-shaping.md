# r194：psKickTA 手塑首轮——RGXKickTA 干净返回 3（fabricated，零硬件触碰）

- **结论**：harness 手塑 psKickTA（5 应用缓冲 + 6 字段）经
  `RGXKickTA` 进到 `RGXPrepareTA` 深部，4 次崩溃逐一定位缺失指针
  （`+0x2d8`、`+0xb8`、`+0x28`）并补齐后，`RGXKickTA → 3` 干净退出、
  无提交流量。整形循环已机械化（崩溃点名下一个需求）。
  全部 fabricated（桥零接触），会话未碰（66/1 不变）。

## 实测（离线；GDB 监督 fabricated UMD）

1. 工作命令（bridge 全 fabricated，DRI→/dev/null）：
   `connect 0` + `buf{20:512,22:8192,23:768,24:64,25:768}` +
   `u64 20:{0x28,0x30}=b22,0x2d8=b25,0x2e0=b23` + `u32 20:0x4=1` +
   `call RGXKickTA conn b20 b24 u0 u0 u0` → `-> 3`，exit 0。
   证据 trace（仅 connect 流量）：`r194-takick-return3.jsonl`。
2. 关键纠偏（伪 C 假设之误）：PrepareTA 的 `param_2` 是 `uint *`，
   `+0xb6/+0xb8/+0xc` 皆为**元素**偏移（字节 `0x2d8/0x2e0/0x30`），
   不是字节偏移——此前按字节写的 `+0xc/+0xb6` 落在无人区，
   两次 u64 写还互叠（182/184 交错，dump 实锤）。
   以反汇编为准后一次走通。
3. 崩溃链（逐个命名，分辨率到指令）：
   `[r13+0x120]`（`+0x2d8` 空）→ `[rax+0x208]`（`+0xb8` 空）→
   `[r8+0x20]`（`+0x28` 空）→ 干净返回 3。
   `+0xb6/+0xba` 字节位无人读（元素制的影子）。
4. 返回 3 的来源未二分（PrepareTA 拒 vs SubmitTA 空转）：
   无提交流量（trace 仅 9 条 connect 链）；
   同步表全零（`flag&2` 缺席）与 `conn+0x400` 零值均可解释。
   下步：造 `flag&2` 条目（CreateSyncPrim 系只给 flag&1，
   需另寻 sync 写入面）再看 3 是否翻为提交尝试。

## 过程记录（诚实）

- 3 轮 GDB 方法学翻车：未初始化计数器；绝对计数在有/无 shim
  下漂移；`$r8==fd` 条件受 fd 分配影响；绝对地址断点两次诱发
  60s 挂起（伴 50MB `pread(fd16,EIO)` 递减循环 artifact，未归因，
  不碰）。
- 有效模式：`catch syscall` 计数杀（#104）、按名断点（pending）、
  纯 `run+bt`、objdump 静态回溯（r13 来源一次钉死）。
- /tmp 残留两次 ~50MB 循环 trace，已确认 3% 无碍，不影响门禁。

## 边界

- fabricated 结论不外推活体：返回 3 ≠ 提交；update 非零仍未见。
  伪 C 假设 cease at 反汇编（以执行为准）。
- 本轮无代码改动，门禁数不变（295+292）；会话零触碰。

## 下一步（候选）

- 手塑回合 2（离线，无需批准）：造 `flag&2` sync 条目 →
  看返回 3 是否翻为 `0x82` 提交尝试（桥 fabricated，
  观察 UMD 侧组装即可）。
- 活体（待批）：`0x82:0x14` observer 落桥 + `=2` 重载。
