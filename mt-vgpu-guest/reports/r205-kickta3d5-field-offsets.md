# r205：从 wrapper 与 trace 对上 KickTA3D5 的数组和计数偏移

## 结论

SHA 匹配的 UMD wrapper 将 `RGXKICKTA3D5` 请求按 108 字节栈块发送。结合 r203 seq 123 原始输入，可把 `0x00` 的 opaque context 槽、八个数组指针槽及三个 count 参数定位到请求偏移；Fabricated 请求中 check/update count 均为 1，PMR count 为 0。尾部 `0x48`–`0x5f` 的参数槽和 count 区与 2.7.1 头从 `0x48` 起的 VA/size/count 布局不一致。尾部参数槽的语义仍未恢复，不能据此实现 handler。

## 实测与静态证据

- 零硬件触碰。只读 UMD SHA 匹配语料与 r203 fabricated trace；没有运行新 GPU 工作或接触硬件。
- UMD `FUN_00137c30` 在 `local_a0` 起始的栈区域构造输入，调用 `FUN_00192930(..., 0x82, 0x14, ..., 0x6c, ..., 4)`。从局部变量相对 `local_a0` 的偏移，可复原 wrapper 参数槽：

| 输入偏移 | wrapper 来源 | r203 seq 123 原始值 | 目前可确认的含义 |
|---:|---|---|---|
| `0x00` | `param_2` | `0` | opaque context 槽 |
| `0x08` | `param_4` | 栈指针 | check 数组指针 1 |
| `0x10` | `param_5` | 栈指针 | check 数组指针 2 |
| `0x18` | `param_6` | 栈指针 | check 数组指针 3 |
| `0x20` | `param_8` | 栈指针 | update 数组指针 1 |
| `0x28` | `param_9` | 栈指针 | update 数组指针 2 |
| `0x30` | `param_10` | 栈指针 | update 数组指针 3 |
| `0x38` | `param_12` | 栈指针 | sync PMR 数组指针 1 |
| `0x40` | `param_13` | 栈指针 | sync PMR 数组指针 2 |
| `0x48` | `param_16` | `0` | 32-bit 参数槽，语义未定 |
| `0x4c` | `param_14` | `0x8000023000` | 64-bit 参数槽，语义未定 |
| `0x54` | `param_15` | `0x4700` | 32-bit 参数槽，语义未定 |
| `0x58`–`0x5f` | `param_17` | `1` | wrapper 占 8 字节；调用点给出 32-bit 值并零扩展，结构语义未定 |
| `0x60` | `param_3` | `1` | check count |
| `0x64` | `param_7` | `1` | update count |
| `0x68` | `param_11` | `0` | sync PMR count |

- 数组指针与 count 的参数身份来自 SHA 匹配语料中 `RGXKickGfx` 对 `FUN_00137c30` 的调用：check count 为 `local_d80`，其三个数组为 `local_d78/local_c78/local_bf8`；update count 为 `local_af0`，数组为 `local_ae8/local_9e8/local_968`；PMR count 为 `local_e60`，后续两个参数为 `local_dd0/local_e58`。
- 偏移和原始值来自 `reports/r203-gfx-update-clean.jsonl` seq 123。数值只是 fabricated 请求内容；它们不是 Guest 内核读到/接受该输入的证据。
- 2.7.1 头在 `0x38/0x40` 也有两个 sync PMR 数组指针；但它将 `ui64SubmissionVa` 放在 `0x48`、`ui32SubmissionSize` 放在 `0x50`，counts 放在 `0x54/0x58/0x5c`。UMD wrapper 在这些偏移处发出 `param_16/param_14/param_15/param_17`，counts 则在 `0x60/0x64/0x68`。这印证 r204 的 12 字节差异是实际偏移布局问题，不能只以结构总大小处理。

## 边界与下一步

目前已将数组指针与三个 count 的偏移闭合；`0x48`–`0x5f` 的尾部参数槽仍缺目标 Guest schema/handler 佐证，`param_16`、`param_14`、`param_15`、`param_17` 的协议语义不能仅凭变量名猜测。下一步应从 5.2 生成头或目标 Guest bridge 实现恢复这四个字段的声明和读取路径；在证据闭合前不增加 dispatch handler。真实 CCB 与活会话继续 freeze。
