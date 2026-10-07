# r202：排除 region descriptor 重复释放，缩小 GFX 清理崩溃范围

## 结论

r201 清理时传给 `PVRSRVFreeUserModeMem` 的 `local_e70` 属于 `RGXPrepareTA` 产出的 update-list buffer，不是 `SubmissionRegionDescCreate` 返回的 region descriptor。`FUN_00178800`（RGXPrepareTA）按 update 数量分配该 buffer、填充计数和条目；GFX 成功路径在 bridge 请求后显式释放它。region descriptor 虽由 `SubmissionDestroy` 释放，但其输出槽与 update-list 槽分离，不能据此解释 r201 的 abort。当前证据将问题缩小到 update-list buffer 本身或此前写坏的 heap 元数据；具体损坏写入者尚未动态定位。

## 证据

- 零硬件触碰；复核同 SHA UMD 语料和二进制，SHA-256 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`。
- `FUN_00178800`（`decompiled.c:52052–52269`）把 producer 第三个参数作为输出结构：字段 `param_3[6]` 存入 `PVRSRVAllocUserModeMem((param_2[0x1bc] + 3) * 0x20)` 的结果；随后在分配块 `+0x24` 记 count，并从 caller 条目复制每项 32 字节数据。r201 的 count=1，所请求块大小应为 `0x80`，条目写入从 `+0x40` 开始。
- `RGXKickGfx` 的失败/成功控制流在 `SubmissionCmdGenerate` 后销毁 SubmissionHead；fabricated `0x82:0x14` 成功返回后才对 update-list 指针调用 `PVRSRVFreeUserModeMem`。UMD `PVRSRVFreeUserModeMem` 实现只是 `free(ptr)`。这是一次预期释放；此前没有证据证明 SubmissionHead 也拥有该 update-list block。
- 为查清析构器范围，另做 fabricated helper-only 重放：`SubmissionHeadCreate`、`SubmissionRegionDescCreate(head, 5, &out)`、`SubmissionRegionCreate(out)`、`SubmissionDestroy(head)` 成功。GDB 显示析构器释放了 region object、descriptor 和 head；descriptor 地址与 `out` 相同。人为再 free descriptor 会报 `double free detected in tcache 2`。此结果只确认 region descriptor 的归属，不能映射到 r201 的 update-list `local_e70`，因此排除“SubmissionDestroy 与最终 free 重复释放同一对象”的解释。
- r201 主路径 trace 在 `0x82:0x14` 后终止于 glibc `double free or corruption (!prev)`，但当时没有记录 free 参数、分配块边界或 heap 元数据；不可据错误文案直接定为同一对象被 free 两次。

## 下一步

在可复现的 GFX fabricated 输入上，于 `PVRSRVAllocUserModeMem`、update-list 条目复制完成后、`SubmissionCmdGenerate`、`SubmissionDestroy` 和最终 free 处用 GDB 记录 update-list 地址、分配大小、写前后相邻 chunk 元数据及是否有其他释放路径。先找到越界写或二次释放的动态证据，再判断修正输入还是 shim 行为。当前没有代码修改；真实 bridge handler、干净函数返回和 GPU 执行仍未验证。
