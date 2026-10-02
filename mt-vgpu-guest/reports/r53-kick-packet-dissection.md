# r53：0x88:0x4 kick 包解剖（S4-3 翻译步骤的地形测绘）

目标是 S4-3 第三块：把 `0x88:0x4` kick 包翻译进 firmware 会话提交环。
动手前先确认包里到底有什么——零硬件风险，全程只动用户态 shim 和离线测试。

## 捕获方法

shim 新增环境变量门控 `UMD_DUMP_BRIDGE="0x88:0x4"`，在 passthrough
（真实驱动）路径转储 bridge IN 十六进制。只影响新启动的被注入进程；
内核、会话、bridge 零触碰。重跑 rung8 两次，均 `exit=0`。

## 解码（对照 5.2 生成头 `MTGPU_BRIDGE_IN_MUSAKICKSYNC3`）

两次捕获的 84 字节完全按该布局解开：

| 偏移 | 字段 | 包 A | 包 B |
|---|---|---|---|
| 0 | hKickSyncContext | 0x102f | 0x102f（bridge 现场铸造值一致） |
| 8/16/24 | check 指针 ×3 | 全 0 | 全 0 |
| 32 | checkCount | 0 | 0 |
| 36/44/52 | update 指针 ×3 | 全 0 | 全 0 |
| 60 | updateCount | 0 | 0 |
| 64 | fenceName | 有效用户态指针（ASLR 变化） | 同左 |
| 72 | checkFD | 垃圾值（见下） | 垃圾值（不同） |
| 76 | timelineFD | -1 | -1 |
| 80 | extJobRef | 0 | 0 |

## 结论

1. **包里没有 GPU 命令字节**，只有同步记账。真正的 work 藏在 kicksync
   context 背后（CCB + 已映射 PMR）。翻译步骤必须是 context-handle →
   CCB/PMR 解析，不是本包重编码——这定死了后续工作形状。
2. `checkFD` 在合成路径上是 UMD 未初始化垃圾（两次运行不同），不断言；
   真实 kick 应为有效 fd 或 -1。
3. 落盘：`kernel/mt_pvr_wire.h` 新增 `mt_pvr_kicksync3_in` + 84 字节断言；
   `tests/test_pvr_kick_packet.py` 钉住全部字段偏移（编译期 offsetof）
   与两次捕获的稳定字段；`test_pvr_wire_sizes.py` 接入既有 JSON
   （0x88:0x4 in=84）门禁。bridge 行为零改动（仍只读 handle）。

## 验证与状态

- 200 项 Python 测试、runtime integration（含主模块 `W=1` + ABI）、
  bridge `W=1` 全过。
- 注意：加头文件注释/结构体后重编的 bridge 候选 build-id 变为
  `1015767c…`（纯加法改动，行为一致）；**运行中的仍是 r51/r52 验证过的
  `c77ad92d…`，未重载**。会话 `Guest/FW 2/2`、`pending=0`，bridge 引用 0。
