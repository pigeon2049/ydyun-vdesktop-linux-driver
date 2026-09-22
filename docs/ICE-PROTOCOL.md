# ICE 画面转发协议调查记录

更新时间：2026-09-21（Asia/Shanghai）

本文只记录从 `/opt/code/ydyun/driver/doc/extracted/ice/` 中 Windows ICE 安装包得到的
静态证据，不把字符串命中当成已验证的线上协议字段。

## 已确认的组件关系

```text
ICE display/IDD driver 或 Windows GPU
    -> IceDisplay / IceVGPUCapture
    -> surface 捕获、dirty rectangle、frame cache
    -> H.264/HEVC（可能还有能力协商的 AV1）或无损区域编码
    -> ICE display channel
    -> IceTunnel（TLS、TCP/UDP、KCP、多路径）
    -> 客户端解码、合成和显示
```

`IceInput`、`IceCursorHook` 和 `ProtocolManager` 中的视频/输入句柄是分开的，所以键鼠
输入和桌面画面应作为两个通道分析。`ZTERemoteRender` 等媒体组件属于播放器媒体重定向，
不能用来解释整屏画面。

## 直接证据

| 文件 | 证据 | 结论 |
|---|---|---|
| `IceVGPUCapture.exe` | `dxgi.dll`、`d3d11.dll`、`IDDCapturer`、`OpenSharedResource`、AMF/NVENC、H.264/HEVC | GPU/IDD 桌面捕获和硬件编码路径 |
| `IceDisplay.exe` | `desktop_stream.c`、`desktop_cache.c`、dirty rect、IDR、lossless region、`use-zip-lossless` | 桌面采用增量更新和关键帧机制 |
| `ProtocolManager-2008.dll` | `ICE_RECV_DISPLAY_CHANNEL_INIT`、`SURFACE_CREATE`、`STREAM_CREATE`、`ICE_SEND_IDR` | 显示通道有控制消息层，不是裸视频 socket |
| `IceTunnel.exe` | OpenSSL/TLS、KCP ACK/RTT/window、UDP path、TCP sub path | 传输层有可靠/低延迟路径和私有隧道 |
| `ice-encoder.dll` | slice/NAL、CBR/VBV、QP、dirty/unchanged/text rectangle | 有专用桌面编码器和区域策略 |
| `zte-lossless-encoder.dll` | `zte_lossless_encoder_*`、zlib/zstd | 有损视频旁的厂商无损区域编码 |

## 仍未知的线上字段

- 会话认证、密钥协商和 TLS 私有扩展；
- display channel 的创建顺序、消息长度、端序、序号和重传规则；
- H.264/HEVC NAL 前的 ICE 帧头及关键帧标记；
- 无损块的坐标、像素格式、压缩算法选择和与有损帧的合成顺序；
- UDP/KCP 的会话号、拥塞参数、多路径切换和 TCP fallback；
- 多屏 surface 的 display ID、尺寸变更、旋转和光标形状消息。

## Linux 实现计划

在取得真实云会话样本后按以下顺序实现：

1. 只读采集控制/显示连接元数据，确认 TLS、TCP/UDP/KCP 选择；
2. 在离线样本上实现有长度上限和序号校验的消息剥离器；
3. 先支持一个显示器和 H.264，再支持 HEVC、IDR 请求、尺寸变更；
4. 再加入无损区域、光标和多屏合成；
5. 使用 FFmpeg/GStreamer 解码，使用 PipeWire/DRM/KMS/SDL 输出；
6. 通过 systemd 最小权限运行，保留协议必需 TLS/认证，排除 QoE、监控、进程守护和
   网络拦截组件。

在第 1 步完成前不提交伪造握手或猜测帧解析器，避免把不可互通代码装进 Debian 包。

## 客户端包补查结果

`vdesktop/VDesktop-setup-uSmartPlayer.exe` 解包出的 `uSmartPlayer.exe` 是媒体重定向
播放器：它调用 FFmpeg/mpv，通过 Qt `QTcpSocket`、本机 `vdagent` 和 UUID 校验传输
媒体样本。其 `buildVideoCreateMsg` 字段包括编码类型、时间戳、流/源尺寸、B 帧和像素
格式；这可以作为媒体通道研究材料，但不能当作整屏 ICE display channel 的帧格式。

播放器包中的 `DriverController.dll` 还包含 Windows 进程黑名单/服务控制 IOCTL，已列入
排除范围。播放器支持 H.264/HEVC/AV1/RTP 只说明通用解码能力，不足以证明 ICE 桌面线
路使用 RTP。

## SPICE 兼容性的直接证据

这一步不再只依赖字符串名称：

- `IceTunnel.exe`、`IceDisplay.exe` 和 `IceInput.exe` 的 x86-64 反汇编都出现对
  `0x51444552` 的比较或写入；按小端解释其字节序是 `REDQ`，正是 SPICE 的
  `SPICE_MAGIC`；
- `IceTunnel.exe` 对 link header 的 `major_version` 要求为 `2`，并将 link message
  长度限制在 `0x2800`（10240）以内；这与其后分配/接收配置消息的循环一致；
- `IceTunnel.exe` 的日志路径明确打印 `header.magic`、`major_version` 和 `size`，并
  调用 `tn_parse_display_stream_data`、`spice_header_allow_drop`；
- 同一文件列出了 `SPICE_MAIN`、`SPICE_DISPLAY`、`SPICE_INPUTS`、`SPICE_CURSOR`、
  `SPICE_PLAYBACK`、`SPICE_RECORD`、`SPICE_USBREDIR` 等标准通道；
- `IceDisplay.exe` 出现 `tn_parse_spice_msg`、`tn_deal_channel_spice_msg`、
  `spice_header.size` 和 `Received wrong header: channel_type != SPICE_CHANNEL_MAIN`；
- Debian 的公开 `libspice-protocol-dev` 头文件中，`SpiceLinkHeader` 是四个小端
  32 位字段，`SpiceDataHeader` 是 `serial/type/size/sub_list`，通道编号和 display
  消息编号与上述 ICE 二进制线索一致。对应头文件保存在
  `doc/reference/spice/extracted/usr/include/spice-1/spice/`。

因此当前最可信的画面模型是：

```text
ZTE TLS/KCP/多路径隧道
    -> SPICE link/data framing
    -> SPICE display/input/cursor/USBREDIR channel
    -> ZTE 对 display stream、丢帧和编码能力的扩展
```

这仍不能证明远端入口可以直接交给 `remote-viewer`：ICE 的隧道建立、认证、KCP/多路径
以及可能的扩展消息尚未还原。但它说明 Linux 接收端不必从零发明画面消息层，可以优先
复用标准 SPICE 的 header、通道号、display/input/cursor 消息和 FFmpeg 解码基础设施。

当前已加入一个不连接网络的基础工具：

```sh
ydyun-ice-probe --link-header sample.bin
```

它只解析标准 SPICE `REDQ` link header 和 data/mini-data header，限制单帧最大 64 MiB，
输出 JSON 行；它不是 ICE 登录器、TLS/KCP 客户端或屏幕显示器。这样在取得合法真实会话
样本后，可以先验证 ICE 隧道解出的字节是否保持标准 SPICE framing，再决定是否需要实现
ZTE 扩展层。

## 标准 display stream 元数据解析

根据 Debian `spice` 0.15.2 的 `spice.proto` 和生成结构，标准 display channel 的
`STREAM_CREATE` 固定前缀为 50 字节，包含 surface/stream ID、编码类型、时间戳、源/目标
尺寸和可变长度 clip；`STREAM_DATA` 的固定前缀为 12 字节，包含 stream ID、媒体时间和
编码样本长度；`STREAM_DATA_SIZED` 的固定前缀为 36 字节。编码枚举明确包含 MJPEG、VP8、
H.264、VP9 和 H.265。

当前 `ydyun_spice.py` 已能在不解码视频、不解释厂商扩展的前提下提取这些字段，
`ydyun-ice-probe` 会把它们作为 JSON 的 `display` 对象输出。标准结构参考源保存在
`doc/reference/spice/source/spice-0.15.2/subprojects/spice-common/spice.proto`；这一步
把后续真实样本验证从“只看帧类型”推进到“能确认 stream 创建、编码器和每帧样本边界”。

探针还支持 `--extract-dir DIR`。对标准 `STREAM_DATA` 和 `STREAM_DATA_SIZED`，它依据已验证
的长度字段把编码样本写成 `stream-<id>-<codec>-<序号>.es`，同时在 JSON 行中记录文件路径。
该功能只适用于已经由合法会话解出的字节，不能代替 ICE/JWAE/SCG 认证，也不会处理 ZTE
私有帧头或无损区域。

这仍不是完整 `ydyun-ice`：样本必须先经过 ICE 的认证/TLS/KCP/多路径层，ZTE 的丢帧策略、
无损区域和合成顺序也仍需真实会话确认。
