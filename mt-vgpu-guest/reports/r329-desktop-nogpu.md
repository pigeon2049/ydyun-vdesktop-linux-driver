# r329：桌面 GPU 禁用与菜单持久化（用户指令）——flag 不释放 renderD128

- **结论**：`--disable-gpu` 已写入 KDE 菜单用户覆盖层（`~/.local/share/applications/` 两份 `.desktop`，`Exec` 加 flag、`%U` 保留），经 `gio launch` 端到端验证生效（新实例 cmdline 带 flag）。但**flag 不释放 renderD128**：主进程启动即打开（两次实锤：直接带 flag 启动 60651→fd 37；菜单拉起 151314 照持）。`--disable-gpu` 只杀掉 GPU 子进程，main 的 render 占用与 flag 无关——bridge 重载仍需停桌面窗口（结论不变，预期管理）。拆桥未动（本轮未重载 bridge，默认在载）；refs 1/1，窗口零新增 WARN。
- **事故**：GDB `call close(fd)` 打进多线程 UI 进程致其重启（用户所见“opencode 重启”）——只读 attach 安全，**函数注入不安全**，此后禁用。另记：`pgrep -x` 对 >15 字符 comm 恒空（`musa_blit_test` 16 字符），早前数次“无残留”结论基于坏检查——好在均有 fuser/refcount 双保险复核，未酿成后果；以后只用 `pgrep -f` + comm 核对。

## 实测（执行过）

1. 用户原话“改一下 + 菜单启动参数禁用 gpu”。`sed` 生成用户级覆盖 + `gtk-launch` 失败（双后缀名 ID 对不上）改 `gio launch` 全路径成功；新实例 cmdline 验证 + fuser 验证（仍持有）。
2. 无代码改动，无门禁影响（桌面配置，非仓库构建）。
