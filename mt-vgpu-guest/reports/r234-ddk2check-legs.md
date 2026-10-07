# r234：DDK2 check 双腿验证（批准执行）——`if (ncheck)` 在 DDK2 下同样真实

- **结论**：`if (ncheck)` 语义修复的 DDK2 回归验证通过：`=2` + `translate_kick=1` 桥上，r213 链形双腿——Leg1 零值匹配 0.046s 全过（`RGXKickSync → 0`，修复后零值仍即时命中，无回退）；Leg2 值1失配 5.005s 后 `RGXKickSync → 37`（等待在 DDK2 下同样真实，无 marker）。dmesg 仅 Leg1 落 `translated kick: check=1 update=0 tag=1 fence=15`（fence 序列延续）。DDK2 零值/失配与 legacy（r213/r222）同构。拆桥干净，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；UMD SHA `b3058c02…`；`/tmp` 2%；dmesg 打 `[r234] ddk2-check-legs-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_kick=1`（probe 未碰），节点仍 `renderD128`。
3. Leg1（match）：DDK2 全链（`b5*`）+ 零值 kick → 六符号全 0、exit 0、0.046s。证据 `r234-ddk2leg1-match.jsonl`（125 行）。
4. Leg2（mismatch）：同链值 1 → 5.005s stall → kick 37，harness 存活退出；无新增 translated 行。证据 `r234-ddk2leg2-mismatch.jsonl`（125 行）。
5. 恢复见 r235（同窗口续跑 ping 后统一恢复）。

## 边界

- DDK2 非零预置仍断（r228）；本轮只证明 DDK2 下等待语义真实，不证明 DDK2 非零链。
- 未用 `timeout` 包裹 harness（桥预算覆盖）；无代码改动。
