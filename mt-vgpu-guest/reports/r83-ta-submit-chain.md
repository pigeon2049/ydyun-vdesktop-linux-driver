# r83：TA 提交链打通到桥（0x82:0xc 全字段）+ fabricated 整形中

T3 第二锹：TA kick 的完整调用链已静态打通并定位到桥入口
**`0x82:0xc`（268B IN / 12B OUT，`BridgeRGXKickTA3D2`）**，
48 参数包装器的逐字段映射已列出；fabricated 整形把
`RGXKickTA` 从门卫一路推进到 PrepareTA 深部（6 轮迭代，
每次崩溃点都是下一个精确需求）。`0x82:0xc` 尚未在 fabricated
下实际发出——psKickTA 还需要同步对象等字段，下一轮继续。
全程离线，零硬件触碰。

## 调用链（语料行号即证据）

```text
RGXKickTA@0x7afd0 (L53180, 68行)
 ├─ 门卫：psKickTA≠0 && [psKickTA+0x30]≠0（与 triage 的 kickbuf 备注一致）
 ├─ PrepareTA=FUN_00178800 (L52052, 218行)：TA 提交记录 + 控制字
 └─ RGXSubmitTA=FUN_001796b0 (L52471, 709行)
     └─ BridgeRGXKickTA3D2=FUN_00136ec0 (L10620)：0x82:0xc，IN 0x10c / OUT 0xc
```

我方桥对 `0x82` 仅实现 `0x2/0x3/0x8/0x9`——`0xc` 进来是 `-ENOTTY`，
与 triage"止于 PrepareTA"的旧观察一致（旧观察只看到 UMD 侧停止，
现在知道它停在向桥要 `0x82:0xc` 的前一步）。

## `0x82:0xc` IN 字段骨架（包装器 L10675–10725，基址 `&local_140`）

`+0x00`=param_48；`+0x08`=param_38；`+0x10`=param_40 …（33 个 8B 槽，
u32 对拼 `CONCAT44` 占多数，尾 4B 未命名）；OUT 12B =
`{eError（初值 0x25=37）, fence×2（经 param_20/24 回写）}`。
各 param 回填到 `RGXSubmitTA` 的实参是下一轮的活（SubmitTA 709 行，
本次只定位了桥调用点，未全读）。

## fabricated 整形日志（执行验证，非纯静态）

| 迭代 | 输入 | 结果 |
|---|---|---|
| 1 | `b30*`（解引用得 0） | `→ 3`，门卫拒绝（传值误用，教训） |
| 2 | `b30` + `+0x30`=u1 | 崩 `mov 0x24(%rcx)`，rcx=1——`+0x30` 是**指针**，指向子结构（`+0x24` 再读） |
| 3 | `+0x30`→512B 缓冲 | 崩 `movl $0,0x120(%r13)`——r13=`[psKickTA+0xb6]` 为空 |
| 4 | `[byte 0xb6]`→缓冲 | 同址崩——**Ghidra uint* 指针算术陷阱**：`param_2+0xb6` 是 0xb6 个 uint = **byte 728**，不是 byte 0xb6（r74 DDK2 `+0x32` 同类错误重犯，已记） |
| 5 | `[728]`→缓冲、`[736]`→缓冲 | 新 RIP（越过 `+0x120` 写入，崩溃前移） |

psKickTA 下限 ≥ ~0x760 字节；TA 缓冲（`+0x30` 槽）2048B 暂够。
同步对象字段（SubmitTA 的 sync 查询，L52790 "Queried an invalid TA3D sync"
分支）是已知的下一需求——整形目标明确，不再盲扫。

## 边界与下一步

- 本轮无新 trace 归档（6 次崩溃 trace 无桥流量；RIP 行即证据）。
- 下一步：SubmitTA 内 `param_4…` 回填映射（定位 48 个包装器参数的来源），
  + psKickTA 同步槽整形；仍离线，不需批准。
