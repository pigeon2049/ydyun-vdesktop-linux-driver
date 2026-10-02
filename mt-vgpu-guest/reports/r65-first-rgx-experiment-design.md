# r65：首次 RGX 实验设计（待批准，未执行）

目标：把 translator T3 之前最后一段未知——真实 RGX 提交的实际行为——
变成实测。设计只复用树内已验证 106 帧的 `mt_live_3d` 路径，不发明新格式。

## 最小作业构成（全部已在树内）

- 11 个 context BO（`mt_gfx_context_bo_specs` + init data），1MB 间隔 VA；
  248 字节 CSW（`mt_gfx_context_build_csw`）嵌入 command BO +0x58，
  envelope +0x10 回填 CSW VA（`mt_gfx_packet_template.h`，0x46f0）。
- VM：create → bind ×12 → bind_boot_shared → upload → seal；
  进程 + 上下文（`node_type=5`，DM2 Universal）；`submit_context`
 （type=3 → opcode `0x66`，command_va + bytes）；fence 等待 + 读回。
- 单帧模式（`count=1`），先不做 render-target 落盘，只确认 fence 完成
  与事件无 fault；任何 `-E*`、超时、`event_result != 0` 即停。

## 前置条件（硬性）

- 重启进全新会话（本次 `fresh-trial --run --runtime-context` 重走），
  且**不加载任何 retained 实验**——`live_3d` 类要求
  `address_spaces/buffers.objects==0`，当前会话已有 retained TQX VM，
  插队必被 `-EBUSY` 拒绝。
- Bridge 保持加载（UMD 侧无感）；`live_3d` 用主模块会话直连，不走 bridge。

## 风险与回退

- 固件 fault（`0x101`）、HWR、会话 wedged 均有可能：首次只提交单帧，
  超时 5 秒；若 `pending` 卡住或 `event_result != 0`，停手并记录，
  最坏情况重启恢复（路径已验证多次）。
- 这是本机第一次 RGX 真实执行（此前真实执行只有 TQX copy + marker），
  按仓库约定需单独批准——本轮只写设计，不加载、不提交。
