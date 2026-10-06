# r150：TDM PMR 生命周期闭合；零核回包定位，真绘制 CCB 仍未到达

- **结论**：真实 TDM 初始化已越过 shared-memory 和 TransferContext2 建销/清理；此前 `GetMultiCoreInfo` 成功回零使 TDM store size 为零，UMD 在 `SubmitTransfer3 (0x89:0xa)` 前退出。新增单核响应已通过离线门禁与 `W=1` 构建，但本轮末尾 Guest 仍为 `2/FW=1`，安全重建前置条件不满足，故新响应尚未活体验证，未生成真实绘制 CCB。

## 实测

1. UMD SHA-256 为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与已使用的 5.2 反编译语料一致。重启前实测 trace 显示 TDM CLI/USC 取得不同 PMR handles，分别 import、unref 和 release 成功；`0x89:0x8` / `0x89:0x9` TransferContext2 create/destroy 均 ret=0。UMD 后续仍因零尺寸 context-store allocation 退出，trace 未到 `0x89:0xa`。
2. 语料 `PVRSRVGetMultiCoreInfo` 对应 `0x1:0xc`，12-byte IN / 16-byte OUT。trace 中请求参数全零；旧 bridge 将此命令交给全零 stub，成功返回但 `num_cores=0`。调用者随后计算 `cores * 0x180`，形成零字节分配。已验证的 [r28 live TQX topology](r28-hardware-copy-success.md) 记录为单核，因此 bridge 现在回显 caps 并返回 `num_cores=1`，其余输出清零。
3. PMR 生命周期修复使 import 增加引用，unref 只在最终引用时销毁 PMR，并在文件关闭时回收未配平引用；TDM CLI/USC 改为独立 PMR。TransferContext2 当前只分配文件内不透明 token，不创建 firmware context；SubmitTransfer3 仍保持不支持。
4. 反向验证：移除 PMR import 引用增加时，对应 lifetime 测试失败；把两个 TDM 输出重新 alias 时，PMR lifetime 测试失败；移除 multicore `num_cores=1` 时，multicore dispatch 测试失败。还原后 `make -C mt-vgpu-guest check-offline` 全绿（260 Python，1 skip；C RAM 272 checks），`make -C mt-vgpu-guest kernel` 全模块 `W=1` 构建零警告。
5. 首次重放误用了 shim fabricated 模式，不能作为内核 bridge 证据，已排除。第一次环境重启后，`fresh-trial.py` 实测 Guest=`2`、FW=`1`，未直接重载；依 r138/r148 的既有流程先验证 18 环全 idle、`started=0/FW=1`，再安全写回 Guest=0，fresh trial 随后成功并恢复 retained `Guest/FW=2/2`。
6. 第二次异常重启的 boot `626d686ad8484b83aacefefe2e07a53b` 留有关键日志：`mt_pvr_bridge` 于 22:48:48 加载；22:48:56 输出 DMA domain 和连续 CPU-only VM allocations；22:48:56.908，systemd-coredump 记录 `ChatGPT` PID 2704 收到 SIGSEGV；16 ms 后内核记录 `BUG: kernel NULL pointer dereference, address: 0`。该 bridge 加载后、Oops 前没有执行 `musa_blit_test`；新 multicore handler 未得到活体验证。内核日志在 Oops 首行后中断，无 RIP、调用栈、进程名或模块归属，`coredumpctl` 找不到 PID 2704 的 core；当前可读配置 `panic_on_oops=0, panic=0`，日志没有系统级 orderly shutdown 记录。故迹象指向 bridge 节点出现后某个 DRM 客户端/清理路径与内核 Oops 紧邻，但具体故障函数及重启发起者无法从现存日志确认。
7. 再启动后只读状态为 Guest=`2`、FW=`1`，PCI 未绑定、无 probe/bridge 模块、`/dev/dri` 仅 `card0`。临时无参数 probe 模块曾 ref=0 且未绑定，已安全卸载；第二次 retained trial 曾在异常重启中失去。

## 推断与边界

- `num_cores=1` 是根据已验证的 S3000 topology=`1` 报告给 UMD；需要在当前 bridge 上再次观察 `0x1:0xc` 响应及其后续分配，才能验证其解堵效果。
- r150 证明的是 TDM 两个 PMR 的独立生命周期和 TransferContext2 token 清理；不证明 firmware TransferContext 可用，也不证明生成了非零 CCB。
- 当前 Guest=`2` 不符合 `fresh-trial.py` 要求的 Guest=`0` / FW=`1`。异常重启伴随内核 NULL dereference，先定位/规避 bridge 节点的内核 Oops，再恢复 Guest、验证 multicore 回包与 TDM 分配；不要直接重复加载 bridge。

执行环境重启后 `/tmp` 清空，trace 与本轮 read-only 预检 JSON 未留存；本报告记录了当时读取到的关键字段。fabricated shim 重放不作为内核 bridge 运行证据。
