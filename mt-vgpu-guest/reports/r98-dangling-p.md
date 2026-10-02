# r98：P 是悬空指针——calloc 回收复用实锤（r96/r97 修正）

r97 的下步（硬件观察点）执行完毕，结论反转：`P+0x54` 不是"没人写"，
而是 **P 指向的堆块已被释放，CCB 自己的 calloc 把同一块内存回收复用**
（观察点两次命中，bt 全在 `calloc ← RGXTDMCreateTransferContextCCB`）：
`0 → 32767 → 0` 即分配器元数据 + 清零。r97 读到的
`+0x50=0x3000/+0x58=0xf` 是释放后残留的结构化内容，不是活对象字段。
离线 gdb，零硬件触碰。

## 证据（watchpoint 双命中回溯一致）

```text
Hit1: 0 → 32767  @ calloc ← CCB ← R2DCreateContext ← main
Hit2: 32767 → 0  @ calloc ← CCB ← R2DCreateContext ← main
（随后重复：CCB 的 0x19 重试循环在反复 calloc）
```

## 含义（自我修正 r96/r97）

- "计数槽空"不准确：槽位属于已释放对象，不存在"正常应填"的值；
  size-0 是悬空读取的偶然结果（恰好残留/清零为 0），不是"缺省零"。
- 真问题上移一层：**谁释放了 P 的属主**（或：P 从一开始就是未初始化的
  野槽）。候选：StaticMem 失败路径的提前释放（r92–r95 的 MapMem 链
  在同一 run 内先走过！）、rogue2d 的错误清理把后用对象一起放了。
- 与 r92–r95 贯通：MapMem 失败 → StaticMem unwind（0x89:0x6×2 等释放）→
  TransferContext 拿到悬空 P → size-0 → 最终 `return 3`。
  **很可能只有一个根因（MapMem），后面全是级联。**

## 下一步（单点）

确认级联：修好 MapMem（r92 的 import 句柄修 + r95 的尺寸问题实质是同一入口）
后重跑，看 TransferContext 的 P 是否自然转正（悬空消失）。
即：Rogue2D 链只差 MapMem 一口气，不用再追 P 本身。
