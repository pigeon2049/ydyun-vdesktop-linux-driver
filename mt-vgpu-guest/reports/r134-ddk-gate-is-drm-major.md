# r134：DDK2 门控来自 DRM `version_major==2`，不是桥的 features 块（离线语料；零硬件触碰）

解释 r132/r133 为何无差异，并订正 r131 的前提。UMD SHA `b3058c02…` 已对（`sha256sum` 实测）。以下为**伪 C 读出的假设**，未执行验证。

## 语料链（`decompiled/linux-legacy-umd-5.2.0/`）
- 门控：`RGXCreateKickSyncContextCCB@0x52180`（L28313）`GetFeatures(conn)+0x54 < 2` → legacy
  `FUN_00135ce0`；否则 gated（SyncPrimAlloc + `SubmissionBufAlloctorCreate`）。
- `GetFeatures` = `*(conn+0xa0)+0x620`（L27855）；`conn+0xa0` 由 `FUN_00151b20`
  （`RGXPopulateFeatureConfig`，L27987）`param_1[0x14]=__ptr` **在 UMD 内 calloc(0x698) 并自填**，
  不是从桥拷来的。
- `+0x674`（= features+0x54）= `(drmGetVersion(fd)->version_major == 2) + 1`（L28085）。
  `FUN_00503750` 是 `ioctl(0xc0406400)` 的 drmGetVersion 封装，`*piVar6` 为 `version_major`。

## 含义（推断）
- 桥 `mt_pvr_bridge.c:2704` 报 `.major = 0` → `+0x54 = 1` → legacy。要 `>=2` 需 `.major = 2`。
- r131/r132/r133 的 `ddk_feature_set` 写的是内核侧 `file->features` 块，供应商 UMD 不读它，
  所以无差异是预期结果，不是"开关无效"的新证据。r132/r133 的实测事实仍成立。
- 副作用待测：`PVRSRVConnect*` 在 `+0x54==1` 时多一次 `BridgeAlignmentCheck`（L22018），
  major=2 时该调用消失；major 变化是否影响 DRM/Mesa 对该节点的识别未知。

## 下一步（需批准重载）
给桥加 `drm_major` 模块参数（默认 0）；重载 `drm_major=2` 跑 r133 同链，判据 = create 之后
是否出现 SyncPrim/SubmissionBuf 桥调用。`ddk_feature_set` 参数可保留或移除，另议。
