# r295：spin 参数抓取未遂 + 同步原语输入锚点（批准执行）——数据丢失备忘

- **结论**：`=2` 窗口内 GDB 两次活体 attach 均干净 detach、进程继续 spin；反汇编钉死等待形状：`SyncPrimWait(rdi)` 以 `clock_gettime` 开头（有界等待的铁证）+ 双重解引用 `[[rbx+8]]` 取轮询值 + `+0x18` 取参；`SemWaitI(rsi=表, rdx)` 以 32B 步进循环调 `SyncPrimWait(&table[i])`（`0x400(rsi)` 为计数）。入口参未得（spin 中期寄存器已破坏：`mov $1,%edi`；`frame` 切换不回 unwind 无 CFI 的 stripped 帧）。离线兜底得手：fabricated 旧 trace 的 IN 字节是真 UMD 输入——`0x2:0x2`（16B）两次均为 `sync=0x6005/0x6009, off=0, val=0`（复位语义）；`0x2:0x7`（20B）含 UMD 指针 + `0x90` 尾。满足等待 = 写目标值到其 backing PMR 的设计已有具体锚点（handle/offset 已知，awaited value 待定）。
- **数据丢失（备忘，不遮掩；r295-gdb-args.txt 已找回）**：本轮暂存区（`build/traces/r295/`）在入库前消失（原因未命名；`find` 全盘无果）——**但 `gdb-args.txt`（寄存器抓取全文，22 行）在 `reports/` 下幸存，已随本轮入库**；丢失的是两份 8201 行 trace（与已入库四份同形：8201/单 submit3@8201，tail 分析已做）+ 两份失败尝试日志。 load-bearing 结论不受影响。教训：证据入库必须紧随采集，不攒到轮末。
- **Freeze 已恢复**（拆桥 + 默认 + L3 双绿，refs 1/1，窗口零新增 WARN）。另记工具卫生：`pkill -f musa_blit_test` 把自身 shell 一起杀死（模式串在自命令行）致会话闪断——以后只用 `pgrep -x`/`[m]usa` 守卫。

## 实测（执行过）

1. 批准：用户 standing 授权。`=2` 窗口（translator 全关）：blit#1（137/8201，observe `nonzero=40` 第 12 轮值）→ blit#2 后台 hang（R）→ GDB 活体栈复核（`SyncPrimWait→sched_yield`，经 `SemWaitI`）→ 寄存器抓取（rdi=0x7fcb84cf4090 落匿名映射/`[vvar]` 邻接区——即已被循环破坏，见上）→ detach 干净 → `maps` + 反汇编（`nm -D`：`SyncPrimWait@0x93c20`、`SemWaitI@0x93de0`）→ SemWaitI 表形状解出。
2. brekpoint 入口抓取失败：batch 语法错（pending 拒绝 + `commands` 位置错）致 blit 在 gdb 下快 abort；清 stray 后未及重抓即转入恢复（用户等 UI）。入口三元组仍未命名——r296 以修正 batch（`set breakpoint pending on` + `commands 1` 分行）一次抓取为准。
3. 恢复：清 stray → `rmmod`（开初被用户重开桌面挡回一次）→ 二次停→拆→默认桥 → L3 双绿 → 拉桌面（10 进程）。终态 refs 1/1。
4. 无代码改动，门禁状态沿用 r294（366+292）。

## 边界

- `rsi=stack` 的表基址（blit 栈上同步表）未读——入口抓取时一并拿（`bt` + `frame 3` 读 blit 帧局部，或直接打印 entry rdi）。
- awaited value 候选：1（fence 语义，r222 腿 1 形状）或 submit3 IN 中的 update 数组值（108B IN 本轮未解——trace 不记字节；fabricated 重放可解，需 UMD 对象指针，r259 限定）。
