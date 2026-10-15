# XIAO nRF52840 Sense Plus 综合测试与功耗测量

本固件提供 **USB 串口自检、BAT 独立供电下的固定负载序列，以及外设关闭后的 System OFF 休眠电流测量**。适用于 Seeed Studio XIAO nRF52840 **Sense Plus**；MicroScope Power 分析仪运行自身固件。

串口版本标识：`XIAO-COMBINED-3`。烧录文件：[artifacts/xiao-sense-plus-combined-test.uf2](artifacts/xiao-sense-plus-combined-test.uf2)。无需更换固件或修改配置来切换自检与测量。

本次固定 **1 Ω 大量程**的完整曲线、各负载阶段和休眠电流统计见[测试分析报告](analysis-report.md)。

本程序以板卡自身活动产生测试负载，用于观察分析仪的负载响应。外设功能、Flash 总线检查和休眠电流分别评价；测量精度需结合零点及参考负载验证。

## 烧录

1. 断开分析仪输出和电池，用 USB 数据线连接 XIAO 与电脑。
2. 快速连续按两次 XIAO 的 **RESET**，等待出现 XIAO bootloader U 盘。
3. 将 `xiao-sense-plus-combined-test.uf2` 复制到该盘根目录。复制完成后，板卡会重启，U 盘通常消失。
4. 固件烧录目标是 **XIAO**；不要将该文件复制到分析仪的 RP2040 bootloader U 盘。

## USB 串口自检

保持 XIAO USB 连接，打开它的串口监视器，波特率设为 **115200**。第一次打开监视器后自动运行一次完整自检，逐项输出结果和最终的失败位掩码；输入 **`r`** 可重新运行。

汇总行格式为：

```text
SUMMARY peripheral_failure_mask=0xNN flash_error=N
```

`peripheral_failure_mask` 的某位为 `1`，表示该项自动检查失败；多项失败时按位叠加，`0x0` 表示未记录普通外设自动检查失败。`flash_error` 单独记录 Flash 检查结果：`0` 表示通过，`1–5` 的含义见后文错误表。程序按十六进制打印掩码，不固定补齐两位。

| 掩码位 | 十六进制值 | 检查项 |
| --- | --- | --- |
| bit 0 | `0x01` | CPU |
| bit 1 | `0x02` | RGB |
| bit 2 | `0x04` | IMU |
| bit 3 | `0x08` | PDM 麦克风 |
| bit 4 | `0x10` | BLE |
| bit 5 | `0x20` | VBAT |

例如 `peripheral_failure_mask=0xC flash_error=0` 表示 IMU 与 PDM 的自动检查失败、Flash 检查通过。`flash_error=0` 不会清除普通外设失败掩码；全零也不能代替 RGB 目视检查、射频接收验证或电压精度校准。

自检覆盖 CPU、RGB 指示灯、IMU 数据读取、PDM 麦克风采集、BLE 广播、VBAT ADC 和外部 Flash 睡眠/唤醒检查。串口中的 PASS 表示对应的软件操作或数据检查通过；RGB 各色是否正常点亮、麦克风是否随声音变化、BLE 广播能否被接收，仍需结合目视、输入和接收端验证。VBAT 读数应与万用表对照，USB 供电下的读数不能直接代表 BAT 独立供电条件。

**USB 连接期间保持串口交互模式。** 自检结束后可查看完整日志或输入 `r` 再次执行测试。此模式下 USB、串口和外围活动均参与耗流，休眠电流在 BAT 独立供电模式下测量。

若 BAT 仍有供电，拔掉 USB 后固件自动复位，进入下述 BAT 测量流程。仅由 USB 供电时，拔线会断电；随后由分析仪 BAT 供电即可冷启动。

## BAT 测量序列

BAT 冷启动后先保留约 **5 秒恢复窗口**，再初始化 Bluefruit/SoftDevice。下表以 **SYNC 第一次蓝灯亮起**为 `t = 0`；前面的恢复窗口、bootloader 和 Bluefruit 初始化耗时不计入同步轴。

| 相对时间 | 阶段 | 行为与观察重点 |
| --- | --- | --- |
| 0–1 s | SYNC | 蓝灯亮 100 ms / 灭 100 ms，共 3 次，其余时间熄灭，标记序列起点。 |
| 1–3 s | CPU | 连续整数运算，观察 MCU 活动阶跃。 |
| 3–6 s | RGB | 红、绿、蓝各约常亮 1 s，观察各色 LED 负载。 |
| 6–9 s | IMU | 初始化并读取加速度、角速度数据，观察传感器工作电流。 |
| 9–12 s | PDM 麦克风 | 采集麦克风数据，观察采集负载。 |
| 12–15 s | BLE | 执行不可连接广播，观察无线突发电流。 |
| 15–16 s | VBAT | 读取电池检测 ADC。 |
| 16–17 s | Flash | 验证识别、睡眠、唤醒及再次睡眠，不擦除、不编程 Flash。 |
| 17–18 s | 收尾标记 | 绿灯亮 80 ms 两次，随后全灭，准备进入 System OFF。 |

固定序列从 SYNC 开始标称 **18 秒**，只运行一次。各阶段预留少量收尾时间，因此实际负载通电时间略短于时槽。RTOS 调度、中断和总线操作会产生时间偏差；阶段身份应结合同步标记及串口 STAGE/RESULT 的实际时间确认，不能仅靠电流幅度判断。

时槽补齐使用至少 2 个 RTOS tick 的阻塞等待，使 CPU 可进入 tickless idle；最后一次等待可能带来不足约 2 ms 的时槽偏差。Flash 指令和短延时期间仍会有真实 CPU 活动。

IMU 使用带超时的软件开漏 I²C；SCL 等待高电平最多 2 ms。VBAT 的 SAADC 启动、采样结束及停止等待均最多 10 ms；超时记为该项失败并清理资源。麦克风启动前检查 HFCLK，并在结束后释放应用时钟请求。

序列结束后停止广播，关闭 IMU 和麦克风，撤除外围总线的输入上拉，完成 Flash 最终睡眠检查，再调用 `sd_power_system_off()`。成功进入 System OFF 后不再定时重复负载；按 RESET 或重新上电可重新运行。

**绿灯两闪只表示程序准备进入 System OFF，不表示全部外设通过。** 普通外设自检失败会记录到结果掩码，程序仍会尝试关闭这些外设并深睡。应先在 USB 串口中检查逐项结果，再解释对应的 BAT 测量曲线。

## Flash 检查与错误提示

Flash 使用 **SPI 模式 0 的软件总线**直接读取实际响应，不使用驱动缓存的 JEDEC ID。要求清醒时读取 `85 60 15`，然后依次执行：等待 WIP 清除 → 发送 `B9` 深度掉电 → 检查 MISO 高阻 → 唤醒并重新读 ID → 再次发送 `B9` 并检查高阻。

高阻检查分别临时给 MISO 上拉、下拉：深睡后应随偏置读到全 `FF`、全 `00`。检查结束后撤除偏置。最终保持 CS、WP、HOLD 为 HIGH，SCK、MOSI 为 LOW；MISO 无上拉/下拉并断开输入缓冲。整个流程不进行 Flash 擦除、写入或文件系统操作。

Flash 识别或睡眠检查失败时，BAT 测量流程停止执行 System OFF 调用，并通过红灯分组闪烁指示错误；USB 模式保留串口交互与 `r` 重复执行功能。各组闪烁次数含义如下。

| 每组次数 | 失败位置 |
| --- | --- |
| 1 | 唤醒后首次读取的实际 JEDEC ID 不是 `85 60 15`。 |
| 2 | Flash WIP 忙状态等待超时。 |
| 3 | 首次 `B9` 后的 MISO 高阻检查失败。 |
| 4 | 重新唤醒后实际 JEDEC ID 检查失败。 |
| 5 | 最终 `B9` 后的 MISO 高阻检查失败。 |
| 6 | Bluefruit/SoftDevice 初始化失败。 |
| 7 | System OFF 调用异常返回。 |

错误码用于标识对应检查结果，USB 串口提供实际 ID 和各步响应。休眠电流测量以 Flash 检查通过、收尾标记完成后的休眠阶段为统计区间。

## 分析仪接线与采集

接负载前断开输出，在空载下用万用表核对分析仪 **OUT 的实际电压**并记录。BAT 测试供电可设置为约 **3.70 V**，接线按板上丝印确认：

```text
电脑 USB ───── MicroScope Power
                  OUT+ ───── XIAO BAT+
                  GND  ───── XIAO BAT− / GND

测量时：XIAO USB 拔掉，锂电池移除，调试器断开。
```

BAT 供电接入 BAT+，不可接入 3V3。测量时保持 XIAO USB、电池及其他供电源断开；BAT+、BAT− 两根线构成独立供电路径。

本次分析报告采用全程固定 **1 Ω 大量程**的记录，采样率选项为 **100 kSPS**，低通关闭。完整曲线采集按以下步骤进行：

1. **空载归零。** XIAO 未接入时，对 1 Ω 量程归零并记录残余偏移。归零时保持负载断开。
2. **采集完整曲线。** 固定 1 Ω 大量程，开始采集后接通 BAT 输出，在同一条记录中覆盖启动、恢复窗口、同步标记、18 秒负载序列及休眠阶段；采集过程中保持量程不变。
3. **记录休眠阶段。** 收尾标记结束后继续采集，记录实际可用的休眠时长。较长时段的稳定性观察可持续记录至少 60 秒；本次报告的实际统计区间以报告所列时间为准。
4. **保存测量条件。** 导出原始 CSV 并保存界面截图，记录实测供电电压、零点、固件版本、量程、采样率选项、滤波设置及界面可见的采集诊断计数。采集期间保持 XIAO USB 与调试器断开。

**可选：小量程休眠电流精测。** 若需提高微安级读数的分辨能力，可另用固定 **10 Ω 小量程、10 kSPS、低通关闭**测量休眠阶段。该量程应在负载断开时独立归零；负载序列结束后切换量程，待切换完成再开始单独记录。小量程测量用于补充精度核对，本次分析报告中的统计均来自固定 1 Ω 记录。

固定 1 Ω 可覆盖活动负载与休眠的连续变化；微安级绝对数值需结合该量程的量化步进、空载零点、测量波动和参考负载评价。10 Ω 小量程的活动峰值可能超量程，应仅用于相应电流范围内的测量。

程序将 **P0.14 保持 LOW**，保留 BAT 检测分压下臂；关闭采样后断开 **P0.31 输入缓冲**。由于 Plus 的分压上臂没有同步切断开关，该引脚配置使检测节点保持分压。BAT 供电下测量的是包含分压、稳压器及其他板上电路的整板电流。

## 构建

使用 Arduino CLI、Python 3 和 **Seeed nRF52 Boards 1.1.13 非 mbed**，板型为 `Seeeduino:nrf52:xiaonRF52840SensePlus`。依赖使用板包提供的库，保持 Release 调试等级。

在仓库根目录执行：

```sh
arduino-cli core update-index --additional-urls https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
arduino-cli core install Seeeduino:nrf52@1.1.13 --additional-urls https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
bash tests/xiao_nrf52840_sense_plus/tools/build.sh
```

烧录产物固定为 `tests/xiao_nrf52840_sense_plus/artifacts/xiao-sense-plus-combined-test.uf2`。脚本只构建，不自动烧录；可用 `ARDUINO_CLI` 与 `ARDUINO_CLI_CONFIG` 指定工具链和隔离配置。Linux 还需按板包说明安装 `adafruit-nrfutil` 并加入 PATH。

本目录集中保存固件源代码、构建与校验工具、主机协议检查和交付文件。固件入口为 `firmware/xiao_nrf52840_combined_test/xiao_nrf52840_combined_test.ino`；普通中间产物保留在已忽略的 `.build/`，`artifacts/` 仅保存一个 UF2、构建日志、校验记录和 SHA-256 清单。

在仓库根目录运行主机协议检查：

```sh
mkdir -p tests/xiao_nrf52840_sense_plus/.build
clang++ -std=c++17 -Wall -Wextra -pedantic tests/xiao_nrf52840_sense_plus/host/flash_sleep_protocol_test.cpp -o tests/xiao_nrf52840_sense_plus/.build/flash_sleep_protocol_test
tests/xiao_nrf52840_sense_plus/.build/flash_sleep_protocol_test
```

该检查使用响应模型验证 Flash 指令顺序和失败分支，不替代实板总线或功耗测量。

## 参考与验证边界

- [Seeed 官方板卡与功耗验证流程](https://wiki.seeedstudio.com/XIAO_BLE/#power-consumption-verification)
- [Sense Plus 官方原理图](https://files.seeedstudio.com/wiki/XIAO-BLE/Seeed_Studio_XIAO_nRF52840_Plus_PDF.pdf)
- [Seeed 电池检测与充电注意事项](https://wiki.seeedstudio.com/battery_charging_considerations/)
- [Seeed nRF52 Arduino 板包](https://github.com/Seeed-Studio/Adafruit_nRF52_Arduino)
- [Puya P25Q16H 数据手册](https://files.seeedstudio.com/wiki/github_weiruanexample/Flash_P25Q16H-UXH-IR_Datasheet.pdf)

本文说明固件功能、测试步骤与采集方法。整板休眠电流及本次实测条件见[测试分析报告](analysis-report.md)；外设功能由串口检查结果和实际输入、输出验证。
