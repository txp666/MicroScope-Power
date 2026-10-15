// SPDX-License-Identifier: MIT
// One firmware: USB peripheral diagnostics, or BAT fixed load sequence -> OFF.
#include <Arduino.h>
#include <bluefruit.h>
#include <Adafruit_TinyUSB.h>
#include <nrf_gpio.h>
#include "TestConfig.h"
#include "FlashSleepProtocol.h"
#include "PeripheralTests.h"

#ifndef TARGET_SEEED_XIAO_NRF52840_SENSE_PLUS
#error "Select Seeed nRF52 Boards / XIAO nRF52840 Sense Plus (non-mbed)"
#endif

static bool loggerEnabled = false;
static bool softDeviceReady = false;
static uint32_t suiteStart = 0;
static uint32_t scheduledEnd = 0;
static uint8_t peripheralFailures = 0;
static FlashSleep::Result flashResult = FlashSleep::Result::AwakeId;

static uint32_t rawPin(uint8_t pin) { return g_ADigitalPinMap[pin]; }
static void allLedsOff() {
  digitalWrite(LED_RED, HIGH); digitalWrite(LED_GREEN, HIGH); digitalWrite(LED_BLUE, HIGH);
}
static void safeBoardState() {
  const uint8_t ledPins[] = {LED_RED, LED_GREEN, LED_BLUE};
  for (uint8_t pin : ledPins) { digitalWrite(pin, HIGH); pinMode(pin, OUTPUT); }
  digitalWrite(PIN_LSM6DS3TR_C_POWER, LOW); pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_PDM_PWR, LOW); pinMode(PIN_PDM_PWR, OUTPUT);
  digitalWrite(PIN_PDM_CLK, LOW); pinMode(PIN_PDM_CLK, OUTPUT);
  const uint8_t inputPins[] = {PIN_WIRE1_SDA, PIN_WIRE1_SCL, PIN_LSM6DS3TR_C_INT1, PIN_PDM_DIN,
                               PIN_VBAT, PIN_CHARGING_CURRENT, D0, D2};
  for (uint8_t pin : inputPins) {
    nrf_gpio_cfg_default(rawPin(pin));
  }
  // Plus: VBAT -- 1M -- P0.31 -- 499k -- P0.14. Keep the divider safe.
  digitalWrite(VBAT_ENABLE, LOW); pinMode(VBAT_ENABLE, OUTPUT);
}
static bool usbPresent() {
  uint32_t state = 0;
  if (softDeviceReady) {
    if (sd_power_usbregstatus_get(&state) != NRF_SUCCESS) return true; // Fail closed.
  } else state = NRF_POWER->USBREGSTATUS;
  return (state & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}
static void logText(const char *text) { if (loggerEnabled && Serial) Serial.println(text); }
static void resetOnUsbAttachment() {
  if (!loggerEnabled && usbPresent()) NVIC_SystemReset();
}

// Slow 1-bit SPI on the Flash's existing QSPI pins. No QSPI ACTIVATE/READY path.
class FlashBus {
  static void output(uint8_t pin, bool high) {
    const uint32_t p = rawPin(pin);
    nrf_gpio_pin_write(p, high);
    nrf_gpio_cfg(p, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
                 NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE);
  }
  static uint8_t transfer(uint8_t value) {
    uint8_t result = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      nrf_gpio_pin_write(rawPin(PIN_QSPI_IO0), (value & 0x80) != 0);
      delayMicroseconds(2);
      nrf_gpio_pin_set(rawPin(PIN_QSPI_SCK));
      delayMicroseconds(2);
      result = uint8_t((result << 1) | nrf_gpio_pin_read(rawPin(PIN_QSPI_IO1)));
      nrf_gpio_pin_clear(rawPin(PIN_QSPI_SCK));
      value <<= 1;
    }
    return result;
  }
  static void select() { nrf_gpio_pin_clear(rawPin(PIN_QSPI_CS)); delayMicroseconds(2); }
  static void deselect() { delayMicroseconds(2); nrf_gpio_pin_set(rawPin(PIN_QSPI_CS)); delayMicroseconds(2); }
  static void input(FlashSleep::Bias bias) {
    const nrf_gpio_pin_pull_t pull = bias == FlashSleep::Bias::Up ? NRF_GPIO_PIN_PULLUP :
                                   bias == FlashSleep::Bias::Down ? NRF_GPIO_PIN_PULLDOWN : NRF_GPIO_PIN_NOPULL;
    nrf_gpio_cfg(rawPin(PIN_QSPI_IO1), NRF_GPIO_PIN_DIR_INPUT, NRF_GPIO_PIN_INPUT_CONNECT,
                 pull, NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE);
    delayMicroseconds(50);
  }
public:
  void begin() {
    // The sketch never starts the QSPI driver. Release any inherited ownership.
    NRF_QSPI->INTENCLR = 0xFFFFFFFF;
    NRF_QSPI->ENABLE = 0;
    NVIC_DisableIRQ(QSPI_IRQn);
    output(PIN_QSPI_CS, true); output(PIN_QSPI_SCK, false); output(PIN_QSPI_IO0, false);
    output(PIN_QSPI_IO2, true); output(PIN_QSPI_IO3, true);
    input(FlashSleep::Bias::None);
    delayMicroseconds(100);
  }
  void command(uint8_t opcode) { select(); transfer(opcode); deselect(); }
  FlashSleep::Id readId(FlashSleep::Bias bias) {
    input(bias); select(); transfer(0x9F);
    FlashSleep::Id id = {{transfer(0), transfer(0), transfer(0)}};
    deselect(); return id;
  }
  uint8_t readStatus() {
    input(FlashSleep::Bias::None); select(); transfer(0x05);
    const uint8_t status = transfer(0); deselect(); return status;
  }
  void waitUs(uint32_t duration) { delayMicroseconds(duration); }
  void traceId(const char *label, const FlashSleep::Id &id) {
    if (!loggerEnabled || !Serial) return;
    Serial.print("FLASH "); Serial.print(label); Serial.print(" =");
    for (uint8_t value : id.bytes) { Serial.print(' '); if (value < 16) Serial.print('0'); Serial.print(value, HEX); }
    Serial.println();
  }
  void traceStatus(uint8_t status) {
    if (loggerEnabled && Serial) { Serial.print("FLASH status = 0x"); Serial.println(status, HEX); }
  }
  void end() {
    // Keep Flash CMOS inputs defined. No test pull resistor remains enabled.
    output(PIN_QSPI_CS, true); output(PIN_QSPI_SCK, false); output(PIN_QSPI_IO0, false);
    output(PIN_QSPI_IO2, true); output(PIN_QSPI_IO3, true);
    nrf_gpio_cfg_default(rawPin(PIN_QSPI_IO1));
  }
};

static void errorLoop(uint8_t code) {
  PeripheralTests::shutdown(); safeBoardState(); allLedsOff();
  for (;;) {
    if (loggerEnabled && Serial) { Serial.print("STOP error_code="); Serial.println(code); }
    for (uint8_t i = 0; i < code; ++i) {
      digitalWrite(LED_RED, LOW); delay(150); digitalWrite(LED_RED, HIGH); delay(150);
    }
    delay(1500);
    if (loggerEnabled && !usbPresent()) NVIC_SystemReset();
    resetOnUsbAttachment();
  }
}
static void ensureSoftDevice() {
  if (softDeviceReady) return;
  Bluefruit.autoConnLed(false);
  if (!Bluefruit.begin(1, 0)) errorLoop(6);
  softDeviceReady = true;
  Bluefruit.autoConnLed(false); allLedsOff();
}
static bool syncMarker(uint32_t budget) {
  (void)budget;
  for (uint8_t i = 0; i < 3; ++i) {
    digitalWrite(LED_BLUE, LOW); delay(100); digitalWrite(LED_BLUE, HIGH); delay(100);
  }
  return true;
}
static bool flashTest(uint32_t budget) {
  (void)budget;
  FlashBus bus; bus.begin(); flashResult = FlashSleep::run(bus); bus.end();
  if (loggerEnabled && Serial) { Serial.print("FLASH result="); Serial.println(uint8_t(flashResult)); }
  return flashResult == FlashSleep::Result::Pass;
}
static bool finishMarker(uint32_t budget) {
  (void)budget;
  PeripheralTests::shutdown(); safeBoardState();
  if (flashResult != FlashSleep::Result::Pass) return false;
  for (uint8_t i = 0; i < 2; ++i) {
    digitalWrite(LED_GREEN, LOW); delay(80); digitalWrite(LED_GREEN, HIGH); delay(80);
  }
  return true;
}

static bool stage(const char *name, uint32_t duration, bool (*function)(uint32_t)) {
  scheduledEnd += duration;
  const uint32_t deadline = suiteStart + scheduledEnd;
  if (loggerEnabled && Serial) {
    Serial.print("STAGE "); Serial.print(name); Serial.print(" t_ms="); Serial.println(millis() - suiteStart);
  }
  const int32_t remaining = int32_t(deadline - millis());
  const bool ok = function(remaining > 20 ? uint32_t(remaining - 20) : 0);
  while (int32_t(millis() - deadline) < 0) {
    resetOnUsbAttachment();
    const int32_t waitRemaining = int32_t(deadline - millis());
    if (waitRemaining <= 0) break;
    const uint32_t waitMs = waitRemaining > 20 ? 20 : uint32_t(waitRemaining);
    TickType_t sleepTicks = pdMS_TO_TICKS(waitMs);
    // A 1-tick delay leaves this core's empty idle hook spinning. Meet the
    // tickless-idle threshold; the last block may overrun by less than 2 ms.
    if (sleepTicks < configEXPECTED_IDLE_TIME_BEFORE_SLEEP)
      sleepTicks = configEXPECTED_IDLE_TIME_BEFORE_SLEEP;
    vTaskDelay(sleepTicks);
  }
  if (loggerEnabled && Serial) {
    Serial.print("RESULT "); Serial.print(name); Serial.print(ok ? " PASS" : " FAIL");
    Serial.print(" end_ms="); Serial.println(millis() - suiteStart);
    if (int32_t(millis() - deadline) > 20) Serial.println("WARNING stage overrun; use actual timestamps");
  }
  return ok;
}
static void runSuite() {
  PeripheralTests::setLogger(loggerEnabled);
  peripheralFailures = 0; scheduledEnd = 0; suiteStart = millis();
  stage("SYNC", 1000, syncMarker);
  if (!stage("CPU", 2000, PeripheralTests::cpu)) peripheralFailures |= 1u << 0;
  if (!stage("RGB", 3000, PeripheralTests::rgb)) peripheralFailures |= 1u << 1;
  if (!stage("IMU", 3000, PeripheralTests::imu)) peripheralFailures |= 1u << 2;
  if (!stage("PDM", 3000, PeripheralTests::microphone)) peripheralFailures |= 1u << 3;
  if (!stage("BLE", 3000, PeripheralTests::ble)) peripheralFailures |= 1u << 4;
  if (!stage("VBAT", 1000, PeripheralTests::battery)) peripheralFailures |= 1u << 5;
  stage("FLASH", 1000, flashTest);
  stage("SHUTDOWN", 1000, finishMarker);
  if (loggerEnabled && Serial) {
    Serial.print("SUMMARY peripheral_failure_mask=0x"); Serial.print(peripheralFailures, HEX);
    Serial.print(" flash_error="); Serial.println(uint8_t(flashResult));
    Serial.println("Bit map: CPU=0 RGB=1 IMU=2 PDM=3 BLE=4 VBAT=5; 0 means no automatic failure.");
    Serial.println("RGB and voltage accuracy still require visual/reference confirmation.");
  }
}

void setup() {
  safeBoardState();
  Serial.begin(115200); // One UF2; CDC is used only while USB is physically present.
  if (usbPresent()) {
    loggerEnabled = true;
    bool ran = false;
    while (usbPresent()) {
      const bool connected = bool(Serial);
      bool requested = connected && !ran;
      while (Serial.available()) { const int c = Serial.read(); if (c == 'r' || c == 'R') requested = true; }
      if (!connected) ran = false;
      if (requested) {
        allLedsOff(); delay(150);
        Serial.println(FIRMWARE_VERSION);
        logText("USB diagnostics: keep connected. R = repeat full suite. No System OFF in USB mode.");
        ensureSoftDevice();
        while (usbPresent() && !Serial) delay(10);
        if (!usbPresent()) break;
        delay(100); runSuite(); ran = true;
        logText("USB suite finished; see SUMMARY. Unplug USB to restart the BAT measurement sequence.");
      }
      if (!ran) digitalWrite(LED_BLUE, (millis() % 1000 < 100) ? LOW : HIGH);
      else if (flashResult != FlashSleep::Result::Pass) {
        const uint32_t onWindow = uint8_t(flashResult) * 300UL;
        const uint32_t phase = millis() % (onWindow + 1500UL);
        digitalWrite(LED_RED, phase < onWindow && phase % 300 < 150 ? LOW : HIGH);
      }
      delay(10);
    }
    // Start BAT acquisition from a clean boot, even after a USB diagnostic run.
    NVIC_SystemReset();
    for (;;) {}
  }

  loggerEnabled = false;
  const uint32_t recoveryStart = millis();
  while (millis() - recoveryStart < RECOVERY_WINDOW_MS) { resetOnUsbAttachment(); delay(10); }
  ensureSoftDevice();
  runSuite();
  if (flashResult != FlashSleep::Result::Pass) errorLoop(uint8_t(flashResult));
  PeripheralTests::shutdown(); safeBoardState(); allLedsOff();
  TinyUSBDevice.detach(); delay(10); resetOnUsbAttachment();
  sd_power_system_off();
  errorLoop(7);
}
void loop() { errorLoop(7); }
