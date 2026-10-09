# r399: R5 Phase 2 活体验证--无 ctx kick 返回 -EINVAL，有 ctx 仍正常

## 结论

r398 删除 per-file VM 回退后的首个活体验证：无有效 render_ctx 的
0x82:0xC kick 返回 -EINVAL（errno 22），有 ctx 的 kick 仍正常
（OUT.update_fence=10 对应 dmesg wire=10 精确匹配）。per-context 为唯一路径
的假设在活体得到确认。门禁 474+299 全绿，make kernel W=1 零警告。
dmesg 零 WARN/BUG/Oops，refs 不变（probe 13 / bridge 0）。

## 验证过程

前置：pre-live 安全门禁（r394 T1/T2/T3）全绿后执行。
bridge 从 r397 构建重载到 r398 构建（safe_rmmod.sh，probe 不动，
ref 0->0，干净）。

V1（无 ctx -> -EINVAL）：
- harness：0x82:0xC IN 268B，h_render_context=0x0，kick_ta=1 at offset 188
- 结果：ioctl 返回 -1，errno=22（Invalid argument）
- dmesg：musakickgfx2: no live render_ctx (r398 Phase 2, per-file VM removed) -> -EINVAL
- 无 oops，无 WARN

V2（有 ctx -> 成功）：
- 0x82:0x12 create -> handle=0x1000，eError=0
- 带 ctx 的 0x82:0xC kick -> ret=0，OUT.error=0，OUT.update_fence=10
- dmesg：render ctx VM created, base_va=0x70000000 -> submitted wire=10
- 回填值与 wire 精确匹配

V3（destroy -> 无泄漏）：
- 0x82:0x13 destroy -> ret=0，eError=0
- probe ref：13（基线）-> 25（create 后）-> 13（destroy 后），delta 归零
- bridge ref 恒 0

## 门禁

- make -C mt-vgpu-guest check-offline：474 Python + 299 C 全绿
- make kernel W=1：零警告
- 本轮无代码改动（纯活体验证），反向验证沿用 r398

## 诚实边界

- Marker 级 TA 路径（零绘制），非真实 payload；MT_TA_VM_READY 门保持关闭
- probe ref=13 基线含 r389 旧泄漏（下次冷重启清零）；本轮 delta=0，无新泄漏
- 一次性 harness 已清理（build/traces/r399/ 已删），未入库

## 安全合规

- pre-live 门禁（T1/T2/T3）先行，全绿
- 一次 bridge 重载（r360 流程 + safe_rmmod.sh），probe 未动
- 未重启；timeout 未进临界区；rmmod -f 零出现
