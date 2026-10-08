# r356：`0x82:0xC`（MUSAKICKGFX2）wire 结构入库 + observer 占位（明确非执行），门禁钉尺寸（离线，零硬件触碰）

## 结论（入库）

1. **`0x82:0xC` wire 结构已入库**：`kernel/mt_pvr_wire.h` 新增 `struct mt_pvr_musakickgfx2_in`（268B）/`mt_pvr_musakickgfx2_out`（12B），字段顺序与 5.2 生成头 `MTGPU_BRIDGE_IN/OUT_MUSAKICKGFX2` 1:1 对应。类型尺寸经 5.2 DKMS 包（`downloads/mthreads-dkms_5.2.0_amd64.deb`，SHA-256 `e3f684b1…` 与脚本期望值一致）内权威头实证：`MTGPU_FENCE`/`MTGPU_TIMELINE` 均为 `int32_t`（`mt/include/mt/mtgpu_sync_ext.h:54`），`MT_BOOL` 为 4 字节枚举（`img_types.h`，`IMG_FORCE_ALIGN`），`MT_HANDLE` 为 `void*`（8B）。
2. **尺寸独立验证通过**：算出的 268/12 与 `reports/stage-b-bridge-requirements.json` 中既有条目 `0x82:0xC = RGXKICKTA3D2`（268/12，UMD 侧真实抓包）逐字节一致——入库前即闭合。
3. **observer 占位已接 dispatch**：`kernel/recovery/mt_pvr_bridge.c` 新增 `pvr_cmd_musakickgfx2_observe()`，`MT_PVR_BRIDGE_RGXTA3D` 组 `case MT_PVR_FN_MUSAKICKGFX2`（0xC）接入。与 0x82:0x14 的 accept-and-log 不同，本占位**明确非执行**：解码打印标量头字段后返回 `-ENOTTY`（保持原有拒绝语义），不 mint 任何对象、不读任何 userspace 指针、不让 UMD 误以为 kick 成功。STATUS.md 红线（不得用 accept-and-log 代替执行）得到遵守。
4. **门禁已钉**：`static_assert` 钉住 IN/OUT 尺寸（268/12）+ 7 个关键字段偏移；`tests/test_pvr_wire_sizes.py` 的 MAPPING/DIRECTION 新增 `(0x82, 0xC)` 条目，与 requirements 表双向对齐。`make check-offline` 全绿（394 Python + 299 C）；`make kernel` W=1 零警告。
5. **反向验证**：故意将 IN 尺寸 assert 改为 267，门禁编译期失败（static_assert 触发，测试报错）；还原后全绿。

## 实测与边界

1. **实测**：5.2 DKMS 包 SHA 已核对；`MTGPU_FENCE`/`MTGPU_TIMELINE`/`MTGPU_ERROR` 定义均从包内头文件实读；268/12 与 requirements 表独立条目一致；`make check-offline`、`make kernel` 均实跑。
2. **入库**：字段名采用内核 wire 风格（小写下划线），注释保留 5.2 原名映射；偏移表见 `r356-wire-offset-table.txt`（0600）。
3. **推断**：`0x82:0xC` 在 5.2 头中命名为 MUSAKICKGFX2，在 requirements 表（2.3/2.7.1 系）中为 RGXKICKTA3D2——同一 wire 位点的两代命名，尺寸一致即同一布局。
4. **未断言**：IN 结构中各指针字段所指数组的 live 内容语义（需 r358 活体观察）；TA/3D/PR 三路 cmd buffer 的解析规则（r359/r360 设计范围）。
5. 全程离线：无模块加载、无 GPU 工作、无 PCI 触碰；会话 freeze 未动。

## 变更清单

- `kernel/mt_pvr_wire.h`：+2 structs，+1 `#define MT_PVR_FN_MUSAKICKGFX2 0xCU`，+9 static_asserts。
- `kernel/recovery/mt_pvr_bridge.c`：+`pvr_cmd_musakickgfx2_observe()`，+1 dispatch case。
- `tests/test_pvr_wire_sizes.py`：MAPPING/DIRECTION +2 条目。
- 证据：`reports/r356-wire-offset-table.txt`（0600）。

## 下一步

- r357（离线 fabricated）：UMD 真实建连 recon（R1）。
- r358（需批准·活体）：0x82:0xC observer 活体观察，记录真实 IN 参数对照 5.2 结构。
