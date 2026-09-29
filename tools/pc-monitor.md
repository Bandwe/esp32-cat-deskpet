# Windows 电脑状态监控

这个工具把本机 CPU/GPU 状态通过 USB 发给桌宠，每秒更新一次。需要 Windows、Python 3.10 或更新版本，以及支持 `pcmon` 的桌宠固件。脚本不会刷写固件，也不会安装系统服务或开机启动项。

以下 PowerShell 命令均在仓库根目录运行。路径随仓库移动，无需使用原作者的磁盘目录。

## 安装依赖

```powershell
py -3 -m venv .venv-pcmon
.\.venv-pcmon\Scripts\python.exe -m pip install -r tools\pc_monitor_requirements.txt
```

直接调用虚拟环境的 Python，不需要先修改 PowerShell 执行策略或激活环境。

## 先查看本机数据

```powershell
.\.venv-pcmon\Scripts\python.exe tools\pc_monitor_bridge.py --dry-run --samples 3
```

`--dry-run` 只读取本机传感器，不枚举或打开串口，不需要 USB 序列号。读取不到的指标用 `-1` 表示，桌宠会显示不可用，而不是误报为零。GPU 数据使用可选的 `nvidia-smi`；CPU 温度使用可选的 LibreHardwareMonitor WMI 传感器，未安装时仍能运行其他指标。

## 找到自己的设备

连接桌宠后运行：

```powershell
.\.venv-pcmon\Scripts\python.exe -m serial.tools.list_ports -v
```

该命令只列出串口元数据，不打开串口。找到对应开发板的条目，核对 USB VID/PID 为 `303A:1001`，并记下完整 USB 序列号。串口号可能在重新插拔后改变。不要仅凭设备显示名称选择，也不需要把枚举输出提交到仓库。

## 手动启动

将下面的 `YOUR_USB_SERIAL` 替换为自己的 USB 序列号：

```powershell
.\.venv-pcmon\Scripts\python.exe tools\pc_monitor_bridge.py --usb-serial "YOUR_USB_SERIAL"
```

脚本按 VID、PID 和指定序列号一起寻找设备。若需要固定端口，可以附加 `--port COM4`，但固定端口不会跳过设备身份校验。按 `Ctrl+C` 停止；也可以加 `--samples 10`，发送十次后自动退出。

也可以只在当前 PowerShell 会话中设置环境变量：

```powershell
$env:DESKPET_USB_SERIAL = "YOUR_USB_SERIAL"
.\.venv-pcmon\Scripts\python.exe tools\pc_monitor_bridge.py
Remove-Item Env:DESKPET_USB_SERIAL
```

显式传入的 `--usb-serial` 优先于环境变量。正常串口模式未提供序列号时会直接报错，不读取传感器、不枚举设备，也不打开串口。仓库没有内置任何人的真实 USB 序列号。

## 连接与兼容性

选中设备后，脚本先发送 `status` 查询，只有固件版本匹配源码中的允许列表，才发送电脑型号与监控数据。目前允许项目 R16 到 R22 的对应版本字符串；其他版本不会收到监控数据。设备拔下后会等待重连，再次核对身份与固件版本。

同一设备身份对应多个端口时，脚本不会自行猜测；可在核实后指定 `--port`。此工具仅向匹配设备发送本机状态，不进行网络请求。

## 无硬件单元测试

```powershell
.\.venv-pcmon\Scripts\python.exe -B -m unittest discover -s tools\tests -p "test_*.py" -v
```

测试使用虚构设备序列号，并模拟串口枚举、连接和传感器采集，不需要接开发板。
