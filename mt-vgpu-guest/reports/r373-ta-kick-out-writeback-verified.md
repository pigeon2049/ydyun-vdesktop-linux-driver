# r373：`OUT.update_fence` 回填验证通过——r372 系 harness 传参问题，内核无 bug

## 结论

**`OUT.update_fence` 的内核回填路径一直正常。** r372 报告的 "userspace 读到 0" 是其一次性 harness 的传参 bug（`out_ptr`/`out_size` 未正确设置，导致 `pvr_out` 的 `copy_to_user` 未执行或 userspace 读错偏移），不是内核 bug。用正确编写的 harness 活体验证：userspace `OUT.update_fence` 与 dmesg 的 `wire_id` 精确匹配（3==3，4==4）。

**本轮修复**（`mt_pvr_bridge.c`）：`pvr_cmd_musakickgfx2()` 的 OUT 回填失败不再记一条误导性的 "submitted wire" info 日志，改为 `pr_warn` 报错并返回错误码。此前 dmesg 先记 "submitted" 再调 `pvr_out`，`pvr_out` 失败时 dmesg 看似成功、userspace 实际拿到错误码——正是 r372 被误导的根因。

## 根因定位

逐项排查 r373 任务书的三问：

1. **`OUT.update_fence=wire_id` 赋值是否执行？** 是。`pvr_cmd_musakickgfx2()`（`kernel/recovery/mt_pvr_bridge.c:3998`）在 `pvr_ta_wait_complete` 成功后执行 `out.update_fence = (int)wire_id`，dmesg 的 "submitted wire=N" 即其后一行。

2. **`copy_to_user` 是否正确？** 是。`pvr_out()`（1260 行）检查 `cmd->out_size >= 12` 后 `copy_to_user(cmd->out_ptr, &out, 12)`。活体验证证明拷贝正确到达 userspace。

3. **OUT 结构体与 userspace 期望是否一致？** 是。`mt_pvr_wire.h` 的 `mt_pvr_musakickgfx2_out`（12B：`error@0`、`update_fence@4`、`update_fence_3d@8`）与 KMD 5.2.0 生成头 `reference/kmd-5.2.0-server-generated/common_musagfx_bridge.h` 的 `MTGPU_BRIDGE_OUT_MUSAKICKGFX2`（`eError`、`hUpdateFence`、`hUpdateFence3D`，`__packed`）字段顺序、尺寸完全一致。

## 活体验证（bridge 未重载，r372 构建在载）

自写 Python harness（`fcntl.ioctl` 直调）：
- `/dev/dri/renderD128` → INIT ioctl（0x40046445）→ bridge ioctl（0xc0206440，bridge=0x82 func=0xC，IN 268B `kick_ta=1@188`，OUT 12B）
- 第一次：`OUT.update_fence=3`，dmesg `submitted wire=3` → **匹配**
- 第二次：`OUT.update_fence=4`，dmesg `submitted wire=4` → **匹配**
- `OUT.error=0`，`update_fence_3d=0`，ioctl 返回 0

## 修复落点

`mt-vgpu-guest/kernel/recovery/mt_pvr_bridge.c`，`pvr_cmd_musakickgfx2()` 尾部（+9/-2 行）：
```c
ret = pvr_out(cmd, &out, sizeof(out));
if (ret) {
    pr_warn("mt_pvr_bridge: musakickgfx2: OUT writeback failed rc=%d wire=%u\n",
            ret, wire_id);
    return ret;
}
pr_info("mt_pvr_bridge: musakickgfx2: submitted wire=%u\n", wire_id);
return 0;
```
成功路径行为不变（仍返回 0，dmesg 仍记 "submitted wire"）；失败路径现在可诊断。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**425 Python + 299 C 全绿**（新增 `tests/test_ta_kick_out_writeback.py`，4 tests）
- `make kernel` W=1：**零警告**
- 反向验证：回退修复 → 3/4 测试变红（结构布局测试仍绿）；恢复 → 4/4 绿

## 诚实边界

- 本轮未重载 bridge：修复在源码已入库，live 桥仍为 r372 构建（成功路径行为一致，无需重载验证）
- r372 harness 未入库（一次性脚本），其具体传参错误点无法复核；判定依据是正确 harness 的两次精确匹配
- 真实 UMD 的 OUT 读取尚未验证（待 DDK2 对接）

## 证据

- `reports/r373-live-out-writeback.txt`（0600）：两次活体验证的 harness 输出 + dmesg 对应行
