# r269：Guest 地址收敛（离线实现，零硬件触碰）——1G 配额纠正 + 字面量收宏

- **结论**：用户纠正落实两处。① 显存配额纠正：`decode-device-info.py` 解 trial `info_raw`（版式来自 `mtkm64.sys` 硬件验证）得 `vm_memory_size_bytes = 1073741824`（**1GiB**）；此前 90MB 只是固件启动期 vram 池，16G 只是 PCI BAR2 地址窗口。② 字面量收敛：BAR2 base/size（`0x800000000`/`0x400000000`，3 个 disconnect 文件）与 info segment-5 地址（`0x43000000`，offline_info 3 处）收敛到 `mt_guest_device.h` 新宏（`MT_GUEST_BAR2_BASE/BYTES/SEG5_ADDR`）；堆表字面量保持原样（vendor 蓝图逐字钉死是设计，`test_windows_heap_table_decoded.py`），1G 配额内核未消费（解码脚本命名即可）。门禁 2 项（含反向：改宏值即红）；`check-offline` 346 Python + 292 C 全绿；`make kernel` 零警告。**未加载，会话未碰。**

## 实测（执行过，零硬件触碰声明）

1. 开工即声明；`lsmod` 开工收工一致（probe ref 1 / bridge 默认 ref 0）。
2. 语料：`mtkm64.sys` SHA `0512ad5a…` 与语料库一致；堆表（11 槽）、segment 符号、MMU 页目录 1G 逐项核对（伪 C 是假设，只取布局事实）。
3. UMD 堆加总：SVM 256G（共享内存）+ USC/PDS 各 4G——SVM 非显存，纠正方法论（以 info 配额为准）。
4. 代码：1 头文件 3 宏 + 4 文件 include/替换（`0x10000` 的 BAR0/1 未动，PCI 标准且 scope 外，如实声明）。
5. 反向验证：改宏值 → FAIL；还原 → OK；重编零警告。

## 边界

- 1G 配额是 Host 切分，本轮只纠正认知 + 收敛字面量；配额验证（Host 侧）不在 Guest 范围。
- requirements.json 未动。

## 下一步（候选，需批准）

1. fire 函数（离线实现+门禁）。
