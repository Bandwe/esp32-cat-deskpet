# 构建与离线测试

所有路径默认相对于当前仓库定位。构建、依赖获取与主机回归不会打开串口或刷写开发板。

## 固定依赖

需要 PowerShell、Git 和 Arduino CLI。先从仓库根目录运行：

```powershell
./tools/fetch_dependencies.ps1
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.11 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
./tools/build_pet.ps1
```

依赖脚本从 Waveshare 固定提交 `25fed2f7e8411f2522448b58906774db99a14a08` 稀疏检出两个库，复制到 `vendor/libraries/`。缓存位于 `.cache/dependencies/`，两个目录均不纳入版本控制。再次运行会逐文件验证现有副本，遇到本地改动会停止，不覆盖改动。

需要的库是 GFX Library for Arduino 1.6.5 和 SensorLib 0.4.1；不需要 LVGL 或 Arduino_DriveBus。出处及需要保留的许可文件见 [DEPENDENCIES.md](DEPENDENCIES.md)。

构建脚本强制检查已安装的 Arduino-ESP32 为 **3.3.11**。CLI 默认从 PATH 查找，可显式指定已安装的工具和配置：

```powershell
./tools/build_pet.ps1 -ArduinoCli 'D:/tools/arduino-cli.exe' -ConfigFile 'D:/tools/arduino-cli.yaml' -Libraries './vendor/libraries' -Jobs 6
```

`-ConfigFile` 可省略，`-Libraries` 默认指向仓库的 `vendor/libraries`；也可向依赖获取脚本传入同名参数。固定 FQBN 为：

```text
esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=default,PartitionScheme=app3M_fat9M_16MB
```

构建临时文件在 `build/firmware-work/`，输出在 `firmware/`。这一步只编译，烧录必须单独由使用者执行。已生成的贤者帧与菜单字形包含在 sketch 中，普通构建不需要原始图片或字体文件。

## 主机回归

需要 Python 3.10 或更新版本以及支持 C++17 的编译器。先安装 PC 监控脚本依赖（推荐在项目虚拟环境内）：

```powershell
python -m venv .venv
./.venv/Scripts/python.exe -m pip install -r tools/pc_monitor_requirements.txt
./.venv/Scripts/python.exe tools/run_host_tests.py --vcvars 'D:/BuildTools/VC/Auxiliary/Build/vcvars64.bat'
```

MSVC 已在开发者命令行中可用时可省略 `--vcvars`。Linux/macOS 或已有 MinGW 的环境可使用：

```sh
python tools/run_host_tests.py --compiler g++
```

默认运行 **11 个 C++ 测试程序**，覆盖配对输入、留言协议、PC 数据协议、R8/R15 触摸规则、猫触摸模型、WLAN 存储、九宫格输入、WLAN 页面、卡片传输及 R22 猫绘制回归；然后运行 `test_pc_monitor_bridge.py` 的 Python 单测。编译结果写入 `build/host-tests/`。测试使用桩和模拟数据，不连接实际板卡。

以下两项明确不属于默认通过范围：

- `--sage-source /path/to/sage_v03/device`：额外运行贤者 804 帧与原始素材逐字节等价测试。原始素材不包含在此公开副本中；固件自带打包后的素材。
- `--legacy-shake`：额外运行历史 R7 测试。其 30 个传感器合成场景仍通过，但最后一项源码字符串检查要求单角色调用，早于 R22 的多角色分流，因此在当前源码上会失败。这是保留的历史检查，不表示已通过全部 `tools/tests/`。

离线回归不证明触摸手感、屏幕观感、真实 Wi-Fi 或实板装配已经验收。当前验证记录使用 Windows MSVC；g++ 路径需由相应平台自行验证。

## PC 性能桥

```powershell
python tools/pc_monitor_bridge.py --help
```

`pc_monitor_requirements.txt` 固定 `psutil==7.2.2` 与 `pyserial==3.5`。离线测试会模拟串口与传感器；正常启动性能桥属于实际连接开发板的独立操作。
