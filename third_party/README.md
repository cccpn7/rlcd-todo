# 第三方来源与修改

- `firmware/components/display/display_bsp.*`：Waveshare ESP32-S3-RLCD-4.2，commit `eb1f63427d735a22b9c30e22fa63ebddae1834d3`，来源 `02_Example/ESP-IDF/09_LVGL_V9_Test/components/port_bsp/`。Copyright 2026 Waveshare，Apache-2.0，完整许可见 Waveshare-LICENSE.txt。修改：二维固定宽度查表改为一维索引、增加坐标边界检查、SPI 完成后再返回。
- `server/assets/SourceHanSansCN-Regular.otf`：Adobe Source Han Sans 2.005R 的 CN 子集，下载地址 `https://raw.githubusercontent.com/adobe-fonts/source-han-sans/2.005R/SubsetOTF/CN/SourceHanSansCN-Regular.otf`。字体未修改，SIL OFL 1.1，完整许可见 SourceHanSans-LICENSE.txt。固件欢迎页是该字体渲染的位图。
- LVGL 9.4.0 由 ESP-IDF 组件管理器获取，MIT 许可随依赖保留。ESP-IDF 使用官方 v5.5.2 工具链；不将整份依赖仓库提交。
- BLE 实现参照 ESP-IDF 5.5.2 自带 NimBLE API 和示例设计，未复制其整份示例源文件。
