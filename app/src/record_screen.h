// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// RECORD (docs/UX-303.md 5.10, D14): record the jam. A take of the master (Tap::Master, the 303 as the main output plays it) or of the input (Tap::Input, through the kit's
// AudioIn: LINE, MIC, RADIO, USB), stereo, into a 30 s O3SDK_BIG buffer, starting at the next bar of the transport (Align::Bar) and ending by itself after its bars (the
// recorder's auto_steps: 16 sixteenths a bar, to the sample), or, FREE, at F1 or a full buffer. Saved as Samples/take_NNN.wav through the kit's Files service.
//
//   F1 ARM / STOP   F2 SOURCE   F3 BARS   F4 GAIN   F5 MONITOR   F6 SAVE   F7 DISCARD (twice within 5 s)
//
// A tap of F2, F3 or F5 acts (the next source, the next bars, monitor on / off) and gives the encoder to that key; F4 only gives it the encoder; a turn then moves that value (hold the
// key and turn moves it without latching). The pads, the arrows, Copy / Insert / Delete and the plain Rec are the home map's (home_map.h), HOME's very own.
//
// The take is the app's, not the screen's: service() runs every pass of the main loop whatever screen is up, and Shift + Rec (shift_rec(), routed by o3_303_app.cpp from every screen)
// arms and stops it without changing screen. A take armed that way is saved by itself when it ends. A take armed on this screen waits for F6 or F7. The status line of every screen
// carries the take's chip (Rig::take). A firmware update is refused while a take waits for its bar or records, and while one is finished and not saved (blocker(); the kit says SAVING itself while the WAV is written).
// Everything the take says (ARMED, STOPPED, NO CARD, 8 BARS TOO LONG AT 55.0 ...) goes to the status line of every screen (Rig::take_msg), not to RECORD's alone.
//
// The view logic (the bar arithmetic, the source list, the time text, the envelope) is apart from the drawing, as the free functions below (pure, host-tested). Main loop only.

#include "frame.h"
#include "home_map.h"
#include "knob_strip.h"
#include "rig.h"

#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/screen.h>

#include <cstdint>

namespace o3_303 {

// ---- The take's geometry --------------------------------------------------------------------------------------------------------------------

// 30 s of stereo at 44.1 kHz and a little: 1,323,540 frames, 5.29 MB in the SDRAM (the engine runs at 44,100 or 44,118 Hz: both fit in 30 s).
inline constexpr std::uint32_t kTakeFrames = 30u * 44'118u;
inline constexpr unsigned kBarChoices = 5;                    // 1, 2, 4, 8 bars and FREE
inline constexpr unsigned kBars[kBarChoices] = {1, 2, 4, 8, 0};  // 0 is FREE
inline constexpr unsigned kDefaultBarsIndex = 2;              // 4 bars
inline constexpr std::uint32_t kTakeSlack = 64;               // a block of room: a take that fills the buffer to the sample is no take at all

// The frames of one 4/4 bar (16 sixteenths) at `mbpm` (milli-BPM), at the engine's rate. 0 for a tempo of 0.
std::uint32_t bar_frames(std::uint32_t mbpm) noexcept;
// `bars` bars (0: FREE, which always fits) fit the buffer of `capacity` frames at `mbpm`.
bool bars_fit(unsigned bars, std::uint32_t mbpm, std::uint32_t capacity = kTakeFrames) noexcept;
// The most bars of the list (1, 2, 4, 8) that fit at `mbpm`; 0 when not even one does.
unsigned max_bars(std::uint32_t mbpm, std::uint32_t capacity = kTakeFrames) noexcept;
// The place of the list `bars` is at (a value that is not in the list: the first place), and the list's place `delta` further, kept inside the list.
unsigned bars_index(unsigned bars) noexcept;
unsigned step_bars_index(unsigned index, int delta) noexcept;
// "4", "8", "FREE".
unsigned bars_text(unsigned bars, char* out, unsigned cap) noexcept;
// "0:05" for the clock, whole seconds, and "0:05.3" with the tenth: `frames` at `rate_hz`.
unsigned clock_text(std::uint32_t frames, std::uint32_t rate_hz, bool tenth, char* out, unsigned cap) noexcept;
// The bar a take of `frames` is in, 1-based, kept at `bars` when the take has a length (bars 0: no limit); 0 frames: bar 1. `per_bar` = bar_frames().
unsigned bar_of(std::uint32_t frames, std::uint32_t per_bar, unsigned bars) noexcept;

// ---- The source ---------------------------------------------------------------------------------------------------------------------------

enum class RecSource : std::uint8_t { Master, Line, Mic, Radio, Usb };
inline constexpr unsigned kSourceCount = 5;
// Which of the list an AudioIn source is (LineMix, LineLeft and LineRight are LINE, the two microphone gains MIC).
RecSource source_of(o3sdk::app::AudioIn::Source s) noexcept;
// The AudioIn source F2 selects for it (Master: none, Line: LineMix, Mic: MicLow).
o3sdk::app::AudioIn::Source input_source(RecSource s) noexcept;
// "MASTER", "LINE", "LINE L", "LINE R", "MIC", "MIC HI", "RADIO", "USB": what the box and the card say; `in` is the AudioIn source in use (it tells the variants).
const char* source_name(bool master, o3sdk::app::AudioIn::Source in) noexcept;
// The next (`dir` > 0) or previous source of the list, the ones that are not offered skipped: `input` the codec has an input, `radio` and `usb` those devices exist. With no input
// only MASTER is left.
RecSource step_source(RecSource cur, int dir, bool input, bool radio, bool usb) noexcept;

// ---- The take's envelope --------------------------------------------------------------------------------------------------------------------

// The peak of the take in kCols columns over `span` frames, built a slice at a time as the take grows (the screen never reads the whole 5 MB at a frame). Column c holds the largest
// |sample| of the frames [ceil(c * span / kCols), ceil((c + 1) * span / kCols)).
class Envelope {
 public:
  static constexpr unsigned kCols = 96;
  void reset(std::uint32_t span) noexcept;
  // Adds the frames [from, to) of `pcm` (interleaved, `channels` of them); frames past the span are ignored. `to - from` is bounded by the caller.
  void add(const std::int16_t* pcm, unsigned channels, std::uint32_t from, std::uint32_t to) noexcept;
  std::uint32_t span() const noexcept { return span_; }
  std::uint16_t peak(unsigned col) const noexcept { return col < kCols ? col_[col] : 0u; }

 private:
  std::uint32_t span_ = 1;
  std::uint16_t col_[kCols] = {};
};

// ---- The view ----------------------------------------------------------------------------------------------------------------------------

// What the screen shows besides the status line, the PADS line and the knob row.
struct RecordView {
  view::Knob knob[view::kKnobs];
  const char* state = "IDLE";   // "IDLE", "ARMED", "REC", "DONE", "SAVING"
  char clock[12] = "0:00.0";    // the take so far
  char bar[16] = "";            // "BAR 2 / 4", "" none
  char line[48] = "";           // the sentence under the state: "STARTS AT THE NEXT BAR", "PRESS PLAY: STARTS AT BAR 1", the take's length
  const char* source = "MASTER";
  char bars[8] = "4";
  bool fits = true;             // the chosen bars fit the buffer at the tempo
  char dest[40] = "";           // where the take goes: "Samples/take_NNN.wav", "NO CARD"
  bool dest_off = false;        // the card cannot take it
  unsigned in[2] = {};          // the meters: the input's and the master's level of L and R (0..kMeterMax) and the held peaks
  unsigned in_peak[2] = {};
  unsigned out[2] = {};
  unsigned out_peak[2] = {};
  bool input_meters = false;    // the input meters are live (the source is an input)
};

class RecordScreen final : public o3sdk::app::Screen {
 public:
  // `home`: the home map HOME has.
  void init(Rig& rig, HomeMap& home) noexcept;

  // ---- The take (the app routes them from every screen)
  // Every pass of the main loop, AFTER the project service's (o3_303_app.cpp): the take's chip and message, a take that ended with nothing, a save that is due or that ended, the envelope
  // growing, a transport that stopped under a BARS take, a tempo that moved. The take's WAV and the project's files share the one files service: a job of either starts only when none is
  // running (Files::busy()), the project service settles its own before this runs, and a WAV job is asked here and never from an input, so neither reads the other's result as its own.
  void service(o3sdk::app::Context& ctx) noexcept;
  // The take's own job's result, read BEFORE the project service runs in the pass (see the definition): the files service keeps one result, and the project's next job overwrites it.
  void settle(o3sdk::app::Context& ctx) noexcept;
  // Shift + Rec: F1 without changing screen. A take armed here saves itself when it ends.
  void shift_rec(o3sdk::app::Context& ctx) noexcept;
  // The text the kit's FIRMWARE list gives for a refused update, or nullptr.
  const char* blocker(const o3sdk::app::Context& ctx) const noexcept;

  const char* title() const noexcept override { return "RECORD"; }
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& keys) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;

  // The settings and the take, for the tests and the console.
  bool master_source() const noexcept { return master_; }
  unsigned bars() const noexcept { return kBars[bars_index_]; }
  bool saving() const noexcept { return saving_; }
  // The key the encoder drives: 1 SOURCE (F2), 2 BARS (F3), 3 GAIN (F4), 4 MONITOR (F5).
  unsigned focus() const noexcept { return owner_; }
  const RecordView& last_view() const noexcept { return view_; }
  const Envelope& envelope() const noexcept { return env_; }
  const std::int16_t* take_pcm() const noexcept;
  // What the status lines say about the take now: the chip and the last message ("" none).
  const char* chip_text() const noexcept { return rig_->take; }
  const char* message() const noexcept { return rig_->take_msg; }

 private:
  enum Key : unsigned { kKeyArm = 0, kKeySource, kKeyBars, kKeyGain, kKeyMonitor, kKeySave, kKeyDiscard };

  void button_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void key_press(o3sdk::app::Context& ctx, unsigned k) noexcept;
  void turn_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  // F1: arms a take or stops the one under way. `auto_save`: the take saves itself when it ends (Shift + Rec).
  void arm_or_stop(o3sdk::app::Context& ctx, bool auto_save) noexcept;
  bool arm(o3sdk::app::Context& ctx, bool auto_save) noexcept;
  // F6 and Shift + Rec on a finished take: asks for the save. The job itself starts in service() (issue_save()), after the project service has had its pass of the card.
  void save(o3sdk::app::Context& ctx) noexcept;
  void issue_save(o3sdk::app::Context& ctx) noexcept;
  // A word on every screen's status line (Rig::take_msg) for a moment; `off`: something did not happen.
  void say(o3sdk::app::Context& ctx, const char* text, bool off = false) noexcept;
  // The tempo moved under a take that waits or records: the bar's length follows; a take that waits and no longer fits is disarmed.
  void track_tempo(o3sdk::app::Context& ctx) noexcept;
  void discard(o3sdk::app::Context& ctx) noexcept;
  void pick_source(o3sdk::app::Context& ctx, int dir) noexcept;
  void pick_bars(o3sdk::app::Context& ctx, int delta, bool wrap) noexcept;
  void change_gain(o3sdk::app::Context& ctx, int delta) noexcept;
  void change_monitor(o3sdk::app::Context& ctx, int delta) noexcept;
  void scan(o3sdk::app::Context& ctx) noexcept;
  void finish_save(o3sdk::app::Context& ctx) noexcept;
  void chip(o3sdk::app::Context& ctx) noexcept;
  bool take_on(o3sdk::app::Context& ctx) const noexcept;  // Waiting or Recording
  void fill_view(o3sdk::app::Context& ctx, RecordView& view) noexcept;
  void enc_text(char* out, unsigned cap) const noexcept;

  Rig* rig_ = nullptr;
  HomeMap* home_ = nullptr;

  // The settings of the next take.
  bool master_ = true;                     // the source is the master, else the input AudioIn has selected
  unsigned bars_index_ = kDefaultBarsIndex;
  unsigned gain_ = 50;                     // the input's gain (percent) while the source is the master: the next input's; otherwise AudioIn's
  unsigned owner_ = kKeyBars;              // the encoder's key
  std::uint8_t down_ = 0;                  // F keys held (bit k)
  std::uint8_t tool_mask_ = 0;             // F keys pressed with Shift: their release is not an action

  // The take.
  bool auto_save_ = false;                 // save when it ends (armed by Shift + Rec)
  bool saving_ = false;                    // a save job is the files service's
  bool save_wanted_ = false;               // a save that the files service could not take yet (busy): tried again
  bool play_seen_ = false;                 // the transport ran under a BARS take that records
  unsigned take_bars_ = 0;                 // the armed take's bars (0 FREE)
  std::uint32_t take_per_bar_ = 0;         // the bar's frames at the tempo it was armed at, for the bar counter
  std::uint32_t scanned_ = 0;              // frames of the take the envelope has seen
  std::uint64_t discard_until_ms_ = 0;     // F7 was pressed: a second press before this discards
  Envelope env_;

  // Meters.
  unsigned level_[2] = {};
  unsigned peak_[2] = {};
  unsigned in_level_[2] = {};
  unsigned in_peak_[2] = {};
  std::uint64_t meter_ms_ = 0;
  RecordView view_;
};

}  // namespace o3_303
