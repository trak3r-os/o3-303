// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin's header (docs/SKIN-PLAN.md 2.1 and 2.2, mockup 01): y 0 to 66 of a skinned screen, in place of the kit's header strip and of the status line. A HeaderView
// is what it shows, filled from a frame::StatusView and a few facts of the context by a pure function (fill_header, tests/skin_core_test.cpp); draw_header() queues its shapes and
// texts into the Panel. The brand, the PATTERN and TEMPO 7-segment displays, the TRANSPORT, KEYBOARD and PERFORM LEDs, and the PAGE and ENC windows.

#include "panel.h"
#include "seg.h"

#include "../frame.h"

#include <o3_303/perform.h>

#include <cstdint>

namespace o3_303::skin {

inline constexpr int kHeaderH = 66;

// One of the four PERFORM slots: a button's action (dark), or an action that is on (lit). "+N" when more are on than the slots hold.
struct PerformSlot {
  char cap[6] = "";
  bool lit = false;
};

// What the TAKE LED says: nothing, or the audio take's state (Rig::take: "TAKE ARMED", "TAKE REC 0:05", "TAKE READY", "TAKE SAVING").
enum class Take : std::uint8_t { None, Armed, Recording, Ready, Saving };

struct HeaderView {
  char pattern[4] = "A01";     // the PATTERN display: a letter A to D and two digits
  char tempo[6] = "120.0";     // the TEMPO display: "125.0", or " 95.5" (the leading blank is a ghost 8)
  bool playing = false;        // PLAY, green
  bool song = false;           // Play plays the song (StatusView::song): the legend reads SONG, in the Fx colour, instead of PLAY
  bool rec = false;            // REC, red: write / record mode
  Take take = Take::None;
  bool take_lit = false;       // the TAKE LED now: armed blinks, the rest are on
  char take_text[8] = "";      // its caption: "ARMED", "0:05", "READY", "SAVING"; "TAKE" when idle
  bool keys = false;           // KEYS lit: the pads play the voice
  bool trnsp = false;          // TRNSP lit: the pads transpose the pattern; both dark when the pads are not a keyboard
  char octave[12] = "";        // "OCT 2-3"; "" when the pads are not a keyboard
  PerformSlot perform[4];
  char page[24] = "";          // the PAGE window: the screen's title
  char enc[32] = "";           // the ENC window: what the encoder moves
  char message[28] = "";       // a toast or a take message for its life, in the PAGE window in amber; "" none
  bool message_off = false;    // it says something did not happen: outlined in red
  bool save_failed = false;    // the tagline turns red: "SAVE FAILED"
  bool stock = false;          // STOCK is on: the tagline says "STOCK . MODS OFF" in Accent (a failed save, which is red, wins)
};

// What the header needs besides the StatusView: the context's, as plain values.
struct HeaderInputs {
  const char* title = "";                                    // Screen::title()
  unsigned pattern = 0;                                      // the selected pattern, 0..47
  std::uint32_t tempo_mbpm = 120'000;                        // Engine::tempo(): the master's while a MIDI clock leads
  bool playing = false;
  std::uint8_t perf[kPerfButtons] = {};                      // the PerfAction of Note, Instr, Vol, Fx (Wiring::perf)
  std::uint64_t now_ms = 0;                                  // for the armed take's blink
  ui::Rect covered{0, 0, 0, 0};                              // the area the kit's opaque system strip will cover in this frame (Context::strip_area(), empty with no strip): Panel::clear() leaves out what lies wholly in it
};

// The PAGE window's message: to 20 characters it is set in the 8x16 Label face, to 27 in the 6x12 Micro (27 x 6 = 162 px of the window's 172), and a longer one is cut at 26 characters and ends
// in "." (the same window, Micro). Pure: tests/skin_core_test.cpp.
inline constexpr unsigned kMessageLabelMax = 20;
inline constexpr unsigned kMessageMicroMax = 27;
ui::Face message_face(unsigned length) noexcept;
// `s` into `out` (`cap` at least 28) as the rule above leaves it.
void cut_message(char* out, unsigned cap, const char* s) noexcept;

// A 7-segment display: the window `win`, the cells right aligned at a 22 px pitch, the point after cell `run.dot` (the header's two displays and MASTER's tempo).
void seg_display(Panel& p, const ui::Rect& win, const SegRun& run) noexcept;

// The TEMPO display's text: tenths of a BPM, right aligned in four cells, "125.0", " 95.5". Returns `out`.
const char* tempo_display(std::uint32_t mbpm, char (&out)[6]) noexcept;

// The PERFORM slots, from the actions on (a bit per PerfAction) and the buttons' assignments: the actions that are on first, lowest first, lit, as many as fit (the last slot "+N" when
// there are more than four); the slots left hold the buttons' assigned actions that are off, dark, in the buttons' order, each once.
void fill_perform_slots(PerformSlot (&slots)[4], std::uint16_t on, const std::uint8_t (&assigned)[kPerfButtons]) noexcept;

// The whole view. `status` is the screen's StatusView, as it fills it for the status line today.
void fill_header(HeaderView& out, const frame::StatusView& status, const HeaderInputs& in) noexcept;

// Queues the header's shapes and texts (not the panel under it: queue_panel()). `v` must stay valid until the Panel's flush_text().
void draw_header(Panel& p, const HeaderView& v) noexcept;

}  // namespace o3_303::skin
