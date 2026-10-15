// SPDX-License-Identifier: MIT
#pragma once

#include <Arduino.h>

// Seeed nRF52 Boards 1.1.13, XIAO nRF52840 Sense Plus only.
// The caller initializes Bluefruit and disables its automatic connection LED.
// Functions include normal startup/cleanup in durationMs and return early on
// failure. A true result confirms execution/data acquisition, not calibration,
// RF reception, acoustic accuracy, or visually verified RGB colors.
// IMU software I2C limits every SCL-high wait to 2 ms; direct SAADC event waits
// are limited to 10 ms. Transport failure returns false after peripheral cleanup.
namespace PeripheralTests {
void setLogger(bool enabled);
bool rgb(uint32_t durationMs);
bool imu(uint32_t durationMs);
bool microphone(uint32_t durationMs);
bool ble(uint32_t durationMs);
bool battery(uint32_t durationMs);
bool cpu(uint32_t durationMs);

// Stops this module's peripherals and advertising. Does not touch Flash pins,
// USB, the main program's stage marker, or the enabled LOW battery divider.
void shutdown();
}  // namespace PeripheralTests
