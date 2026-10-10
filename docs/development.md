# 开发与运行

## Mac 服务

安装 Python 3.12，然后在项目根目录执行：

```bash
./scripts/setup-server.sh
./scripts/service.sh start
./scripts/service.sh status
./scripts/service.sh stop
```

可用 `RLCD_PYTHON` 指定 Python 3.12。服务后台运行，只监听 `127.0.0.1:8765`；关闭浏览器不会退出。依赖位于 `local/venv`，数据库与绑定凭据在 `local/tasks.sqlite3`，运行日志在 `local/service.log`，不要提交这些文件。备份该数据库时先停止服务。

第一次接入需要 USB 数据线，服务自动发现 Espressif 串口，通过协议确认硬件身份后绑定；不依赖端口名。USB 优先于蓝牙，只供电的连接不算握手成功。Mac 蓝牙须开启；首次使用时允许宿主应用的蓝牙权限，并在系统配对框填写设备屏幕上的六位码。macOS 没有 Bleak 显式 pair 方法，通过访问认证特征值触发系统配对。

如果 Mac 丢失本地数据库而设备仍绑定，不会自动覆盖旧绑定；应恢复数据库备份。配对失败先检查系统设置中的权限与蓝牙状态，不在固件中关闭认证来规避。

## ESP-IDF

固定 ESP-IDF v5.5.2、LVGL 9.4.0、目标 ESP32-S3。推荐 Python 3.12 和 macOS Command Line Tools。参考 [官方安装文档](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/get-started/linux-macos-setup.html)：

```bash
mkdir -p "$HOME/esp/v5.5.2"
git clone --branch v5.5.2 --recursive https://github.com/espressif/esp-idf.git "$HOME/esp/v5.5.2/esp-idf"
python3.12 "$HOME/esp/v5.5.2/esp-idf/tools/idf_tools.py" install --targets esp32s3
python3.12 "$HOME/esp/v5.5.2/esp-idf/tools/idf_tools.py" install cmake ninja
python3.12 "$HOME/esp/v5.5.2/esp-idf/tools/idf_tools.py" install-python-env
cp .env.example .env.local
```

配置 `IDF_PATH`、`IDF_TOOLS_PATH`、`IDF_PYTHON_ENV_PATH`，不要覆盖已有工程。`.env.local` 是会被 Bash 执行的可信本机配置。公开脚本不包含私人路径。

```bash
./scripts/idf.sh --version
./scripts/build-firmware.sh
```

构建默认镜像到 `$HOME/esp/rlcd-todo/application`，可用 `RLCD_BUILD_SOURCE` 指定专用英文路径。该目录专用于构建，会同步删除失去对应源文件的文件，不应存放手写源代码。产物在其 `build/`，组件版本由依赖清单固定，锁文件随固件源码保存。

## 备份、烧录、恢复

固件修改按“实现与测试 → 烧录 → 真机验证与用户确认 → Git 提交及推送”的顺序交付。烧录依照当次已授权范围执行，未获真屏确认前不要提前提交固件改动。

停止服务，发现当前串口后设置本次使用的端口（不要把真实设备标识写进公开文档）。首次烧录前，使用 IDF Python 环境中的 esptool 读取完整 16MiB，并保存校验和：

```bash
./scripts/service.sh stop
mkdir -p local/backups
python -m esptool --chip esp32s3 --port "$ESPPORT" read_flash 0 0x1000000 local/backups/factory-full.bin
shasum -a 256 local/backups/factory-full.bin > local/backups/factory-full.sha256
./scripts/idf.sh -C "$HOME/esp/rlcd-todo/application" -p "$ESPPORT" flash
./scripts/service.sh start
```

这里的 `python` 须为已安装 esptool 的 IDF 虚拟环境解释器。烧录是显式操作，构建脚本不会顺带烧录。不要只烧应用而遗漏首次安装所需的引导程序和分区表。若烧录中断，停止串口使用者，重新烧录并校验。

需要恢复出厂备份时，在服务停止、备份校验正确且明确要恢复的情况下，用 esptool 将完整镜像写回地址 0。它会覆盖当前应用、绑定与设备缓存。不要把单独的应用 bin 当完整 Flash 镜像。

## 检查

```bash
local/venv/bin/pip install -r tests/requirements.txt
local/venv/bin/python -m pytest -q
./scripts/test-core.sh
./scripts/service.sh stop
local/venv/bin/python scripts/check-device.py usb --benchmark
local/venv/bin/python scripts/check-device.py ble --benchmark
./scripts/service.sh start
```

设备检查会验证传输失败不覆盖旧版本；benchmark 临时显示 100 条通用测试事项，结束后重新发布最近已发布内容。不会把未发布草稿顺带发布。BLE 测试前需停掉 USB 心跳至少 6 秒，即使 USB 仍供电，也不会被设备的 USB 优先机制拒绝。

硬件真屏验收须由人完成，35 秒性能目标从生成页面到设备确认计时，不用单纯串口写入耗时代替。

## 模拟外部助手

```bash
local/venv/bin/python scripts/demo-push.py '回收演示反馈' --source-id example-1
local/venv/bin/python scripts/demo-push.py '回收演示反馈并确认结果' --source-id example-1 --publish
```

相同来源编号更新原事项，不重复创建；没有 `--publish` 只保存草稿。真实助手未来使用同一 API，不需要直接连接设备。

## 官方示例

上游 [Waveshare 官方仓库](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)，固定提交 `eb1f63427d735a22b9c30e22fa63ebddae1834d3`，示例 `02_Example/ESP-IDF/09_LVGL_V9_Test`。将副本放在英文路径，固定 `main/idf_component.yml` 的 LVGL 为 9.4.0，配置 `RLCD_EXAMPLE_DIR` 后仍可运行 `./scripts/build-example.sh`。官方下载目录与缓存不提交。
