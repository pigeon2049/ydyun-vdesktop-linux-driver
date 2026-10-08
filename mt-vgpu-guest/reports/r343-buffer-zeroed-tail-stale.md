# r343：修正 r342——rdx 缓冲 `[0,0x820)` 由 app 清零，非零尾部 `+0x820` 起是残留栈（离线，零硬件触碰）

- **结论**：r342 把 `rdx` 缓冲 `+0x820/+0x828/+0x838` 的非零值解读为“app 在清零后填入”，这一解读撤回。反汇编闭合：
  - `0x3ffc: mov %r12,%rdi` 之前 `%r12` 已在 `0x3c5c` 设为 `-0x850(%rbp)`，`0x3cc0–0x3ffc` 之间无任何对 `%r12` 的改写；
  - `0x3fff: mov $0x104,%ecx` + `0x400b: rep stos` 清零 `0x104 × 8 = 0x820` 字节，即 `[-0x850(%rbp), -0x30(%rbp))`；
  - `0x4023: mov %r12,%rdx` 将同一缓冲交给 `RGXTDMQueueTransferNew`。
  因此传入的 `[0,0x820)` 确为全零（与 r342 活体读数一致：`RDXNZ` 只列出 `0x820` 以后的项），而 `+0x820` 起落在清零范围之外，值为未初始化的栈残留（指向栈的指针），不是 app 写入。
- **修正后的图景**：copy-setup 传给 transfer API 的描述结构整体为零初始化；计数槽（`ctx+0x58→+0x20→+0xc`，r337/r338）同样为零。两者都是“空”，且 app 侧不负责回填——这与 r341 “生产者即 copy-setup 自身”的结论一致，r342 的“非零尾部”论据不成立，应删去。
- **旁证（3550/3c81）**：`-0x850(%rbp)` 在 `0x3c92 MTSRVAcquireCPUMapping` 中作为出参写入 CPU 映射指针，随后 `0x3cae` 读出用于后续调用；`0x3ffc` 的整体清零在其之后执行，意味着映射指针不会留到 transfer 调用时。这一点只由反汇编得出，未做活体对照，标为推断。

## 实测与边界

1. 本轮零硬件触碰：未加载、未重载、未跑 UMD；`bridge ref 0` / `probe ref 1` 不变，会话 freeze 继续。
2. 证据来源：`objdump -d` 对 `musa_tq_performance_test`（`0x3c50–0x3ffc`、`0x3ffc–0x4040`）的对齐反汇编；r342 活体读数（`r342-app-args.txt`）作为清零边界的对照。
3. 未决：`QueueTransferNew` 调用后 transfer 结构如何被 TQ 侧填充（若真需要计数非零），须看 `RGXTDMQueueTransferNew`（`0x614e0`）及其 `jmp TQJobSubmit` 之前的参数消费，这属于下一步离线 RE，不需要硬件。
4. 本轮不做活体复验：残留栈的内容本身对结论无影响（清零范围之外无论值为何，都不参与 transfer 的 `[0,0x820)` 语义）。
