# r186：scene 预设值抽入 `mt_addr_plan.h`（零硬件触碰）

- **结论**：bridge 与 6 个 live 文件重复写死的同一套 scene VA，
  已收敛到新建 `kernel/mt_addr_plan.h`（15 个宏，三处语义分组），
  7 个消费点只剩别名引用，无行为变化。门禁 287+292 全绿，
  `W=1` 零警告；反向验证（漂移任一值即被新门禁抓获）通过。

## 实测

1. 复核面：TQX 三元组（VA×3 + 字节×3）在 bridge + 6 live 文件共
   9 处重复；ctx Bo 基底/步长 3 处重复；translator scene
   （CMD VA/字节、space 页数、两处 fence 时序）只在 bridge 定义；
   prototype 几何 1 处。UAPI 数（bridge/ioctl ID）、堆蓝图、BAR
   尺寸各有其主，不动。
2. 改动：新 `mt_addr_plan.h`；bridge.c 删 3 组本地 `#define`、
   别名数组改引宏；6 live 文件加 include 并替换 20 处字面量
   （stream 端点 `0x40100000`/`0x40200000`、slot_va、
   readback 除数语义不同， deliberately 保留，见头文件注释）。
3. 过程发现（诚实记录）：首版 `MT_TQX_STATE_BYTES` 与既有
   `mt_tqx_copy.h:9`（同名不同值：`0xa8` 系 copy 命令状态结构）
   撞车，被 `W=1` 构建抓获——此前 `head -3` 截断了碰撞检查输出，
   教训重演 §8（截断≠没有）。已改用 `MT_TQX_*_BO_BYTES` 解决，
   现零警告。
4. 门禁：新增 `test_pvr_addr_plan.py` 5 项（逐值钉死 legacy 字面量、
   7 文件 include、code 区无裸字面量、bridge/ live 别名引宏）；
   反向：`MT_TQX_CMD_VA` 改 `0x40000001` 即红，还原即绿。
   全量 `check-offline` 287 Python + 292 C 全绿。

## 边界

- 纯重构：宏值与 legacy 字面量逐位一致（门禁钉死），无行为变化；
  未碰模块加载与会话。
- `mt_translate_kick.h`（RT 块，用户态测试共享）保持不动，
  避免跨域 churn。

## 下一步（候选）

- 活体项仍待批：kill-while-busy 关账 / TQX 真发射 / `=2` update 验证。
