# 官方下载页真实 Chrome 核验

核验日期：2026-09-22（Asia/Shanghai）

本机 `/usr/bin/google-chrome` 已启动真实 Chrome 153，并在独立 profile 中打开：

`https://soho.komect.com/clientDownload`

页面标题为“下载云电脑客户端”。随后在该真实页面上下文中读取官方下载 API：

`https://soho.komect.com/cube/h5/user/download/urls/v2/1`

API 返回成功，且包含以下与本地保存包对应的官方入口：

| 客户端 | 官方下载标识 |
|---|---|
| Windows | `eeda8cbe9e396866` |
| UOS AMD64 | `ad2bcdde85d84d6a` |
| 麒麟 AMD64 | `ce91e6b419aaf831` |

该步骤只验证官方网站、下载路由和包来源，没有登录、提交凭据或执行官方客户端。
真实 Chrome 与 Codex 内置浏览器是两个独立运行时；本次使用的是本机 Chrome 的可见页面。

## 真实 Chrome 当前下载复核（2026-09-22）

本次直接控制本机真实 Chrome（Chrome 153，CDP `127.0.0.1:9223`）刷新页面，并在页面
上下文中重新 `fetch` 了下载接口。接口返回 HTTP 200，当前 AMD64 UOS 下载地址仍为：

`https://dl.soho.komect.com/upgrade/download/app/ad2bcdde85d84d6a`

同时返回的 Linux 入口包括：

- UOS AMD64：`ad2bcdde85d84d6a`；
- UOS ARM64：`a4949f3ad5699fe3`；
- 麒麟 ARM64：`25c35a077a07a1a3`；
- 麒麟 AMD64：`ce91e6b419aaf831`。

从真实下载地址取得的当前 UOS 包响应为 `CMCC-JTYDN-UOSx86-2.23.1.deb`，大小
246700884 字节，SHA-256 为：

`65fa5a093d73bfef407304b32ed45a1c761d4b105ebdcfa2ffea67e9de751202`

包已保存并解压到当前工作区的 `doc/client-packages/`，没有写入 `/tmp`。其关键 ELF
哈希与此前保存的 UOS 包相同，说明当前官方下载包不是一份行为不同的更新样本。
