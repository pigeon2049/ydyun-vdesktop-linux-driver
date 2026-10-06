# r148：真实 check-only marker 后 translator 卸载干净；probe 引用回基线

- **结论**：`translate_kick=1` 下，一个真实厂商 UMD check-only kick 经 DM2 空 marker 完成；随后卸载桥触发 translator teardown，桥报告 `unloaded cleanly`，probe 引用从 25 回到 1，无新增 WARNING。Translator 的正常持有与退出路径首次活体验证闭环。
- 会话重建按用户批准执行。终态保持 `mt_guest_probe` retained；桥已卸载，render node 随之移除。

## 实测

1. 开始时无 `mt_*` 模块，PCI 未绑定，`Guest=2/FW=1`。`mt_cold_disconnect finish=0` 只读核对 18 个环全 idle、`started=0/FW=1`；移除 helper 后以 `finish=1` 安全置 `Guest=0`，双重读回 `Guest=0/FW=1`。
2. `fresh-trial.py --run --runtime-context` 成功：trial `20261004T115236Z-56dba2d1`，connect/publish/retain 均为 1，Guest/FW `2/2`，probe ref=1。UMD SHA-256 为 `b3058c02…`；内核 vermagic 与运行内核一致。
3. 初次 check 值为 1，而 PMR 实测值为 0；桥等待预算 5 秒后返回 `-ETIMEDOUT`，UMD 返回 37。没有 marker 提交，引用数未变。改用实测值 0 后，同链六符号全 0，`0x88:0x4 ret=0`；dmesg：`translated kick: check=1 update=0 tag=1 fence=1`。
4. 卸载 `mt_pvr_bridge` 后 dmesg 记 `unloaded cleanly`；probe ref `25 → 1`，仅 probe 保持加载，`/dev/dri` 仅 `card0`，无新增 WARNING/BUG/Oops。

## 边界

本轮验证一个 check-only marker 与正常 bridge teardown；未验证 DDK2 模式、非空 CCB、update 数组或 TA/CDM 专属提交口。易失 trace 位于 `/tmp/opencode/umda/r148t2.jsonl`，重建证据位于 gitignored `build/fresh-trials/20261004T115236Z-56dba2d1/`。
