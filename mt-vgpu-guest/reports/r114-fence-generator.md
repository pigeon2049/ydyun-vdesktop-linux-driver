# r114：fence 生成器语义——同步表到三元组的最后拼图

T3 输入侧收官：`FUN_001770f0` 即 `SyncUtilGenerateFenceData`
（具名，非 FUN）：遍历同步表（`param_1[6]`=条数，`+0x10` 起步长 6
u32 的条目），对 flag&1 者逐个 `SyncPrimLocalGetHandleAndOffset`
取回 `{handle, offset}` + 值数组（`param_4/5/6` 三输出），带客户端
上限钳制（超限返 3 并打印）。update 侧 `FUN_00177840` 是 17 行薄皮，
转调 `FUN_00177360`（同构，参数位对应）。

## 链条闭环（输入侧全通）

```text
客户端 kick 结构（check/update 数组，r72/r74 已映射）
 → 同步表（SubmitTA 内组装，r84）
 → SyncUtilGenerateFenceData / UpdateData（本轮：三元组语义）
 → 0x82:0xc 48 参数（r83/r84 字段+回填）
```

我方翻译器对应物（0x88:0x4 路径）：T1（数组拷贝）= 前两步的精简版，
T2（UFO→GPU PA+offset+值校验）= 生成器输出的直接等价——
**r113 首帧设计输入侧的每一字节都有了厂商侧出处**，
不再有"不知哪来"的字段。剩下的只是 DM2 发射（我方信封已验证）。

## 附带（钳制一致性）

厂商侧同样钳制输出条数（超限 3）——r62 的
`MT_PVR_KICK_SYNC_MAX=64` 钳制与厂商行为同构，门禁有效性再确认。
