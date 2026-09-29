# 仅在 Guest 内的恢复试验（2026-09-28）

目标是在宿主不可操作的条件下恢复可验证的本机能力。目前通信已经恢复，固件执行和
硬件加速仍未成功；本报告没有把通信成功当作显卡可用。

## 实现与实际结果

`kernel/mt_guest_probe.c` 新增默认关闭的 `recover_channels`。只允许已核对的 PCI
0000:00:0e.0、1ed5:0222/1ed5:1101，以及 Guest=1/FW=1。
它要求 query_info/probe_rpc，拒绝固件试验、显存分配、固件装载和写入测试。
`trial_control` 在该模式下返回 EOPNOTSUPP。

1. 先分配新 CPU 页并安装共享 IRQ 处理，再按参考 `140025a48/1400271d8`
   以 3→0 顺序写 BAR1+0x158 撤销旧注册。不访问重启前的旧 GPA。
   本机四次返回值均为零；这里只确认通信槽撤销，不能推出其他旧映射已清理。
2. 注册新四页，发送 normal-mode、软件包、版本协商；本机 mode=1、版本接受位为 1。
3. 查询设备信息、提交 BAR2/BAR4 基址并发送共享区通知。
   信息页 SHA256 为 `6163ff0648e2ec45fde070e39b8bc9b0c1686f71ac42d184cb074d70594eaa2b`，
   与原记录一致。保留新页面并继续处理宿主查询与 IRQ。
4. `snapshot_memory=1` 仅映射并读取信息布局指定的固件首 1 MiB 和共享区首 64 KiB，
   随后释放映射/临时 BAR2 reservation。没有写 VRAM。
   固件前 1 MiB 与 `build/firmware/trial-after-package.bin` 相同；started=0、DM0 5/0。
5. 原始 `1400270e0` 的 7 个离线执行案例通过：OSID=4 写 +0x110，然后查询与布局比较。
   `refresh_osid=1` 限定该恢复模式，只做一次 OSID 刷新。实机返回成功，但信息页、
   整个 1088 KiB 快照和 Guest/FW 状态均未改变。
   未模拟完整 Windows resume，也未发送 +0x108/+0xf8 完成通知。

两次恢复模式的正常 rmmod/insmod 成功；当前模块不自持引用，没有本轮发布的固件资源。
旧固件内存仍保留，不应把当前 `trial: published=0` 当成旧会话已重置的证据。
编译使用运行内核头文件、W=1，未报告编译警告。未改开机加载配置。

## 文件与复查

- `build/recovery-channel/result-initial.json`：首次成功恢复通信。
- `build/recovery-channel/samples.json`：首次六次持续通信采样。
- `build/recovery-channel/snapshot-validation.json`：正常卸载、重绑和只读队列快照。
- `build/recovery-channel/osid-refresh-validation.json`：OSID 刷新实测及前后散列。
- `reports/resume-info-oracle.json`：原始指令对照；Host 返回被建模，不证明 Host 内部行为。
- `reports/local-recovery-validation.json`：最终汇总，包含最终采样路径和模块散列。

复查当前已绑定的恢复会话：

```bash
cd /opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest
python3 scripts/capture-recovery.py
```

该脚本只读 sysfs 和日志，保存独立目录；memory_raw 是绑定时快照，不是实时固件读取。
离线重跑参考验证使用 `python3 scripts/verify-resume-info.py`。

首次加载的参数为 `enable_probe=1 query_info=1 probe_rpc=1 recover_channels=1`；
后续增加 `snapshot_memory=1`，最终一次额外增加 `refresh_osid=1`。当前已经加载，不应重复
insmod。不要用恢复模式强行替代 fresh-trial 的 Guest0/FW1 前置条件。
旧启动审核的 .ko 已保存在 `build/recovery-channel/mt_guest_probe-before-recovery.ko`；
新构建与旧启动审核散列不同，`fresh-trial.py` 保持拒绝，等待重新审核。
