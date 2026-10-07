# r195：flag&2 注入未改变 RGXKickTA 结果——update 路径不在该入口（fabricated，零硬件触碰）

- **结论**：连接对象中手塑一个 `flag&2` sync 条目后，`RGXKickTA` 仍返回 3，trace 只有连接初始化、没有 kick ioctl。对过 SHA 的 UMD 调用图显示，`RGXKickTA` 不调用 `SubmissionSetUpdateSyncPrim`；该 helper 的调用者包括 `RGXKickGfx`、`RGXMultiKickGfx` 等。r194 的“造 flag&2 后看 RGXKickTA 是否提交”假设选错入口。下一步转向真正编组 update 的 producer（优先 `RGXKickGfx`），重建其输入并在 fabricated bridge 观察 `0x82:0x14`。

## 实测（fabricated 离线）

1. 使用树内 harness、假桥与 UMD `libsrv_um_MUSA.so.1.0.0`；执行前 SHA-256 实测为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与 `DECOMPILATION.md` 一致。零硬件触碰。
2. 复用 r194 的 `psKickTA` 手塑字段，另分配 local sync 与 block 对象；在连接对象写入首条目（flag=2、sync 指针、value=7）和 `conn+0x400=1`。`RGXKickTA → 3`，进程干净退出。
3. 新 trace `r195-takick-flag2.jsonl` 共 16 条记录，包含连接初始化；没有 `0x82:0x14` 或其他 kick 请求。注入字段是否被该入口消费，本轮未证明。

## 调用图核对（静态语料，作为路径假设）

- `RGXKickTA`（`0x17afd0`）调用 `RGXPrepareTA` 与 `FUN_001796b0`，其调用边不含 `SubmissionSetUpdateSyncPrim`。
- `SubmissionSetUpdateSyncPrim`（`0x15f760`）的调用者包括 `RGXKickGfx`（`0x17e230`）、`RGXMultiKickGfx`（`0x17f680`）及其他提交 helper。故本轮手塑的 `RGXKickTA` 不能验证 update 编组是否会进入 `0x82:0x14`。
- **推断**：下一步应手塑 producer 层输入，让真实 UMD 编组 `flag&2`，而不是继续调整 `RGXKickTA` 参数。字段布局须继续用离线执行验证，不能把伪 C 当实证。

## 边界与下一步

- 本轮无代码改动、无硬件操作；live bridge 仍 freeze。未验证真实 `0x82:0x14` handler，也未证明 update 被设备接受。
- 下一轮：按名查询 `RGXKickGfx` 及其必要输入/同步对象，使用 fabricated harness 找到可到达 producer 的最小调用；有 kick 请求时先记录输入形状，再讨论 observer/live 验证。
