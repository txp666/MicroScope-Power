# 接线说明

本文按当前固件 `firmware/src/board.h` 的默认引脚描述。

## ADS8866 到 Pico

| Pico | ADS8866/前端 | 说明 |
| --- | --- | --- |
| GP16 | SDO / MISO | SPI0 RX |
| GP17 | CONVST / CS | 转换启动；高电平转换，低电平读出 |
| GP18 | SCLK | SPI0 SCK |
| GP19 | DIN | 固件保持高电平，使 ADS8866 进入 CS 模式 |
| GP20 | SEL | 前端量程选择，`1` 为 10Ω 小量程 |
| 3V3 | DVDD / AVDD | 数字/模拟电源，按硬件设计连接 |
| GND | GND | 共地 |
| 2.5V REF | REF | ADS8866 基准 |

固件参数：

- SPI 端口：`spi0`
- SPI 时钟：`24MHz`
- SPI 格式：16-bit, CPOL=0, CPHA=0, MSB first
- ADS8866 转换等待：约 `9us`

## 电流采样链路

```text
电源输出 -> Rsense -> 被测负载 -> GND

Rsense Kelvin 引出
  -> INA296A3, Gain = 50V/V
  -> ADS8866
  -> RP2040
```

默认量程：

| 量程 | SEL | 默认采样电阻 | 用途 |
| --- | ---: | ---: | --- |
| 大量程 | 0 | 1Ω | 较大电流 |
| 小量程 | 1 | 10Ω | 较小电流 |

网页和固件都允许修改两个采样电阻值。单位：

- 网页输入单位为 `Ω`。
- 固件命令单位为 `mΩ`。
- OLED 空闲电流使用固件内保存的同一套电阻和零点。

## DAC / LDO / 输出电压检测

| Pico | 信号 | 说明 |
| --- | --- | --- |
| GP8 | I2C SDA | DAC、OLED 共用 I2C |
| GP9 | I2C SCL | DAC、OLED 共用 I2C |
| GP0 | LDO_EN | 输出电源使能 |
| GP1 | LDO_PG | 输出电源 Power Good |
| GP26 / ADC0 | 输出电压检测 | 通过 100k/100k 分压测量 |

DAC60501 地址自动扫描 `0x48..0x4B`。固件当前 DAC 设定范围为 `0..2500mV`。

输出电压显示来自 ADC0 实测值，不是直接显示 DAC 设定值，并带 IIR 滤波。

## OLED

OLED 使用 I2C，地址 `0x3C`，与 DAC 共用：

| Pico | OLED |
| --- | --- |
| GP8 | SDA |
| GP9 | SCL |
| 3V3 | VCC |
| GND | GND |

当前 OLED 显示内容：

- 上方大字：输出电压，单位 `V`。
- 下方大字：空闲电流，单位 `UA` / `MA` / `A`。

上位机采集运行时，本地 OLED 不读取 ADS8866，避免干扰高速采样；此时显示会按本地刷新节奏保持。

如果 OLED 最右侧出现花屏，当前驱动已经加入控制器列偏移 `+2`。不同 OLED 模块若仍有边缘异常，可调整 `firmware/src/oled_ssd1306.c` 中的 `OLED_CONTROLLER_X_OFFSET`。

## 编码器和蜂鸣器

| Pico | 信号 |
| --- | --- |
| GP6 | 编码器 A |
| GP4 | 编码器 B |
| GP5 | 编码器按键 |
| GP13 | 蜂鸣器 PWM |

本地操作：

- 旋转：调整 DAC 输出电压。
- 按住旋转：快速调整。
- 短按：切换步进档位。

DAC 输出电压在停止调节一段时间后保存到 flash。
