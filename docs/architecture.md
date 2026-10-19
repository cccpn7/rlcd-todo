# 当前架构与接口

```mermaid
flowchart LR
  Assistant[大圣 Skill] --> Confirm[候选与本人确认]
  Confirm --> API[独立 HTTPS 服务]
  Web[Mac 备用网页] --> Proxy[本机代理]
  Proxy --> API
  API --> DB[(唯一 SQLite 清单与发布)]
  Device[设备长轮询] --> API
  Device --> Layout[本地字库与分页]
  Layout --> Screen[显示及双缓存]
  Screen --> Ack[版本与摘要回执]
  Ack --> API
```

Mac 不参与日常无线转发。热点提供网络，设备主动连接服务；网页与大圣共用同一份任务和发布版本。正文排版在设备完成，网页只显示文字草稿和按需获取的设备截图。

## 当前模块与边界

- `server/remote_store.py`：SQLite 草稿、不可变候选、发布快照、回执和通知台账。
- `server/confirmation.py`：从受信本人会话的原始消息中核验完整确认编号、时间和身份；模型自行声明确认无效。
- `server/remote_app.py`：三类凭据隔离、版本化任务接口、HTTPS 长轮询、快照与回执、按需截图。
- `server/proxy_app.py`、`server/remote_web/`：仅本机入口，复用远端接口，不建立第二份数据库。
- `firmware/main/text_layout.hpp`：RLF1 字库、RTD4 原文快照、完整性/容量检查和设备原生分页。
- `firmware/main/network.cpp`：Wi-Fi、HTTPS 整体超时、退避恢复；通过队列交给显示线程，网络等待不阻塞轮播。
- `skills/desktop-todo/`：读取 → 候选 → 本人确认 → 发布 → 查询设备回执；需要延迟反馈时登记一次结果通知。

## 当前 API 与一致性

完整接口见 [实施计划](assistant-integration-plan.md) 与运行服务的 `/docs`。网页使用 `POST /api/v1/draft`（base 修订号与增量 changes）和显式 `POST /api/v1/publish`；助手只能创建候选并通过可信确认接口发布，不能使用网页凭据绕过确认。

设备 `GET /api/v1/device/updates` 等待最多25秒，有变化立即返回；下载完整文本快照、校验、保存并显示后提交回执。旧版本不会覆盖新版本，相同版本和摘要不重复刷新。正常204保持连接健康；错误时清理连接、退避并重新查询最新版本。设备在日期旁独立显示 Wi-Fi 和 HTTP 状态，服务端记录带采集时间，不能把陈旧状态当作实时画面。

`GET /api/v1/publications/{version}` 返回 queued、updated、failed 或 superseded；`POST /api/v1/publications/{version}/watch` 登记一次结果通知，复用既有投递台账，结果不明不盲目重发。连接成功与内容显示成功分开记录。

RTD4 使用小端头 `<4sQQHH>`（magic、版本、发布时间、事项数、布局版本），逐项 `<BHI>`（分类、编号字节数、原文 UTF-8 字节数）后接编号与原文。完整 SHA-256 由下载元数据和缓存头保存。限制128KiB、1024项、每项20KiB、128页；接受前逐页验证。RLF1 字库分区4MiB，按需映射读取，不整库占用内部内存。RTD0–3仅用于迁移前缓存兼容，首次文本同步后拒绝旧图片发布。

USB/BLE 共用已有认证分块协议，仅用于维护及转发服务端同一已发布版本。`network` 配置操作限已绑定USB；不把热点或凭据编入固件。部署、恢复和私有文件布局见 [直连开发说明](remote-development.md)。

---

# 旧版 Mac 图片模式（回退参考）

```mermaid
flowchart LR
  Web[Mac 网页] --> API[FastAPI 本机接口]
  Assistant[未来大圣] -.-> API
  API --> DB[(SQLite 草稿与发布快照)]
  DB --> Render[统一字体与分页引擎]
  Render --> Preview[网页预览]
  Render --> Link[USB 优先 / BLE 回退]
  Link --> Device[LVGL 显示 / 按键 / RTC]
  Device --> Cache[两份校验缓存]
```

## 模块职责

- `server/`：数据库、接口、统一黑白页面渲染、发布调度及 USB/BLE 传输。
- `server/web/`：直接编辑三份清单，通过本机接口保存；不维护第二份任务库。
- `firmware/`：300×400 像素映射、LVGL 显示、按键、RTC、通信认证、缓存和确认。
- `scripts/`：环境、构建、服务启动／停止、验证入口。
- `local/`：私有配置、数据库、出厂备份、测试响应，全部忽略。

## HTTP API v1

只监听 `127.0.0.1:8765`。网页同源，拒绝跨站来源和任意 Host；不配置公网访问。

| 方法与路径 | 行为 |
| --- | --- |
| GET `/api/v1/state` | 草稿、最新发布内容、未发布修改标记、连接及确认状态 |
| POST `/api/v1/tasks` | 新增；同时提供 source/source_id 时幂等更新 |
| PUT `/api/v1/tasks/{id}` | 修改原文、分类、完成状态 |
| DELETE `/api/v1/tasks/{id}` | 删除草稿中的事项，不立即改变屏幕 |
| POST `/api/v1/tasks/{id}/move` | direction 为 -1 或 1，分类内移动 |
| POST `/api/v1/bulk` | text 逐行新增到 category，空行忽略 |
| POST `/api/v1/publish` | 在事务中生成不可变快照、排队发送，返回版本 |
| GET `/api/v1/preview?mode=draft或published` | 返回相同排版引擎生成的 PNG 页面 |

任务字段：id、text、category（focus/misc/follow）、position、done、updated_at（毫秒）、source、source_id。新增最低只需要 text/category。完整 OpenAPI 文档在运行后的 `/docs`。

## USB / BLE 协议 v1

每条消息为 `@RTD1 `＋一行 JSON＋换行；忽略其他日志。最大接收行 4095 字节。每条请求有 id、op、绑定 token，响应含 id、ok 或 error。

- hello：协议版本、稳定设备标识、发布版本，可信请求携带 epoch 校时。
- bind：首次通过 USB 保存随机 32 位十六进制 token，Mac 保存到私有数据库。
- begin：size、version、sha256，限 128KiB。
- chunk：offset、base64 data，USB 每次 384、BLE 每次 2048 原始字节，顺序严格匹配并确认偏移。
- end：校验、解析、双缓存持久化、显示，再确认 version。

USB 握手后优先。BLE 使用认证加密特征值，读特征触发 macOS 配对。GATT 服务 UUID `beef1000-5e7a-4a61-9d20-e848672f0001`，RX/TX 分别为 `beef1001`/`beef1002` 同后缀。BLE 按协商 MTU 分片写入，读取匹配 id 的响应。双通道共享同一发布状态，传输失败重发整份最新快照。

二进制快照：小端 `RTD3`、uint64 版本、uint64 发布时间（秒）、uint16 页数，随后为整份页面主体的 zlib 压缩流。解压后每页包含 uint8 分类、uint16 分类内页码、uint16 分类页数、uint32 数据长度（15000），随后为平铺单色数据。最多 128 页，解压缓冲有明确上限；按像素连续打包，不能使用 300 宽图片的行补齐字节。固件兼容早期 RTD1/RTD2 缓存，欢迎页仍使用简单游程编码。

设备缓存为独立 flash 分区，数据先写，带 SHA-256 的有效头最后写；启动选择校验通过的最高版本。未完成传输不修改当前画面。设备确认意味着数据已持久化、SPI 刷新已返回；视觉效果仍需人眼验收。
