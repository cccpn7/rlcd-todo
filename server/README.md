# Mac 服务

Python 3.12。运行入口 `../scripts/setup-server.sh` 和 `../scripts/service.sh start`，本机地址 http://127.0.0.1:8765。

SQLite 位于 `local/tasks.sqlite3`，包含原文、完成记录、最近两次发布及设备绑定 token，禁止提交。`RLCD_DB` 可用于隔离测试数据库。渲染引擎使用随仓库分发的思源黑体；发布前校验字符，设备与网页预览复用同一页面位图。

API 见 docs/architecture.md，运行后的 `/docs` 提供交互文档。设备状态由通信确认驱动，发布 HTTP 请求成功不意味着设备已经更新。
