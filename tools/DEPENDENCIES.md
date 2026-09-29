# 第三方构建依赖出处

依赖下载固定为 [Waveshare ESP32-S3-Touch-LCD-1.69 提交 25fed2f](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.69/tree/25fed2f7e8411f2522448b58906774db99a14a08)，不跟随默认分支更新。脚本只导出构建所需两个库到 `vendor/libraries/`；原文件和版权头保持不变。

| 库 | 固定版本 | 原始位置 | 保留文件 |
| --- | --- | --- | --- |
| GFX Library for Arduino | 1.6.5 | `examples/arduino/libraries/GFX_Library_for_Arduino` | `license.txt`（Adafruit BSD）、README、全部源码版权与字体许可头 |
| SensorLib | 0.4.1 | `examples/arduino/libraries/SensorLib` | `LICENSE`（MIT）、`THIRD_PARTY_NOTICES.md`、Bosch 子目录 LICENSE 和版权头 |

上层 Waveshare 仓库的 Apache 2.0 `LICENSE` 复制为 `vendor/libraries/WAVESHARE-LICENSE`。它不取代库和库内组件各自的许可证。GFX 字体和 SensorLib 的 Bosch SDK 等组件保留原始声明，不能统称为本项目自有代码。

库自身出处：

- [Arduino_GFX](https://github.com/moononournation/Arduino_GFX)
- [SensorLib](https://github.com/lewisxhe/SensorLib)

ESP32 平台使用 Arduino CLI 安装 **esp32:esp32@3.3.11**，不复制到仓库；其发行包保留平台依赖各自的许可文件。PC 桥依赖为 **psutil 7.2.2** 和 **pyserial 3.5**，通过 pip 安装，不把虚拟环境加入仓库。

下载脚本使用 HTTPS 和固定 Git 提交校验来源。若重新运行发现目标依赖内容与固定提交不同，会停止保留本地修改；请在另一个 `-Libraries` 目录获取干净副本。
