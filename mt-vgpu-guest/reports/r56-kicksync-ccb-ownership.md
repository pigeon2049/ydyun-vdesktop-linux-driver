# r56：kicksync context 与 CCB 归属（翻译链的最后一块拼图定位）

承接 r53（kick 包无 work）/r54（PMR 全清单）。问题：CCB（命令环）从哪来、
长什么样。全程离线 + 只读用户态重放，零内核改动。

## 0x88:0x0 创建包（同期捕获）

IN 16 字节 `{hPrivData, ui32ContextFlags, ui32PackedCCBSizeU88}` 在合成路径
全零；OUT `{hKickSyncContext=0x102f, eError=0}`。已并入
`test_pvr_kick_packet.py`（202 项全过）。合成 kick 的 CCB 大小为 0——
真实渲染的 CCB 内容只能来自走完整绘制路径的 kick，本轮 ladder 给不出。

## CCB 是 server 侧分配的（rung6 证据）

rung6（create→destroy 全周期）的 bridge 流量与 rung8 的 render 部分完全
一致：12 PMR + 15 heap + sync，没有为 kicksync context 增发的 PMR/heap。
结论：CCB 不在 UMD 可见对象里，server 在创建 context 时内部持有。
翻译器因此必须拥有 **context 对象 = {CCB 设备内存 + 同步状态}**，
而不仅是句柄——当前 bridge 的 `MT_PVR_KIND_KICKSYNC` 空对象是已知缺口。

## 客户端侧形状（UMD 反汇编备注）

`RGXCreateKickSyncContextCCB` 是标准 UMD 包装（`PVRSRVAllocUserModeMem`、
AppHint、`GetSrvHandle`），work 藏在 server 侧。另记工具 caveat：
shim 的 `UMD_TRAP` 只在 fabricated 分支生效，passthrough 下静默不触发——
要抓调用栈须切 fabricated 模式重放（本轮未需要，未动）。

## 翻译器输入规约（现状）

齐了：kick 包布局（r53）+ 12 PMR/VA（r54）+ 对齐策略（r55）+ context/CCB
归属（本轮）。缺的唯一一块是**非零 CCB 的真实内容**，需要一次走绘制路径
的 kick 观察（真实渲染提交，超出当前 S4-1 ladder 范围，需单独批准）。
在此之前不写翻译器骨架——输入规约先行，代码随后。
