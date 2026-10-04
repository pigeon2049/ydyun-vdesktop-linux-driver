# r131：桥增加 `ddk_feature_set` 开关（默认 0 = legacy 路径，零硬件触碰）

STATUS 下一步 1 的前置：r78 结论是 DDK2 需 `features+0x54 >= 2` 广告。本轮只做离线实现，**未加载、未重载任何模块**，活会话不动。

## 改动（实测：离线门禁 + 内核构建）
- `kernel/mt_pvr_device.h`：新增 `mt_pvr_features_set_ddk()`；传 0 为 no-op，非 0 只写 `+0x54`。`mt_pvr_features_init()` 行为不变。
- `kernel/recovery/mt_pvr_bridge.c`：`module_param(ddk_feature_set, uint, 0400)`，默认 0，在 `pvr_open` 里 init 之后应用。
- `tests/pvr_bridge_core_test.c`：+4 断言（0 无操作、2 落在 +0x54、不污染 core count、re-init 回到 <2）。

## 验证
- `make check-offline`：234 Python 全绿；C 272 checks OK（此前 268）。
- `make kernel`（W=1）无警告；`.ko` 含 `ddk_feature_set` 符号。
- 反向验证：把写入偏移改成 `+0x58`，测试在 `:364` 与 `test_device_layout` 红；还原后全绿。

## 边界（推断，未实测）
- 默认 0 时行为与旧模块一致，但新 `.ko` 尚未加载，`pvr_open` 路径只靠离线测试覆盖。
- 开关打开后 UMD 走 gated create 是 r78 的推断，新分配路径未经验证。

## 下一步（需用户批准）
重载 `mt_pvr_bridge`（带 `ddk_feature_set=2`）要卸活会话模块，违反 freeze；需明确批准、且可能需重建会话。
