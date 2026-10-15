// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace FlashSleep {
struct Id { uint8_t bytes[3]; };
enum class Bias : uint8_t { None, Up, Down };
enum class Result : uint8_t {
  Pass = 0, AwakeId = 1, Busy = 2, FirstSleep = 3, WakeId = 4, FinalSleep = 5
};
inline bool matches(const Id &id, uint8_t a, uint8_t b, uint8_t c) {
  return id.bytes[0] == a && id.bytes[1] == b && id.bytes[2] == c;
}
inline bool expected(const Id &id) { return matches(id, 0x85, 0x60, 0x15); }

template <typename Bus>
bool sleeping(Bus &bus, const char *upLabel, const char *downLabel) {
  const Id up = bus.readId(Bias::Up);
  bus.traceId(upLabel, up);
  const Id down = bus.readId(Bias::Down);
  bus.traceId(downLabel, down);
  return matches(up, 0xFF, 0xFF, 0xFF) && matches(down, 0, 0, 0);
}

// Read/control instructions only: no write-enable, program, status-write or erase.
// Every failure is a gate: the caller must not claim successful System OFF.
template <typename Bus>
Result run(Bus &bus) {
  bus.command(0xAB);
  bus.waitUs(100);
  const Id awakeUp = bus.readId(Bias::Up);
  bus.traceId("awake.up", awakeUp);
  const Id awakeDown = bus.readId(Bias::Down);
  bus.traceId("awake.down", awakeDown);
  if (!expected(awakeUp) || !expected(awakeDown)) return Result::AwakeId;

  bool ready = false;
  for (uint8_t attempt = 0; attempt < 50; ++attempt) {
    const uint8_t status = bus.readStatus();
    if (attempt == 0 || !(status & 1)) bus.traceStatus(status);
    if (!(status & 1)) { ready = true; break; }
    bus.waitUs(1000);
  }
  if (!ready) return Result::Busy;

  bus.command(0xB9);
  bus.waitUs(10000);
  if (!sleeping(bus, "sleep1.up", "sleep1.down")) return Result::FirstSleep;

  // A readable ID after AB proves that the earlier floating reply was not just
  // an unconnected bus. Then repeat B9 and verify the actual final sleep state.
  bus.command(0xAB);
  bus.waitUs(100);
  const Id wakeUp = bus.readId(Bias::Up);
  bus.traceId("wake.up", wakeUp);
  const Id wakeDown = bus.readId(Bias::Down);
  bus.traceId("wake.down", wakeDown);
  if (!expected(wakeUp) || !expected(wakeDown)) return Result::WakeId;

  bus.command(0xB9);
  bus.waitUs(10000);
  if (!sleeping(bus, "sleep2.up", "sleep2.down")) return Result::FinalSleep;
  return Result::Pass;
}
}  // namespace FlashSleep
