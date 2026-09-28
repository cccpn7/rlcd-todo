# RLCD Todo · 桌面待办纸

Waveshare ESP32-S3-RLCD-4.2 的个人每日待办展示器：今日重点、今日杂项、今日追踪，300×400 黑白竖屏。

在 Mac 网页编辑三份清单，修改自动保存；点击 **更新屏幕** 才发布。设备通过 USB 或 BLE 接收，USB 优先；断开 Mac 后继续显示缓存。未完成事项自然留到第二天。

## 开始使用

准备 Python 3.12，在项目根目录运行：

```bash
./scripts/setup-server.sh
./scripts/service.sh start
```

网页地址：[桌面待办纸](http://127.0.0.1:8765)。仅允许本机访问。关闭网页不会停止服务，停止用 `./scripts/service.sh stop`。

设备需先安装本项目固件；第一次通过 USB 绑定，之后可使用 BLE。Mac 应打开蓝牙，首次配对按设备屏幕显示的六位码操作。不要把仅供电线缆当成 USB 数据连接。

- KEY 短按下一页；BOOT 短按上一页；KEY 长按返回重点首页。
- 正文 18px、行高 24px；完整原文自动换行、跨页，不省略。
- 网页右侧分别预览草稿和已发布内容；完成、排序和删除也需要发布才更新设备。

## 状态与资料

第一版已实现并烧录。USB、BLE 发布与设备刷新确认通过，画面和按键已由用户实测；物理拔线切换、完整断电和日常试用仍待补验，详见 [验证记录](docs/validation.md)。

| 入口 | 内容 |
| --- | --- |
| [产品计划](docs/product-plan.md) | UI、产品规则、实施顺序和验收 |
| [项目规则](AGENTS.md) | 协作、开发与公开仓库约束 |
| [架构与接口](docs/architecture.md) | 模块职责、API 与传输协议 |
| [开发说明](docs/development.md) | 安装、编译、烧录、调试与恢复 |
| [硬件资料](docs/hardware.md) | 引脚、屏幕和硬件限制 |
| [决策记录](docs/decisions.md) | 当前选择及其原因 |
| [测试说明](tests/README.md) | 自动化与实物验收 |

`firmware/` 是 ESP-IDF 固件；`server/` 是 Mac 服务，`server/web/` 为网页；`scripts/` 放启动、构建和测试入口。`local/` 存储私有数据库、绑定信息和出厂备份，全部忽略；删除它会丢失本机事项和绑定，请自行备份。

第一版不做项目管理、父子任务、语音、计时、多用户或公网服务。大圣接入预留统一 API，当前只验证模拟推送。

## 第三方来源

显示驱动来自 [Waveshare 官方仓库](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)，固定提交及修改见 [第三方说明](third_party/README.md)。字体使用 [思源黑体](https://github.com/adobe-fonts/source-han-sans) 2.005R。分别保留 Apache-2.0、SIL OFL 1.1 声明。本项目原创代码暂未选择许可证。
