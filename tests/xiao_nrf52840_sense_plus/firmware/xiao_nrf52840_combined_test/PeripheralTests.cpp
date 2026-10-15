// SPDX-License-Identifier: MIT
#include "PeripheralTests.h"

#include <Adafruit_TinyUSB.h>
#include <PDM.h>
#include <bluefruit.h>
#include <nrf_gpio.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#ifndef TARGET_SEEED_XIAO_NRF52840_SENSE_PLUS
#error "Select Seeed XIAO nRF52840 Sense Plus from Seeed nRF52 Boards"
#endif

namespace PeripheralTests {
namespace {
bool loggerEnabled = false;
bool pdmStarted = false;
bool hfclkRequested = false;
const char *i2cFailure = "none";
volatile uint32_t cpuChecksum = 0x12345678UL;

constexpr uint8_t kImuAddress = 0x6A;
constexpr uint32_t kPdmRate = 16000;
volatile bool discardFirstPdmBlock = true;
volatile uint32_t pdmSamples = 0;
volatile int64_t pdmSum = 0;
volatile uint64_t pdmSquares = 0;
volatile uint32_t pdmPeak = 0;

void logf(const char *format, ...) {
  if (!loggerEnabled || !Serial) return;
  char line[192];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length <= 0) return;
  const size_t count = static_cast<size_t>(length) < sizeof(line)
                           ? static_cast<size_t>(length)
                           : sizeof(line) - 1;
  Serial.write(reinterpret_cast<const uint8_t *>(line), count);
  Serial.println();
}

bool timeLeft(uint32_t started, uint32_t durationMs) {
  return millis() - started < durationMs;
}

void waitUntil(uint32_t started, uint32_t targetMs) {
  for (;;) {
    const uint32_t elapsed = millis() - started;
    if (elapsed >= targetMs) break;
    const uint32_t remaining = targetMs - elapsed;
    delay(remaining > 5 ? 5 : remaining);
  }
}

void disconnectInput(uint8_t pin) {
  nrf_gpio_cfg_default(g_ADigitalPinMap[pin]);
}

void ledsOff() {
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}

void imuOff() {
  // Remove I2C pull-ups before removing sensor power.
  disconnectInput(PIN_WIRE1_SDA);
  disconnectInput(PIN_WIRE1_SCL);
  disconnectInput(PIN_LSM6DS3TR_C_INT1);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, LOW);
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
}

void microphoneOff() {
  if (pdmStarted) {
    PDM.end();
    pdmStarted = false;
  }
  PDM.onReceive(nullptr);
  NRF_PDM->INTENCLR = 0xFFFFFFFFUL;
  NVIC_DisableIRQ(PDM_IRQn);
  NVIC_ClearPendingIRQ(PDM_IRQn);
  digitalWrite(PIN_PDM_PWR, LOW);
  pinMode(PIN_PDM_PWR, OUTPUT);
  digitalWrite(PIN_PDM_CLK, LOW);
  pinMode(PIN_PDM_CLK, OUTPUT);
  disconnectInput(PIN_PDM_DIN);
  if (hfclkRequested) {
    (void)sd_clock_hfclk_release();
    hfclkRequested = false;
  }
}

void adcOff() {
  // Only used after a bounded STOP attempt, or before this module enables ADC.
  NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Disabled;
  NRF_SAADC->INTENCLR = 0xFFFFFFFFUL;
  for (uint8_t channel = 0; channel < 8; ++channel) {
    NRF_SAADC->CH[channel].PSELP = SAADC_CH_PSELP_PSELP_NC;
    NRF_SAADC->CH[channel].PSELN = SAADC_CH_PSELN_PSELN_NC;
  }
  NVIC_DisableIRQ(SAADC_IRQn);
  NVIC_ClearPendingIRQ(SAADC_IRQn);
  NRF_SAADC->RESULT.PTR = 0;
  NRF_SAADC->RESULT.MAXCNT = 0;
  disconnectInput(PIN_VBAT);
}

uint32_t sdaPin() { return g_ADigitalPinMap[PIN_WIRE1_SDA]; }
uint32_t sclPin() { return g_ADigitalPinMap[PIN_WIRE1_SCL]; }

bool releaseScl() {
  nrf_gpio_pin_set(sclPin());  // S0D1: HIGH disconnects the output driver.
  const uint32_t started = micros();
  while (!nrf_gpio_pin_read(sclPin())) {
    if (micros() - started >= 2000) {
      i2cFailure = "SCL high timeout";
      return false;
    }
  }
  delayMicroseconds(5);
  return true;
}

void lowerScl() {
  nrf_gpio_pin_clear(sclPin());
}

bool i2cBegin() {
  i2cFailure = "none";
  const uint32_t pins[] = {sdaPin(), sclPin()};
  for (uint32_t pin : pins) {
    nrf_gpio_pin_set(pin);
    // No push-pull HIGH. Pull-ups exist only while the IMU is powered for this
    // test. INPUT_CONNECT also permits clock-stretch detection.
    nrf_gpio_cfg(pin, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT,
                 NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
  }
  if (!releaseScl()) return false;
  if (!nrf_gpio_pin_read(sdaPin())) {
    i2cFailure = "SDA held low";
    return false;
  }
  return true;
}

bool i2cStart() {
  nrf_gpio_pin_set(sdaPin());
  delayMicroseconds(5);
  if (!releaseScl()) return false;
  if (!nrf_gpio_pin_read(sdaPin())) {
    i2cFailure = "SDA held low";
    return false;
  }
  nrf_gpio_pin_clear(sdaPin());
  delayMicroseconds(5);
  lowerScl();
  return true;
}

bool i2cStop() {
  nrf_gpio_pin_clear(sdaPin());
  delayMicroseconds(5);
  const bool clockReleased = releaseScl();
  nrf_gpio_pin_set(sdaPin());
  delayMicroseconds(5);
  const bool dataReleased = nrf_gpio_pin_read(sdaPin()) != 0;
  if (!dataReleased && clockReleased) i2cFailure = "SDA held low after STOP";
  return clockReleased && dataReleased;
}

bool i2cWriteByte(uint8_t value) {
  for (uint8_t bit = 0; bit < 8; ++bit) {
    nrf_gpio_pin_write(sdaPin(), (value & 0x80) != 0);
    delayMicroseconds(5);
    if (!releaseScl()) return false;
    lowerScl();
    value <<= 1;
  }
  nrf_gpio_pin_set(sdaPin());
  delayMicroseconds(5);
  if (!releaseScl()) return false;
  const bool acknowledged = nrf_gpio_pin_read(sdaPin()) == 0;
  lowerScl();
  if (!acknowledged) i2cFailure = "NACK";
  return acknowledged;
}

bool i2cReadByte(uint8_t &value, bool acknowledge) {
  value = 0;
  nrf_gpio_pin_set(sdaPin());
  for (uint8_t bit = 0; bit < 8; ++bit) {
    delayMicroseconds(5);
    if (!releaseScl()) return false;
    value = static_cast<uint8_t>((value << 1) | nrf_gpio_pin_read(sdaPin()));
    lowerScl();
  }
  nrf_gpio_pin_write(sdaPin(), acknowledge ? 0 : 1);
  delayMicroseconds(5);
  if (!releaseScl()) return false;
  lowerScl();
  nrf_gpio_pin_set(sdaPin());
  return true;
}

bool imuWrite(uint8_t reg, uint8_t value) {
  const bool transferred = i2cStart() && i2cWriteByte(kImuAddress << 1) &&
                           i2cWriteByte(reg) && i2cWriteByte(value);
  const bool stopped = i2cStop();
  return transferred && stopped;
}

bool imuRead(uint8_t reg, uint8_t *bytes, size_t count) {
  bool ok = i2cStart() && i2cWriteByte(kImuAddress << 1) && i2cWriteByte(reg) &&
            i2cStart() && i2cWriteByte((kImuAddress << 1) | 1);
  for (size_t i = 0; ok && i < count; ++i) {
    ok = i2cReadByte(bytes[i], i + 1 < count);
  }
  const bool stopped = i2cStop();
  return ok && stopped;
}

bool waitAdcEvent(volatile uint32_t &event) {
  const uint32_t started = micros();
  while (event == 0) {
    if (micros() - started >= 10000) return false;
  }
  return true;
}

void adcBegin() {
  adcOff();
  NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_12bit;
  NRF_SAADC->OVERSAMPLE = SAADC_OVERSAMPLE_OVERSAMPLE_Bypass;
  NRF_SAADC->SAMPLERATE = SAADC_SAMPLERATE_MODE_Task << SAADC_SAMPLERATE_MODE_Pos;
  NRF_SAADC->CH[0].CONFIG =
      (SAADC_CH_CONFIG_RESP_Bypass << SAADC_CH_CONFIG_RESP_Pos) |
      (SAADC_CH_CONFIG_RESN_Bypass << SAADC_CH_CONFIG_RESN_Pos) |
      (SAADC_CH_CONFIG_GAIN_Gain1_6 << SAADC_CH_CONFIG_GAIN_Pos) |
      (SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos) |
      (SAADC_CH_CONFIG_TACQ_40us << SAADC_CH_CONFIG_TACQ_Pos) |
      (SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos) |
      (SAADC_CH_CONFIG_BURST_Disabled << SAADC_CH_CONFIG_BURST_Pos);
  NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_AnalogInput7;
  NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_PSELN_NC;
}

bool adcSample(uint32_t &sample) {
  alignas(4) volatile int16_t value = 0;
  NRF_SAADC->EVENTS_STARTED = 0;
  NRF_SAADC->EVENTS_END = 0;
  NRF_SAADC->EVENTS_STOPPED = 0;
  NRF_SAADC->RESULT.PTR = reinterpret_cast<uint32_t>(&value);
  NRF_SAADC->RESULT.MAXCNT = 1;
  NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Enabled;
  NRF_SAADC->TASKS_START = 1;
  bool ok = waitAdcEvent(NRF_SAADC->EVENTS_STARTED);
  if (ok) {
    NRF_SAADC->TASKS_SAMPLE = 1;
    ok = waitAdcEvent(NRF_SAADC->EVENTS_END) && NRF_SAADC->RESULT.AMOUNT == 1;
  }
  // STOP and disable on every path, before the stack DMA buffer goes out of scope.
  NRF_SAADC->TASKS_STOP = 1;
  const bool stopped = waitAdcEvent(NRF_SAADC->EVENTS_STOPPED);
  NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Disabled;
  NRF_SAADC->RESULT.PTR = 0;
  NRF_SAADC->RESULT.MAXCNT = 0;
  sample = value < 0 ? 0 : static_cast<uint32_t>(value);
  return ok && stopped;
}

void onPdmReceive() {
  // Runs in the PDM interrupt. Drain promptly; no Serial or floating point here.
  int16_t samples[256];
  int available = PDM.available();
  if (available <= 0) return;
  if (available > static_cast<int>(sizeof(samples))) available = sizeof(samples);
  const int bytes = PDM.read(samples, static_cast<size_t>(available));
  if (discardFirstPdmBlock) {
    // This core first exposes its zeroed startup buffer before DMA completes.
    discardFirstPdmBlock = false;
    return;
  }
  for (int i = 0; i < bytes / static_cast<int>(sizeof(int16_t)); ++i) {
    const int32_t sample = samples[i];
    const uint32_t magnitude = sample < 0 ? -sample : sample;
    if (magnitude > pdmPeak) pdmPeak = magnitude;
    pdmSum += sample;
    pdmSquares += static_cast<uint64_t>(static_cast<int64_t>(sample) * sample);
    ++pdmSamples;
  }
}
}  // namespace

void setLogger(bool enabled) { loggerEnabled = enabled; }

bool rgb(uint32_t durationMs) {
  if (durationMs == 0) return false;
  const uint32_t started = millis();
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  ledsOff();
  const uint8_t colors[] = {LED_RED, LED_GREEN, LED_BLUE};
  logf("RGB: red/green/blue sequence; visually confirm each color");
  for (uint8_t i = 0; i < 3; ++i) {
    digitalWrite(colors[i], LOW);
    const uint32_t target = static_cast<uint32_t>(
        static_cast<uint64_t>(durationMs) * (i + 1) / 3);
    waitUntil(started, target);
    digitalWrite(colors[i], HIGH);
  }
  ledsOff();
  return true;
}

bool imu(uint32_t durationMs) {
  if (durationMs < 150) return false;
  const uint32_t started = millis();
  imuOff();
  nrf_gpio_cfg(g_ADigitalPinMap[PIN_LSM6DS3TR_C_POWER], NRF_GPIO_PIN_DIR_OUTPUT,
               NRF_GPIO_PIN_INPUT_DISCONNECT, NRF_GPIO_PIN_NOPULL,
               NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
  delay(10);
  uint8_t who = 0;
  bool ok = i2cBegin() && imuRead(0x0F, &who, 1);
  if (ok && who != 0x6A && who != 0x69) {
    ok = false;
    i2cFailure = "unexpected WHO_AM_I";
  }
  logf("IMU WHO_AM_I=0x%02X (LSM6DS3TR-C expected 0x6A)", who);
  ok = ok && imuWrite(0x12, 0x01);
  const uint32_t resetStarted = millis();
  uint8_t ctrl3 = 1;
  while (ok && (ctrl3 & 1)) {
    if (millis() - resetStarted >= 100 || !timeLeft(started, durationMs)) {
      i2cFailure = "reset timeout";
      ok = false;
      break;
    }
    delay(1);
    ok = imuRead(0x12, &ctrl3, 1);
  }
  ok = ok && imuWrite(0x12, 0x44) &&  // BDU + register increment.
       imuWrite(0x10, 0x40) &&       // Accelerometer: 104 Hz, +/-2 g.
       imuWrite(0x11, 0x40);         // Gyroscope: 104 Hz, +/-245 dps.
  uint32_t samples = 0;
  uint32_t changedSamples = 0;
  int16_t last[6] = {};
  bool nonzero = false;
  while (ok && timeLeft(started, durationMs)) {
    uint8_t status = 0;
    ok = imuRead(0x1E, &status, 1);
    if (ok && (status & 0x03) == 0x03) {
      uint8_t bytes[12];
      ok = imuRead(0x22, bytes, sizeof(bytes));
      if (!ok) break;
      bool changed = false;
      for (uint8_t axis = 0; axis < 6; ++axis) {
        const int16_t value = static_cast<int16_t>(
            static_cast<uint16_t>(bytes[2 * axis]) |
            (static_cast<uint16_t>(bytes[2 * axis + 1]) << 8));
        nonzero = nonzero || value != 0;
        changed = changed || value != last[axis];
        last[axis] = value;
      }
      if (samples != 0 && changed) ++changedSamples;
      ++samples;
    }
    if (timeLeft(started, durationMs)) delay(2);
  }
  imuOff();
  logf("IMU samples=%lu changed=%lu nonzero=%u Gxyz=%d,%d,%d Axyz=%d,%d,%d",
       static_cast<unsigned long>(samples), static_cast<unsigned long>(changedSamples),
       nonzero ? 1 : 0, last[0], last[1], last[2], last[3], last[4], last[5]);
  if (!ok) logf("IMU transport/reset failed: %s", i2cFailure);
  // Quiet/stationary data are not a reason to reject a functioning data path.
  return ok && samples != 0;
}

bool microphone(uint32_t durationMs) {
  if (durationMs < 100) return false;
  const uint32_t started = millis();
  microphoneOff();
  uint8_t sdEnabled = 0;
  if (sd_softdevice_is_enabled(&sdEnabled) != NRF_SUCCESS || !sdEnabled) return false;
  // Claim the application HFCLK explicitly. PDM.begin() otherwise waits without
  // a timeout and PDM.end() does not release its application clock request.
  if (sd_clock_hfclk_request() != NRF_SUCCESS) return false;
  hfclkRequested = true;
  uint32_t running = 0;
  const uint32_t clockStarted = millis();
  while (!running && timeLeft(started, durationMs) && millis() - clockStarted < 100) {
    if (sd_clock_hfclk_is_running(&running) != NRF_SUCCESS) break;
    if (!running) delay(1);
  }
  if (!running) {
    microphoneOff();
    logf("MIC: HFCLK did not start within 100 ms");
    return false;
  }
  discardFirstPdmBlock = true;
  pdmSamples = 0;
  pdmSum = 0;
  pdmSquares = 0;
  pdmPeak = 0;
  PDM.setPins(PIN_PDM_DIN, PIN_PDM_CLK, PIN_PDM_PWR);
  PDM.setBufferSize(512);
  PDM.onReceive(onPdmReceive);
  pdmStarted = PDM.begin(1, kPdmRate) != 0;
  if (!pdmStarted) {
    microphoneOff();
    return false;
  }
  PDM.setGain(20);
  while (timeLeft(started, durationMs)) delay(2);
  microphoneOff();  // Statistics are stable after the PDM interrupt is disabled.
  const uint32_t count = pdmSamples;
  const int64_t sum = pdmSum;
  const uint64_t squares = pdmSquares;
  const uint32_t peak = pdmPeak;
  const int32_t mean = count ? static_cast<int32_t>(sum / count) : 0;
  const uint32_t rms = count
      ? static_cast<uint32_t>(sqrt(static_cast<double>(squares) / count) + 0.5)
      : 0;
  logf("MIC 16k mono gain20: samples=%lu RMS=%lu peak=%lu mean=%ld PCM counts",
       static_cast<unsigned long>(count), static_cast<unsigned long>(rms),
       static_cast<unsigned long>(peak), static_cast<long>(mean));
  // RMS/peak are raw PCM diagnostics, not calibrated sound pressure. Silence,
  // including all-zero samples, is not itself a failed microphone test.
  return count != 0;
}

bool ble(uint32_t durationMs) {
  if (durationMs == 0) return false;
  const uint32_t started = millis();
  Bluefruit.autoConnLed(false);
  if (Bluefruit.Advertising.isRunning() && !Bluefruit.Advertising.stop()) return false;
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.setName("MicroScope-XIAO");
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.setInterval(160, 160);  // Nominal 100 ms, plus BLE random delay.
  Bluefruit.Advertising.setFastTimeout(0);
  bool ok = Bluefruit.setTxPower(0) &&
      Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED) &&
      Bluefruit.Advertising.addName() && Bluefruit.Advertising.start(0);
  if (ok) waitUntil(started, durationMs);
  if (Bluefruit.Advertising.isRunning()) ok = Bluefruit.Advertising.stop() && ok;
  ledsOff();
  logf("BLE: nonconnectable/nonscannable, 0 dBm, 100 ms nominal; start/stop=%u",
       ok ? 1 : 0);
  return ok;
}

bool battery(uint32_t durationMs) {
  if (durationMs == 0) return false;
  const uint32_t started = millis();
  digitalWrite(VBAT_ENABLE, LOW);
  pinMode(VBAT_ENABLE, OUTPUT);
  disconnectInput(PIN_VBAT);
  adcBegin();  // AIN7, 12-bit, internal 0.6 V / gain 1/6, 40 us acquisition.
  uint32_t value = 0;
  bool ok = adcSample(value);  // Discard first acquisition after reconfiguration.
  uint32_t count = 0;
  uint64_t sum = 0;
  uint32_t minimum = 4095;
  uint32_t maximum = 0;
  // At most 50 acquisitions, distributed over the requested slot. Timeout is
  // a failed data path, not a zero-voltage sample added to the estimate.
  while (ok && count < 50 && timeLeft(started, durationMs)) {
    ok = adcSample(value);
    if (!ok) break;
    sum += value;
    if (value < minimum) minimum = value;
    if (value > maximum) maximum = value;
    ++count;
    const uint32_t target = static_cast<uint32_t>(
        static_cast<uint64_t>(durationMs) * count / 50);
    waitUntil(started, target);
  }
  adcOff();
  const uint32_t mean = count ? static_cast<uint32_t>(sum / count) : 0;
  // Plus schematic: VBAT--1 Mohm--AIN7--499 kohm--P0.14(LOW).
  // Internal reference, resistors and source impedance all contribute error.
  const uint32_t estimateMv = count
      ? static_cast<uint32_t>(sum * 3600ULL * 1499ULL / (count * 4096ULL * 499ULL))
      : 0;
  logf("BAT: samples=%lu ADC12 mean/min/max=%lu/%lu/%lu estimated=%lu mV; not calibration",
       static_cast<unsigned long>(count), static_cast<unsigned long>(mean),
       static_cast<unsigned long>(minimum), static_cast<unsigned long>(maximum),
       static_cast<unsigned long>(estimateMv));
  if (!ok) logf("BAT: SAADC START/END/STOP timeout or incomplete DMA");
  return ok && count != 0;
}

bool cpu(uint32_t durationMs) {
  if (durationMs == 0) return false;
  const uint32_t started = millis();
  uint32_t value = cpuChecksum;
  uint32_t batches = 0;
  while (timeLeft(started, durationMs)) {
    for (uint16_t i = 0; i < 128; ++i) {
      value ^= value << 13;
      value ^= value >> 17;
      value ^= value << 5;
    }
    cpuChecksum = value;
    ++batches;
  }
  logf("CPU: batches128=%lu checksum=0x%08lX", static_cast<unsigned long>(batches),
       static_cast<unsigned long>(value));
  return batches != 0;
}

void shutdown() {
  microphoneOff();
  imuOff();
  if (Bluefruit.Advertising.isRunning()) (void)Bluefruit.Advertising.stop();
  Bluefruit.autoConnLed(false);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  ledsOff();
  adcOff();
}
}  // namespace PeripheralTests
