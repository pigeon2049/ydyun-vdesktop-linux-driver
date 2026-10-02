# r110：size-0 免疫 perturb；整流重试环发现

堆喷洒验证闭环，结论三段（全离线 fabricated）：

1. **大块喷洒无效**（6×8KiB 全 B 填充前置，142 条不变）——
   尺寸类隔离：P（56B 级）邻居不可能是 8K 块。喷洒必须同尺寸类，
   大块是噪音。
2. **`MALLOC_PERTURB_=65` 下 size-0 依然**——槽位是新鲜 calloc 零，
   非残留/邻居（若是邻居/残留，perturb 必改写它）。
   故 +0x54 在本流程**从未被任何人写入**（r99 结论再确认一次，
   这次是扰动无关性证明）。
3. **意外收获：整流重试环**——perturb=65 下全流程跑两遍
   （284 = 142×2 逐条相同，含 connect/disconnect），某步返 `0x19`
   触发 R2DCreateContext 级重试后仍死于同一 size-0。
   说明：(a) UMD 堆内容确实能转向流程（喷洒作为方向盘有效）；
   (b) 存在外层重试语义——以后"调用数翻倍"优先查 0x19 重试，
   不必重走全链。

## 方向含义

+0x54 的填充者不在当前调用序列里——它来自 surface/layout 链
（R2DCreateSurface* 系）或设备状态装配。结合 r108（surface 需
context，context 需 +0x54），死锁形态完整：
**context 要 surface 填数，surface 要 context 给堆**。
解法只剩两种：找到打破死锁的第三调用（dev-select-ex？surface
独立堆路径？），或承认 Rogue2D 需要真实 surface（→ 活体 EGL/
显示通路，另立项）。fabricated 单步整形的路到此为止，
再往下是排列组合地狱——转去验证"第三调用"存在性，而非继续试参。
