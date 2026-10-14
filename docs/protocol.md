# WebUSB 协议

固件使用 TinyUSB Vendor Class + WebUSB。网页端通过一个 IN 端点同时接收二进制波形帧和文本状态，通过帧魔数恢复边界。

## USB 参数

| 项目 | 值 |
| --- | --- |
| VID | `0xCAFE` |
| PID | `0x4012` |
| Interface | `0` |
| Endpoint OUT | `1` |
| Endpoint IN | `1` |

OUT 端点发送 ASCII 行命令，以 `\n` 结束。IN 端点返回二进制帧、`OK`、`ERR <code>` 和 `STATUS ...` 文本。

## 波形帧

二进制帧为小端序，固定 530 字节。

```c
typedef struct __attribute__((packed)) {
    uint16_t magic;        // 0xA55A
    uint16_t seq;          // 16-bit frame sequence
    uint32_t t0_us;        // first sample timestamp, time_us_32()
    uint32_t sample_rate;  // 10000 / 20000 / 50000 / 100000
    uint16_t count;        // 256
    uint16_t flags;        // bit flags
    uint16_t samples[256]; // raw ADS8866 code
    uint16_t crc;          // currently 0
} wave_frame_t;
```

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | `magic` |
| 2 | 2 | `seq` |
| 4 | 4 | `t0_us` |
| 8 | 4 | `sample_rate` |
| 12 | 2 | `count` |
| 14 | 2 | `flags` |
| 16 | 512 | `samples` |
| 528 | 2 | `crc` |

## Flags

| Bit | 名称 | 说明 |
| ---: | --- | --- |
| 0 | `ADC_DATA` | 当前帧为真实 ADC 数据 |
| 1 | `OVERFLOW` | 固件队列满，发生过丢帧 |
| 2 | `RANGE_10_OHM` | 当前帧使用 10Ω 小量程 |
| 3 | `AUTO_RANGE` | 自动量程已启用 |
| 4 | `STREAM_START` | 本次开始/重新配置后的首个有效帧；上位机应开启新采集段并重置序号基线 |
| 5 | `GAP_BEFORE` | 前方有换档稳定期或 ADC 读取失败导致的数据缺口，不应跨边界连线 |
| 6 | `TIMING_OVERRUN` | 软件采样调度超限，但有效 ADC 样本仍连续保留；与数据缺口分开报告 |

没有 `RANGE_10_OHM` 时，网页按 1Ω 大量程换算。

连续帧的帧内时间按相邻 `t0_us` 的间隔均匀估算，避免把实际采样变慢集中画成帧尾空白；这不能还原每个样本的精确时序抖动。最新帧暂用上一段测得的间隔。真正的数据缺口保留其设备时间，序号缺失也会形成绘图断点。

早期未发布固件曾把调度超限也写入 `GAP_BEFORE`。网页仅在时序诊断非零、ADC 错误为零、帧序号连续、量程相同且帧间隔小于两个标称帧周期时兼容该标记；新固件使用独立的 `TIMING_OVERRUN` 位。

## 命令

| 命令 | 说明 |
| --- | --- |
| `PING` | 返回 `OK PONG` |
| `START` | 开始采样 |
| `STOP` | 停止采样 |
| `RESET` | 停止采样并回到空闲 |
| `BOOTSEL` | 停止采样并重启到 Pico BOOTSEL/U盘升级模式 |
| `OTA` | `BOOTSEL` 的别名 |
| `REBOOT_BOOTSEL` | `BOOTSEL` 的别名 |
| `SRATE 10000` | 设置 10kSPS |
| `SRATE 20000` | 设置 20kSPS |
| `SRATE 50000` | 设置 50kSPS |
| `SRATE 100000` | 设置 100kSPS |
| `ZERO_CAL` | 用固件最近一次采样值更新当前量程零点 |
| `SET_ZERO_CODE <0..65535>` | 设置当前量程零点 |
| `SET_ZERO_CODE_RANGE <1\|10> <0..65535>` | 设置指定量程零点 |
| `SET_SENSE_MOHM_RANGE <1\|10> <milliohm>` | 设置指定量程采样电阻，单位 mΩ |
| `SET_AUTO_RANGE <0\|1>` | 开关自动量程 |
| `SET_RANGE <1\|10>` | 自动量程关闭时手动设置量程 |
| `GET_STATUS` | 返回文本状态 |

`SET_SENSE_MOHM_RANGE 1 1000` 表示 1Ω 大量程；`SET_SENSE_MOHM_RANGE 10 10000` 表示 10Ω 小量程。

## 状态文本

示例：

```text
OK
ERR <code>
OK BOOTSEL
STATUS state=IDLE fw=0.1.0 srate=100000 source=ADC dropped=0 seq=0 zero=0 zero1=0 zero10=0 last=0 range=10 auto_range=1 sense1_mohm=1000 sense10_mohm=10000 vdac_mv=0
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `state` | `BOOT` / `USB_WAIT` / `IDLE` / `RUNNING` / `FAULT` |
| `fw` | 固件版本，来自 `VERSION` 或 CI tag |
| `srate` | 当前采样率 |
| `dropped` | 固件累计丢帧数 |
| `seq` | 下一帧序号 |
| `zero` | 当前量程零点 |
| `zero1` | 1Ω 大量程零点 |
| `zero10` | 10Ω 小量程零点 |
| `last` | 最近一次 ADS8866 原始 code |
| `range` | 当前量程，`1` 或 `10` |
| `auto_range` | 自动量程，`0` 或 `1` |
| `sense1_mohm` | 1Ω 量程采样电阻，单位 mΩ |
| `sense10_mohm` | 10Ω 量程采样电阻，单位 mΩ |
| `vdac_mv` | 当前 DAC 设定输出，单位 mV |
| `adc_errors` | 上电以来 SPI 读取/写入等待超时次数；超时不会再复制上次 ADC 值充当新样本 |
| `timing_overruns` | 上电以来软件采样调度超限次数；非零时需检查目标板实际采样周期 |
| `save_pending` | 参数等待空闲保存，`0` 或 `1` |
| `save_error` | 最近一次参数保存及回读验证是否失败，`0` 或 `1` |

## 电流换算

网页和 OLED 空闲电流使用同一套参数：

```text
current_uA = (code - zeroCodeForRange) * ADS_REF_MV * 1000000
             / 65536 / INA_GAIN / sense_milliohm
```

当前默认值：

- `ADS_REF_MV = 2500`
- `INA_GAIN = 50`
- 1Ω 大量程默认 `sense_milliohm = 1000`
- 10Ω 小量程默认 `sense_milliohm = 10000`

## 丢帧统计

网页端通过 16-bit `seq` 计算丢帧：

```js
const delta = (seq - lastSeq) & 0xffff;
if (delta > 1) dropped += delta - 1;
```

固件队列满时丢弃旧帧，并在新帧 `flags` 设置 `OVERFLOW`。

`OVERFLOW` 是提示位，不能在序号缺口之外再次累加丢帧。`STREAM_START` 必须先重置序号基线；它随队列淘汰转移到下一帧，因此首个收到的帧序号可能大于零。序号只为有效发布帧递增，换档稳定期和 ADC 错误导致的缺口使用 `GAP_BEFORE` 与 `t0_us` 表示。

网页扩展 `t0_us` 的 32 位回绕，以真实帧起始时间绘制、计算游标并导出 CSV。连续帧内按相邻帧起点估算间隔，最新帧暂用上一段测得的间隔；协议无法恢复逐样本抖动，不能据此宣称满足标称采样精度。

网页命令串行发送，检查完整 USB 写入，再等待对应的 `OK`、`ERR` 或 `STATUS`。超时/短写会终止本次连接，重新连接前不会继续接受迟到的应答。每次连接先执行 USB 总线复位，清理旧端点数据；这不等同于重启 MCU。

## 断电保存

固件把以下参数保存在 Pico flash 最后一个扇区：

- 1Ω 大量程零点。
- 10Ω 小量程零点。
- 1Ω 大量程采样电阻。
- 10Ω 小量程采样电阻。
- DAC 输出电压。

保存请求在采集停止/空闲时处理；采集运行时不写 flash。

擦写时同时暂停另一核心并关闭本核中断，完成后回读校验和及实际参数。`OK` 表示命令已应用，不等于已经断电保存；可用 `save_pending` / `save_error` 检查保存状态，失败时另报 `WARN SETTINGS_SAVE_FAILED`。

## 固件升级

当前“OTA”实现为 WebUSB 触发 BOOTSEL：

1. 网页发送 `BOOTSEL`。
2. 固件停止采样，返回 `OK BOOTSEL`。
3. Pico 调用 `reset_usb_boot(0, 0)` 重启到 ROM BOOTSEL。
4. 电脑出现 Pico U 盘后，拷贝新的 UF2。

这不是边运行边覆盖 flash 的网络 OTA。RP2040 当前硬件没有网络链路，直接在应用运行时覆盖自身 flash 风险较高，因此采用 BOOTSEL 升级路径。
