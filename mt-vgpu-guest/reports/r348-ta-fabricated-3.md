# r348：T2-a——fabricated `RGXKickTA` 跑通不崩，`PrepareTA=0`，3 来自 `SubmitTA`（离线 fabricated，零硬件触碰）

- **结论**：新脚本 `scripts/ta-kick-attempt1.sh`（纯 harness，可复现，回应 r259）双映射（A：`rdi=render` 对象；B：`rdi=conn`） fabricated 单发，进程 `exit=0`、无崩溃：`RGXKickTA(...) -> 3`，`0x82:0x14` 未发出（trace 121 行止于 sync ioctl，无 CCB 落盘）。返回归因（GDB 返回地址断点，两步走）：
  - 守卫点 `0x7b198` **未命中**——3 不是入口守卫；
  - `PRET rax=0`（`PrepareTA` 全过）→ `SURET rax=3`（`SubmitTA @ 0x796b0` 返回 3）。
  r194 的“返回 3”至此平反：当年大概率同样是 SubmitTA，而非 `+0x30` 守卫。
- **整形三跳（fabricated GDB，零硬件）**：
  1. `MTSRVGetClientEventFilter+0x29`（`0x3fb09`：`*(r14+rax*4)`，`r14=*(rdi+0x50)`）→ 补 `rdi+0x50` 零数组；
  2. `+0x55`（`0x3fb35`：`(r15+rax*4)`，`r15=*(rdi+0x28)`）→ 补 `rdi+0x28` 零数组；
  3. `PrepareTA+0x630`（`0x78e30`：`0x20(%r8)`，`r8=*(psKickTA+0x28)`）→ `psKickTA+0x28` 指零缓冲；之后干净返回。
  每次崩溃点即下一字段的出生证——手塑收敛方法有效。
- **`SubmitTA` 初探**：入口解包 `(rdx)/8(rdx)/(r10)/8(rsi)`；被调者仅 `0x9c250×2/GetFeatures/PVRSRVGetFabricType/GetSrvHandle`——3 大概率出自 `0x9c250` 校验（未证实，T2-b）。

## 实测与边界

1. 全程 fabricated：`umd_connect_harness` + shim 默认模式（无 PASSTHROUGH），UMD SHA `b3058c02…` 对版，`musa.ini` 工作目录；无模块、无 DRM、无 PCI、无 GPU。门禁沿用（394+299，无代码改动）。
2. 证据：`r348-ta-attempt1-mapA/B.txt`（0600：双映射 `-> 3`）+ `r348-ta-attempt1.jsonl`（0600，121 行）；暂存区已清空。GDB 单行命令未落盘，崩溃地址记正文。
3. T2 总目标（非零 `0x82:0x14` + 返回 0）**未达成**——停在 SubmitTA 返回 3，不 inflated。
4. 本轮未停桌面（无持有）；未动 bridge（`ref 0`）/probe（`ref 1`），freeze 继续。

## 下一步

1. T2-b：`SubmitTA` 的 3 归因（`0x9c250` 校验？入参？）——返回地址断点逐个被调者（离线 fabricated）。
