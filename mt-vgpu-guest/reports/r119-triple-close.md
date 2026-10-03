# r119：三线并行收敛——SubmitTransfer 语义齐 + EGL 此路不通实锤

三 subagent 并行（离线）+ 主会话实锤缺件，一轮收齐：

## 1. SubmitTransfer 参数语义（subagent A，语料行号为证）

| 参数 | 含义 |
|---|---|
| local_544/540/53c | TA fence / TA update / 3D update 条数（三生成器 OUT） |
| uVar27 | TA3D sync 检查点（初 flags&0x10，后 0xffffffff/local_63c） |
| uVar25/local_2d0/local_d0 | 去重计数 / 合并 flags / 同步对象数组 |
| uVar14/uVar16 | fence 值（ctx+0x40/+0x38 对象 +0x30，空则 0） |
| lVar3/uVar1/local_57c/5ac | FW 块/上下文头/flags 掩码/partial bool（常量类） |

仅 lVar3/uVar6 为推断级，其余皆有赋值点实证。T3 输入至此**无盲区**。

## 2. FromDmaBuf 评估（subagent B）

绕不开 Context 侧 P+0x54（不同堆块，不可混淆）；至多做 Layout 侧
memsize 门旁路验证；且 surface 无 context 零桥调用（r108），
顺序死锁不变。结论：不单独开线，维持"先 Context"顺序。

## 3. EGL 此路不通（主会话实锤，subagent C 拼图补齐）

- `musa_dri.so`：**仅 1 个 T 导出**（`__driDriverExtensions` 还是 B 段）——
  不是功能 DRI 驱动，是桩子/装载垫片。
- `libglapi.so.0`（musa_dri 硬依赖）：本机不存在。
- 系统有 `libEGL(.so.1)`/mesa，但无 MUSA 后端可接。
故：EGL-on-MUSA 在 loader 层即死；Rogue2D（免 EGL）是唯一真绘制候选。
此条线关闭，不再投入。
