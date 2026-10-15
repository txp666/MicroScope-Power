// SPDX-License-Identifier: MIT
// Host-side state-machine checks against the same protocol used by the sketch.
// This fake models instruction responses and MISO bias, not electrical hardware.
#include "../firmware/xiao_nrf52840_combined_test/FlashSleepProtocol.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using FlashSleep::Bias;
using FlashSleep::Id;
using FlashSleep::Result;

static_assert(static_cast<uint8_t>(Result::Pass) == 0, "pass code contract");
static_assert(static_cast<uint8_t>(Result::AwakeId) == 1, "awake-ID error code");
static_assert(static_cast<uint8_t>(Result::Busy) == 2, "busy error code");
static_assert(static_cast<uint8_t>(Result::FirstSleep) == 3, "first-sleep error code");
static_assert(static_cast<uint8_t>(Result::WakeId) == 4, "wake-ID error code");
static_assert(static_cast<uint8_t>(Result::FinalSleep) == 5, "final-sleep error code");

static void require(bool value, const char *message) {
  if (!value) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

enum class Fault {
  None, AwakeUpId, AwakeDownId, BusyForever, FirstSleepIgnored,
  CannotWakeAgain, FinalSleepIgnored, AlwaysFloating
};

struct FakeBus {
  explicit FakeBus(Fault fault = Fault::None) : fault(fault) {}
  Fault fault;
  bool asleep = true;
  unsigned wakeCount = 0;
  unsigned sleepCount = 0;
  unsigned statusReads = 0;
  unsigned idReads = 0;
  std::vector<uint8_t> instructions;
  std::vector<std::string> events;
  std::vector<std::string> traceLabels;
  std::vector<uint8_t> tracedStatuses;

  void command(uint8_t opcode) {
    // The public protocol must never emit a write-enable, program, status-write
    // or erase instruction. Refuse unknown commands at the interaction point.
    require(opcode == 0xAB || opcode == 0xB9,
            "unexpected or mutating Flash command");
    instructions.push_back(opcode);
    if (opcode == 0xAB) {
      events.push_back("AB");
      ++wakeCount;
      asleep = fault == Fault::CannotWakeAgain && wakeCount >= 2;
    } else {
      events.push_back("B9");
      ++sleepCount;
      const bool ignored =
          (fault == Fault::FirstSleepIgnored && sleepCount == 1) ||
          (fault == Fault::FinalSleepIgnored && sleepCount == 2);
      asleep = !ignored;
    }
  }

  static Id floating(Bias bias) {
    require(bias == Bias::Up || bias == Bias::Down,
            "JEDEC read must use an explicit up/down bias");
    return bias == Bias::Up ? Id{{0xFF,0xFF,0xFF}} : Id{{0,0,0}};
  }

  Id readId(Bias bias) {
    instructions.push_back(0x9F);
    ++idReads;
    events.push_back(bias == Bias::Up ? "ID.up" : "ID.down");
    if (fault == Fault::AlwaysFloating || asleep) return floating(bias);
    const bool mismatch = wakeCount == 1 &&
        ((fault == Fault::AwakeUpId && bias == Bias::Up) ||
         (fault == Fault::AwakeDownId && bias == Bias::Down));
    return mismatch ? Id{{0x85,0x60,0x14}} : Id{{0x85,0x60,0x15}};
  }

  uint8_t readStatus() {
    instructions.push_back(0x05);
    ++statusReads;
    // Normal operation also exercises the WIP-to-ready transition.
    const uint8_t status = fault == Fault::BusyForever || statusReads <= 2 ? 1 : 0;
    events.push_back(status ? "STATUS.busy" : "STATUS.ready");
    return status;
  }

  void waitUs(uint32_t value) {
    events.push_back("WAIT." + std::to_string(value));
  }
  void traceId(const char *label, const Id &) { traceLabels.emplace_back(label); }
  void traceStatus(uint8_t value) { tracedStatuses.push_back(value); }

  unsigned count(uint8_t opcode) const {
    return static_cast<unsigned>(std::count(instructions.begin(), instructions.end(), opcode));
  }
  void requireReadControlOnly() const {
    for (const auto opcode : instructions) {
      require(opcode == 0xAB || opcode == 0xB9 || opcode == 0x9F || opcode == 0x05,
              "instruction stream must be read/control only");
    }
  }
};

static void normalSequence() {
  FakeBus bus;
  require(FlashSleep::run(bus) == Result::Pass, "normal protocol must pass");
  // Besides the result, assert the externally observable protocol contract:
  // two biased reads at every state, ready before B9, and settle before reading.
  const std::vector<std::string> expected{
      "AB", "WAIT.100", "ID.up", "ID.down",
      "STATUS.busy", "WAIT.1000", "STATUS.busy", "WAIT.1000", "STATUS.ready",
      "B9", "WAIT.10000", "ID.up", "ID.down",
      "AB", "WAIT.100", "ID.up", "ID.down",
      "B9", "WAIT.10000", "ID.up", "ID.down"};
  require(bus.events == expected, "normal command/bias/wait ordering");
  require(bus.traceLabels == std::vector<std::string>{
      "awake.up","awake.down","sleep1.up","sleep1.down",
      "wake.up","wake.down","sleep2.up","sleep2.down"},
      "diagnostic labels must identify every read");
  require(bus.tracedStatuses == std::vector<uint8_t>{1,0},
          "trace initial busy and final ready status");
  require(bus.asleep && bus.sleepCount == 2 && bus.wakeCount == 2,
          "pass must leave Flash asleep after a proven wake cycle");
  require(bus.count(0x9F) == 8 && bus.count(0x05) == 3,
          "pass must inspect live IDs and readiness");
  bus.requireReadControlOnly();
}

static void invalidAwakeId() {
  for (const auto fault : {Fault::AwakeUpId, Fault::AwakeDownId}) {
    FakeBus bus(fault);
    require(FlashSleep::run(bus) == Result::AwakeId,
            "mismatch with either awake bias must report error 1");
    require(bus.count(0xB9) == 0 && bus.statusReads == 0 && bus.idReads == 2,
            "awake-ID failure must stop before status polling or B9");
    bus.requireReadControlOnly();
  }
}

static void boundedBusyWait() {
  FakeBus bus(Fault::BusyForever);
  require(FlashSleep::run(bus) == Result::Busy, "stuck WIP must report error 2");
  require(bus.statusReads == 50, "busy polling must stop after exactly 50 reads");
  require(std::count(bus.events.begin(),bus.events.end(),"WAIT.1000") == 50,
          "every busy retry must have its bounded wait");
  require(bus.count(0xB9) == 0 && bus.wakeCount == 1 && bus.idReads == 2,
          "never issue B9 to a Flash that remains busy");
  bus.requireReadControlOnly();
}

static void firstSleepFailure() {
  FakeBus bus(Fault::FirstSleepIgnored);
  require(FlashSleep::run(bus) == Result::FirstSleep,
          "responsive ID after first B9 must report error 3");
  require(bus.sleepCount == 1 && bus.wakeCount == 1 && bus.idReads == 4,
          "first-sleep failure must stop before the wake/re-sleep cycle");
  require(!bus.asleep, "fake fault must leave the first sleep command ineffective");
  bus.requireReadControlOnly();
}

static void failedWakeProof() {
  FakeBus bus(Fault::CannotWakeAgain);
  require(FlashSleep::run(bus) == Result::WakeId,
          "floating response after second AB must report error 4");
  require(bus.sleepCount == 1 && bus.wakeCount == 2 && bus.idReads == 6,
          "unproven wake must stop before final B9");
  bus.requireReadControlOnly();
}

static void finalSleepFailure() {
  FakeBus bus(Fault::FinalSleepIgnored);
  require(FlashSleep::run(bus) == Result::FinalSleep,
          "responsive ID after final B9 must report error 5");
  require(bus.sleepCount == 2 && bus.wakeCount == 2 && bus.idReads == 8,
          "final-sleep fault must exercise the full wake/re-sleep path");
  require(!bus.asleep, "a passing earlier sleep must not hide a failed final sleep");
  bus.requireReadControlOnly();
}

static void disconnectedBusCannotPass() {
  FakeBus bus(Fault::AlwaysFloating);
  // This bus would satisfy an isolated high-impedance check: FF with pull-up,
  // 00 with pull-down. A real awake JEDEC ID must still be required first.
  require(FlashSleep::run(bus) == Result::AwakeId,
          "a permanently floating bus must not masquerade as sleeping Flash");
  require(bus.count(0xB9) == 0 && bus.idReads == 2,
          "floating bus must be rejected at the awake-ID gate");
  bus.requireReadControlOnly();
}

int main() {
  normalSequence();
  invalidAwakeId();
  boundedBusyWait();
  firstSleepFailure();
  failedWakeProof();
  finalSleepFailure();
  disconnectedBusCannotPass();
  std::cout << "PASS: XIAO Flash sleep protocol (normal path and failure gates 1-5)\n";
}
