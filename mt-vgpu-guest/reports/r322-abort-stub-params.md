# r322：abort 桩命中读参 + 结构转储（批准执行）——`RGXTDMSubmit` 内自杀

- **结论**：GDB 文件脚本法（`catch load` + `commands` 放脚本文件，batch `-ex` 展不开多行块——r296/r320 教训定论）断 abort 调用点成功命中三次：① 桩入口寄存器已破坏（raise 覆盖）；② 桩参数 `rdi` 非字符串而是**堆上 job 描述结构**（指针×5 + `0x101c/0x101d` + `0x3a1` + cookie + `0x8e`，两次运行同形）；③ `info symbol` 定锤 abort  helper 在 **`RGXTDMSubmit` 内**（`0x88650` 处尾跳直达 abort 调用点；全二进制直接 call 零命中）。abort 是 RGXTDMSubmit 组装/校验 surface 描述符时的断言自杀。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**无代码改动。
- **仍未命名**：abort 校验的具体条件（静态反汇编错位严重：0x60180/0x88600 一带数据表混杂线性解码；`0x88650` 周边 0x70 条件跳_BUSCAR 未果，它是共享 die 跳板——跳入者不在附近）。候选：job 结构某字段（NULL/计数）或某 bridge 返回的派生值。

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → `=2` → 三次 GDB 运行（桩入口/调用点/结构转储；`start` 无 main 问题绕过——直接 `run` + catch 文件脚本）→ 读寄存器/结构/bt → kill → 拆桥 → 默认 → L3 双绿 → 拉桌面（用户中途重开挡回一次，二次停后关账）。
2. 证据：`r322-abort-struct.txt`（0600，桩入口寄存器）+ `r322-abort-struct2.txt`（0600，24×u64 结构转储）+ 既有 `r317-tqperf-core.bin`（栈交叉验证）；暂存区已清空。门禁沿用（386+299）。

## 边界与下一步

1. 下一刀（离线优先）：`RGXTDMSubmit` 真入口对齐反汇编（以 `nm -D` 符号为锚逐段跟随，而非线性扫）找 abort 分支比较的操作数；或对 job 结构做差分（不同参数运行对比哪字段变化——quartz 慢但准）。
