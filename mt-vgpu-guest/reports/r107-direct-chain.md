# r107：CCB 直调链钉死 + 堆非确定（方法论）

r106 的下步执行：bt 证明 `RGXTDMCreateTransferContextCCB` 由
`R2DCreateContext` **直接调用**（无 wrapper/select_ex 中转）；
create+8（P）= OUT 记录 `+0x10`，由 init 子调用（`0x14d80` 系）
在上下文装配期填入。`+0x54` 写入者仍未抓到，但搜索域已从
"全 UMD"缩到"init 子调用群及其写入"。离线，零硬件触碰。

## 方法论（堆观察三定律，实测得来）

1. 堆地址跨 run 非确定（同命令三连：`0x55e4…/0x55e7…/0x55a1…`）——
   绝对地址 watchpoint 跨 run 无效；r97 的"确定"只是短 streak 巧合。
2. 同 run 内计算地址有效（python-gdb 模板，r97 成功过）。
3. 无符号内部断点：exported 符号（CCB/DebugPrintf）永远优先；
   文件偏移换算只在同 run 映射下成立，禁跨 run 复用。

## 战略备注（给下一阶段）

 shaping 已连挖 13 轮（r95–r107），边际收益递减。两条出路：
 (a) 加载窗口（r88 代码就绪）：真桥 + 真句柄下，大量 UMD 侧表查询
     可能自然通过，现状很多"零值"源于 fabricated 假句柄；
 (b) 承认 Rogue2D 需要 surface 先行，打破"Context 优先"的假设——
     但 surface 也要 Context 堆（r106 死锁），除非 surface 创建
     不依赖 context（待验证 R2DCreateSurfaceLayout 独立调用，
     r101 已证明它独立可调但返 3）。
