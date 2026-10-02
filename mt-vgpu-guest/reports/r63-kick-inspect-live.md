# r63：kick T1+T2 只读观察落桥并实测

r62 的算法（T1 拷贝数组 → T2 解析 UFO）落成 `0x88:0x4` 路径上的纯观察：
`mt_pvr_kicksync3_in` 结构体解析句柄（替代裸 `in84` 偏移读），非零 counts
时拷贝四组数组、逐句柄对照本文件 PMR/对象表、记一行 inventory；
任何失败都降级为原样 accept，fence + OUT 路径逐行未动。

## 非零路径实测（`probe/pvr_kick_probe`，新增）

直发 raw `0x88:0x4`：check=2（真 sync PMR 句柄 + `0xdead`）update=1
（真 sync 对象句柄），4 项线检查全过；dmesg 精确命中预期：

```text
kick sync inventory: check=2 update=1 ufo_known=2/3
check_fd=-1 timeline_fd=-1 extref=0
```

## 全链路（构建 `894faf50`，loaded == 在盘）

221 项 Python（含 4 项 inspect 门禁）、runtime integration、`W=1`
零警告；热换后 smoke、探针、cover 探针、UMD 八级阶梯全部通过；
各文件 `fallbacks=0`；零 WARN/BUG/Oops。终态 `Guest/FW 2/2 pinned`、
`pending=0`；主模块引用 1，bridge 引用 0。

## 附带教训

核对 loaded/in-disk 时一度把 `.ko` 文件 sha256 当成 build-id 比对，
虚惊一场——唯一可比的是 `readelf Build ID` vs
`/sys/module/.../notes`。已记入复核清单，不再犯。
回滚点：`/tmp/opencode/bridge-rollback-r60/`（`e812d938`）。
