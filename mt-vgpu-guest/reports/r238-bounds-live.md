# r238：定界活体验证（批准执行）——超窗 `-EINVAL`、野 index `-ERANGE`

- **结论**：两处安全定界在活体证实真实生效：`pvr_observe_ping` 在 CCB fire 后追加负向双测——2MiB submission_size → `-EINVAL`（1MiB 上限守住），`index=0xFFFFFFFF` → `-ERANGE`（`index*4` 防溢出守住），均无内存触碰（errno 语义证明拒绝发生在解析/定界层）。24 项全 ok，exit 0；refs 1/0 不变（默认桥，无需重载），dmesg 干净。**Freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0；`/tmp` 2%；dmesg 打 `[r238] bounds-start` 标记。
2. 工具（离线部分）：负向双测（errno 先存后判，node_probe `step_expecting` 同款习语）；门禁 +1（双 errno 断言）；`-Werror` 零警告构建。
3. 反向验证：改错期望 errno → FAIL，还原 → OK，重编。
4. 活体：在 `mt-vgpu-guest/` 下运行（CCB 相对路径），24/24 ok。事后 refs 不变；dmesg 零 WARNING/BUG/Oops。
5. `check-offline`：327 Python（326+1 新）OK；C/内核沿用 r222（本轮零内核改动，未重跑）。

## 边界

- 只证明拒绝语义；拒绝后的 UMD 行为（37 容错）由 r221 先例覆盖。
- 本轮 fresh file 自包含 + 全 teardown；未用 `timeout` 包裹。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
