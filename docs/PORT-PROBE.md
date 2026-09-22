# ZTE USB/IP 端口验证

当前没有真实云桌面会话，所以不能在本机证明 `3246` 是否由当前云端实例转发。Linux 适配器提供了无设备导入的标准控制面探测：它只发送 `OP_REQ_DEVLIST`，不会 attach、卸载或写入远端设备。

## 配置并探测

编辑配置中的 `remote_host`，依次尝试标准端口和 ZTE 候选端口：

```sh
sudo sed -i 's/^remote_port = .*/remote_port = 3240/' /etc/ydyun-usb/ydyun-usb.conf
sudo ydyun-usbctl probe

sudo sed -i 's/^remote_port = .*/remote_port = 3246/' /etc/ydyun-usb/ydyun-usb.conf
sudo ydyun-usbctl probe
```

成功示例：

```text
standard-usbip version=0x0111 devices=2
```

这表示该 TCP 端点能返回标准 USB/IP `DEVLIST` 响应，可继续执行 `list`，再将允许的 bus ID 写入 `[devices]` 后执行 `attach` 或 systemd 服务。

## 失败解释

- `connection refused/timeout`：端口未被云端会话转发，或地址不对。
- `version mismatch/unexpected reply code`：端口是 ZTE 控制面、发现、打印机/扫描仪通道或其他协议，不要用它执行 USB attach。
- 能 `probe` 但 `list` 失败：可能是权限、设备列表扩展、压缩协商或 ZTE 私有控制面问题，应保存命令输出并进入下一轮协议兼容分析。

`5100` 和 `19000` 在 Windows 二进制中同时出现 UDP NAT/发现标识，默认不作为 USB/IP 数据端口尝试。
