# r382: submit_3d_work 落地（mt_marker_ops 第 6 op，opcode 0x68，门控关闭，离线）

## 结论

**`submit_3d_work` 作为 `mt_marker_ops` 第 6 个 op 落地**（仿 r366 `submit_ta_work` 模式），DM2/opcode `0x68` (RGXCompute)，完成码标准 0。**门控宏 `MT_3D_SUBMIT_GATE` 默认关闭（0）**，op 返回 `-EOPNOTSUPP`；`0x82:0x14` dispatch 保持 r215 observer 不切换。**本轮零硬件触碰，离线实现。**

## 1. 实现落点

### 1.1 新头 `kernel/mt_3d_submit.h` (108 行)

| 定义 | 值 | 来源 |
|---|---|---|
| `MT_FW_DM_3D` | 2U | mt_work_command.h type 5→DM2 [MEASURED] |
| `MT_FW_3D_OPCODE` | 0x68U | r381: RGXCompute [MEASURED] |
| `MT_FW_3D_COMPLETE_CODE` | 0U | r381: 标准完成码 [MEASURED] |
| `MT_3D_SUBMIT_GATE` | 0 | r382: 默认关闭 |

`struct mt_3d_submit_params` (56B): `submission_va`@0、`submission_size`@24、
`check_fence`@44、`vm_map_hook`@48 (R5 预留，void* 未实现)。

`mt_3d_params_from_rgxkickta3d5()`: 纯函数映射 0x82:0x14 IN→params；
0x82:0x14 无 IN check_fence 字段，初始化为 0。

### 1.2 `kernel/mt_marker_fence.h` (+163 行)

- `#include "mt_3d_submit.h"`
- `struct mt_3d_work` (mirror `mt_ta_work`)
- `struct mt_marker_fence` += `d3_params` (Appended; pre-r382 不触碰)
- `struct mt_marker_ops` += 第 6 op `submit_3d_work` (+ ABI WARNING)
- `mt_fw_3d_command()`: opcode@0x0c, wire_id@0x48, command_va@0x28 (64-bit),
  size@0x30, pid@0x4c (r381 §4 布局)
- `mt_marker_submit_3d_work()`: 门控 `#if !MT_3D_SUBMIT_GATE` → `-EOPNOTSUPP`；
  开启后：校验 (非空：`submission_va`/`submission_size` 为 0 → `-EINVAL`，
  r380 教训) → check_fence 等待 → 加锁分配 fence → 建包 → 提交
- ops 表 += `.submit_3d_work`; 声明 `mt_bridge_submit_3d_work()`
- 完成码标准 0 → 通用 `mt_marker_complete()` 处理，无需 TA 式特殊 matcher

### 1.3 `kernel/recovery/mt_pvr_bridge.c` (+9 行)

```c
int mt_bridge_submit_3d_work(struct mt_marker_store *s,
                             struct mt_3d_work *work,
                             struct dma_fence **out)
{
        return mt_marker_submit_3d_work(s, work, out);
}
EXPORT_SYMBOL_GPL(mt_bridge_submit_3d_work);
```

## 2. 门禁

- `make -C mt-vgpu-guest check-offline`: **430 Python + 299 C 全绿**
  (+2 新测试: `test_3d_submit_op.py`, `test_3d_submit_layout.py`)
- `make kernel` W=1: **零新增警告** (4 个 pre-existing 警告来自 r376
  probe 死代码 `mt_probe_ta_vm_*`，与本轮无关)
- 反向验证: opcode 改 0x68→0x99 → 1 failure; 恢复 → 全绿

## 3. 未做事项 (诚实边界)

- **零硬件触碰**：未重载、未活体验证 (per task)
- `0x82:0x14` dispatch 未切换 (保持 r215 observer)
- `submission_va` → R5 VM 映射未实现 (仅 `vm_map_hook` 占位)
- `mt_probe_ta_vm_*` 死代码 (r376) 仍在，待清理
- 门控开启需活体验证 0x68 路径 (r381 TO-VALIDATE)

## 4. 证据

- `reports/r382-evidence.txt` (0600): 门禁输出 + 反向验证记录
