# r309：颜色扫描双发——RED/GREEN 均排除（批准执行）

- **结论**：颜色覆盖参数（`translate_fire_color`，0=池色）窗口内连打两发：RED（`0xffff0000`，libsrv ×20）→ fire 全验（`first=last=0xffff0000`，覆盖生效）→ FAIL；换 GREEN（`0xff00ff00`，blit 用例分支）→ fire 全验 → FAIL。**三色（池 `0xff0000ff` + 红 + 绿）全灭**——颜色不是答案（至少不是单实色答案），剩余候选：stride/偏移/比对区/图案复制。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → 五开 + RED → blit#1（FAIL）→ 热换 GREEN（桌面全程未动）→ blit#2（FAIL）→ 拆桥 → 默认 → L3 双绿 → 拉桌面。
2. 两发 fire 均 `todst=1` 全验（执行面与颜色无关地正常）；UMD 两次止于像素比对。
3. 证据：`r309-red.jsonl` + `r309-green.jsonl`（0600）+ 双 stdout（0600）；暂存区已清空。门禁（r308）沿用。
