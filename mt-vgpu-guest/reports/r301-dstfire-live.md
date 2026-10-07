# r301：fire-into-destination 首验——执行落地，像素仍差（批准执行）

- **结论**：五开（`=2` + tqx_ctx + fire + bump + to_dst）窗口两发：首发尾块 `-ERANGE`（span 忘加 POOL_HEAD，20/21 块验过；排序 machinery 按设计拒 bump→abort，无 hang）；现场修 span（`span=dst->bytes` + 调度预检）+ 门禁，热换复打：`fired=1 chunks=21 verified=1 bad=0/1310720 todst=1` → bump → UMD 等待即过 → **仍 `Output does not match source` FAIL**。执行链（GPU fill + 落池）已通，差的是**内容/位置对错**：候选 ① 选错池（locate“最大池”误中源池）；② stride/pitch 非紧排；③ clear colour 取错（用了池解析色 `0xff0000ff`，待与 UMD 真 clear 色对照）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → 五开 → blit#1（134；`chunk 20 dst bounds`，`submit3 bump: fire failed: -34`，无 hang——排序正确性反证）→ span 修 + 门禁 + 重建（W=1 零警告）→ 热换（桌面全程未动）→ blit#2（exit=1，FAIL）。
2. blit#2 全链（dmesg + stdout 双收）：`scheduled todst=1` → `fired=1 ... todst=1` → `bump update=2` → `Submit transfer command OK → Wait OK` → `Output does not match source`。
3. 证据：`r301-dstfire.jsonl` + `r301-dstfire2.jsonl`（0600）+ `r301-blit-stdout{,2}.txt`（0600，UMD 判决原文）；暂存区已清空。门禁 378+292 全绿（沿用 + 增量）。

## 边界与下一步

- r302（离线）：① 池源/目归属（trace PMR 清单：尺寸/内容/绑定）；② stride（`mt_transfer_surface` 有无 pitch）；③ UMD 真 clear 色（blit 二进制字符串/ fabricated `0xff0000ff` 出处）；④ 比对区（整池 vs 矩形）。判据：任一定位即改 + 窗口验。
