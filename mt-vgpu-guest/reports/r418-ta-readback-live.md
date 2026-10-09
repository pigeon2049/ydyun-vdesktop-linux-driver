# r418: 双门控回读活体——路径通、ABI bug 修复、固件超时（Q0 待深挖）

> Round r418 (2026-10-09). **最高风险轮。** 双门控构建 + `mt-ta-readback` 真机单发。

## Conclusion

**回读路径机械打通，但活体暴露一个真实 ABI bug；修复后提交成功，固件 5s 超时未完成。**

1. **ABI bug（已修复）**：`struct mt_pvr_ta_readback_in` 内核侧未 packed（24B）vs userspace packed（20B）→ `pvr_in` 报 `-EINVAL`。r416/r417 的离线测试未捕获（只验源码结构，未验实际 sizeof）。修复：`__attribute__((packed))`（1 行，`kernel/mt_ta_real.h:223`）。
2. **提交成功**：修复后 0xFD 全路径执行——pvr_in 通过 → ctx 查到 → target_ready → `mt_ta_submit_real` 成功 → fence 分配 → `dma_fence_wait_timeout(5s)`。
3. **固件超时**：5s 内无完成事件（`-ETIMEDOUT`），签名 "submitted-but-ignored"。r414 同结构 TA（Q0=0）219µs 完成；本轮 Q0=`target_va|0x48000000000`（[INFERRED]）后超时。**Q0 编码很可能不对**——不断言，按 r380 教训不做盲探。
4. **Teardown 受阻**：pending TA fence 持有 bridge ref=1，`safe_rmmod.sh` 正确拒绝（未用 -f）。桥仍在载，系统稳定，无 oops/WARN。待用户冷重启清除（同 r406 前例）。

## Live evidence

dmesg（`build/traces/r418/dmesg-r418.txt`，0600）：
```
[ 5734.755973] mt_pvr_bridge: r416: target BO bound va=0x7b000000 bytes=16384
[ 5734.755976] mt_pvr_bridge: r389: render context READY (11 BOs, CSW, exec)
```
- 12th target BO 绑定成功（0x7b000000，16KB）——r416 设计活体确认。
- 调试过程：`r418dbg pvr_in ret=-22 in_size=20 need=24` → 定位 ABI bug。

userspace：
```
[*] connected bvnc=0x23000406600017
[*] render context handle=0x1000
check failed line 161: 0 errno=110   (ETIMEDOUT，修复后)
```
（修复前：`errno=22` EINVAL。）

## 过程记录

1. Pre-live T1/T2/T3：522+625 全绿。
2. 双门控构建（`MT_TA_READBACK_DEBUG=1` + `MT_TA_REAL_PACKET=1`，intentional static_assert 临时中和）：`make kernel` W=1 **零警告**。
3. `insmod` 新桥（ref=0），跑 `mt-ta-readback /dev/dri/renderD128` → EINVAL。
4. 临时 dmesg 调试（已移除）定位到 `pvr_in`：in_size=20 vs need=24。
5. 根因：内核 struct 未 packed。修复 1 行，重建（零警告），重载，重跑 → ETIMEDOUT。
6. `safe_rmmod.sh` 拒绝（ref=1，pending fence），未强卸。dmesg 干净。
7. 门控/断言已 revert（源码树仅保留 packed 修复）；证据已归档。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**522 Python + 625 C 全绿**
- `make kernel` W=1：**零警告**（双门控测试构建 + 默认构建）
- 反向验证：未单做（ABI bug 本身即反向证据：修前 EINVAL / 修后 ETIMEDOUT）

## 交付物

- 本报告 `reports/r418-ta-readback-live.md`
- 证据 `build/traces/r418/dmesg-r418.txt`（0600）
- `reports/README.md` 主线表 +1 行
- `MEMORY.md` 顶部插入 r418（§4）
- `PROGRESS-SNAPSHOT.md` §12 追加 r418

## 诚实边界

- **Q0=`va|0x48000000000` 仍 [INFERRED]**：固件超时强烈暗示编码不对，但不做活体盲探；需离线反汇编深挖 TA state buffer 的 target 字段语义。
- 像素回读未验证（固件未完成，无像素可读）；T2 仍 open。
- 生产代码零改动（packed 修复是 ABI 对齐，非行为变更）；门控默认关闭。
- 桥仍在载（ref=1），待用户冷重启。

## 下一步

1. **P0（离线）**：反汇编深挖 Q0 target 字段的真实编码（`FUN_00169240` 的 Q0 构造逻辑）。
2. **P1（活体）**：Q0 修正后重跑 `mt-ta-readback`，验证像素。
3. 备选：若 Q0 长期无解，T2 回读可降级为"完成码验证"（r414 已达），像素级验证待 UMD。
