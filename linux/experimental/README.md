# Optional standard-SPICE viewer

`ydyun_spice_viewer.c` is a small viewer for Debian's public `spice-gtk` APIs.
It creates a `SpiceSession`, attaches display channels to a GTK window, and
therefore exercises the standard SPICE display and input boundary identified
in the official Linux client. It is installed as the opt-in
`ydyun-spice-viewer` command.

The China Mobile client still needs JWAE/SCG, vendor connection JSON, a session
ID and authentication before its SPICE channels become reachable. The viewer
does not log in, bypass authentication, load vendor ELF files, collect
telemetry, or implement the private tunnel. It is useful for standard SPICE
endpoints and for the local endpoint produced by an authorized session adapter.

Build from the repository root after installing Debian's development package:

```sh
cc -Wall -Wextra -O2 linux/experimental/ydyun_spice_viewer.c \
  $(pkg-config --cflags --libs spice-client-gtk-3.0) \
  -o build/ydyun-spice-viewer
```

Run only against a standard SPICE endpoint for which the operator is
authorized:

```sh
build/ydyun-spice-viewer HOST PORT [TLS_PORT]

# 对官方 viewer URL 只取安全的首段 endpoint；+ 后缀保持 opaque
build/ydyun-spice-viewer 'spice://127.0.0.1:10800+opaque-session-fields'
```

URL 模式只连接 `spice://` 或 `spice-conn://` 后的 `HOST:PORT`，校验长度、主机、端口和
控制字符；不会解析、打印或执行认证后缀，也不会加载 `libjwae`、安全或监控库。它只适合
验证授权会话端口是否暴露标准 SPICE display/input；官方厂商扩展仍由官方运行时负责。

## Optional official Linux SDK loader

`ydyun_chuanyun_session.c` is a small `dlopen`-based adapter for the public
Linux header shipped in the official client package:
`ccsdk/uos/include/chuanyun_api.h`. It is not enabled by systemd and does not
ship any vendor library. It passes only the server/session fields and the
connection/first-frame callbacks; log-server, network-quality and monitor
callbacks are deliberately left null.

Build it with:

```sh
cc -Wall -Wextra -Werror -O2 linux/experimental/ydyun_chuanyun_session.c \
  -ldl -o build/ydyun-chuanyun-session
```

The operator must supply an authorized vendor `libchuanyun.so` and a config
file containing `library`, `server_ip`, `server_port`, `vm_id`, `username`,
`auth_code`, and `biz_code`. Optional fields are `terminal_sn` and
`unit_type`. The loader never prints the auth code or vendor error strings.
The vendor library still owns the actual JWAE/SCG/SPICE implementation, so
this adapter is a compatibility boundary, not a replacement for that closed
runtime.

The binary is included in the Debian package as an opt-in helper only. No
systemd unit starts it, and the normal USB/IP controller remains independent
of the vendor library.

## Optional official USB loader

`ydyun_chuanyun_usb.c` loads an operator-supplied official
`libchuanyun_usbip.so` and uses the statically verified public exports
`cdpusblib_get_device_list`, `cdpusblib_attach_device`,
`cdpusblib_unattach_device`, `cdp_usbip_drive_map`,
`cdp_usbip_drive_unmap`, `cdp_usbip_start_service` and
`cdp_usbip_stop_service`. The two drive-map exports are optional and are
used only for an explicitly requested mass-storage drive mapping. It deliberately does not load or emulate the
official `chuanyun-redirect` root daemon, its SysV queues, deny list, udev
permission injection, or state/quality reporting callbacks.

Build it with:

```sh
cc -Wall -Wextra -Werror -O2 linux/experimental/ydyun_chuanyun_usb.c \
  -ldl -o build/ydyun-chuanyun-usb
```

The config must contain `library`; it may contain `json_file` for the
operator-supplied USB service JSON. Run the explicit interactive mode:

```sh
build/ydyun-chuanyun-usb ./authorized-usb-loader.conf run
```

Commands are `list`, `attach BUSID`, `detach BUSID`, `map BUSID`,
`unmap BUSID` and `stop`. Optional
`jwae_enabled = yes` invokes the vendor token/JWAE exports with operator-
supplied authorized values; the token is never printed. This is an ABI
compatibility boundary and does not prove a real cloud session until the
official session and `g_link` traffic have been observed.
