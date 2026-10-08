# r347：T1 关闭——EnQueue 不发桥命令；0x82 静态普查无 0xC（离线，零硬件触碰）

- **结论**：
  1. `SubmitTADataEnQueue @ 0x54380` 不发出任何桥命令：其 8 处 `call 0x54230` 是队列拼接 helper（`PVRSRV­MemCopy`/`puts`/`DebugPrintf`），另 1 处 `call 0x2c740`——纯 userspace 入队，与其名相符。出队/提交走共享下游（RGXKickTA→SubmitTA 链，终点 `0x82:0x14`，r190/r346）。
  2. 本 UMD（5.2）group-`0x82` 静态 wrapper 普查（全部汇入 `0x92930` 分发器，按 `mov $0x82,%esi` + `mov $cmd,%edx` 配对，40 行窗口）：共 18 个，cmd = {1,2,3,4,5,6,7,8,9,a,b,d,e,f,11,12,13,14}。**`0xC` 缺席**（`0x0/0x10` 亦缺席）。S4 “`0x82:0xC` 边界”应改述为“本 UMD 无静态生产者”，而非“已知命令未实现”。
  3. T2 方向锁定：fabricated TA producer 打已观测的 `0x82:0x14`（observer 现成），不追 `0xC`。
- **附带发现**：`nm -D` 最小导出地址为 `0x39b90`——`0x82` wrapper 簇（`0x362ef–0x37c51`）及此前分析的大片逻辑皆为 static 函数；`info symbol`/`nm` 归属在此区间内会误指最近全局符号（r322 教训再添一例，r334 的区间 math 方法仍然有效）。

## 实测与边界

1. 本轮零硬件触碰：未加载、未重载、未跑 UMD；`bridge ref 0` / `probe ref 1` 不变，会话 freeze 继续。
2. 方法：`objdump -d` 全量管道直扫（`mov` 配对窗口 18→40 行复核一致）；`nm -D` 排序定导出下界。
3. **违规自记**：本轮两次把中间产物写进 `/tmp`（`full.asm` 全量反汇编、`nm.txt`，均已删除；`/tmp` 全程 1% 未满但规则即规则）。后续大体积中间结果走 `mt-vgpu-guest/build/traces/`，小体积走管道。
4. 未断言：`0x14` wrapper（`0x37c51` 一带）的直接调用者（未展开，不影响 T1 结论）；动态计算 cmd 的 wrapper 是否存在（静态普查覆盖不到，如实记）。

## 下一步

1. T2：fabricated TA producer（r210 配方移植：真实 render ctx + 手塑 psKickTA，目标 `0x82:0x14` 非零发出 + 返回 0；fabricated，不碰硬件）。
