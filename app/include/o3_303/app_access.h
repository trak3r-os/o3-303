// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A way into the running app for tests and tools (the simulator's checks read what the gestures did): the sequencer, with its performance
// flags, its live transpose and its chain. The app owns it; nothing here is for another screen.

#include <o3_303/seq/sequencer.h>
#include <o3_303/snapshot.h>

namespace o3_303 {

seq::Sequencer& app_sequencer() noexcept;
// The eight snapshots (Wiring::snaps), main loop only: tests read what a boot, a NEW or a RESET PROJECT gave them.
SnapBank& app_snapshots() noexcept;

}  // namespace o3_303
