# r204：`0x82:0x14` 的 108/96 字节差异阻止直接复用 2.7.1 结构

## 结论

fabricated GFX 请求已证明 UMD 发出 `0x82:0x14`、输入 108 字节、输出 4 字节；它并未证明 2.7.1 KMD 结构可接收该请求。2.7.1 生成头中的 `PVRSRV_BRIDGE_IN_RGXKICKTA3D5` 编译大小为 96 字节，较 UMD 实际长度少 12 字节；2.3 Guest 头文件没有该命令。已有 ABI 审计记录 5.2 Host schema 的该调用为精确 108/4，但 Host schema 不证明 Guest handler 存在或语义兼容。现在仍缺可信的 Guest handler/字段契约，不能按 2.7.1 头直接加 dispatch。

## 实测

- 零硬件触碰。读取 r203 默认 fabricated trace；未启用 passthrough，也未加载/卸载模块、访问 DRM/PCI 或提交 GPU 工作。
- `r203-gfx-update-clean.jsonl` seq 123 的请求是 `0x82:0x14`，`in_size=108`、`out_size=4`，fake shim 返回 0。trace 保存了完整 108 字节输入；这仅证明 UMD 发包和离线调用路径，不证明内核 handler 接收。
- 该 UMD ELF SHA-256 为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与 `DECOMPILATION.md` 记录一致。SHA 匹配语料的 `FUN_00137c30` 以长度 `0x6c`（108）调用 `FUN_00192930(..., 0x82, 0x14, ..., 0x6c, ..., 4)`。
- 本地 2.7.1 `common_rgxta3d_bridge.h` 定义 `PVRSRV_BRIDGE_IN_RGXKICKTA3D5`，但按本仓库 ABI 审计脚本的编译探针大小为 96 字节，输出为 4 字节。该头有 render context、check/update sync 数组指针、sync PMR 指针、submission VA/size 和三项 count 字段；其定义不是 UMD 的 108 字节输入本身。
- 2.3 Guest 生成头中没有 `RGXKICKTA3D5` 定义；当前 `mt_pvr_bridge` dispatcher 也没有 `0x82:0x14` handler，命令会落到默认 `-ENOTTY`。
- `legacy-umd-pvr-bridge-abi.json` 记录的 5.2 Host 头比较中，`RGXKICKTA3D5` 不在 size mismatch 表，故审计结果是 UMD/KMD Host schema 108/4。审计说明同时明确该 5.2 DKMS 是 Host 配置，不能据此推断 Guest runtime 支持。

## 推断与边界

12 字节的差异说明 2.7.1 声明和 5.2 UMD 线上的参数布局不同，不能仅凭同名函数或相同输出大小视为兼容。现有资料还没有把 UMD 的 108 字节逐字段映射到 Guest handler，也没有可用的 2.7.1 bridge 实现来证明哪些字段被读或忽略。r203 的 fabricated 零回包并未执行任何 handler。

## 下一步

先恢复 108 字节请求的逐字段布局，并取得与目标 Guest 路径匹配的 handler/结构证据，再决定 `0x82:0x14` 的 bridge 实现。实现前不得把 96 字节 2.7.1 结构强转为 108 字节请求，也不得以 accept-and-log 的成功回包冒充同步语义。真实 CCB 与活会话继续 freeze，仍需用户明确批准。
