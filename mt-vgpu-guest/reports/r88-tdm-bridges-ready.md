# r88：0x89 TDM 共享内存桥已实现（离线全绿，未加载）

r87 的卡点已清除于代码侧：`0x89:0x5/0x89:0x6` 落桥（真 8 KiB arena
PMR 双别名 + 正常释放路径），L1（226 Python + 268 C）全绿、
内核 `W=1` 构建 exit 0 无新增警告、反向验证通过。
**新模块未加载**（活会话仍跑 `894faf50`；重载需明确批准），
故 Rogue2D 的下一步（fabricated 重放看 UMD 是否越过卡点）
要等加载窗口。本轮零硬件触碰（纯用户态测试 + 构建）。

## 实现（`feat` 提交 `95a7492`，纯加法 196 行）

- wire：`mt_pvr_tdm_shmem_out {u64,u64,u32}`（eError 居尾，20B）、
  `mt_pvr_tdm_release_in {u64}`（8B）/ `_out {u32}`（4B）+ static_assert。
- 桥：`MT_PVR_BRIDGE_RGXTDM=0x89` 分发；`pvr_cmd_tdm_shmem`
  （`pvr_pmr_new(0x2000,12)`，双别名同一 handle，单生命周期）；
  `pvr_cmd_tdm_release`（`pvr_pmr_put`，二次释放诚实 `-ENOENT`）。
- 语义假设 H1（待活体验收）：两槽同 PMR 可满足 MapMem/MapUSCMem；
  若分叉则拆双 PMR + 配对表（wire 注释已留升级路径）。

## 门禁（本轮实跑）

- 新 `test_pvr_tdm_shmem.py` 5 项：offsetof 三字段 + 20/8/4 尺寸 +
  分发/真 PMR/双别名单释放三形状断言。
- 附带修：`test_pvr_wire_sizes.py` 覆盖测试强制新结构入 MAPPING——
  TDM 以 `None` 接入（无厂商生成头可 diff，尺寸由 static_assert +
  新测试守；生成器产物 `stage-b-*.json` 未动）。
- 反向验证：`ptr2=0` 注入 → 新测试 1 失败；还原 → 全绿。
- `make kernel` exit 0（仅 4 处 r80 已记录旧警告，无新增）。

## 待加载窗口（需批准）

换桥（rmmod/insmod）→ 重跑 L3/L4 全阶梯（防回归）→ fabricated
Rogue2D 重放（看是否越过 `0x89:0x5`，OUT 非零即赢）→ passthrough
抓包。任一步骤红灯即停，H1 错则按升级路径拆分。
