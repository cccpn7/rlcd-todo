# 开发环境与验证

当前只有官方示例可编译，业务目录尚无应用实现。以下步骤不会烧录设备。

## 工具版本

- ESP-IDF：v5.5.2；原有其他版本无需删除。
- Python：推荐 3.12；本机验证使用 3.12.14 的独立 IDF 虚拟环境。
- ESP32-S3 工具链、CMake 和 Ninja：由对应 IDF 的工具安装器安装。
- LVGL：验证副本固定为 9.4.0。官方网页提到 9.3.0，但所固定的源码依赖为 `^9.4.0`，以源码为准。
- macOS 需要可用的 Command Line Tools、Git；构建目录建议采用不含中文和空格的路径。

参考 [ESP-IDF v5.5.2 macOS 安装文档](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/get-started/linux-macos-setup.html)。准备可用的 Python 3.12 后：

```bash
mkdir -p "$HOME/esp/v5.5.2"
git clone --branch v5.5.2 --recursive https://github.com/espressif/esp-idf.git "$HOME/esp/v5.5.2/esp-idf"
python3.12 "$HOME/esp/v5.5.2/esp-idf/tools/idf_tools.py" install --targets esp32s3
python3.12 "$HOME/esp/v5.5.2/esp-idf/tools/idf_tools.py" install cmake ninja
python3.12 "$HOME/esp/v5.5.2/esp-idf/tools/idf_tools.py" install-python-env
```

这些目录若已存在，请核对版本后复用，不覆盖已有工程。ccache 为可选项，本机未安装也已编译成功。无需修改全局终端启动文件。

## 获取固定版本示例

上游：[waveshareteam/ESP32-S3-RLCD-4.2](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)。固定提交：`eb1f63427d735a22b9c30e22fa63ebddae1834d3`。示例：`02_Example/ESP-IDF/09_LVGL_V9_Test`。

在新建的英文路径准备验证副本，保留下载源码中的许可文件：

```bash
mkdir -p "$HOME/esp/rlcd-todo"
git clone https://github.com/waveshareteam/ESP32-S3-RLCD-4.2.git "$HOME/esp/rlcd-todo/vendor"
git -C "$HOME/esp/rlcd-todo/vendor" checkout --detach eb1f63427d735a22b9c30e22fa63ebddae1834d3
mkdir -p "$HOME/esp/rlcd-todo/validation/lvgl9"
rsync -a --exclude=build --exclude=managed_components --exclude=sdkconfig --exclude=sdkconfig.old --exclude=.vscode --exclude=dependencies.lock "$HOME/esp/rlcd-todo/vendor/02_Example/ESP-IDF/09_LVGL_V9_Test/" "$HOME/esp/rlcd-todo/validation/lvgl9/"
```

编辑验证副本的 `main/idf_component.yml`，将 `lvgl/lvgl: ^9.4.0` 改为 `lvgl/lvgl: "9.4.0"`。不修改下载的官方原件，不复用其他机器的构建缓存。构建会在副本中生成依赖锁文件。

## 配置本项目入口

在项目根目录：

```bash
cp .env.example .env.local
```

按本机实际目录调整 `.env.local`：

| 变量 | 用途 |
| --- | --- |
| `IDF_PATH` | v5.5.2 的 ESP-IDF 根目录，包含 `export.sh` |
| `IDF_TOOLS_PATH` | IDF 工具根目录 |
| `IDF_PYTHON_ENV_PATH` | 独立 Python 虚拟环境，包含 `bin/python` |
| `RLCD_EXAMPLE_DIR` | 上面准备的验证副本，包含 `CMakeLists.txt` |
| `ESPPORT` | 可选串口；重新插拔后应重新发现，不依赖旧记录 |

`.env.local` 是由 Bash 加载的可信本地配置，会覆盖同名环境变量；不应从陌生来源复制可执行内容。如果不创建此文件，也可提前导出这些环境变量。

```bash
./scripts/idf.sh --version
./scripts/build-example.sh
```

`idf.sh` 只在当前进程加载环境，`build-example.sh` 只编译。输出位于验证副本的 `build/`，包括 `09_LVGL_V9_Test.bin`、引导程序和分区表。不能将应用 bin 误当完整 Flash 镜像。

初次验证完整构建通过，应用固件 1,211,952 字节，适配 8 MB 应用分区。后续工具或依赖改变可能改变体积，应重新检查，不把此数字当作固定验收哈希。

## 串口与真机边界

可在已安装 pyserial 的 Python 环境用 `python -m serial.tools.list_ports -v` 发现当前串口。端口名和设备唯一标识只保存在本机记录。

当前不执行烧录。后续在已授权的真机开发阶段，先保存现有固件／配置，再使用验证工程的 IDF 烧录入口；关闭占用串口的监视器。官方故障恢复方式是按住 BOOT 重新上电进入下载模式。不要用屏幕承受插拔压力。

本机初始 Python 解释器来自桌面运行时，若它被清理或移动，需以可用 Python 3.12 重建 IDF 虚拟环境。公开脚本不依赖该运行时的固定路径。

## 公开内容与本地内容

本机配置、数据库、设备日志、原始 API 响应和旧设备记录留在 `.env.local`、`local/` 或 `logs/`；官方源码与下载网页也不提交。需要私人记录目录时运行 `mkdir -p local`。项目规则见 [AGENTS.md](../AGENTS.md)。
