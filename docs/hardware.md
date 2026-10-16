# 硬件与验证记录

设备：Waveshare ESP32-S3-RLCD-4.2。资料和首次环境验证日期：2026-09-28。

## 已核对能力

- ESP32-S3；实测芯片 revision v0.2、16 MB Flash、8 MB 封装 PSRAM、40 MHz 晶振。
- 4.2 英寸反射式 LCD，300×400；项目使用竖屏，官方验证示例默认为横屏 400×300。
- ST7305，官方示例最终输出 1 bit 黑白图像，帧缓冲 15,000 字节；LVGL 颜色会转换为黑白。
- 2.4 GHz Wi-Fi、BLE，KEY 与 BOOT 可自定义，PWR 为电源键。
- 第一版使用 PCF85063 RTC 保持日期；SHTC3 温湿度、音频和 SD 卡暂不启用。
- 无背光，依靠环境光；不要将它当作电子墨水屏并假设断电保图。

## 引脚（来自官方示例）

| 功能 | GPIO |
| --- | --- |
| 屏幕 MOSI / SCLK | 12 / 11 |
| 屏幕 DC / CS / RST / TE | 5 / 40 / 41 / 6 |
| I²C SDA / SCL | 13 / 14 |
| KEY / BOOT | 18 / 0，低电平有效 |

BOOT 在启动时还承担下载模式选择，普通按键体验不得要求用户按住 BOOT 再上电。

## 竖屏适配要求

官方示例有竖屏映射函数，但当前像素查表类型固定第二维为 300，并使用 `[x][y]` 访问。改为 300×400 后仍沿用此布局会发生越界索引与映射重叠。本项目已改为按 `y * width + x` 索引的一维查表，并增加边界检查；120000 像素映射已穷举验证，真屏方向与短／长按已由用户确认。

## 实际验证与限制

- USB 通信成功；更换 USB 插口后也成功查询到同一设备，当前串口仅记录于本地。
- 原有程序启动日志标识为 `03_Fac`，使用 ESP-IDF v5.5.2，8 MB PSRAM 初始化成功。
- 官方 LVGL 9 示例编译成功，固件 1,211,952 字节，8 MB 应用分区大小检查通过。
- 已备份完整 16MiB 出厂 Flash 并烧录待办固件；最新通信、性能和缓存验证见 [验证记录](validation.md)。Wi-Fi、传感器与实际续航未测。
- 原程序有 SD 初始化超时并继续启动；若未插卡可出现此现象，尚未据此确认卡槽故障。

官方 FAQ 的出厂程序续航约 24 小时，不能作为本项目续航承诺。插拔、装电池和搬运时不要让屏幕受力。RTC 备用电池要求可充电型号，细节遵循官方资料。

## 来源

- [产品概览](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/)
- [资源与文档](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/Resources-And-Documents/)
- [ESP-IDF 示例说明](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/ESP-IDF/)
- [产品 FAQ](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/FAQ/)
- [官方源码](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/tree/eb1f63427d735a22b9c30e22fa63ebddae1834d3)

首次访问原理图和 ST7305 PDF 返回网站防护页面，未完整审阅；当前引脚依据源码核对。下载页面和原始设备日志仅在本地保留。
