// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// When the project service writes live.o3a (docs/UX-303.md 8.4): a pure decision over time, no card and no bank, so a host test can drive it.
//
//   The file is written `period` after the LAST change (OFF when the period is 0), and at most kCapMs after the FIRST unsaved change while changes
//   keep coming (a long jam of edits is still saved once a minute). changed() is called for each change the service sees, due() once a pass, and
//   started() when the service begins a save (what changes from then on is a new unsaved change: the save holds the state as it was).
//
// Times are the app's uptime in milliseconds (Context::uptime_ms(), 64 bits, never wraps). Main loop only.

#include <cstdint>

namespace o3_303 {

class AutosaveClock {
 public:
  static constexpr std::uint32_t kDefaultPeriodMs = 10'000;
  static constexpr std::uint32_t kCapMs = 60'000;  // the longest an unsaved change waits while more come

  // 0 is OFF: due() is never true and changes are still counted, so turning it on again saves what is waiting.
  void set_period(std::uint32_t period_ms) noexcept { period_ms_ = period_ms; }
  std::uint32_t period() const noexcept { return period_ms_; }

  // A change at `now_ms`. The first one after a save starts the cap's clock.
  void changed(std::uint64_t now_ms) noexcept {
    if (!pending_) {
      pending_ = true;
      first_ms_ = now_ms;
    }
    last_ms_ = now_ms;
  }

  // Unsaved changes are waiting.
  bool pending() const noexcept { return pending_; }

  // A save should start now: a change is waiting and the period has gone by since the last one, or the cap since the first.
  bool due(std::uint64_t now_ms) const noexcept {
    if (!pending_ || period_ms_ == 0u) {
      return false;
    }
    return now_ms - last_ms_ >= period_ms_ || now_ms - first_ms_ >= kCapMs;
  }

  // The service began a save of the state as it is: nothing is waiting any more.
  void started() noexcept { pending_ = false; }

  // Forgets whatever was waiting (a project was loaded: the session is what the file holds).
  void reset() noexcept { pending_ = false; }

 private:
  std::uint32_t period_ms_ = kDefaultPeriodMs;
  bool pending_ = false;
  std::uint64_t first_ms_ = 0;  // the first change since the last save
  std::uint64_t last_ms_ = 0;   // the latest
};

}  // namespace o3_303
