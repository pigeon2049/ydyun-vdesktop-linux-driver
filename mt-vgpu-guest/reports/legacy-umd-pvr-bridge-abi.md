# Legacy Linux UMD PVR bridge ABI 审计

日期：2026-09-29。只对隔离解包的 legacy UMD、本地 2.3.0 Guest KMD、2.7.1 Native 头文件和 5.2.0 DKMS Host 头文件做静态反汇编、结构核对及临时 C 大小探针；没有安装软件包、加载模块、打开 DRM 设备或执行 MUSA 程序。

## 结果

`libsrv_um_MUSA.so.1.0.0` 中识别出 205 个对 PVR SRVKM wrapper 的直接调用点，桥接 ID/function ID 均唯一。当前 2.3.0 Guest KMD 生成头文件覆盖 179 个 ID；26 个 ID 没有对应定义。对已覆盖的命令，以当前内核构建头文件环境临时编译 `sizeof()` 结果后，154 个输入/输出长度相同，25 个不同。完整调用点、长度和命令映射保存在 [JSON 清单](legacy-umd-pvr-bridge-abi.json)。
本地 2.7.1 源树的头文件作为 ABI 参照覆盖 198 个 ID，其中 7 个缺失；172 个长度完全一致，26 个不同。该对照仅说明生成头文件层面的匹配度；这份 2.7.1 源树自身配置仍为 `RGX_NUM_OS_SUPPORTED=1`，不代表现成 Guest 运行栈。
官方 5.2.0 DKMS Host 包的生成头文件覆盖 205 个调用 ID；177 个调用的编译结构长度相同，28 个有长度差异或头文件未建模结构。其 Sync 原语分配结构为 8/32 字节，与 legacy UMD 相同；此 DKMS 包是 Host 配置，不能直接作为 Guest KMD。三套版本逐命令结果均保存在 JSON 清单。

长度或编号不一致本身不总能证明失败：bridge dispatcher 会按调用方提供的长度复制缓冲区，且若干 2.3 KMD 输入结构只有一个占位字段，legacy UMD 对它传 0 字节。不过，以下结构差异不能只凭 Connect ABI 判作兼容：

| Bridge ID:function ID | 命令 | UMD 输入/输出字节 | 2.3 KMD 输入/输出字节 |
| --- | --- | ---: | ---: |
| `0x1a:0x02` | `DMADEVICEPARAMS` | 0/12 | 4/12 |
| `0x06:0x1d` | `HEAPCFGHEAPCONFIGCOUNT` | 0/8 | 4/8 |
| `0x06:0x22` | `GETMAXPHYSHEAPCOUNT` | 0/8 | 4/8 |
| `0x06:0x24` | `GETDEFAULTPHYSICALHEAP` | 0/8 | 4/8 |
| `0x81:0x07` | `RGXGETLASTDEVICEERROR` | 0/8 | 4/8 |
| `0x84:0x01` | `RGXFWDEBUGDUMPFREELISTPAGELIST` | 0/4 | 4/4 |
| `0x84:0x07` | `RGXCURRENTTIME` | 0/12 | 4/12 |
| `0x86:0x02` | `RGXGETHWPERFBVNCFEATUREFLAGS` | 0/196 | 4/196 |
| `0x86:0x08` | `MUSAPFMSTART` | 0/20 | 0/4 |
| `0x86:0x0c` | `MUSAPFMWRAPPERCONFIG` | 152/4 | 24/4 |
| `0x86:0x0d` | `MUSAPFMTRIGGERDUMPONCE` | 1176/4 | 0/4 |
| `0x86:0x0e` | `MUSAPFMCLEARCONFIG` | 20/4 | 0/4 |
| `0x86:0x09` | `MUSAPFMSTOP` | 24/12 | 0/4 |
| `0x86:0x0a` | `MUSAPFMGLOBALCONFIG` | 16/4 | 32/12 |
| `0x86:0x0b` | `MUSAPFMINSTANCECONFIG` | 20/4 | 16/8 |
| `0x82:0x0e` | `RGXKICKTA3D3` | 276/4 | 268/4 |
| `0x8a:0x01` | `RGXENDTIMERQUERY` | 0/4 | 4/4 |
| `0x89:0x05` | `RGXTDMGETSHAREDMEMORY` | 0/20 | 4/20 |
| `0x01:0x01` | `DISCONNECT` | 0/4 | 4/4 |
| `0x01:0x02` | `ACQUIREGLOBALEVENTOBJECT` | 0/12 | 4/12 |
| `0x01:0x08` | `GETDEVCLOCKSPEED` | 0/8 | 4/8 |
| `0x01:0x09` | `HWOPTIMEOUT` | 0/4 | 4/4 |
| `0x01:0x0b` | `GETDEVICESTATUS` | 0/8 | 4/8 |
| `0x01:0x0f` | `ACQUIREINFOPAGE` | 0/12 | 4/12 |
| `0x02:0x00` | `ALLOCSYNCPRIMITIVEBLOCK` | 8/32 | 4/28 |

### 跨版本头文件对照

| Schema | Purpose | Covered IDs | Exact sizes | Different or unmodeled |
| --- | --- | ---: | ---: | ---: |
| 2.3.0 | Guest KMD | 179/205 | 154 | 25 size differences, 26 missing IDs |
| 2.7.1 | Native KMD headers | 198/205 | 172 | 26 size differences, 7 missing IDs |
| 5.2.0 | Host DKMS headers | 205/205 | 177 | 28 size differences or unmodeled payloads, 0 missing IDs |

5.2 的“不同或未建模”包含生成头文件没有对应 IN/OUT 结构的命令，不能把 0 字节探针结果当成 handler 预期长度。对照只负责缩小差异范围，不替代对 dispatcher 和具体 handler 的分析。5.2 DKMS 源码配置关闭多 OS Guest；2.7.1 头文件来自 Native 单 OS 源树。

### 需要优先解决的结构差异

- `SYNC/ALLOCSYNCPRIMITIVEBLOCK`（`0x02:0x00`）：UMD 请求 8/32 字节；2.3 KMD 结构是 4/28 字节，而 5.2 DKMS 头文件是 8/32。2.3 bridge handler 不读取输入的 `ui64MemType`，在输出偏移 `+24` 写 32 位 `ui32SyncPrimVAddr`；UMD 随后按 64 位读取该地址。bridge staging 区由 `OSAllocZMem(0x3000)` 零分配，所以高 32 位会为零。仍需确认 32 位 VA 足够，以及忽略 memType 不会改变分配语义；这两点都不能从当前静态证据推出。
- `RGXKICKTA3D3`（`0x82:0x0e`）：UMD 发 276 字节，2.3、2.7.1 和 5.2 头文件的输入结构均为 268 字节，字段名和顺序相同。2.3 KMD handler 的可见读取到输入偏移 `0x108` 的 32 位 `ui32TACmdSize` 为止，没有读取 268 字节结构之后的 8 字节；dispatcher 按调用方长度接收输入（上限 `0x2000`）并复制到 staging 区，不要求长度等于 C 结构大小。因此这 8 字节很可能是尾部扩展并会被旧 handler 忽略，但 UMD 端字段偏移尚未完全恢复，先标为高概率兼容而非已证明。
- HWPerf/PFM（`0x86:0x08`–`0x0e` 中七个已调用命令）：输入/输出长度和/或结构定义不同，包含 UMD 的 1176 字节 dump-trigger 配置，而 2.3 KMD 对应命令没有输入结构。此组不应按同名命令视作兼容。

16 个剩余长度差异是 UMD 传 0 字节、KMD 结构含 4 字节空结构占位符，输出长度相同。此差异有机会是无害的生成器占位符差异，但需要逐个确认 handler 不读取该字段；目前只从核心对象确认 `Disconnect` handler 不读输入。

2.3 KMD 缺少的调用 ID：

`0x02:0x0a`, `0x02:0x0b`, `0x02:0x0c`, `0x02:0x0d`, `0x02:0x0e`, `0x06:0x2a`, `0x06:0x30`, `0x06:0x31`, `0x81:0x0d`, `0x81:0x0e`, `0x81:0x0f`, `0x81:0x10`, `0x82:0x11`, `0x82:0x12`, `0x82:0x13`, `0x82:0x14`, `0x86:0x0f`, `0x86:0x10`, `0x86:0x11`, `0x88:0x04`, `0x88:0x05`, `0x88:0x06`, `0x88:0x07`, `0x89:0x08`, `0x89:0x09`, `0x89:0x0a`

## 对适配的决定

结果支持继续以官方 2.3.0 Guest KMD 为适配起点：它已有 Guest/VZ 构建路径；5.2 Host 包虽更接近 UMD 结构，却编译时关闭多 OS Guest。当前还不能把整套 Linux 5.2 UMD 宣称为可用配套。下一步应恢复 Sync `ui64MemType` 的实际传值，并追踪启动/普通提交是否触发缺失命令和 HWPerf/PFM 命令，再决定是否需要针对性 bridge 兼容层；无需因 `RGXKICKTA3D3` 的 8 字节差异先改 KMD。

## 复现

```sh
python3 scripts/audit-legacy-umd-pvr-bridges.py
```

脚本校验 legacy ELF 与 5.2 DKMS 包 SHA-256，使用 `objdump` 提取 bridge 参数，从本地 2.3/2.7.1 和临时解包的 5.2 头文件编译结构大小探针；仅生成临时 `.o/.ko` 文件，不加载该文件。
