# r428：无可运行 Linux vguest UMD；真实 TA Header 只能从 Windows 驱动静态提取

**结论**：全仓库取证确认 Linux 端不存在可运行的 vGPU guest UMD（用户澄清成立）：所有 deb/src 均无图形 UMD `.so`（mtgpu-1.0.0=固件+dkms 源码、mtml-1.8.2=仅 `libmtml.so` 计算库、dkms 包无 .so）。`mtdxum64.dll` 实证为 DirectX 10/11 UMD（导出 `OpenAdapter`/`OpenAdapter10`/`OpenAdapter10_2`/`MtDxExtGetInterfaceImpl`），是"真实可工作"驱动的 UMD 部分；但 Wine/VM 活体运行均不可行（UMD 需 KMD `.sys`、KMD 需 PCI vGPU、vGPU 被 yuyun 占用且用户无法触碰 host）。**选项 A 的可行形态是静态提取**：反汇编语料完备（29 Windows PE + Linux UMD，Ghidra 工程可重开）；`0x168`（360B）在 mtdxum64.dll 出现 151 次，是 TA Header builder 的行为锚点；Linux UMD 的 `RGXAddRenderTargetDDK2`（decompiled.c:50202）分配 MLIST/RgnHeader 固件可见结构。r429 工作包已列（见 §5）。

## 1. 反汇编来源取证（[MEASURED]）

| 来源 | 内容 | 性质与结论 |
|---|---|---|
| `decompiled/linux-legacy-umd-5.2.0/` | Ghidra 12.1.4 反汇编 `libsrv_um_MUSA.so.1.0.0`（ELF x86-64，SHA-256 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`），8410/8411 函数，2026-09-29 | host-side MTML/MUSA 服务库（`mthreads-legacy-umd` deb）；**非 vGPU guest UMD**（用户澄清；已有报告：Connect ABI 匹配 2.3 Guest KMD PVR 1.0，但"不能证明它面向虚拟 Guest 或 S3000"） |
| `decompiled/<29 PE>/` | `/opt/MTT-driver-only/` 全部 29 PE（26 DLL + 3 SYS，436MB，只读）的反汇编；2.4 GiB 文本 + 2.6 GiB Ghidra 工程（`ghidra-projects/` 可重开） | **真实可工作的 Windows vGPU guest 驱动** |
| `downloads/*.deb`、`src/` | mtgpu-1.0.0（固件+dkms 源码）、mtml-1.8.2（仅 libmtml.so）、mthreads-dkms 5.1.0/5.2.0（无 .so）、src/official-vgpu-2.3.0（固件+mtgpu 内核源码） | **无 Linux 图形 UMD .so** |

## 2. mtdxum64.dll = DirectX 10/11 UMD（[MEASURED]）

导出表（`decompiled/mtdxum64.dll/pe-headers-imports-exports.txt`）：

| Ordinal | 导出名 | 说明 |
|---|---|---|
| 1 | `OpenAdapter10` | D3D10 UMD 入口 |
| 2 | `OpenAdapter10_2` | D3D10.1 UMD 入口 |
| 3 | `OpenAdapter` | D3D11 UMD 入口 |
| 4 | `MtDxExtGetInterfaceImpl` | Moore Threads 扩展接口（MT 特有） |

字符串实证：`D3DDDIEscapeCb`（经 D3D runtime escape 提交命令）。TA 相关命名函数：**无**——全语料库 `SubmitTA*` 仅 Linux UMD 含有（`FiniMultiThreadSubmitTA`、`InitMultiThreadSubmitTA`、`SubmitTADataEnQueue`）；Windows 侧本地函数名已 strip，需行为搜索定位 TA builder。

## 3. 可行性矩阵

| 方案 | 结论 | 依据 |
|---|---|---|
| Wine 运行 Windows UMD 捕获 | **不可行** | 未安装 wine；UMD 依赖 KMD（mtkm64.sys），Windows 内核驱动无法在 Wine 下加载；KMD 需 PCI vGPU（00:0e.0）而该设备被 yuyun 的 Linux 驱动栈占用；render context 创建与设备内存分配需真实 KMD+固件，stub 环境生成的 Header 无捕获价值 |
| Windows VM + vGPU 透传后捕获 | **不可行（当前）** | 需 host 配合迁移 vGPU；用户无法触碰 host 侧；需完整 Windows guest + 驱动安装 + 捕获工具链；超出当前授权 |
| 驱动 Linux `.so`（libsrv_um_MUSA.so） | **不可行** | 全 deb 取证无图形 UMD `.so`；用户澄清其为非 vguest；r85 结论"手工整形到此为止" |
| **静态反汇编提取** | **可行（选项 A 落点）** | 语料完备（29 Windows PE + Linux UMD）；行为锚点明确（见 §4）；Ghidra 工程可重开复核 |

## 4. 静态提取的弹药（[MEASURED]）

1. **行为锚点**：`0x168`（360B = `ta_cmd_size` 实测值）在 `mtdxum64.dll/decompiled.c` 出现 **151 次**（Linux UMD 70 次）——TA Header builder 候选定位用（需结合分配/提交上下文去噪，r429）。
2. **RGXAddRenderTargetDDK2**（`linux-legacy-umd-5.2.0/decompiled.c:50202`，270 行伪 C）：全语料库仅 2 个 DDK2 后缀函数之一（另一个是 `RGXKickSyncDDK2`）；`PVRSRVCallocUserModeMem(0x160)` 后经 `FUN_00196f30` 分配设备内存 `"MLIST"`（macro-tile list）、`"RgnHeader"`（region header）——**固件可见结构**，字符串字面量实证；VA 回填 host 侧结构。
3. **对 r427 结论的修正**：r427 称"UMD 只做 VA 透传、布局无法从 UMD 确定"——过于绝对。UMD 分配并引用 MLIST/RgnHeader 等固件可见结构，其布局信息在 UMD（+KMD）反汇编中可还原；完整填充逻辑为 r429 工作。
4. **Windows KMD**（mtkm64.sys，4,219 函数，回调表已恢复 107 入口）：guest KMD 侧 render-target/TA 初始化逻辑待查（`corpus.py` 可查）。

## 5. r429 工作包（静态提取 TA Header 构建逻辑）

1. mtdxum64.dll 内以 `0x168` 为锚点定位 TA Header builder → 提取 Header 字段写入逻辑。
2. 与 Linux UMD `RGXPrepareTA`（FUN_00178800）Header 写入清单对比 → 找 MTT guest 特有 delta。
3. `RGXAddRenderTargetDDK2` 全量分析 → render-target 元数据 VA 的 guest 侧来源与 MLIST/RgnHeader 布局。
4. mtkm64.sys 中 render-target/TA 相关逻辑 → 固件可见结构的 KMD 侧初始化。
5. 输出：可构造的最小有效 TA Header 字段表（[MEASURED]/[INFERRED] 标注）。

## 6. 诚实边界

- 静态提取得到的是**构建逻辑**，不是活体捕获的 Header blob；但本轮目标本就是构造有效 TA 提交，逻辑即所需。
- `0x168` 在 mtdxum64.dll 的 151 次命中多数为噪声，锚点有效性待 r429 以分配/提交上下文验证。
- Windows 本地函数名 strip，"无 TA 命名函数"指命名搜索无命中，不代表 TA 逻辑不存在。
- 本轮零硬件触碰，纯离线；无代码改动；门禁 `check-offline` 全绿（见提交）。
