# r270：显存误读反思（零硬件触碰）——三层概念混淆 + 画像从未入库

- **结论**：用户纠正成立（1GiB 配额）。根因不是算错，是**方法论失效**：Guest 显存有三层（PCI BAR2 16G 窗口 / 固件启动期 90MB 池 / device-information 配额 1G），历轮抓了前两个就下结论，权威源（trial `info_raw` + `decode-device-info.py`，一直存在且版式经 `mtkm64.sys` 硬件验证）从未执行。STATUS/快照/MEMORY 全链无"硬件画像"条目，错误认知代代相传而无人核对。本轮横向排查确认遗漏矩阵：`max_width/height`（2560x1600）、编解码 3/5、mpc=1、`host_version`、`render_ready`、`vm_memory`、`bar2_actual`、`mapped_segment_end`、`pb_free_list`——**全部零入库**（grep 实锤）。修复：画像入快照 §12（首行）+ 头部硬件行（配额 + 内核 `.107`→`.111` 纠正，又一漂移活证据）+ AGENTS.md §8 检查单加画像解码项。门禁无法覆盖（trial 产物 gitignore），靠流程约束。

## 实测（执行过，零硬件触碰声明）

1. 开工即声明；未碰会话（`lsmod` 1/0，默认桥在载）。
2. 语料：`mtkm64.sys` SHA 全比一致（`0512ad5a…`）；11 槽堆表（GPA 布局，非配额）；MMU 页目录 1G 系地址划分；DXGK segment 符号确认上报路径存在。
3. UMD 堆加总反证：SVM 256G（共享内存）+ USC/PDS 各 4G——SVM 非显存，确立"以 info 配额为准"的方法论。
4. 解码：`decode-device-info.py` 出 21 字段；`connection` 出 `host_version`/`render_ready`；`preflight.json` 出 trial 参数/sha。
5. `info_raw` 拷 /tmp 解码后已删除（r258 流程）。

## 反思（本轮核心交付）

1. **为什么多轮都错**：① 三层概念未分（窗口/启动池/配额混为一谈）；② 权威源近在咫尺但重建流程（r211/r264）只核对 trial 状态，不解码画像；③ 知识库无画像条目，错误无收敛点；④ 无人质疑"90MB 配个 16G 窗口"的反常比例（数量级直觉失效）。
2. **教训**：基础硬件事实必须有单一可信源（快照 §12）+ 重建时重解 diff；引用基础事实先查快照，不凭记忆；用户纠正后横向排查同类（本轮执行）。
3. **同类排查结论**：显示能力/编解码/mpc/Host 版本/render 就绪全部未入库——已入库画像；PCI DID/VID/子系统（`1ed5:0222/1101`）散在各 recovery 文件字面量中（`0x1ed5/0x0222` 重复 4+ 处）——下轮收敛候选；firmware sha（`35d40f75…`）只在报告正文，无门禁——gitignore 产物，流程约束覆盖即可。

## 边界

- 1G 配额是 Host 切分，Guest 只读；配额验证（Host 侧）超出范围。
- 本轮未改内核码、未跑门禁（纯文档+AGENTS 约束）；`check-offline` 未跑（无代码改动）。

## 下一步（候选，需批准）

1. DID/VID/子系统字面量收敛（离线，r269 翻版）。
2. fire 函数（离线实现+门禁）。
