# r363：0x82:0xC 活体 IN 参数观察成功——r362 修正后 TA 路径打通，observer 解码 268B 全字段（真机活体）

**结论**：r362 的 GDB 脚本修正（`CreateSyncPrim` 入口捕获 `$rsi` 而非 `$rdi`）后，完整 TA 路径在真机上一次打通：`SyncPrimRef` 返回 0（未崩溃），`0x82:0xC` 到达桥侧 observer，268B IN 缓冲被解码记录并逐字段对照 r356 wire 结构。observer 返回 `-ENOTTY` 后路径即止，**未提交任何 GPU 工作**。

## 1. 基线（开工前，实测）

| 检查项 | 结果 |
|---|---|
| 在载桥 build-id | `0d6bb8d74929eec7c0bc1f88ec0b4dae7bd455da`（r356，含 observer）✓ |
| `mt_pvr_bridge` refs | 0 ✓ |
| `mt_guest_probe` refs | 1（freeze 未动）✓ |
| UMD `.so` SHA | `b3058c02…34237b0` ✓ |
| dmesg | 干净，无 WARN/BUG/Oops |

## 2. 执行流程（实测）

1. **GDB 脚本修正**（r362→r363 唯一改动）：`CreateSyncPrimBP.stop()` 中 `b10_buf = int(gdb.parse_and_eval("$rsi"))`（原为 `$rdi`）。返回时从 `*(uint64_t*)$rsi` 读描述子。
2. **描述子验证**：`b10_desc=0x…`，`desc+8=1` ✓，`desc+0x18=<ptr>`（非 NULL）✓，`desc+0x20=0` ✓——与 r352/r353 一致，r362 修正生效。
3. **INIT 修复**（本轮新发现）：GDB 在 main-stop 处 `open("/dev/dri/renderD128")` 后，必须再做 `ioctl(fd, 0x40046445, &init_module=2)`，否则 `pvr_bridge_dispatch` 因 `file->conn->srv_handle==0` 返回 `-ENOTCONN`，observer 不会被调用。首轮未做 INIT 时 observer 无日志；补 INIT 后一次成功。
4. **Poke**：`0x79c92` 处 `$rdx+0x48` 槽位 `old=0x0 → desc`，verify 通过。
5. **`SyncPrimRef` 返回 0**：未崩溃（对比 r361 在 `0xa0fa0` 的 SIGSEGV），TA 路径继续。
6. **`0x82:0xC` 命中**：`BridgeCallBP` 在 `0x92930` 捕获 `rsi=0x82, rdx=0xc`，`in_len=268` ✓；IN 缓冲已 dump（`in-82c.bin`，268B）；`$rdi` 换为真实 fd 后 ioctl 到达桥侧。
7. **Observer**：dmesg 记录解码行，返回 `-ENOTTY`；UMD 侧 `rax=0x26`（ENOTTY 映射）；路径即止。

## 3. IN 参数逐字段对照（实测 vs r356 wire 结构）

268B IN 缓冲按 `mt_pvr_musakickgfx2_in`（45 字段）解码，全字段见 `r363-field-table.txt`。observer 解码行：

```
ctx=0x0 abort=0 kick_ta=1 kick_3d=0 kick_pr=1 ta_size=360 3d_size=544 3dpr_size=544
draws=0 indices=0 mrt=0 ta_upd=1 ta_fence=0 3d_upd=0 pmr_sync=0
check_fence=0 check_fence_3d=0 rt_size=0
```

| 字段组 | 实测值 | 5.2 语义对照 |
|---|---|---|
| kick 开关 | `kick_ta=1, kick_pr=1, kick_3d=0, abort=0` | TA+PR kick，无 3D——与测试路径一致（TA only） |
| cmd 尺寸 | `ta_cmd_size=360, cmd_3d_size=544, cmd_3dpr_size=544` | 非零；3D 缓冲存在但 kick_3d=0 |
| update/fence | `client_ta_upd_count=1`（余 0），`pr_fence_value=1` | 恰为回填的 b10 sync prim 那 1 个 update |
| 绘制 | `num_draw_calls=0, num_indices=0, num_mrts=0` | 最小 TA kick，无实际绘制 |
| 句柄 | `h_render_context=0`（fabricated），`h_pr_fence_ufo_block=0x102d`（小整数 handle 形态） | ctx 为 0 符合 fabricated 预期 |
| 指针 | 全部非零：`p_ta_cmd` 等堆地址、`p_client_ta_upd_*` 等栈数组地址；`p_ta_cmd == p_3d_cmd`（同缓冲） | 指针链完整 |

## 4. Live 边界（如实记录）

- **已发送 1 次 `0x82:0xC` ioctl**：到达桥侧 observer，解码记录后返回 `-ENOTTY`；**未执行任何提交**（observer 明确非执行）。
- **未提交 GPU 工作**：符合本轮安全约束。
- **未做 rmmod/insmod**：桥保持 r356 构建在载（本轮无需换桥）。
- **未跑 `make probe`**：WITH_BRIDGE 会 rmmod；以完整交互为等效健康证据（沿用 r358/r360 取舍）。
- GDB 实验均为 userspace 进程（harness），对内核零影响。

## 5. 收尾安全检查（实测）

| 检查项 | 结果 |
|---|---|
| `mt_pvr_bridge` refs | 0（不变）✓ |
| `mt_guest_probe` refs | 1（不变，freeze 完好）✓ |
| dmesg | 仅 observer 日志 + 正常 arena close；无新增 WARN/BUG/Oops ✓（旧 userspace segfault 为既往轮次遗留，非本轮） |
| 桥 build-id | 仍为 `0d6bb8d7…`（未重载）✓ |

## 6. 方法教训（实测）

1. **INIT 是必须的**：GDB 内直接 `open()` 的 fd 必须再做 `ioctl(0x40046445)`（INIT），否则 dispatch 卡在 `srv_handle==0` → `-ENOTCONN`，observer 永不触发。此前 r361 脚本缺这一步（但 r361 在 SyncPrimRef 即崩，未暴露该问题）。
2. **r362 修正一次生效**：`$rsi` 捕获的描述子 `+0x18` 为有效指针，`SyncPrimRef` 返回 0，TA 路径直达 `0x92930`。

## 7. 建议的 r364

`0x82:0xC` 的 IN 语义已实测钉死。下一步二选一（按 r355 分解）：
- **R2a**：`0x82:0x14`（MUSAKICKGFX5）执行翻译设计（离线，r360 原计划）。
- **R2b**：`0x82:0xC` 真实执行设计：基于本轮 IN 字段映射 + `0x36ec0` 反汇编（r361），设计 bridge 侧真实执行路径（离线设计；活体执行需单列一轮）。

## 证据（0600）

- `reports/r363-observer-dmesg.txt`：observer 解码日志行
- `reports/r363-in-hexdump.txt`：268B IN 缓冲 hexdump
- `reports/r363-field-table.txt`：45 字段逐项解码对照表
