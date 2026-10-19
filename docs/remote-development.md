# 大助手与设备直连：开发和运行

此模式通过热点 + HTTPS 长轮询更新设备，Mac 仅提供维护和备用网页入口。固件执行原文排版，服务端拥有唯一任务版本。第一版 USB/BLE 代码保留用于维护；旧 Mac 图片发布器在迁移后停止运行。

## 构建

沿用 `docs/development.md` 的 ESP-IDF 5.5.2、LVGL 9.4.0 和 Python 环境。运行 `scripts/build-firmware.sh` 时生成单色字库到忽略目录 `local/device-font/`，再复制到构建镜像。源字体仍为 `server/assets/SourceHanSansCN-Regular.otf`，许可见同目录声明。不要手改生成的字库。

字体分区位于 `0x860000`，大小4MiB；原 NVS、应用与双缓存地址保持不变。完整 flash 目标包含字体；只刷应用无法首次启用中文排版。RTD4 原文快照和字体格式 RLF1 固定为版本1。RTD0–3缓存仍可读取；首次接收 RTD4 后拒绝旧图片覆盖。

## 独立服务

Python3.11虚拟环境，依赖 `server/requirements-remote.txt`。仅在项目自有目录安装，不改系统 Python、OpenClaw 配置或其他进程。部署布局：

```text
/opt/rlcd-todo/
  app/server/      # 服务源码
  venv/            # 专用依赖
  private/         # 配置、证书、令牌（0600）
  data/            # SQLite、字体清单、响应记录
  logs/            # 本服务日志
```

用 `RLCD_REMOTE_CONFIG` 指向私有 JSON，字段：database、font_manifest、tokens（assistant/web/device 三个独立令牌）、host、port、tls_key、tls_cert、sessions_root、owner_sessions；可用 ntp_host 指定服务端校时监听地址。只在确认 UDP123 空闲时启用；不得替换已有时间服务。

HTTPS 使用私有 CA，校验主机名/IP SAN。设备信任 CA，校时后验证有效期，绝不关闭证书校验。服务器证书到期前需续签；CA私钥留离线维护端。服务端不修改系统时间。部署前后比较现有服务 PID、OpenClaw 配置和 cron 文件摘要。Supervisor 仅增加/更新 `rlcd-todo` 这一项，不执行全局 restart/update。

从旧服务迁移：先备份 SQLite，再用 `scripts/migrate-remote.py SOURCE DESTINATION --font-manifest MANIFEST` 迁入**空**目标库。草稿与最近发布分别保留，新文本发布版本递增，不自动发布原草稿；原库不修改。

## 设备配置与状态

通过已经绑定的 USB 协议发送 `network` 操作，config 为私有 JSON 字符串，包含 ssid、password、url（https）、ca（PEM）、token（device 凭据）、ntp（时间服务器）。配置写入设备 NVS；不在源码或日志中记录密码/令牌。更换配置会中断本设备请求并重新连接。

日期旁显示 Wi-Fi 与 HTTP 两个图标：Wi-Fi 实心表示获得地址，空心表示连接中，划线表示离线；HTTP 相连表示最近请求成功，断开表示网络错误，横线表示尚未验证，感叹号表示授权或证书异常。25秒正常等待不闪断。状态变化不重置5秒轮播。

HTTP 请求分别有35秒长轮询、30秒下载、10秒回执的整体截止时间。网络失败按1/2/4/8/16/30秒加随机抖动恢复；恢复后先立即检查最新版本。200/204均表示服务可达；内容错误与网络错误分开，设备保留上一份有效内容。

## 备用网页与技能

Mac 私有网页配置包含 url、token（web 凭据）、ca（本机证书文件）。配置文件位于 `local/assistant-integration/web-config.json` 时，原 `scripts/service.sh start` 启动仅监听127.0.0.1的代理。网页不持有第二份任务库，草稿和发布使用服务端版本检查；无法连接时不伪装保存成功。

右侧是文字草稿/已发布内容，非精确排版预览。“获取设备当前画面”按需返回300×400单色截图，包含日期与状态。关闭网页或退出Mac不影响设备直连。

`skills/desktop-todo/` 部署到助手技能目录，配置 `RLCD_ASSISTANT_CONFIG` 或默认的项目 private 配置。候选只有从 allowlist 中本人消息核验到完整“确认 T…”后才发布；候选24小时有效，清单改变后必须重新确认。不要用模型返回的 true 代替本人确认。

## 验证与回退

运行 `python -m pytest -q`、`scripts/test-core.sh` 及文本排版测试；构建成功、通信成功、烧录成功和真屏确认分别记录。固件接受前验证全文、分页、字体支持、大小、版本及摘要，写双缓存并刷新后才回执。测试真实断网、服务器仅本进程重启、设备重启、重复发送与过期候选。

有问题时停止本服务，保留数据库、私有配置及原设备完整备份。需要恢复旧模式时，恢复已核验的完整Flash与旧Mac数据库再启用旧服务；不要把旧图片推送器直接接到新文本版本上。

## 等待设备后的通知

Skill 在三次短时状态查询后仍未显示时，可用 `todo.py watch VERSION` 注册结果通知。独立服务的持久化队列等待已显示、失败或被更新版本替代，再调用私有配置中的 workflow_script，复用现有双端投递台账。没有注册的发布不主动通知。发送结果不明确时保留 needs-review/sending 状态，检查既有台账后人工处理，不自动重发；此机制不新增 cron、不调整聊天网关。

维护示例：`python scripts/maintain-device.py status`、`configure --network-config PRIVATE_JSON`、`push --transport usb` 或 `push --transport ble`。补发下载服务端已发布快照并验证摘要，不生成本地版本、不发布未确认草稿。
