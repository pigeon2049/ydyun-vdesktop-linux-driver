# r84：SubmitTA 回填映射——fence/update 双生成器 + 同步组装

T3 第三锹：`RGXSubmitTA`（L52471）内两处桥调用点的 48 参数回填已逐项
定位，同步语义的核心生成器全部点名。链条至此静态完整：
psKickTA → PrepareTA（记录）→ sync 查询/组装 → fence/update 双生成 →
`0x82:0xc`。下一步是执行验证（fabricated 同步整形或活体 KickTA），
不再是静态问题。全程离线，零硬件触碰。

## 回填表（包装器形参 → SubmitTA 来源）

| IN 偏移 | 包装器 param | 来源 | 含义 |
|---|---|---|---|
| +0xD0 | p15 | `local_510`（`CONCAT44` tag 2/3 组装） | TA3D sync 描述符 |
| +0xE0 | p38 | `uVar6 = [puVar5 + uVar11*2 + 0x76]` | TA 上下文数组槽 |
| +0xA8 系 | p3–p6 | `FUN_001770f0` "generate TA fence data" → local_544/280/4b0/460 | fence 侧 |
| +0xB0 系 | p7–p10 | `FUN_00177840` "generate TA update data" → local_540/1f0/410/3c0 | update 侧（p8 = kick 计数器） |
| — | p18/p22 | `[lVar12+0x48]/[lVar12+0x4c]`（lVar12 = record[3]） | kick 对象同步槽 |
| — | p19/p23 | `[TAbuf+0xC4]` / `[TAbuf+0x630]`（-1 缺省） | fence 值/更新值 |
| — | p26–p30 | `-(cond)&0x168 / lVar23 / -(cond)&0x220 / lVar4 / 0x220` | fence 掩码三件套（lVar23/3/4 = record[0/1/2]） |
| — | p31/p32 | lVar3（= record[1]）、`[lVar12+4]` | TA 状态指针 + 标志 |
| — | p39/p40 | 路径 2 才非零：`[[lVar12+0x38]+0x30]` / `[[lVar12+0x40]+0x30]` | 附属 sync 值 |

## 同步组装（L52755–52820，"generate…" 双子 + 查询器）

- `FUN_001a0f70(sync)` = "Query TA3D sync"，失败即 `"invalid TA3D sync"` 中止——
  fabricated 整形的下一个硬需求：有效的 TA3D 同步对象（真 sync prim）。
- 描述符编码：`CONCAT44` 打包 + tag（2 = 一致，3 = fence 值漂移）+
  `local_63c` 递增计数；每组装完调 `FUN_0014a380(plVar22,1,1,…)` 累加入表
  （plVar22 = 同步表 builder）。
- "no overlap sync"分支：`[TAbuf+0x5C]≠0` 时查 `puVar2[7]`，tag 3，
  `local_500 = {local_650, [TAbuf+8]}`。
- 重试语义：桥返回 `0x19`（= eError 初值 0x25？否——0x19=25 与初值同数，
  即 TRY_AGAIN）时 `PVRSRVEventObjectWait` 后重发；我方桥 `-ENOTTY`
  是传输错，直接 break（r81"wait 恒成功"在此无影响——压根没走到 wait）。

## fence/update 对偶（翻译器形状）

0x88:0x4 的 check/update 二分在 TA 层是 fence-data/update-data 两个
生成器（`0x11` 通道号相同，输出槽不同）。T1/T2 的输入规约在 TA 层的
对应物就是这两套输出——check-only 首帧假设（r79）对应到"只喂 fence
生成器输出"的最小翻译。方向一致，无矛盾。

## 下一步（执行验证，需选一条）

A. fabricated：整形出能通过 `Query TA3D sync` 的同步对象（真 sync prim
句柄 + TA 上下文数组），看 `0x82:0xc` 是否发出（桥回 `-ENOTTY` 即算到达）。
B. 活体 passthrough `RGXKickTA`（需批准）：同步对象全真，UMD 自然走完全链，
`UMD_DUMP_BRIDGE=0x82:0xc` 直接抓包——比 A 快，但动活会话。
