# r349：T2-b——3=`INVALID_PARAMS`，出自 `SyncPrimRef` 同步校验（离线 fabricated，零硬件触碰）

- **结论**：`SubmitTA` 内 5 处被调者返回地址断点（fabricated GDB，`GETSRVH/GETFEAT/FABTYPE/UNK1/UNK2/DEVINFO` 全良性）之后，`0x79caa` 的 `SyncPrimRef` 首报 `rax=3`，`SubmitTA` 即经 `jne 0x7ada7` 退出——3 全程即此一处。`PVRSRVGetErrorString(3)=MTSRV_ERROR_INVALID_PARAMS`（ctypes 直调）。含义：`SubmitTA` 校验 kick 中的同步原语，我方全零 `b24–b27` 无真实 sync handle，校验拒收。GFX 配方（r210：`CreateSyncPrim` + `b22+0x48` handle/`+0x50` value）是现成模板，TA 侧缺的是 tuple 偏移（哪个 buffer、何偏移）。
- **整形方法确认**：返回地址断点逐个被调者（r380 两步走）+ 错误串表命名，二连击定位到具体校验函数，未靠猜。

## 实测与边界

1. 全程 fabricated（harness + shim 默认模式，UMD SHA 对版，`musa.ini` 工作目录）；`build/r349-replay` 系 GDB 单行工作区，用完即删（不在 `build/traces` 留名）。无模块、无 DRM、无 PCI、无 GPU。门禁沿用（394+299，无代码改动；脚本未改，无需新提交脚本）。
2. 证据：本报告正文（被调者返回值序列 + 错误串命名）；`ta-kick-attempt1.sh` 现状即复现体（双映射 `-> 3`，r348 已入库）。无新增证据文件。
3. 未断言：`SyncPrimRef` 的确切入参（`0x79c80–0x79c96` 一带线性解码错位，未展开）；`UNK1/UNK2`（`0x9c250`）的身份（本次未开火，不命名）。

## 下一步

1. T2-c：`CreateSyncPrim` 真 handle + 回填 kick tuple（偏移待定：GDB-watch `SyncPrimRef` 读位，或 GFX 类比 `+0x48/+0x50`）——脚本内加三行，fabricated。
