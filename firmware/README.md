# 设备固件

ESP-IDF 5.5.2、LVGL 9.4.0，目标 ESP32-S3，16MB Flash / 8MB PSRAM。

通过 `../scripts/build-firmware.sh` 在英文路径镜像中构建，不自动烧录。显示驱动查表已修正为按 width 索引的一维数组并检查边界。设备解析 Mac 生成的压缩单色页面，保存双缓存后显示；收到 SPI 完成通知后才回复发布成功。

两种传输使用同一协议：USB 原生 Serial/JTAG；BLE NimBLE，认证加密 GATT、屏幕显示配对码、NVS 保存绑定。RTC 地址 0x51，以 UTC 保存时间。

分区表：factory 应用 8MiB；cache0/cache1 各 0x21000，容纳 128KiB 快照和有效头；保留 NVS 和 PHY 分区。首次更换固件前必须本地备份完整 16MiB Flash，详细步骤见开发说明。
