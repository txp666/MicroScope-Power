# Pico Power Scope

RP2040 / Raspberry Pi Pico 电流波形采集固件和 WebUSB 上位机。固件通过 ADS8866 读取 INA296A3 电流采样链路，网页端显示 uA/mA 波形、统计、游标和校准参数。

## 目录

- `firmware/`: Pico SDK 固件工程。
- `web/`: WebUSB 上位机，纯 HTML/CSS/JavaScript。
- `docs/`: 接线、协议、使用和排障文档。
- `scripts/`: Windows 环境检查、Pico SDK 拉取和固件构建脚本。
- `third_party/`: 本地 SDK/示例代码目录，`pico-examples` 只作为示例参考。

## 当前功能

- ADS8866 真实采样，支持 `10k / 20k / 50k / 100kSPS`。
- WebUSB 二进制帧上传，每帧 256 个 16-bit 原始 code。
- 网页端按采样电阻、零点和 INA 增益换算为电流。
- 左轴显示 `uA`，右轴同步显示 `mA`。
- 支持自动量程和手动量程：
  - 大量程：默认 `1Ω`。
  - 小量程：默认 `10Ω`。
- 两个量程独立零点、独立采样电阻。
- 零点、两个电阻值、DAC 输出电压断电保存到 Pico flash。
- OLED 本地显示输出电压和空闲电流；上位机采集运行时本地不抢 ADS8866。
- DAC/LDO 输出通过本地旋钮调节，输出电压带滤波显示。
- 上位机可让 Pico 进入 BOOTSEL/U盘模式，免按 BOOTSEL 按键升级 UF2。
- 网页端支持清屏、归零、暂停显示、自由缩放和平移、CSV/PNG 导出。

## 硬件默认引脚

详细接线见 [docs/wiring.md](docs/wiring.md)。

| 功能 | Pico GPIO |
| --- | --- |
| ADS8866 MISO | GP16 |
| ADS8866 CONVST/CS | GP17 |
| ADS8866 SCLK | GP18 |
| ADS8866 DIN | GP19 |
| 量程 SEL | GP20 |
| I2C SDA | GP8 |
| I2C SCL | GP9 |
| LDO EN | GP0 |
| LDO PG | GP1 |
| 编码器 A/B/KEY | GP6 / GP4 / GP5 |
| 蜂鸣器 | GP13 |
| 输出电压 ADC | GP26 / ADC0 |

## 构建固件

Windows 下建议直接使用脚本：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-env.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\bootstrap-pico-sdk.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-firmware.ps1
```

构建产物：

```text
build/firmware-ninja/pico_power_scope.uf2
```

按住 Pico `BOOTSEL` 插入电脑，把 UF2 拷贝到弹出的 Pico 盘符即可烧录。

已烧录过本固件后，也可以在网页上位机点击“固件升级”，让 Pico 自动重启到 BOOTSEL/U盘模式，再拷贝新的 UF2。

## CI 和 Release

GitHub Actions 会在 push、PR 和手动触发时编译固件，并上传构建 artifact。

打 `v*` tag 会自动创建 GitHub Release，并把固件产物作为 release assets 上传：

```powershell
git tag v0.1.0
git push origin v0.1.0
```

Release assets 包含：

- `pico_power_scope-<version>.uf2`
- `pico_power_scope-<version>.elf`
- `pico_power_scope-<version>.bin`
- `pico_power_scope-<version>.hex`
- `pico_power_scope-<version>.map`
- `pico_power_scope-<version>.txt`
- `SHA256SUMS.txt`

固件版本来自 tag，例如 `v0.1.0` 会注入 `fw=0.1.0`；普通分支构建会使用 `VERSION+commit`。

## 启动上位机

WebUSB 需要安全上下文，使用本地 HTTP 服务打开：

```powershell
python -m http.server 8000 --bind 127.0.0.1 --directory web
```

浏览器打开：

```text
http://127.0.0.1:8000/
```

使用 Chrome 或 Edge。连接设备后，先确认量程电阻值和零点，再开始采集。

## 常用流程

1. 烧录固件，打开网页上位机。
2. 点击“连接设备”。
3. 在“量程与校准”里确认大量程/小量程的采样电阻。
4. 空载或短接输入到零点状态，点击“归零”。
5. 选择采样率，点击“开始采集”。
6. 使用鼠标滚轮或按钮缩放，拖动曲线平移。
7. 需要重新测量时点击“清屏”。

详细操作见 [docs/operation.md](docs/operation.md)。

## 协议

WebUSB 命令和二进制帧格式见 [docs/protocol.md](docs/protocol.md)。
