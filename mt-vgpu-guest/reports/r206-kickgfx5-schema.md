# r206：5.2 Host schema 证实 KickGFX5 尾部字段与 trace 完全对齐

## 结论

已从 SHA-256 匹配的 `mthreads-dkms_5.2.0_amd64.deb` 中直接读取生成头 `common_musagfx_bridge.h`。`MTGPU_BRIDGE_MUSAGFX_MUSAKICKGFX5` 是 MUSA GFX bridge 的 function `+20`，对应 ABI 审计表里的 `0x82:0x14 RGXKICKTA3D5`。结构声明给出的尾部字段依次是 submission flags、submission VA、submission size、submission ID，再接 check/update/PMR 三个 count；偏移和值与 r203 seq 123 fabricated 请求逐项吻合。r205 标成“语义未知”的四个参数槽现可命名。

这确认的是 5.2 Host 包的 wire schema，不是 Guest handler 的读取/执行语义。同包 `inc/mt/services/musagfx.h` 还声明了 `MTGPUMUSAGFX5KM` 服务端 API，参数顺序包含 render-context2/context handle、update/check sync 数组和数量、sync PMR、flags、VA、size、submission ID；这给出了目标服务接口，但包内没有 bridge handler 实现体。Guest 端当前仍无 `0x82:0x14` dispatch，不能把 schema/API 声明当成真实提交已实现。

## 实测

- 零硬件触碰。仅读取本地 DKMS 包和 r203 fabricated trace；没有加载/卸载模块、打开 DRM/PCI 或提交 GPU 工作。
- 包 SHA-256 实测为 `e3f684b1f7582fa0b399a234b945ad4539c565a46399f8294db91368fa32c62b`，与 `reports/legacy-umd-pvr-bridge-abi.json` 记录一致；包版本为 `mthreads-dkms 5.2.0 amd64`。
- 直接从包内 `usr/src/mtgpu-5.2.0-server/inc/mt/generated/common_musagfx_bridge.h` 读取 `MTGPU_BRIDGE_IN_MUSAKICKGFX5`：

| 偏移 | 5.2 Host 结构字段 | r203 seq 123 值 |
|---:|---|---:|
| `0x00` | `hRenderContext` | `0` |
| `0x08`–`0x47` | 8 个 check/update/sync-PMR 数组指针 | 八个栈地址 |
| `0x48` | `ui32SubmissionFlags` | `0` |
| `0x4c` | `ui64SubmissionVa` | `0x8000023000` |
| `0x54` | `ui32SubmissionSize` | `0x4700` |
| `0x58` | `ui64SubmissionId` | `1` |
| `0x60` | `ui32CheckCount` | `1` |
| `0x64` | `ui32UpdateCount` | `1` |
| `0x68` | `ui32SyncPMRCount` | `0` |

- 头文件的 bridge macro 为 `MTGPU_BRIDGE_MUSAGFX_MUSAKICKGFX5 = ... +20`，输入结构以 `__packed` 声明，输出结构只有 `MTGPU_ERROR eError`。既有审计的编译探针结果为 108/4。
- 同包 `inc/mt/services/musagfx.h` 声明 `MTGPUMUSAGFX5KM`，其签名把 update 数组置于 check 数组之前，再接 PMR、flags、submission VA/size/ID；这说明 bridge 需要按数量解引用并转换多个嵌套数组，而不是把 108 字节结构直接交给后端。包内路径清单未发现该 bridge handler 的 `.c` 实现体。Host 包仅给出接口/字段声明，不能证明 Guest 如何验证 handle、复制用户数组或消费 ID。
- 本地 2.7.1 `PVRSRV_BRIDGE_IN_RGXKICKTA3D5` 没有 `ui32SubmissionFlags` 或 `ui64SubmissionId`；它把 VA/size/count 放在 `0x48/0x50/0x54` 起始处，因此 96 字节布局与 5.2 的 108 字节布局不同。

## 边界与下一步

5.2 Host schema 和 fabricated trace 对所有可见字段已一致，r204/r205 的 12 字节偏移差不再是未解释的布局问题。仍未证明的是 Guest bridge handler 如何验证上下文、同步数组与 submission VA/size/ID，以及它如何触发真实 TA/3D 工作。下一步应核对当前 Guest bridge 的 context/sync handle 台账与已有执行路径，确定能否安全承接 `MTGPUMUSAGFX5KM` 形状的多数组提交；实现真实 handler 时需复制并校验每个嵌套数组、解析 PMR/sync handle，并保留 flags/VA/size/ID 语义。不得把 accept-and-log 当作执行。真实 CCB/活会话仍需用户明确批准。
