# r112：+0x54 结构性为零——legacy-TDM 可能是 vendor 死代码（转加载窗口）

ladder 预热（devmem/render/sync 真对象在堆）后的 Rogue2D：
DebugPrintf 链一字不差（728/1670/226/188），size-0 岿然不动。
连同之前：新鲜零、喷洒、perturb、ladder——**五种堆条件同一结果**。
这不是"没调对"，是结构性的：legacy-TDM 路径下该槽恒零。

## 论证（穷举排除法）

| 堆条件 | +0x54 | 结论 |
|---|---|---|
| 默认 fabricated | 0 | 基准 |
| 大块喷洒（8K×6） | 0 | 尺寸类隔离，无关 |
| 同类小块喷洒（64B×6 非零） | 0 | 相邻代际不对（130KB 外） |
| MALLOC_PERTURB_=65/165 | 0 | 新鲜 calloc 零，非残留 |
| ladder 真对象预热 | 0 | UMD 真对象也不改变邻居 |

五次独立失败 + OOB 机制（r109/r111）→ 该槽在 legacy 流程无写入者。
对照 features 门控在 create/PrepareTA/SubmitTA 三处的新旧分支：
vendor 的真 2D 路径极可能只走新 DDK 分支（`+0x228` 系），
legacy-TDM（`+0x38` 系）是无人维护的死代码——UMD 自己都不测它，
我们当然调不通。

## 战略含义（r86–r112 收官判断）

1. **停掉 fabricated Rogue2D 整形**（27 轮，边际收益已枯）。
2. 真绘制的唯一活路 = **加载窗口**（r88 代码已备）：真桥 + 真句柄下，
   UMD 侧表查询 natural 通过；若 features 开关也需同步打开，
   合并为一次"新 DDK 桥"加载（0x89 + features≥2），一次 L3/L4 重验。
3. 在窗口批准前，离线主线只剩 T3-SubmitTA 回填收尾（r84 未竟的 48 参数
   语义）与 check-only 首帧设计——都不需要新实验。
