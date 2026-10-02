# r62：0x88:0x4 签发包装解码（翻译器算法的输入端）

r53 解了包格式，本轮解签发路径：UMD 内哪个函数、取哪些入参、如何装配
84 字节。全程 fabricated 重放 + gdb + 反汇编，零内核改动、零 GPU 执行。

## 调用链（gdb trap 实测）

`RGXKickSync` → 0x88:0x4 专用 marshalling 包装（`0x7272000`）→ 通用
ioctl 发送器。包装取 6 寄存器参数 + 7 个栈参数，原样拼出 84 字节：

| IN 偏移 | 字段 | 来源 |
|---|---|---|
| 0:8 | handle | rsi |
| 8:16 | pCheckDevVarOffset | rdx |
| 16:24 | pCheckValue | rcx |
| 24:32 | phCheckUFOBlock | r8 |
| 32:36 | checkCount | r9d |
| 36:44 | pUpdateDevVarOffset | 栈 +8 |
| 44:52 | pUpdateValue | 栈 +16 |
| 52:60 | phUpdateUFOBlock | 栈 +0 |
| 60:64 | updateCount | 栈 +24 |
| 64:72 | fenceName | 栈 +56 |
| 72:76 | checkFD | 栈 +32 |
| 76:80 | timelineFD | 栈 +40 |
| 80:84 | extJobRef | 栈 +64 |

OUT（fence fd）写回栈参（`+0x30`）指向的 client 地址。合成 kick 全零故
counts 为 0；真实 kick 的 counts/指针非零，数组在 UMD 内存中。

## 翻译器算法（现已可写）

- T1：从 UMD 内存拷贝 check/update 数组 + fence 名（按 counts 定界，
  上限钳制）。关键：bridge dispatch 运行在调用进程上下文，
  `copy_from_user` 可达——真驱动就是这么做的，我方此前只是故意不读。
- T2：UFO block 句柄 → bridge 拥有的 sync PMR → GPU PA + offset，校验值。
- T3：组装 firmware 侧 kick（DM 队列格式仍未知——RGX ring RE 或 trial）。
- CCB：创建时 `ui32PackedCCBSizeU88` 定大小（合成路径为 0）；server 侧持有
  （r56）；非零 CCB 内容仍需一次真实绘制路径观察。

## 状态

本轮无代码改动（只读分析 + fabricated 重放）；会话 `pending=0` 未触碰。
`UMD_TRAP` 仅 fabricated 分支生效的 caveat 已在 r56 记过，不改。
