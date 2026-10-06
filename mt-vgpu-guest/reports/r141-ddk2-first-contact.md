# r141：`drm_major=2` 可达 DDK2——UMD 改发 3 个新桥命令，全被 `-ENOTTY` 拒（批准执行）

- **结论**：`drm_major=2` 的第一条活体链即分叉：`RGXCreateRenderContext → 37`
  （对照组 `=0` 同链全 0，直达 CCB create/destroy，与 r133 一致）。
  =2 轨迹 116 调用 vs =0 轨迹 91 调用；差值 = 3 个新桥命令
  （`0x82:0x12`、`0x88:0x5`、`0x2:0x2`，各 1 次，全 `-ENOTTY`）+
  `0x1:0xa AlignmentCheck ×2` 消失（恰为 r134 L22018 的预测）。
  桥已恢复默认参数并 freeze（ref 8/0，dmesg 零 WARNING）。
- 零 timeout；未提交 GPU 工作（kick 未触及）；probe 未动。

## 实测（执行过）

1. 桥 `drm_major=2`（读回 2，ref 0、无持有者）：r133 同链
   （rung5 + `RGXCreateKickSyncContextCCB b7* b5 u0 u0x33 u0x07 u0 b12` + destroy，
   pack `0x0733`）→ render **37**、CCB **37**、destroy 3，exit=0。
   证据：`/tmp/opencode/umda/r141-major2-ccb.jsonl`（易失；116 调用）。
2. 对照组：桥重载默认（读回 0）同链重跑 → render/CreateSyncPrim/CCB/destroy
   **全 0**，91 调用全 `ret=0`（= r133 的 91=91）。证据：`r141-major0-ctrl.jsonl`。
   故 37 分叉由 major=2 引起，非环境漂移。
3. 语料归属（`decompiled/linux-legacy-umd-5.2.0/decompiled.c`，按名查，未通读）：
   - `0x82:0x12` = `BridgeRGXCreateRenderContext2`（L11079；IN 12B = u32+ptr，OUT 4B）。
   - `0x88:0x5` = `BridgeRGXCreateKickSyncContext2`（L9915；IN 8B，OUT 8B 句柄）。
   - `0x2:0x2` 未定位（调用点模式未匹配；位于首个 ENOTTY 之后，或为错误路径调用，
     待实现后以活体上下文确认）。
   - 37 之谜解开：各 `*2` stub 在 BridgeCall 失败时统一 `return 0x25`
     （L9915/L9957/L11079/L11100），37 是 stub 失败常量，不是 MTSRV 错误码。
   - UMD 内 DDK2（`…2` 后缀）桥桩共 **18** 个（含 Compute/HWRT/CDM/TA3D/TDM/TL，
     Translator T3 的 Register 侧输入见遗留）。

## 推断（未执行，下一轮）

- 实现顺序应按依赖：`0x82:0x12/0x82:0x13`（render 建销对，legacy 形状在
  `pvr_cmd_handle_only/release` 侧已有对象模型）→ `0x88:0x5` 及其 destroy 对端
  → r134 预言的 CCB 期 SyncPrim/SubmissionBuf（届时才可达）→ `0x2:0x2` 按活体定名。
  IN/OUT 布局以语料调用点尺寸 + legacy handler 为双源，逐个配门禁测试。
- `r135-major2-ccb-create.jsonl`（未入库遗留文件，14:20）：桥调用序列与本轮 =2
  轨迹**逐项一致**（139 vs 143 行，差异仅为 seq/metering 行），同为 render-37
  中止形。结合其时间（drm_major 提交后 1 分钟）与文件名，判定为同一实验的早期
  跑序，已被本轮对照取代，不作为证据引用，文件本身不动。

## 遗留

- 76+ 提交未 push；`r135` jsonl 未入库文件仍在，未动。
- Translator T3（r113 首帧设计）排在本线之后：DDK2 render 先通，否则无真实 CCB 输入。
