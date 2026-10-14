# MicroScope Power 软件功能与公开参数核对

核对日期：2026-10-14。本文用于开源使用说明及宣传参数取证，依据已发布源码与公开入口；源码支持的配置不等于经整机测量确认的性能指标。

## 公开版本和入口

| 项目 | 版本及入口 | 对应提交 |
| --- | --- | --- |
| 固件 | [MicroScope-Power v0.2.1](https://github.com/txp666/MicroScope-Power/releases/tag/v0.2.1) | `c3d4eeb74bdbbef8342aa0d417f5076fffc27246` |
| 网页上位机 | [ModuSignal v1.0.4](https://github.com/txp666/modusignal/releases/tag/v1.0.4) | `43f9be998b88205581ccb93ade89c3eee216fd45` |
| 在线使用 | [MicroScope Power 设备页](https://modusignal.cn/pages/devices/microscope-power/) | 核对时资源版本 `43f9be9`，与上位机发布提交一致 |
| 固件下载 | [pico_power_scope-0.2.1.uf2](https://github.com/txp666/MicroScope-Power/releases/download/v0.2.1/pico_power_scope-0.2.1.uf2) | RP2040 UF2，112,640 字节 |
| 上位机离线包 | [modusignal-offline-v1.0.4.zip](https://github.com/txp666/modusignal/releases/download/v1.0.4/modusignal-offline-v1.0.4.zip) | 解压后通过本地 HTTP 服务使用 |

固件维护在 [MicroScope-Power](https://github.com/txp666/MicroScope-Power)，网页源码及测试维护在 [ModuSignal](https://github.com/txp666/modusignal)。固件自有代码、文档及图片采用根 [MIT 许可证](../../LICENSE)，第三方组件保留各自许可证，见 [第三方通知](../../THIRD-PARTY-NOTICES.txt)。不能据此把整个 ModuSignal 仓库也标为 MIT。

本次独立核对公开固件标签 CI `38525871755`、上位机标签 CI `38525892145` 均成功；固件 9 个公开资产下载和 SHA256 验证通过。软件回归和构建不替代整机精度、噪声、纹波、动态响应及长期稳定性测试。

## 可用于海报的确证参数

| 可用表述 | 源码证据和解释 |
| --- | --- |
| RP2040 主控，双核采集与 USB 服务 | Pico SDK 工程；`sampler.c` 将采集放在 core 1，主循环处理 USB。 |
| ADS8866，16-bit ADC | 固件以 16-bit SPI 读取 ADS8866，上传 `uint16_t` ADC 数据；16-bit 是转换器/数据位宽，不是整机有效位数或精度保证。 |
| 可配置 10 / 20 / 50 / 100 kSPS | [protocol.h](../../firmware/src/protocol.h) 定义四档，默认 100 kSPS；应写“最高配置 100 kSPS”，不能写“已验证连续 100 kSPS”。 |
| INA296A3 电流前端，默认增益 50 V/V | [接线说明](../wiring.md) 与固件、网页的 `FRONTEND_GAIN=50` 一致；增益准确度需器件、电阻及整机校准共同确认。 |
| 双量程，默认换算电阻 1 Ω / 10 Ω | 固件及网页均保存独立电阻和零点；GP20 低选 1 Ω 大量程，高选 10 Ω 小量程。实际采样电阻、负担电压及其拓扑应以硬件资料为准。 |
| 128 × 64 OLED，本地旋钮调压 | [oled_ssd1306.h](../../firmware/src/oled_ssd1306.h) 和 `local_ui.c`；OLED 显示输出端 ADC 电压及空闲时电流。采集运行期间本地界面更新暂停。 |
| WebUSB 浏览器上位机 | VID `0xCAFE`、PID `0x4012`，TinyUSB Vendor Class；使用 Chrome / Edge，从 HTTPS 或 localhost 打开。 |
| µA / mA 同步双轴 | 左轴、读数、游标和 CSV 使用 µA，右轴以 mA 显示同一范围；单位显示不证明 µA 级精度。 |
| 波形框选放大、平移与 A/B 游标 | 左键拖框同时放大时间轴和电流轴，右键拖动平移，双击恢复最新波形和自动 Y 轴；启用游标后单击设置 A/B。 |
| 可选频率的一阶数字低通 | 默认关闭；预设 10 / 30 / 100 / 300 Hz、1 / 3 / 10 / 20 kHz，自定义 1–45000 Hz，实际截止频率不超过时间轴估算采样率的 45%。 |
| 原始 / 低通 CSV、PNG 导出 | 导出当前可见窗口；滤波前数据保留，可单独导出原始 CSV，低通 CSV 另含原始电流及有效截止频率。这里的“原始 CSV”仍是经校准换算的电流，不是原始 ADC code 文件。 |
| 独立校准和断电保存 | 两档零点、两档电阻、DAC 设定值保存到 Flash；运行采集时延后写入，停止后保存。 |

可直接采用的简短介绍：

> MicroScope Power 便携式可调电源与电流波形分析仪。RP2040 + ADS8866 16-bit ADC，最高可配置 100 kSPS；支持默认 1 Ω / 10 Ω 双量程、OLED 本地显示、WebUSB 波形查看、可选截止频率的一阶低通及原始 CSV 导出，适用于观察单片机休眠与唤醒过程。

目前没有整机证据支持“µA 级精度”“nA 级测量”“保证捕获任意微秒脉冲”“50 kHz 已验证测量带宽”或带数字的低噪声电源纹波指标。代码换算步长、显示小数位数、ADC 位宽和空载归零后的平均值均不能替代这些实测指标。

## 实际采样时间与带宽边界

固件 [sampler.c](../../firmware/src/sampler.c) 215–248 行按目标采样率进行软件调度，每点先启动转换、等待 `9 µs`，再读取 SPI 并完成统计和帧管理。SPI 配置为 `24 MHz`、16-bit，理想 16 位传输时间约 `0.667 µs`，还存在片选等待、函数调用、计时及调度开销。因此把目标设为 100 kSPS，并不能证明实际 CONVST 一定每 `10 µs` 出现一次。

每个 530 字节波形帧包含 256 个 ADC 样本、帧序号、首样本时间 `t0_us`、标称采样率和标志。当前协议没有逐样本硬件时间戳。网页 [timeline.js](https://github.com/txp666/modusignal/blob/v1.0.4/pages/devices/microscope-power/timeline.js) 对连续帧使用相邻帧首时间差除以 256 估计帧内间隔，保留 32-bit 计时回绕和真实缺口。这能避免把帧间平均调度延迟画成每帧末尾空白，不能证明每个样本均匀采样，也不能还原帧内每次调度抖动。

| 标志 / 状态字段 | 意义 |
| --- | --- |
| `STREAM_START` | 新采集段开始，网页重置该段缓存和时间轴。 |
| `GAP_BEFORE` | 有效数据之间存在缺口，如换档稳定期或 SPI 失败后的帧丢弃；曲线保留断点，滤波不跨缺口连接。 |
| `TIMING_OVERRUN` / `timing_overruns` | 软件调度错过目标时间；成功读取的样本仍保留，单独报告时序超限。不能把该字段等同于 ADC 读取失败，也不能据此忽略实际采样率降低。 |
| `OVERFLOW` / `dropped` / 帧序号跳变 | 帧队列溢出或主机未保留相应帧；需检查 USB 接收、负载和采样率。 |
| `adc_errors` | SPI 读取失败计数；失败时不以旧 ADC 值伪造新样本。它不代表模拟测量误差或噪声大小。 |

自动换档会丢弃 6 帧稳定数据，即 1536 个转换。在完全达到标称 100 kSPS 的假设下约为 `15.36 ms`，实际持续时间可能更长。捕获唤醒边沿时宜先固定合适量程，避免自动换档窗口覆盖关键事件。

只有在实际均匀 100 kSPS 的前提下，10 µs 才是点间隔、50 kHz 才是数学 Nyquist 上限；二者都不是本机已验证的脉冲分辨能力或模拟测量带宽。短脉冲可能因未采到、前端响应、饱和或混叠而丢失。用逻辑分析仪/示波器检查 GP17 CONVST 周期及抖动，再以有效帧数和设备时间戳交叉核验实际吞吐。

## 低通与统计如何解释

上位机 [lowpass.js](https://github.com/txp666/modusignal/blob/v1.0.4/pages/devices/microscope-power/lowpass.js) 使用一阶递推 `y[n] = y[n-1] + α · (x[n] - y[n-1])`，系数使数字响应在指定截止频率处约为 −3 dB。它对每个保留样本处理后再绘图，依据帧时间轴估算的间隔计算系数；曲线、游标、统计和低通 CSV 共用结果。换档、采集段或数据缺口会重置滤波状态。缩放和平移不会改变实际截止频率。

低通降低显示及处理结果的带宽，同时降低短脉冲峰值并延迟边沿。在截止频率远低于采样率时，一阶模型时间常数约为 `1/(2πfc)`：100 Hz 约 1.59 ms，1 kHz 约 0.159 ms。这是模型估计，不是整机动态响应测试结果。数字低通不能恢复采样前已经混叠、饱和或缺失的信息。

“RMS 噪声”是可见窗口去除均值后的 AC RMS；开启滤波后对应低通数据，并保留原始 RMS 对照。窗口若同时包含休眠、唤醒和活动阶段，RMS 会包含真实负载变化，不能直接作为仪器底噪。原始 CSV 列为 `index,time_s,range_ohm,current_uA`；低通 CSV 增加 `raw_current_uA,lowpass_cutoff_hz`。时间列是该导出窗口相对时间，需结合缺口及量程解释。归零始终使用原始 ADC 数据，不以低通结果改写校准。

## 单片机休眠 / 唤醒测量流程

1. 下载 v0.2.1 UF2 并烧录。先不连接被测负载，核对实际输出电压和硬件连接。用本地旋钮设置 DAC，观察 OLED，并用外部电压表核验 DUT 所需的电源电压。
2. 用 Chrome / Edge 打开在线设备页或离线包的 localhost 页面，连接设备，在“状态”确认固件版本及 ADC/时序诊断。
3. 按实际焊接值填写两个采样电阻。输入保持零电流状态，分别固定大量程、小量程采集并归零；归零用于补偿偏移，不能当作增益或精度校准。
4. 捕获唤醒全过程时先固定 1 Ω 大量程，关闭低通，保留原始数据并打开包络；确认活动峰值没有顶到 ADC 上轨，再选择目标采样率及足够时间窗口。采集开始前完成输出调压；运行采集时本地旋钮更新暂停。
5. 接入 DUT，触发其休眠/唤醒流程。被测电流应经设计的采样路径进入；注意调试 USB、外部电源及信号线可能给 DUT 另行供电，影响休眠电流解释。
6. 暂停显示后左键框选唤醒区域，右键平移查看，开启 A/B 游标比较时间和电流；暂停显示仍持续采集，环形缓存最终可能覆盖旧数据。需要冻结采集和保存时点击“停止”，导出当前窗口原始 CSV。
7. 专门测休眠稳态时，可在确认不会饱和的条件下固定 10 Ω 小量程，选取纯休眠窗口，比较原始 RMS 和 100 Hz / 1 kHz 等低通读数。保留低通条件及原始 CSV，不能将滤波后 RMS 写成高速原始采样底噪。
8. 若出现丢帧、ADC 错误或时序超限，先降低采样率并检查实际 CONVST、USB 和模拟链路；没有实际带宽验证前，不承诺任意微秒脉冲的峰值和持续时间测量。

## DAC 如何调 LDO，是否使用 PID

当前固件通过 [power_output.c](../../firmware/src/power_output.c) 在 I2C `0x48..0x4B` 寻找 DAC60501，将设定毫伏换成 12-bit DAC 数据、左对齐写入寄存器。DAC 设定范围为 `0..2500 mV`。GP0 控制 LDO EN，GP1 读取 PG；DAC 改变硬件 LDO 调节网络的设定，最终输出电压与 DAC 电压的对应关系由硬件反馈/注入网络决定。

**固件没有 PI/PID 电压闭环。** [local_ui.c](../../firmware/src/local_ui.c) 517–520 行将旋钮增量直接加到 DAC 设定；[adc_voltage.c](../../firmware/src/adc_voltage.c) 读取 GP26 上的 100 kΩ / 100 kΩ 分压，经平均和本地 IIR 后仅用于 OLED 显示，没有“目标输出电压减实际电压”到 DAC 的反馈运算。LDO 自身的模拟稳压环路与 MCU PID 是两件事。

`0.01 / 0.1 / 1 V` 是当前 DAC 设定步进，不能据此宣称输出端具有相同步进、1 mV 精度或恒流功能。输出电压范围、负载能力、压差、纹波、温升和动态响应需硬件资料与实测支持。当前 WebUSB 命令没有输出电压设定接口，网页不实现远程电压 PID 控制。运行采集时 `main.c` 暂停 `local_ui_update`，因此应先调压再采集，或停止采集后调节。

## 固件默认引脚

下表按 [board.h](../../firmware/src/board.h) 及 [接线说明](../wiring.md) 核对，编号是 RP2040 GPIO，不是芯片封装脚号。

| 功能 | GPIO / 接口 |
| --- | --- |
| ADS8866 SDO / MISO | GP16 / SPI0 RX |
| ADS8866 CONVST / CS | GP17；高电平启动转换，低电平读出 |
| ADS8866 SCLK | GP18 / SPI0 SCK |
| ADS8866 DIN | GP19；固件作为 GPIO 保持高 |
| 前端量程 SEL | GP20；高选 10 Ω，低选 1 Ω |
| OLED、DAC SDA / SCL | GP8 / GP9，I2C0，400 kHz |
| LDO EN / PG | GP0 / GP1 |
| 编码器 A / B / KEY | GP6 / GP4 / GP5 |
| 蜂鸣器驱动 | GP13 / PWM |
| 输出电压检测 | GP26 / ADC0，默认 100 kΩ / 100 kΩ 分压 |

软件默认 ADS8866 基准为 2.5 V，OLED I2C 地址为 `0x3C`。这些是当前工程配置；改硬件后应同步核对引脚、基准和校准参数。

## 烧录与源码构建

**直接使用发布固件：** 下载上述 v0.2.1 UF2，按住 Pico BOOTSEL 接入 USB，复制到枚举出的 Pico 盘符，等待自动重启。已有本固件时也可点击网页“固件升级”，或按住本地滚轮按键上电进入 ROM BOOTSEL。该流程是 USB UF2 升级，不是网络 OTA。

**macOS / Linux 源码构建：** 预先安装 CMake、Ninja、Arm GNU `arm-none-eabi` 工具链，使编译器 `bin` 目录在 PATH 中；需准备 Python 3 和 Pico SDK 使用的宿主 picotool。以下为工程一致的构建模板，不代表所有机器都已有所需依赖：

```sh
git clone --recurse-submodules --branch v0.2.1 https://github.com/txp666/MicroScope-Power.git
cd MicroScope-Power
cmake -S firmware -B build/firmware-release -G Ninja \
  -DPICO_SDK_PATH="$(pwd)/third_party/pico-sdk" \
  -DPICO_BOARD=pico -DAPP_VERSION=0.2.1 -DCMAKE_BUILD_TYPE=Release
cmake --build build/firmware-release --parallel
```

产物为 `build/firmware-release/pico_power_scope.uf2`。需显式指定工具链时加 `-DPICO_TOOLCHAIN_PATH=/你的ArmGNU/bin`；离线构建应预先准备依赖和 picotool，而不要假定 SDK 下载步骤一定可用。

**Windows 源码构建：** 安装 CMake/Ninja/Arm GNU 工具链后，按根 README 使用 `scripts/check-env.ps1`、`scripts/bootstrap-pico-sdk.ps1` 和 `scripts/build-firmware.ps1`；默认 Ninja 产物在 `build/firmware-ninja/`。

**上位机直接离线使用：** 解压 v1.0.4 离线包，安装 Python 3 后运行 `start-modusignal.bat`（Windows）或 `start-modusignal.sh`（macOS/Linux）。手动启动可在解压目录执行 `python3 -m http.server 4173`，用 Chrome / Edge 打开 `http://localhost:4173/pages/devices/microscope-power/`。不要用 `file://` 直接打开设备页。

**上位机源码构建：** Node.js 22 和 npm，独立克隆 ModuSignal，执行：

```sh
git clone --branch v1.0.4 https://github.com/txp666/modusignal.git
cd modusignal
npm ci
npm test
npm run build
npm run preview
```

访问同一 localhost 设备路径；构建目录为 `_site/`。固件本仓库不再包含另一套网页上位机副本。

## 核对证据索引

- 固件工程、引脚、采样帧：[CMakeLists.txt](../../firmware/CMakeLists.txt)、[board.h](../../firmware/src/board.h)、[protocol.h](../../firmware/src/protocol.h)、[sampler.c](../../firmware/src/sampler.c)、[ADS8866 驱动](../../firmware/src/ads8866.c)。
- 本地显示、DAC 与电压检测：[local_ui.c](../../firmware/src/local_ui.c)、[power_output.c](../../firmware/src/power_output.c)、[adc_voltage.c](../../firmware/src/adc_voltage.c)、[主循环](../../firmware/src/main.c)。
- 上位机 v1.0.4：[页面控件](https://github.com/txp666/modusignal/blob/v1.0.4/pages/devices/microscope-power/index.html)、[采集/校准/CSV](https://github.com/txp666/modusignal/blob/v1.0.4/pages/devices/microscope-power/app.js)、[一阶低通](https://github.com/txp666/modusignal/blob/v1.0.4/pages/devices/microscope-power/lowpass.js)、[时间轴](https://github.com/txp666/modusignal/blob/v1.0.4/pages/devices/microscope-power/timeline.js)、[绘图](https://github.com/txp666/modusignal/blob/v1.0.4/pages/devices/microscope-power/plot.js)。
- 操作与协议：[使用说明](../operation.md)、[接线说明](../wiring.md)、[协议](../protocol.md)、[ModuSignal README](https://github.com/txp666/modusignal/blob/v1.0.4/README.md)。

该文档没有更改源代码、硬件、校准参数或发布版本，也没有进行新的目标板测量。
