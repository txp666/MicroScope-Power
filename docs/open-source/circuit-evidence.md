# MicroScope Power：电路图文证据与作图依据

核对日期：2026-10-14。本文供开源介绍、框图和电路说明使用，区分图纸连接、芯片规格、理论计算和当前固件设定；不将理论码宽、器件噪声指标或软件采样率选项写成整机实测精度。

## 1. 核对对象与结论

硬件依据是用户提供的 [PADS 原始归档](<hardware/PADS_MicroScope Power便携式可调电源与微电流波形分析仪_2026-10-13.zip>) 和 [原理图 PNG 原始归档](hardware/PNG_MicroScope-Power_2026-10-13.zip)。原理图页标版本 V1.0，P1/P2 图纸日期分别为 2026-07-20、2026-07-14，不能把导出日期当成新版 PCB 的验证日期。

已查看两张完整原理图，并用 PADS ASCII 的 `PARTTYPE`、`PART`、`SIGNAL/ROUTE` 连接交叉核对。硬件文件：

- [P1：USB、供电、RP2040、DAC](hardware/SCH_MicroScope-Power_1-P1_2026-10-13.png)。
- [P2：电流前端、ADC、参考、OLED、编码器、蜂鸣器](hardware/SCH_MicroScope-Power_2-P2_2026-10-13.png)。
- [PCB ASCII](hardware/MicroScope-Power.asc)；[原理图 ASCII](hardware/MicroScope-Power.txt)。
- [完整 PCB 器件清单 CSV](hardware/components.csv)：按当前 PCB 的 108 个 `PART` 记录生成，归并为 50 行，数量合计 108，每个位号仅出现一次；其范围是设计记录，不包含未画入设计的机壳、线缆等。料号只采用源文件的明确 `ManufacturerPart` 属性，U4 面板和 J1/J2 自定义香蕉座缺失该属性，保持空白。封装列保留 PCB 内部 decal 名称，不能将这份设计清单当成已经核准的采购物料表。
- 固件依据：[board.h](../../firmware/src/board.h)、[power_output.c](../../firmware/src/power_output.c)、[ads8866.c](../../firmware/src/ads8866.c)、[adc_voltage.c](../../firmware/src/adc_voltage.c)、[sampler.c](../../firmware/src/sampler.c)、[local_ui.c](../../firmware/src/local_ui.c)。

框图必须保留以下关系：

1. USB 输入 `VBUS` 分成整机 3.3 V 电源和可调输出两路。
2. 可调输出为 `VBUS → TPS7A90 → VREG → 1 Ω → 10 Ω → VOUT → 被测负载 → GND`。
3. 两个采样电阻始终串联；TMUX1119 选择两路 INA 输出，未旁路任何一个采样电阻。
4. INA296A3 两路增益均为 50 V/V，之后共用一个 OPA320 和一个 ADS8866，不能画成两路同时采样的 ADC。
5. ADS8866 的 2.5 V 参考来自 REF3025；DAC60501 使用自身的 2.5 V 内部参考。这是两套不同参考源。
6. 电压 ADC 取样点在 `VOUT`，LDO 的模拟反馈点在 `VREG`，两者隔着总计 11 Ω 的分流电阻。

## 2. USB 输入、整机供电与输出口

| 器件/网络 | 图纸中的实际连接与作用 |
| --- | --- |
| USB1，KH-TYPE-C-16P | 输入/通信口。VBUS 接 A4/A9/B4/B9；GND 共地。D+、D− 的正反插触点各自合并。没有 USB PD 控制器，不应写支持 PD 升压输入。 |
| R2、R3，5.1 kΩ | 分别将输入口 CC1/CC2 下拉到 GND。 |
| D5，H5VL10B | VBUS 对地保护器件；D6，USBLC6-2SC6，为 USB 数据线保护网络。保护器件存在不等于整机已完成 ESD 测试。 |
| R15、R16，27 Ω | 分别串在 D+、D− 到 U3 的 USB_DP/USB_DM 路径。 |
| U2，HE9073A33M5R | VIN 和 CE 均接 VBUS，GND 接地，VOUT 为 `V_SYS`，名义 3.3 V。C4/C9 在图中均为 100 nF。U2 为整机供电，与可调输出 LDO 独立。 |
| V_SYS | 给 U3 RP2040、U8 Flash、U7 DAC、U13 OPA320、U14/U15 INA、U16 TMUX、U6 ADC、U17 REF3025、蜂鸣器及 OLED 二级电源供电。 |
| USB2，KH-TYPE-C-16P | 输出口的 VBUS 接 `VOUT`，GND 接共地；R33/R34 各 56 kΩ 下拉 CC。D+/D− 未接 RP2040，不能画成第二个数据口。 |
| J1/J2 | J1 为 VOUT 正端，J2 为 GND 负端，与 USB2 输出共用同一电源节点。D4 为 VOUT 对地保护器件，C37 为输出侧 100 nF。 |

HE9073 的 3.3 V 型号、SOT-23-5 引脚和使能关系由厂家资料确认。其文档保存在原 BOM 指向的元器件平台地址：[HE9073 厂家 PDF，p.1–2](https://atta.szlcsc.com/upload/public/pdf/source/20200818/C723783_47C0FB0690B3745218579CD1DF0C296C.pdf)。图文只标“整机 3.3 V LDO”，不将其中的芯片供流指标当成整机输出规格。

## 3. DAC 控制 TPS7A90：电压设定发生在反馈网络

U1 是 TPS7A9001DSKR，输入 IN9/IN10 接 VBUS，输出 OUT1/OUT2 接 `VREG`。EN7 经 R19 100 Ω 接 RP2040 GP0，R4 100 kΩ 下拉使能；PG5 为开漏输出，经 R1 20 kΩ 拉到 V_SYS，再送 GP1。SS_CTRL6 接 GND，C3 100 nF 接 NR/SS8；C1/C2 各 10 µF 分别位于输入和输出。C14 2.2 nF 跨接 VREG 与 FB，为反馈网络的前馈电容。TPS7A90 是 LDO，不能把这一块画成带电感的开关降压电源。[TI TPS7A90，§4 引脚、§5.5 的 0.8 V 反馈参考、§7 应用](https://www.ti.com/lit/ds/symlink/tps7a90.pdf)。

U7 是 **DAC60501MDQFR，12 位 DAC**。WSON-8 的实际连接：VDD1→V_SYS；AGND3→GND；SPI2C4→V_SYS，选择 I²C；SYNC/A0 6→GND，硬件地址为 7 位 `0x48`；SDA7/SCL5 与 OLED 共用 GP8/GP9；VOUT2→`VDAC`；VREFIO8 的 C19 100 nF 对地。[TI DAC60501，§6、§8.3、§8.5、§8.6](https://www.ti.com/lit/ds/symlink/dac60501.pdf)。

当前固件扫描 `0x48…0x4B`，写 GAIN=`0x0101`，即参考 ÷2、缓冲增益 ×2，总输出标称范围 0…2.5 V；12 位数据左对齐写入 DAC 数据寄存器。`M` 型 DAC 芯片上电复位为中码，但固件在使能 U1 之前先写入 VDAC=0 mV，随后读取有效保存设定。

反馈网络是 R13=33 kΩ 从 VREG 到 FB，R12=9.31 kΩ 从 FB 到 GND，R14=20 kΩ 从 VDAC 到 FB。因此在稳压、忽略输入偏置和元件误差时：

```text
(VREG − 0.8) / 33 kΩ + (VDAC − 0.8) / 20 kΩ = 0.8 / 9.31 kΩ

VREG ≈ 4.95566 − 1.65 × VDAC      （单位 V）
```

该式由图纸连接和器件的 0.8 V 反馈参考推导：**VDAC 增大时，VREG 减小。** VDAC 标称 0…2.5 V 对应 VREG 约 4.956…0.831 V。12 位 DAC 最高码实际为 `4095/4096` 满量程，不能将 UI 的 2500 mV 字符串当成实测电压。由于输入约 5 V，最大输出还受到 USB 线缆压降、LDO 压差、温升和负载影响，不能宣传“任何负载均可输出 4.956 V”。

此设计的闭环由 LDO 内部误差放大器和 FB 电阻网络完成。当前固件没有依据 VOUT ADC 实时做 PID；不能把“DAC 可调”写成“软件 PID 恒压”。

## 4. 输出电流路径与双量程前端

```text
VREG ── R23 1 Ω ── $3N81 ── R24 10 Ω ── VOUT ── 被测负载 ── GND
          │  │                  │   │
          └ U14 INA296A3 ─┐      └ U15 INA296A3 ─┐
                          └──── U16 TMUX1119 ────┘
                                  │
                           U13 OPA320 跟随器
                                  │
                           R25 33 Ω / C41 1 nF
                                  │
                             U6 ADS8866
```

| 路径 | 器件与实际引脚连接 |
| --- | --- |
| 1 Ω 通道 | R23=ARG05BTC0010，图纸为 1 Ω；R26/R27 各 10 Ω 分别接分流电阻两端到 U14 IN+8/IN−1，C42=1 nF 跨两输入。U14 OUT5 送 U16 S1 4。 |
| 10 Ω 通道 | R24=RT0805BRD0710RL，图纸为 10 Ω；R28/R29 各 10 Ω 分别接两端到 U15 IN+8/IN−1，C43=1 nF 跨两输入。U15 OUT5 送 U16 S2 6。 |
| INA 供电/零参考 | U14/U15 均为 INA296A3IDDFR：VS6 接 V_SYS，GND2、REF1 7、REF2 3 接 GND；C39/C40 各 100 nF。两个参考端接地，模拟输出对应正向电流，未设置中点偏置。 |
| 模拟复用 | U16=TMUX1119DCKR：VDD2 接 V_SYS、GND3 接地、D5 接 OPA320 同相输入、SEL1 接 GP20。R30 100 kΩ 上拉到 V_SYS；C44 100 nF。SEL=0 为 S1/1 Ω，SEL=1 为 S2/10 Ω。 |
| ADC 驱动 | U13=OPA320AIDBVR：同相输入3 接 TMUX D，OUT1 与反相输入4相连，构成单位增益跟随器；VS+5 接 V_SYS、VS−2 接 GND，C45 100 nF。 |
| 输入 RC | R25 33 Ω 串接 OPA 输出到 U6 AINP3；C41 1 nF 从 AINP 网络到 GND。U6 AINN4 接 GND。 |

INA296A3 的增益确为 **50 V/V**，不是 20 倍或 100 倍；S1/S2 的选择由 TMUX 的真值表确认。[TI INA296，§5、§6.5](https://www.ti.com/lit/ds/symlink/ina296a.pdf)，[TI TMUX1119，§4、§7.5](https://www.ti.com/lit/ds/symlink/tmux1119.pdf)。OPA320 在本图中为缓冲器，不再增加电流增益。[TI OPA320](https://www.ti.com/lit/ds/symlink/opa320.pdf)。

TMUX 位于小信号输出侧，因此切换测量通道并不改变主电流路径的 11 Ω：

```text
VOUT ≈ VREG − I × (1 Ω + 10 Ω)
```

例如忽略其他因素时，50 mA 对应两个采样电阻合计约 0.55 V 压降。R24 在 BOM 中为 125 mW，理论热功耗 `I²R` 与负载脉冲也需考虑。TPS7A90 的 500 mA 芯片额定能力不能直接当成这块板的 500 mA 输出/测量能力。

R26/R27/C42 与 R28/R29/C43 的理想差分 RC 极点约 7.96 MHz；R25/C41 的理想单极点约 4.82 MHz。它们用于前端/ADC 接口，不能写成适配 100 kSPS 的完整抗混叠滤波器。网页低通是采样后的处理，也不代替模拟抗混叠。

## 5. ADS8866 与独立 2.5 V 参考

U6=ADS8866IDRCR，是 16 位、器件额定最高 100 kSPS 的单端 SAR ADC。AVDD2 与 DVDD10 均接 V_SYS，GND5 和裸露焊盘接 GND，AINN4 为地，AINP3 为 OPA320 缓冲后的电流信号。[TI ADS8866，§6、§7、§9](https://www.ti.com/lit/ds/symlink/ads8866.pdf)。

U17=REF3025AIDBZR，IN1 接 V_SYS，GND3 接地，OUT2 为 `V_REF`，送 U6 REF1。该具体料号的名义输出是 **2.5 V**。[TI REF30 系列，型号表与 REF3025 参数](https://www.ti.com/lit/ds/symlink/ref30.pdf)。C48=100 nF 接参考源输入侧，C47/C49 各 100 nF 位于 V_REF；C46/C50 各 100 nF 接 V_SYS。U7 的 C19/VREFIO 参考网络与 U17/V_REF 不相连。

按标称参考、标称分流电阻、理想 50 倍增益和没有零点偏移推导：

```text
VADC = 50 × RSHUNT × I
ADC 码宽 = 2.5 V / 65536 ≈ 38.147 µV
I = (code − zero_code) × 2.5 / (65536 × 50 × RSHUNT)
```

| 通道 | ADC 正向理论满量程 | 理想电流码宽 | 表述边界 |
| --- | ---: | ---: | --- |
| 1 Ω，大量程 | 约 50 mA | 约 0.763 µA/码 | 零点、增益误差和饱和裕量会改变实际范围。 |
| 10 Ω，小量程 | 约 5 mA | 约 0.0763 µA/码 | 不是“0.0763 µA 精度”，也不是实测分辨能力。 |

参考引脚与 INA 参考引脚不是同一含义。INA 的 REF1/REF2 接 GND 设定输出偏置；ADS8866 的 REF 接 2.5 V 设定 ADC 量程。硬件没有负向电流中点偏置；网页零点相减后出现负数不等于模拟链可线性测量反向电流。

## 6. RP2040、采样通信与输出电压读回

| RP2040 GPIO / 引脚 | 连接 |
| --- | --- |
| GP0 / U3.2 | 经 R19 到 TPS7A90 EN。 |
| GP1 / U3.3 | TPS7A90 PG。 |
| GP8 / U3.11，GP9 / U3.12 | SDA/SCL，DAC 与 OLED 共用；R17/R18 各 4.7 kΩ 上拉 V_SYS。 |
| GP16 / U3.27 | U6 DOUT7，采样数据返回。 |
| GP17 / U3.28 | U6 CONVST6，固件拉高开始转换、拉低读数。 |
| GP18 / U3.29 | U6 SCLK8，当前固件 SPI0 时钟 24 MHz。 |
| GP19 / U3.30 | U6 DIN9。当前固件将此脚保持高，选择 ADC CS 模式；不要画成持续向 ADC 发送 MOSI 数据。 |
| GP20 / U3.31 | TMUX SEL。 |
| GP26 / U3.38 | 内部 ADC0，测 VOUT 分压节点。 |
| GP4 / U3.6，GP6 / U3.8 | 编码器 B/A。 |
| GP5 / U3.7 | 编码器按键。 |
| GP13 / U3.16 | 蜂鸣器 PWM。 |

U3 是 RP2040；X1 为 12 MHz 晶体，C25/C30 各 15 pF，R20 1 kΩ 在晶体输出侧。U8 为 W25Q32JVSSIQ，QSPI 连接 RP2040 专用 Flash 引脚，用于程序和配置存储；其芯片容量为 32 Mbit/4 MB，固件实际地址配置需另核对。SW2 经 R22 1 kΩ 关联 QSPI_SS，R21 10 kΩ 上拉，属于 Flash/启动按键网络。编码器按键上电按住进入 USB BOOT 是当前固件提供的另一条路径。[Raspberry Pi RP2040 官方数据手册](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)，[Winbond W25Q32JV 官方规格入口](https://www.winbond.com/hq/new-online-purchasing-guide/?__locale=tw&pLine=%2Fproduct%2Fcode-storage-flash%2Fqspi-nor%2F&pNo=W25Q32JV)。

RP2040 的内部稳压器将 V_SYS 转为 `1V1`，供数字内核 DVDD；C26=1 µF，C27/C29=100 nF。V_SYS 的 C15=1 µF 和 C16/C18/C20–C24/C28=100 nF 分布在 3.3 V 供电侧。

电压读回路径为 `VOUT → R31 100 kΩ → ADC0 节点 → R32 100 kΩ → GND`，C31 100 nF 接 ADC0 节点到地。RP2040 内部 ADC 是 12 位，当前固件以 3.3 V 名义参考换算、平均 16 次后乘 2 得到 VOUT；100 kΩ/100 kΩ 的戴维南阻抗为 50 kΩ，配合 C31 的理论截止频率约 31.8 Hz。这一路适合输出电压显示，不能当成与外部电流 ADC 同带宽、同时采样的高速电压波形通道。

该分压网络在分流电阻**之后**，因此其电流计入外部电流 ADC：3.3 V 时理论约 `3.3/200 kΩ = 16.5 µA`。输出电容的充放电、保护器件泄漏和前端输入电流也可进入这条支路。电流链测到的是 VREG 到 VOUT 的输出支路；外部 USB 电流表则包含 MCU、OLED、模拟前端、LDO 和蜂鸣器等整机输入电流，两者不能直接相等。

## 7. OLED、旋转编码器与蜂鸣器

| 电路 | 器件与作用 |
| --- | --- |
| OLED 电源 | U5 为华轩阳 662K，VIN3 接 V_SYS，VOUT2 为 OLED_VCC，GND1 接地，C13=1 µF。662K 名义输出 3.3 V，但其输入已经为 3.3 V，受 LDO 压差影响，不能把 OLED_VCC 描述为负载下保证的稳压 3.3 V。[华轩阳 662K 官方 PDF](https://www.hxymos.com/web/userfiles/productfile/Adatasheet/LDO/SOT_23_662K.pdf)。 |
| OLED 控制器 | 图纸 U4 标 SSD1306、1.3 英寸 30 引脚 OLED；GP8/GP9 用 I²C。图中“0x78”为含方向位的地址写法，对应当前固件 7 位地址 0x3C。 |
| OLED 外围 | C5/C6 各 1 µF 属电荷泵外围；C7/C8 各 1 µF 为 OLED_VCC 旁路。R5 4.7 kΩ/C10 4.7 µF/D1 1N4148WT 为复位外围；R7 910 kΩ 为 IREF 外围；C11 4.7 µF、C12 2.2 µF 接控制器显示供电/偏置端。面板料号与驱动兼容性仍以实装模块为准。 |
| 编码器 | SW1=SIQ-02FVS3，A/B/S 分别接 GP6/GP4/GP5。R9/R10/R11 各 10 kΩ 将三路上拉到 V_SYS；公共端及外壳焊脚接地。 |
| 蜂鸣器 | BUZZER1=MLT-5020，图纸/BOM 标无源电磁式 4 kHz；一端接 V_SYS，另一端接 Q1 漏极。Q1=AO3400A：栅极接 GP13 BUZZ，源极接地；R8 10 kΩ 栅源下拉。PWM 高电平使 Q1 导通，电流方向为 V_SYS→蜂鸣器→Q1→GND。[AOS AO3400A 官方 PDF](https://www.aosmd.com/sites/default/files/res/datasheets/AO3400A.pdf)。 |

当前固件已在停止蜂鸣器时强制 GPIO 输出低电平，避免 PWM 关闭后栅极残留高电平；该源代码变更不能替代“已烧录该固件、输入电流已下降”的实机确认。

## 8. 当前默认值与公开参数边界

| 参数 | 当前依据 |
| --- | --- |
| 外部 ADC 参考 | 图纸 U17=REF3025，固件 `ADS8866_REF_MV=2500`。 |
| INA 增益 | 芯片 A3=50 V/V，固件/网页换算按 50。 |
| 两档分流电阻 | 图纸 1 Ω、10 Ω；固件默认 1000/10000 mΩ，可保存校准值。 |
| 采样率选项 | 当前协议 10/20/50/100 kSPS；默认 100 kSPS 是请求/配置值，非本板所有运行状态下保证的实测速率。 |
| 自动量程 | 固件默认开启，切换时包含稳定等待/帧标记。 |
| 默认零点 | 未载入保存配置时两个 zero_code 均为 0；应按实际零输入校准。 |
| DAC | 软件设定 0…2500 mV；初始化先写 0，有效保存配置随后恢复。该值是 VDAC，不是 VOUT。 |
| 网页处理 | 原始采样与可选数字低通均可保留/查看，曲线平滑程度不等于硬件噪声已降低。 |

适合开源图文的简洁事实是“USB 供电、DAC 调节 LDO、双采样电阻前端、16 位外部 ADC、网页查看电流波形”。“约 50 mA/5 mA”必须标成基于标称参数的理论满量程；不写“亚微安精度”“整机 16 位有效精度”“低噪声已达标”“500 mA 保证输出”。

## 9. 已知待改善事项，保留在技术说明中

1. **ADC 参考/旁路**：现有 C47/C49 合计 0.2 µF，不满足 ADS8866 的 REF 10 µF 及以上建议；AVDD/DVDD 现为 100 nF，需与器件 1 µF 旁路建议核对。相关局部回流也需收紧。C49.2 到 U6.1 铜线路径约 3.018 mm、10 mil，C49 地端最近地过孔约 3.360 mm。该布局和容量问题不等于已确认全部噪声来源。[TI ADS8866，§6、§11.1、§12.1](https://www.ti.com/lit/ds/symlink/ads8866.pdf)。
2. **INA 引脚**：图纸和 ASC 中 U14/U15 的 NC4 悬空，而 INA296 数据手册要求该保留脚接地，下一版应修正。[TI INA296，§5 引脚表](https://www.ti.com/lit/ds/symlink/ina296a.pdf)。
3. **输入 RC 和反馈布局**：R25/C41 可向 ADC 输入脚收紧；OPA320 跟随器反馈线可以缩短。未测量振荡或动态建立误差，不宣称现板已被确定存在该失真。
4. **输出压降与读回**：VOUT 比 VREG 少 11 Ω 的负载压降；电压分压读取的是负载端，但并未参与软件补偿。对负载端调节与更大输出电流的需求要单独设计，不能凭网页滤波解决。
5. **V_SYS 与 OLED 供电**：HE9073 厂家资料建议较大的输入/输出储能电容，现局部 C4/C9 仅 100 nF；需结合实际整板有效电容和负载验证。U5 3.3 V→3.3 V 的二级 LDO 压差需实测。不要把两颗 LDO 的器件特性拼成整机已验证的低噪声指标。

PCB ASCII 头为 BASIC 单位，换算系数是 `25,400/38,100 nm`，全部长度按该系数计算，并用 0.5 mm 引脚间距交叉核对。内层 2 的 GND 和内层 3 的 V_SYS POUR 轮廓存在，但仅凭 ASCII 轮廓不能代替完整浇铜/DRC/制造输出检查。[KiCad PADS 格式说明](https://dev-docs.kicad.org/en/import-formats/pads/)。
