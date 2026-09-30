# Windows 原厂 KMD 交叉核对（bA16）

日期：2026-09-30。目的：用 `mtkm64.sys` 等 Windows 驱动的反编译结果，校核我们自研
Guest 侧的记忆/提交模型，为 Stage B 内核桥提供**厂商自己的设计**而不是我们的发明。
全程只读：无硬件访问、无模块加载。

证据标注：T=动态、S=静态反编译、[ASM]=原始指令、H=头文件、[✗]=无法证实。
每条结论都给了 `decompiled/<file>/<file>` 的行号，可直接复核。

---

## 1. 堆表：11 项与 Windows 参考表逐字节一致（本轮新证据）

`mtkm64.sys` 的 `FUN_14001ccd8`（`decompiled.c:22804`）建立显存计划：
按 `MMU+0x108` 选两张静态描述符表之一（[ASM] `disassembly.txt:31103`）：
MMU 模式 1 → `0x141030fa0`，其它（本机 S3000 为模式 0）→ `0x141030d90`；
共 **22** 项（`decompiled.c:23169`：`param_4+0xe1 = 0x16`）。
每项 24 字节：`+0x00` 堆 id、`+0x08` GPU VA base、`+0x10` size。

新脚本 `scripts/dump-windows-heap-table.py` 读出全表并与 `mt_guest_plan_heaps()` 对照，
落盘 `reports/windows-heap-table-22.json`，由 `tests/test_windows_heap_table.py`（7 项）钉住。

| 堆 | Windows（mode 0） | 我们 | 判定 |
| --- | --- | --- | --- |
| 0 | `0x40000000` + `0x8000000000` | 同 | 一致 |
| 1 | `0x8100000000` + `0x100000000` | 同 | 一致 |
| 2 | `0x8400000000` + `0x100000000` | 同 | 一致 |
| 3 | `0xa000000000` + `0x1000000` | 同 | 一致 |
| 4 / 5 | 空 | 空 | 一致 |
| 6 | `0xe1c0000000` + `0x100000000` | 同 | 一致 |
| 7 | `0xec00000000` + `0x8000` | 同 | 一致 |
| 8 | `0xec40000000` + `0x1000` | 同 | 一致 |
| 9 | `0xeb00000000` + `0x100000000` | 同 | 一致 |
| 10 | `0xf000000000` + `0x100000000` | 同 | 一致 |
| 11–21 | 空（id 0x0b–0x15） | 未填 | 一致 |

**结论：9 个非空堆全部逐字节吻合，4/5 两堆双方都空。** 此前把 3/7/8/9 记为
「无法比较」是错的——值就在 `.data` 里，方法与 `scripts/verify-mmu.py:66` 相同。
另外，模式 1 表的 17–21 号槽里是**非表数据**（乱码状大数），说明该表在静态镜像里
只部分初始化，**不要**把它当堆表读。

### 1.1 顺带纠正两处旧报告

| 旧说法 | 位置 | 实际情况 | 证据 |
| --- | --- | --- | --- |
| 「`+0x24` 是堆/段数，`selector 0x0200` → 4 heaps」 | `windows-resource-mapping-audit.md:9` | `+0x24` 是 **IO 窗口槽计数**（4/5），`gpu_device+0x24` 全文无堆表读取方；Windows 堆表是 **22 项**、资源 profile 是 **13 项** | [ASM] `disassembly.txt:65539-65540`、`:31107`；[PC] `decompiled.c:51084-51091`（另一个对象上的 `+0x24` 才是段数，上限 0x20，日志 `"add segment failed, segment_cnt:%d more than supported"`） |
| 「`0x1800000` 是 Host/Guest 每 OSID 步进」 | `windows-fw-heap-ring-investigation.md:60-67,167-171` | 在 `mtkm64.sys` 里 `0x1800000` 是**普通私有池大小 24 MiB** | [ASM] `disassembly.txt:41878` `mov r10d,0x1800000` → `[r8+0x498]` |

### 1.2 13 项资源 profile 也一致

Windows 的 13 项「资源↔堆」profile（名称见 `strings.jsonl:304-316`，循环
`[ASM] disassembly.txt:31167-31168` 判 13 次）与我们的 13 项逐项一致，**包括掩码**：

- 启用位掩码 `0x1ef9`（`decompiled.c:11055`）——bit1/2/8 关闭，正好解释我们表里
  `Pmva Info`/`PMVA`/`PH1 patch buf` 为 0；
- 延后（VA 推后）掩码 `0x1220` = bit{5,9,12}（`decompiled.c:23094-23095`）
  ⇔ `kernel/mt_guest_heaps.h:77` 的 `&0x1220`；
- 堆尾保留与 2 MiB 对齐（`decompiled.c:23112-23117,23154-23165`）⇔ 我们的
  `reserved_base/reserved_size` 算法。

**命名缺口**：Windows 侧只给 13 个**资源**命名，从不给 22 个**堆**命名；
`General / PDS Code and Data / USC Code / Component Control` 只存在于 Linux
PVRSRV UMD 侧（`linux-legacy-umd-5.2.0/decompiled.c:38335,47526,57340`）。
⇒ Stage B 的堆名表只能以 PVR UMD 的名字为准，Windows 侧提供的是**数值锚点**。

### 1.3 Windows 不做 per-OSID 堆

`mtkm64.sys` 全文 `osid` 零命中；`FUN_140026390`（`decompiled.c:32048-32117`）只读
信息页 `+0x10`（flags）、`+0x20`（卡容量）、`+0x28`（24B 段表）、`+0xc50`（段数），
**从不读 `+0x08`（OSID）**。Host 算好段表，Guest 只做本地 `MmMapIoSpace`
（`decompiled.c:49294-49307`）。BAR 用途：BAR0=寄存器、BAR1=虚拟化自定义寄存器、
BAR2(+3)=显存 aperture、BAR4=第二窗口、另有固定槽给 Local MMU 页表
（`decompiled.c:39463-39484`）。

---

## 2. 完成信号：厂商用的就是「令牌 + 环形队列」模型

这是对 Stage B 最有价值的一条：**驱动不自增 fence，也不做串行化裁决**。

### 2.1 提交侧（FenceID 由 OS 分配）

```
dxgkrnl SubmitCommand → FUN_140005f98 "SubmitComand"（decompiled.c:3830）
  → FUN_140013f3c "SIMSubmitCommand"（:16121）
      队列块 = base + 0x28 + engine*0x78 + queue*0x40     (FUN_14000bd54, :8929)
      queue->+0x04 = cmd->FenceID (cmd+0x3c)             (:16163)  ← OS 给的值
      rec = FUN_1400149ec(...)  0x98 字节工作记录         (:16519)
      0x50 字节描述符 → FUN_14000bf20 → 门铃              (:16406/:9141)
      MMIO 写: *(u32*)(bar+off)=val                       (FUN_140007d78, :5535)
```

0x98 字节记录布局（`decompiled.c:16529-16556`，已复核）：

| 偏移 | 内容 |
| --- | --- |
| `+0x00` | DMA 缓冲源指针低 32 位 |
| `+0x04` | flags：bit0/bit1/bit3/bit4 由 DDI 头推导，**bit4 = 有扩展数据**；`0x20` = 可被抢占 |
| `+0x08` | **FenceID**（与固件回填值比对） |
| `+0x10` | context 指针 |
| `+0x28/+0x2c/+0x30` | 初值 `0xffffffff`（未就绪哨兵） |

### 2.2 完成侧（固件回填 + 逐条比对）

固件在完成时把同一 FenceID 原样写进 **0x18 字节事件**的 `+0x08`；
KMD 排空 6 个 DM 的事件环并比对（`FUN_14000e6a4`，`decompiled.c:11467`，已复核）：

```c
if (queue->head /*+0x28*/ != queue->tail /*+0x2c*/) {
    fence = *(int *)(event + 8);                                   /* 固件回填 */
    if (fence == *(int *)(*(u64 *)(queue+8) + 8 + head*0x98)) {      /* 我方记录 */
        queue->+0x08 = fence;                                       /* lastCompleted */
        notify(0x50 字节, Type=1);                                  /* DxgkCbNotifyInterrupt */
        head = (head + 1) % adapter->0x42c;                         /* 队列深度 */
    }
}
```

事件类型：`0`=正常(Notify 1)、`5`=抢占(Notify 2)、`0x50`、`0x101`=故障(Notify 9)。
固件环布局（[ASM] `disassembly.txt:12327-12395`）：每 DM 块 **0x2e30** 字节，
命令环 64×0x50 在 `+sub*0x1400`、完成事件 64×0x18 在 `+0x24f0`、
命令环 head/tail 在 `+0x2e00`、事件环 head/tail 在 `+0x2e20`（索引 `&0x3f`）。
**单生产者（固件）单消费者（KMD）的无锁 64 槽环。**

### 2.3 对 Stage B 的直接含义

1. 我们不需要发明 fence 语义：照搬「**每队列单调令牌 + 提交环 + 完成环 + 比对推进**」。
2. `EVENTOBJECTWAIT`（`0x1:0x5`）的语义就清楚了：**等某个令牌被完成环兑现**，
   而不是等中断。我们的固件完成事件链（`kernel/mt_fw_event*`）就是那个完成环。
3. 提交环满时厂商的行为是自旋 10000 次 `KeStallExecutionProcessor(0x5a)` 后
   **静默丢弃**（[ASM] `disassembly.txt:12417-12441`）——我们应改为回错
   （`-EAGAIN`/`-EBUSY`），比丢弃好，但要有界。
4. 驱动侧**没有** per-fence 的 `KeSetEvent`（全文仅 1 处，在
   `"Render Complete Event"` 路径，`decompiled.c:1022-1033`，与 fence 无关）；
   唤醒靠 `DxgkCbNotifyInterrupt` 等价回调。

---

## 3. 上下文规模：Windows 真实分配了什么

| 结构 | 大小 | 证据 |
| --- | --- | --- |
| SIM Context 主结构 | `0x88` | `decompiled.c:184376`（tag `"Ctxt"`） |
| D3MK Context | `0x118` | `decompiled.c:12124`（tag `"D3MK"`） |
| 上下文子分配 ×2 | `0x5000`（20 KiB）对齐 `0x80` | `decompiled.c:184442`、`12129-12133` |
| **每上下文状态区** | **96,000 字节**（`+0x18…+0x178B0` ≈ 96.4 KiB） | `decompiled.c:24529-24530` |
| `mext` 上下文扩展 | 描述符 `0x50`，**上载 `0x800`（2 KiB）**/引擎 | `decompiled.c:24606-24627` |
| DMA 缓冲 | `0x4000`（16 KiB） | `decompiled.c:18414`、`:12282` |
| 每核描述符 stride | `0x80`，最多 8 核，core map `+0xb8`，`0xF`=跳过 | `decompiled.c:18412-18441` |
| 每核工作区 | `0x228`，内含 6 个 `0x58` 步进 mutex | `decompiled.c:24374-24383` |
| 每节点（DM）块 | `0x2e30` | [ASM] `disassembly.txt:12385` |
| 队列深度（模数） | `adapter+0x42c` | `decompiled.c:10883`、`:16556` |

**与我们 r36 的对照**：r36 我们凭原厂错误串自造了 11 个上下文 BO（合计约 84.3 KiB），
其中 TA state/DCE/VDM/DDM/光栅全部为零。Windows 侧证据说明真实每上下文状态区
**约 96 KiB**，量级与我们猜的接近，但内容必须来自 UMD 生成的 CSW 任务
（`RGXGenerateContextSwitch*Tasks`），这与 MEMORY「关键阻塞」结论一致，
**不改变**「必须走 UMD 桥接」的判断。

**一条可用的新线索**：`FUN_140019cc4`（`decompiled.c:20383`）向
`param_3+0xb0`（每槽 `0x40`，每槽 3 个 qword）与 `param_3+0xf0`（9 项）写入
成组常量，例如 `0x20000000 / 0x200020000000 / 0xfff00000fff00000 …`
（`decompiled.c:20414-20462`）。这是**宿主侧唯一定值的上下文寄存器组**，
其中第 0/1/2/3 组是同一模式的 4 份（每 MP/每 CDM 核一份）。
驱动没有给它们命名，固件里的 `MT_CR_*` 名字与这张表在文本层无法关联——
但它是**构造 render context 描述符时最像真值的一张表**，值得作为 S3 阶段的
审包对照物。

---

## 4. 明确查不到的部分（不要在这上面耗时间）

1. **CSW / Context-Store / Context-Load 的寄存器名与写序列**：
   相关格式串（`RGX_CR_CDM_CONTEXT_STORE0` 等）只存在于内嵌 GPU 固件镜像
   （`0x141042000`–`0x1411b7000`，`references_from:[]`），而 `functions.jsonl`
   在该区间**零函数条目** → 伪 C 不存在。要拿到必须单独反汇编固件 blob
   （Ghidra 未覆盖该区间）。
2. **固件侧串行化与超时参数**（`Serial-Kick`、`Kick Event Timeout`、
   `POLL_MAX_COUNT`/`POLL_INTERVAL` 实际数值）同上，只在固件里。
3. **`DAT_1402a6af0`**（引擎类型 → 上下文对象数量表，4 项 × 3 qword）位于 `.rdata`，
   现有语料只导出 `.text/PAGE/INIT` 三节（`disassembly.txt:5`），数值不可读。
   需要单独 `objdump -j .rdata`。
4. **22 堆表在 mode 1 下的完整内容**（17–21 号槽是未初始化的非表数据）。
5. Windows 侧**没有** PVR 的 `PhysHeap` 概念，也**没有**堆名表；
   Stage B 的 11 堆名字无法从 Windows 侧交叉验证（只能用 PVR UMD 侧字符串）。

---

## 5. 建议的下一步（按性价比）

1. **照 §2 的模型改 Stage B §7.4**：每队列令牌 + 提交环 + 完成环 + 比对推进，
   完成环直接用现有 `mt_fw_event*`。这是 L3→L4 的关键路径。
2. **堆表数值已闭环**（§1），Stage B §3.5 可加一句「9 个非空堆与 Windows 参考表
   逐字节一致，单测 `tests/test_windows_heap_table.py` 门禁」。
3. **S3 审包时把 §3 的 0x96,000 字节状态区与 `FUN_140019cc4` 常量表作为对照物**。
4. 若要拿 CSW 寄存器语义，单独对固件 blob 做一次 Ghidra 分析
   （`0x141042000`–`0x1411b7000`，约 1.6 MB），别混在主机代码批次里。

---

## 6. 复现命令

```sh
cd mt-vgpu-guest

# 22 项堆表 dump + 与 guest 计划对照
python3 scripts/dump-windows-heap-table.py

# 门禁
python3 -m unittest tests.test_windows_heap_table -v
python3 -m unittest discover -s tests        # 全量 116 项

# 关键常量的人工复核入口
rg -n "FUN_14001ccd8|0x1ef9|0x1220" decompiled/mtkm64.sys/decompiled.c
rg -n "0x2e30|0x24f0|0x98" decompiled/mtkm64.sys/disassembly.txt | head
```
