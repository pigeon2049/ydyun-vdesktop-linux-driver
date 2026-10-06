# r174：SubmitTransfer3 accept-and-log 上线，真实 CCB 39/39 落定（批准执行）

- **结论**：桥新增 `pvr_cmd_tdm_submit3_observe`（108B 校验 + transfer-context 验证 + VA→reservation→PMR 三重定界 + 1MiB 上限 + FNV/非零统计/dmesg 一行；不读嵌套指针、不执行、无 fence），门禁 5 项（路由/定界/鉴权/零嵌套读/零执行路径，反向掐断可抓；r150 旧“无 0xa”断言按 r130 先例改判为新契约）。真实 blit 两轮：窗口全部 **39/39 非零字节与 fabricated 模型逐字节一致，仅 `+0x40` 2B 不同**（`5c 80`/`cd 7c`）。`+0x40` 全样本（`28xx`×4、`7c`、`80`、`5c`）**非单调**，r161“计数器”命名收回，改称轮变 2B、模式未定。桥已恢复默认 + L3 复绿，freeze 继续。

## 实测

1. 代码：`pvr_cmd_tdm_submit3_observe` + `case 0xa` + wire.h 注释同步；`make kernel` W=1 零警告；`check-offline` 274 Python（+5 新门禁）+272 C 全绿；反向（掐路由）新门禁 FAIL、恢复 PASS。
2. 两轮 `=2` 重载窗口（每次 ref0，probe 零触碰）：真实 blit 提交均被观察——`va=0x8000f44000/4608/res=0x1035/pmr=0x1034/nonzero=39/first=0x10`，FNV 轮变（意料之中）；64B head 两轮：`58 40 f4 80 67 78 10 01 [5c80|cd7c] 02 2010 03 9010 01 20 01 0801 d007 20a3 80 12a1 80 10a1 80 01 10 03 03 01 0c08`——与 r160 runs 表逐项对齐（`+0x40` 除外），第二轮补全后 27 字节尾部。
3. UMD 接受后行为两样：首轮 exit 0（无输出），次轮 60s 未退被 timeout 终结——提交后路径（Wait/比对）行为不定，GPU 未执行是确定的（handler 无执行路径）。不展开断言。
4. 恢复：默认重载 + L3（node 0 failing，smoke PASS）+ dmesg 无模块 WARN；probe ref 1、bridge ref 0。证据：[`r174-real-ccb.jsonl`](r174-real-ccb.jsonl)（第二轮 8201 行）。

## 边界

- accept-and-log 不是执行：CCB 有效性、update 数组、fence/完成语义一概未碰；T3 翻译仍缺 DM 格式。
- `head` 只收非零字节（64 上限恰覆盖 39）；全零大窗口假设下此举无损——本窗口 4608B/39 非零已由计数器背书。
- `+0x40` 模式未定；UMD 退出行为不定（0/timeout）未深究。

## 下一步（候选）

- T3 translator 输入规约现已闭合（真实 CCB 在手）：DM 队列格式反推（r82–r84 链）可重启；update 非零 kick 仍待 `=2` 窗口。
