# r193：psKickTA 构造 recon——无铸造函数，harness 须手塑（离线）

- **结论**：psKickTA 无 UMD 侧铸造函数（全语料无 CreateTA*；
  `RGXPrepareTA` 只读/写调用方传入结构，不分配）。
  其关键锚点是真实 render-context 对象（`+0xc` 非空即过第一关；
  `+0x1c8` 系由 PrepareTA 自己回填）。于是 TA ladder 的下一
  步是 bounded 的：以真实 render context 为锚手塑 psKickTA
  （续 r85），而非找不存在的 create API。本轮纯离线，
  无代码改动。

## 实测（语料按名；伪 C 为假设）

1. `RGXPrepareTA`（`FUN_00178800`，218 行）：`param_2+0xc==NULL`
   直接返 3；读 `+0xb6/+0xb8/+0xba`（输出句柄槽）、`+0` 标志位
   （`0x10/0x20` 选分支）、render 上下文表；写 `+0x1c8–0x1d4`
   （prepared state 回填）。features`+0x54<2 / >=2` 在函数内
   第二次出现（`+0x38/+0x228` 布局二选一）——与 r134 同门，
   `=2` 仍是 DDK2 形状的前提。
2. 全语料 `*Create*TA*`/`*PrepareTA*`/`*SubmitTA*` 仅
   `SubmitTADataEnQueue`、`Init/FiniMultiThreadSubmitTA`、
   `RGXKickTA`——无铸造函数。导出表 496 符号，无 TA-create。
3. 于是调用方（app/harness）自备 psKickTA；harness 路径下
   `+0xc` 可取真实 `RGXCreateRenderContext` 产物（桥已全绿），
   这是 r85 那堵墙的可攀爬面。

## 建议（park 还是继续）

- 继续（离线，无需批准）：psKickTA 手塑回合可在 fabricated shim
  下做（r152–r165 先例）——以真实 render 上下文为锚，逐字段满足
  PrepareTA 前置，GDB 监督看死在哪一关。
- 活体（待批）：`0x82:0x14` observer 落桥 + `=2` 重载 +
  harness rung 链；update 项验证仍需此三件套。
- 不建议：找罐装 3D 样例（blit/compute/tq-perf/twiddling 全系
  不进 TA，r192 已定）。

## 边界

- 伪 C 为路径假设；`+0x30/+0x4/+0xb6` 语义待整形实测。
- 语料 SHA 沿用 r159；本轮无代码改动，门禁数不变（295+292）。

## 下一步（候选）

- psKickTA 手塑回合：fabricated shim 下跑真实 UMD（桥是假的，
  零硬件触碰，r152–r165 先例），GDB 监督看 PrepareTA 死在哪一关。
  无需硬件批准即可立项。
- 活体（待批）：`0x82:0x14` observer 落桥 + `=2` 重载 + harness
  rung 链；update 项验证仍需此三件套。
