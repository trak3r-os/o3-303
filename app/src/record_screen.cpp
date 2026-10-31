// SPDX-License-Identifier: GPL-3.0-or-later
#include "record_screen.h"

#include "help_texts.h"
#include "master_screen.h"
#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/core/placement.h>
#include <o3sdk/dsp/engine.h>
#include <o3sdk/hal/audio.h>
#include <o3sdk/hal/record.h>
#include <o3sdk/ui/components.h>

#include <cstring>

namespace o3_303 {

namespace {

using namespace o3sdk;
using app::Button;
using app::Input;
using hal::record::State;
using ui::Color;
using ui::Face;
using ui::Rect;

// The take: stereo, interleaved, 30 s, in the SDRAM (5.29 MB; the SDRAM holds 7.47 MB in all with the delay and the freeze, 44.5 % of its 16 MiB; the render writes it, the main loop reads it
// for the envelope and the save).
constexpr unsigned kTakeChannels = 2;
struct alignas(4) TakeBuffer {
  std::int16_t v[kTakeFrames * kTakeChannels];
};
static_assert(alignof(TakeBuffer) == 4 && sizeof(TakeBuffer) == kTakeFrames * kTakeChannels * sizeof(std::int16_t), "the recorder's buffer is 4-byte aligned, whole frames");
O3SDK_BIG TakeBuffer g_take;
static_assert(kTakeFrames >= 30u * 44'118u, "30 s at the faster of the engine's two rates");

constexpr std::uint32_t kStepsPerBar = 16;      // the recorder's Bar: 16 sixteenths
constexpr unsigned kGainDefault = 50;           // the input's gain when the player has not chosen one (percent)
constexpr std::uint64_t kMessageMs = 2500;      // how long a take message stays on the status lines (as the toasts)
constexpr std::uint64_t kDiscardAskMs = 5000;   // a destructive key asks for a second press within this (UX-303 2.4)
constexpr std::uint32_t kScanFrames = 8192;     // the envelope reads at most this many frames a pass (the main loop's bounded work)

// The meters, as MASTER's.
constexpr unsigned kLevelFallPerS = 2000;
constexpr unsigned kPeakFallPerS = 500;
constexpr unsigned kFallStepMs = 16;

void append(char* out, unsigned cap, const char* s) noexcept {
  unsigned n = 0;
  while (n < cap && out[n] != '\0') {
    ++n;
  }
  for (; s != nullptr && *s != '\0' && n + 1u < cap; ++s) {
    out[n++] = *s;
  }
  if (n < cap) {
    out[n] = '\0';
  }
}

unsigned put_uint(unsigned v, char* out, unsigned cap) noexcept {
  char tmp[12];
  unsigned n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + v % 10u);
    v /= 10u;
  } while (v != 0u && n < sizeof tmp);
  unsigned len = 0;
  while (n != 0u && len + 1u < cap) {
    out[len++] = tmp[--n];
  }
  if (cap != 0u) {
    out[len] = '\0';
  }
  return len;
}

constexpr std::uint32_t engine_rate_hz() noexcept { return (hal::audio::sample_rate_mhz() + 500u) / 1000u; }

const char* kKeyNames[] = {"F1 ARM", "F2 SOURCE", "F3 BARS", "F4 GAIN", "F5 MONITOR"};

// The drawing's geometry (the area is the screen's, under the kit's header).
constexpr int kCardY = frame::kStatusH + 8;
constexpr int kGap = 8;
constexpr int kLeftW = 440;
constexpr int kCardH = 150;

Rect left_card(const Rect& area) noexcept { return Rect{area.x + kGap, area.y + kCardY, kLeftW, kCardH}; }
Rect right_card(const Rect& area) noexcept { return Rect{area.x + kGap + kLeftW + kGap, area.y + kCardY, area.w - kLeftW - 3 * kGap, kCardH}; }
Rect take_panel(const Rect& area) noexcept {
  const int top = area.y + kCardY + kCardH + kGap;
  return Rect{area.x + kGap, top, area.w - 2 * kGap, area.y + area.h - app::kKnobStripH - kGap - top};
}

void draw_state_card(ui::Renderer& r, const Rect& card, const RecordView& view) noexcept {
  const ui::Theme& theme = r.theme();
  const int x = card.x + 16;
  r.text(x, card.y + 10, "TAKE", Color::TextDim, Face::Label);
  const bool live = std::strcmp(view.state, "REC") == 0;
  r.text(x, ui::caps_y(theme, Face::Display, card.y + 28, 40), view.state, live ? Color::Rec : Color::Text, Face::Display);
  const int clock_x = x + 190;
  r.text(clock_x, ui::caps_y(theme, Face::Display, card.y + 28, 40), view.clock, Color::Text, Face::Display);
  if (view.bar[0] != '\0') {
    const int w = r.text_width(view.bar, Face::Label) + 12;
    const int cx = card.x + card.w - 16 - w;
    r.panel(Rect{cx, card.y + 12, w, 18}, Color::Sel);
    r.text(cx + 6, ui::caps_y(theme, Face::Label, card.y + 12, 18), view.bar, Color::SelText, Face::Label);
  }
  const int row = card.y + 80;
  r.text(x, row + 4, "SOURCE", Color::TextDim, Face::Label);
  r.text(x + 80, ui::caps_y(theme, Face::Data, row, 24), view.source, Color::Text, Face::Data);
  r.text(x + 230, row + 4, "BARS", Color::TextDim, Face::Label);
  r.text(x + 290, ui::caps_y(theme, Face::Data, row, 24), view.bars, view.fits ? Color::Text : Color::Rec, Face::Data);
  r.text(x, ui::caps_y(theme, Face::Micro, card.y + 108, 14), view.line, Color::Accent, Face::Micro);
  r.text(x, ui::caps_y(theme, Face::Micro, card.y + 126, 14), view.dest, view.dest_off ? Color::Rec : Color::TextDim, Face::Micro);
}

void draw_meter_card(ui::Renderer& r, const Rect& card, const RecordView& view) noexcept {
  const ui::Theme& theme = r.theme();
  const int x = card.x + 16;
  const int mx = x + 40;
  const int mw = card.w - 16 - 40 - 16;
  constexpr int kMh = 14;
  struct Group {
    const char* name;
    const unsigned* level;
    const unsigned* peak;
    bool live;
  };
  const Group groups[2] = {{"IN", view.in, view.in_peak, view.input_meters}, {"OUT", view.out, view.out_peak, true}};
  for (unsigned g = 0; g < 2u; ++g) {
    const int y = card.y + 14 + static_cast<int>(g) * 66;
    r.text(x, ui::caps_y(theme, Face::Label, y, 2 * kMh + 4), groups[g].name, groups[g].live ? Color::Text : Color::TextDim, Face::Label);
    for (unsigned c = 0; c < 2u; ++c) {
      ui::meter(r, Rect{mx, y + static_cast<int>(c) * (kMh + 4), mw, kMh}, groups[g].live ? groups[g].level[c] : 0u, groups[g].live ? groups[g].peak[c] : 0u, kMeterMax,
                Color::ChromeDim, Color::Pane, groups[g].live ? Color::Play : Color::Empty, Color::Text);
    }
  }
}

// The take's envelope: the peaks, mirrored about a centre line, in Rec red while it records and green once it is done; the bars of a BARS take marked.
void draw_envelope(ui::Renderer& r, const Rect& panel, const Envelope& env, std::uint32_t scanned, unsigned bars, bool recording, bool any) noexcept {
  const ui::Theme& theme = r.theme();
  const int left = panel.x + 16;
  const int width = panel.w - 32;
  const int mid = panel.y + panel.h / 2;
  const int half = panel.h / 2 - 24;
  r.text(panel.x + 16, panel.y + 6, "TAKE", Color::TextDim, Face::Label);
  r.rule(left, mid, width, ui::Axis::Horizontal, Color::Empty);
  if (!any) {
    const char* hint = "F1 ARMS A TAKE AT THE NEXT BAR . SHIFT + REC DOES IT FROM ANY SCREEN";
    r.text(left + (width - r.text_width(hint, Face::Micro)) / 2, ui::caps_y(theme, Face::Micro, mid - 20, 14), hint, Color::TextDim, Face::Micro);
    return;
  }
  if (bars > 1u) {
    for (unsigned b = 1; b < bars; ++b) {
      r.rule(left + static_cast<int>(b) * width / static_cast<int>(bars), panel.y + 22, panel.h - 28, ui::Axis::Vertical, Color::Empty);
    }
  }
  const unsigned span = Envelope::kCols;
  const auto col_x = [&](unsigned c) { return left + static_cast<int>(static_cast<unsigned>(width) * c / span); };
  const std::uint64_t reached = env.span() != 0u ? static_cast<std::uint64_t>(scanned) * span / env.span() : 0u;  // columns the take has filled (the last one partly)
  for (unsigned c = 0; c < span; ++c) {
    if (c > reached) {
      break;
    }
    const unsigned norm = meter_norm(static_cast<float>(env.peak(c)) / 32768.0f);
    int h = static_cast<int>(static_cast<std::uint64_t>(norm) * static_cast<unsigned>(half) / kMeterMax);
    if (h < 1) {
      h = 1;
    }
    const int w = col_x(c + 1u) - col_x(c);
    r.panel(Rect{col_x(c), mid - h, w > 2 ? w - 1 : w, 2 * h}, recording ? Color::Rec : Color::Play);
  }
}

}  // namespace

// ---- The view logic ---------------------------------------------------------------------------------------------------------------------------

std::uint32_t bar_frames(std::uint32_t mbpm) noexcept {
  if (mbpm == 0u) {
    return 0;
  }
  return static_cast<std::uint32_t>(static_cast<std::uint64_t>(hal::audio::sample_rate_mhz()) * 240u / mbpm);  // Hz * 240 s per BPM, both in thousandths
}

bool bars_fit(unsigned bars, std::uint32_t mbpm, std::uint32_t capacity) noexcept {
  if (bars == 0u) {
    return true;
  }
  const std::uint32_t per = bar_frames(mbpm);
  return per != 0u && static_cast<std::uint64_t>(bars) * per + kTakeSlack <= capacity;
}

unsigned max_bars(std::uint32_t mbpm, std::uint32_t capacity) noexcept {
  unsigned best = 0;
  for (unsigned i = 0; i < kBarChoices; ++i) {
    if (kBars[i] != 0u && bars_fit(kBars[i], mbpm, capacity)) {
      best = kBars[i];
    }
  }
  return best;
}

unsigned bars_index(unsigned bars) noexcept {
  for (unsigned i = 0; i < kBarChoices; ++i) {
    if (kBars[i] == bars) {
      return i;
    }
  }
  return 0;
}

unsigned step_bars_index(unsigned index, int delta) noexcept {
  const int top = static_cast<int>(kBarChoices) - 1;
  const int next = static_cast<int>(index > static_cast<unsigned>(top) ? 0u : index) + delta;
  return static_cast<unsigned>(next < 0 ? 0 : (next > top ? top : next));
}

unsigned bars_text(unsigned bars, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  if (bars == 0u) {
    append(out, cap, "FREE");
  } else {
    (void)put_uint(bars, out, cap);
  }
  return static_cast<unsigned>(std::strlen(out));
}

unsigned clock_text(std::uint32_t frames, std::uint32_t rate_hz, bool tenth, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  if (rate_hz == 0u) {
    rate_hz = 1;
  }
  const std::uint32_t secs = frames / rate_hz;
  const unsigned tenths = static_cast<unsigned>(static_cast<std::uint64_t>(frames % rate_hz) * 10u / rate_hz);
  char part[12];
  (void)put_uint(secs / 60u, part, sizeof part);
  append(out, cap, part);
  append(out, cap, ":");
  if (secs % 60u < 10u) {
    append(out, cap, "0");
  }
  (void)put_uint(secs % 60u, part, sizeof part);
  append(out, cap, part);
  if (tenth) {
    const char t[3] = {'.', static_cast<char>('0' + tenths), '\0'};
    append(out, cap, t);
  }
  return static_cast<unsigned>(std::strlen(out));
}

unsigned bar_of(std::uint32_t frames, std::uint32_t per_bar, unsigned bars) noexcept {
  if (per_bar == 0u) {
    return 1;
  }
  unsigned bar = frames / per_bar + 1u;
  if (bars != 0u && bar > bars) {
    bar = bars;
  }
  return bar;
}

RecSource source_of(app::AudioIn::Source s) noexcept {
  switch (s) {
    case app::AudioIn::Source::LineMix:
    case app::AudioIn::Source::LineLeft:
    case app::AudioIn::Source::LineRight:
      return RecSource::Line;
    case app::AudioIn::Source::MicLow:
    case app::AudioIn::Source::MicHigh:
      return RecSource::Mic;
    case app::AudioIn::Source::Radio:
      return RecSource::Radio;
    case app::AudioIn::Source::Usb:
      return RecSource::Usb;
    case app::AudioIn::Source::Count:
      break;
  }
  return RecSource::Line;
}

app::AudioIn::Source input_source(RecSource s) noexcept {
  switch (s) {
    case RecSource::Mic:
      return app::AudioIn::Source::MicLow;
    case RecSource::Radio:
      return app::AudioIn::Source::Radio;
    case RecSource::Usb:
      return app::AudioIn::Source::Usb;
    case RecSource::Master:
    case RecSource::Line:
      break;
  }
  return app::AudioIn::Source::LineMix;
}

const char* source_name(bool master, app::AudioIn::Source in) noexcept {
  if (master) {
    return "MASTER";
  }
  switch (in) {
    case app::AudioIn::Source::LineMix:
      return "LINE";
    case app::AudioIn::Source::LineLeft:
      return "LINE L";
    case app::AudioIn::Source::LineRight:
      return "LINE R";
    case app::AudioIn::Source::MicLow:
      return "MIC";
    case app::AudioIn::Source::MicHigh:
      return "MIC HI";
    case app::AudioIn::Source::Radio:
      return "RADIO";
    case app::AudioIn::Source::Usb:
      return "USB";
    case app::AudioIn::Source::Count:
      break;
  }
  return "LINE";
}

RecSource step_source(RecSource cur, int dir, bool input, bool radio, bool usb) noexcept {
  const auto offered = [&](unsigned i) {
    switch (static_cast<RecSource>(i)) {
      case RecSource::Master:
        return true;
      case RecSource::Line:
      case RecSource::Mic:
        return input;
      case RecSource::Radio:
        return input && radio;
      case RecSource::Usb:
        return input && usb;
    }
    return false;
  };
  const int n = static_cast<int>(kSourceCount);
  int at = static_cast<int>(cur);
  const int step = dir < 0 ? n - 1 : 1;
  for (int i = 0; i < n; ++i) {
    at = (at + step) % n;
    if (offered(static_cast<unsigned>(at))) {
      return static_cast<RecSource>(at);
    }
  }
  return RecSource::Master;
}

void Envelope::reset(std::uint32_t span) noexcept {
  span_ = span != 0u ? span : 1u;
  for (std::uint16_t& c : col_) {
    c = 0;
  }
}

void Envelope::add(const std::int16_t* pcm, unsigned channels, std::uint32_t from, std::uint32_t to) noexcept {
  if (to > span_) {
    to = span_;
  }
  if (pcm == nullptr || channels == 0u || from >= to) {
    return;
  }
  std::uint32_t f = from;
  while (f < to) {
    const unsigned c = static_cast<unsigned>(static_cast<std::uint64_t>(f) * kCols / span_);
    // The first frame of the next column: ceil((c + 1) * span / kCols).
    const std::uint64_t edge = (static_cast<std::uint64_t>(c + 1u) * span_ + kCols - 1u) / kCols;
    const std::uint32_t end = edge < to ? static_cast<std::uint32_t>(edge) : to;
    std::uint32_t peak = col_[c < kCols ? c : kCols - 1u];
    for (; f < end; ++f) {
      for (unsigned ch = 0; ch < channels; ++ch) {
        const std::int32_t v = pcm[static_cast<std::size_t>(f) * channels + ch];
        const std::uint32_t a = static_cast<std::uint32_t>(v < 0 ? -v : v);
        peak = a > peak ? a : peak;
      }
    }
    col_[c < kCols ? c : kCols - 1u] = static_cast<std::uint16_t>(peak > 32767u ? 32767u : peak);
  }
}

// ---- Setup and screen hooks ------------------------------------------------------------------------------------------------------------------

const std::int16_t* RecordScreen::take_pcm() const noexcept { return g_take.v; }

void RecordScreen::init(Rig& rig, HomeMap& home) noexcept {
  // Every piece of state starts afresh: the app object outlives a run in the simulator's tests.
  rig_ = &rig;
  home_ = &home;
  master_ = true;
  bars_index_ = kDefaultBarsIndex;
  gain_ = kGainDefault;
  owner_ = kKeyBars;
  down_ = 0;
  tool_mask_ = 0;
  auto_save_ = false;
  saving_ = false;
  save_wanted_ = false;
  play_seen_ = false;
  take_bars_ = 0;
  take_per_bar_ = 0;
  scanned_ = 0;
  discard_until_ms_ = 0;
  env_.reset(kTakeFrames);
  level_[0] = level_[1] = 0;
  peak_[0] = peak_[1] = 0;
  in_level_[0] = in_level_[1] = 0;
  in_peak_[0] = in_peak_[1] = 0;
  meter_ms_ = 0;
  view_ = RecordView{};
  rig.take[0] = '\0';
  rig.take_msg[0] = '\0';
  rig.take_msg_off = false;
  rig.take_msg_until_ms = 0;
}

void RecordScreen::enter(app::Context& ctx) noexcept {
  home_->enter(ctx);
  down_ = 0;
  tool_mask_ = 0;
  discard_until_ms_ = 0;
  if (ctx.audio_in().selected()) {  // the kit's saved gain, or the Config's AUDIO section's, is the one shown: the first F2 does not overwrite it
    gain_ = ctx.audio_in().gain();
  }
  meter_ms_ = ctx.uptime_ms();
  ctx.redraw();
}

void RecordScreen::leave(app::Context& ctx) noexcept {
  home_->leave(ctx);
  down_ = 0;
  tool_mask_ = 0;
  discard_until_ms_ = 0;
}

bool RecordScreen::take_on(app::Context& ctx) const noexcept {
  const State st = ctx.recorder().state();
  return st == State::Waiting || st == State::Recording;
}

// While a take is being written the kit says SAVING itself (its list asks the files service first); this adds the two states it cannot see: a take that waits or records, and one that
// is finished and not on the card (an update restarts the unit and the SDRAM with it).
const char* RecordScreen::blocker(const app::Context& ctx) const noexcept {
  const State st = ctx.recorder().state();
  if (st == State::Waiting || st == State::Recording) {
    return "RECORDING";
  }
  return st == State::Done && ctx.recorder().frames() != 0u ? "SAVE OR DISCARD TAKE" : nullptr;
}

// A word from the take on every screen's status line (Rig::take_msg), for a moment.
void RecordScreen::say(app::Context& ctx, const char* text, bool off) noexcept {
  std::strncpy(rig_->take_msg, text, sizeof rig_->take_msg - 1u);
  rig_->take_msg[sizeof rig_->take_msg - 1u] = '\0';
  rig_->take_msg_off = off;
  rig_->take_msg_until_ms = ctx.uptime_ms() + kMessageMs;
  ctx.redraw();
}

// ---- The take ---------------------------------------------------------------------------------------------------------------------------

// The envelope reads what the render has written since the last pass, a bounded slice of it.
void RecordScreen::scan(app::Context& ctx) noexcept {
  const State st = ctx.recorder().state();
  if (st != State::Recording && st != State::Done) {
    return;
  }
  const std::uint32_t have = ctx.recorder().frames();
  if (scanned_ >= have) {
    return;
  }
  const std::uint32_t to = have - scanned_ > kScanFrames ? scanned_ + kScanFrames : have;
  env_.add(g_take.v, kTakeChannels, scanned_, to);
  scanned_ = to;
}

void RecordScreen::chip(app::Context& ctx) noexcept {
  const State st = ctx.recorder().state();
  char* out = rig_->take;
  const unsigned cap = sizeof rig_->take;
  out[0] = '\0';
  if (saving_) {
    append(out, cap, "TAKE SAVING");
  } else if (st == State::Waiting) {
    append(out, cap, "TAKE ARMED");
  } else if (st == State::Recording) {
    char t[12];
    (void)clock_text(ctx.recorder().frames(), engine_rate_hz(), false, t, sizeof t);
    append(out, cap, "TAKE REC ");
    append(out, cap, t);
  } else if (st == State::Done) {
    append(out, cap, "TAKE READY");
  }
}

// The first half of the take's pass: its own job's result. The WAV job ends in Files::poll, before the app's update, and the files service keeps ONE result (status() is the last job's):
// the project service, which runs next, may issue a job at once (a step that waited on busy()) and overwrite it. So the app calls this BEFORE project_.update(), and the result read
// here is the WAV's. (The project's own result is read by its update, which runs before service() starts any WAV job.)
void RecordScreen::settle(app::Context& ctx) noexcept {
  if (saving_ && !ctx.files().busy()) {
    finish_save(ctx);
  }
}

void RecordScreen::service(app::Context& ctx) noexcept {
  app::Recorder& rec = ctx.recorder();
  if (rig_->take_msg[0] != '\0' && ctx.uptime_ms() >= rig_->take_msg_until_ms) {
    rig_->take_msg[0] = '\0';
  }
  if (saving_) {
    // the WAV is being written: settle() reads its end
  } else if (rec.state() == State::Done) {
    if (rec.frames() == 0u) {  // stopped before it began: nothing to keep
      rec.release();
      auto_save_ = false;
      save_wanted_ = false;
      scanned_ = 0;
      env_.reset(kTakeFrames);
    } else if (auto_save_ || save_wanted_) {
      issue_save(ctx);
    }
  } else {
    save_wanted_ = false;
  }
  // A BARS take records to its end by itself, but a transport that stops under it drops that count (the recorder keeps going): the take ends where the transport did.
  if (rec.state() == State::Recording && take_bars_ != 0u) {
    if (ctx.engine().playing()) {
      play_seen_ = true;
    } else if (play_seen_) {
      rec.disarm();
      play_seen_ = false;
    }
  } else if (rec.state() != State::Recording) {
    play_seen_ = false;
  }
  if ((rec.state() == State::Waiting || rec.state() == State::Recording) && take_bars_ != 0u) {
    track_tempo(ctx);
  }
  scan(ctx);
  chip(ctx);
}

// A LOAD, NEW, the tempo nudge or a tap changed the tempo after the take was armed: the bar is as long as the tempo says now. A take that waits for its bar and no longer fits the
// buffer is disarmed (the simpler of refusing it and letting it start into a full buffer), and the envelope and the bar counter go on from the new length. A take that records keeps
// going (the recorder ends it where the buffer is full).
void RecordScreen::track_tempo(app::Context& ctx) noexcept {
  const std::uint32_t mbpm = ctx.engine().tempo();
  const std::uint32_t per = bar_frames(mbpm);
  if (per == take_per_bar_) {
    return;
  }
  if (ctx.recorder().state() == State::Waiting && !bars_fit(take_bars_, mbpm)) {
    char text[40] = "DISARMED: ";
    char n[8];
    char bpm[12];
    (void)put_uint(take_bars_, n, sizeof n);
    (void)tempo_text(mbpm, bpm, sizeof bpm);
    append(text, sizeof text, n);
    append(text, sizeof text, " BARS TOO LONG AT ");
    append(text, sizeof text, bpm);
    ctx.recorder().disarm();  // Done with no frames: released by the next pass
    say(ctx, text, true);
    return;
  }
  take_per_bar_ = per;
  env_.reset(take_bars_ * per);  // (the frames already taken are read again, a slice a pass)
  scanned_ = 0;
}

bool RecordScreen::arm(app::Context& ctx, bool auto_save) noexcept {
  app::Recorder& rec = ctx.recorder();
  if (!rec.available()) {
    say(ctx, "NO RECORDER", true);
    return false;
  }
  if (!master_ && !ctx.audio_in().available()) {
    say(ctx, "NO AUDIO INPUT", true);
    return false;
  }
  const unsigned bars = kBars[bars_index_];
  const std::uint32_t mbpm = ctx.engine().tempo();
  if (!bars_fit(bars, mbpm)) {
    char text[40] = "";
    char n[8];
    (void)put_uint(bars, n, sizeof n);
    char bpm[12];
    (void)tempo_text(mbpm, bpm, sizeof bpm);
    append(text, sizeof text, n);
    append(text, sizeof text, " BARS TOO LONG AT ");
    append(text, sizeof text, bpm);
    say(ctx, text, true);
    return false;
  }
  rec.set_tap(master_ ? hal::record::Tap::Master : hal::record::Tap::Input);
  hal::record::Take t;
  t.buf = g_take.v;
  t.capacity_frames = kTakeFrames;
  t.channels = hal::record::Channels::Stereo;
  // A FREE take with the transport stopped starts at once (there is no bar to wait for); every other take waits for its bar, and Play starts it.
  t.align = bars == 0u && !ctx.engine().playing() ? hal::record::Align::Free : hal::record::Align::Bar;
  t.auto_steps = bars * kStepsPerBar;
  if (!rec.arm(t)) {
    say(ctx, "CANNOT ARM", true);
    return false;
  }
  auto_save_ = auto_save;
  save_wanted_ = false;
  take_bars_ = bars;
  take_per_bar_ = bar_frames(mbpm);
  play_seen_ = false;
  scanned_ = 0;
  env_.reset(bars != 0u ? bars * take_per_bar_ : kTakeFrames);
  discard_until_ms_ = 0;
  return true;  // (the chip and the state say ARMED)
}

void RecordScreen::arm_or_stop(app::Context& ctx, bool auto_save) noexcept {
  app::Recorder& rec = ctx.recorder();
  const State st = rec.state();
  if (st == State::Waiting || st == State::Recording) {
    rec.disarm();  // a waiting take is Done with 0 frames, a recording one ends at the next block
    if (st == State::Recording && auto_save) {
      auto_save_ = true;  // Shift + Rec again: stops and saves it
    }
    say(ctx, st == State::Waiting ? "DISARMED" : "STOPPED");
  } else if (saving_) {
    say(ctx, "SAVING", true);
  } else if (st == State::Done) {
    if (auto_save && rec.frames() != 0u) {
      save(ctx);  // Shift + Rec on a finished take saves it: also the way to try again after a save that failed
    } else {
      say(ctx, "SAVE OR DISCARD THE TAKE (F6 F7)", true);
    }
  } else {
    (void)arm(ctx, auto_save);
  }
  ctx.redraw();
}

void RecordScreen::shift_rec(app::Context& ctx) noexcept { arm_or_stop(ctx, true); }

void RecordScreen::save(app::Context& ctx) noexcept {
  app::Recorder& rec = ctx.recorder();
  if (saving_) {
    return;
  }
  if (rec.state() != State::Done || rec.frames() == 0u) {
    say(ctx, take_on(ctx) ? "STOP THE TAKE FIRST" : "NO TAKE TO SAVE", true);
    return;
  }
  save_wanted_ = true;  // started by service(), once the project service has settled its own job for this pass
  discard_until_ms_ = 0;
}

void RecordScreen::issue_save(app::Context& ctx) noexcept {
  app::Recorder& rec = ctx.recorder();
  if (ctx.files().busy()) {  // the project's step or the kit's settings: the save goes as soon as the card is free
    return;
  }
  ctx.files().set_quiet(false);  // a project step may have left the files service quiet: the WAV says SAVING and SAVED on the kit's strip
  if (rec.save(ctx.files(), "Samples", "take")) {
    saving_ = true;
    save_wanted_ = false;
    return;
  }
  save_wanted_ = false;
  auto_save_ = false;
  const char* why = ctx.files().status().text;
  say(ctx, why != nullptr && why[0] != '\0' ? why : "NOT SAVED", true);  // the take stays: F6 or Shift + Rec tries again
}

void RecordScreen::finish_save(app::Context& ctx) noexcept {
  const app::Files::Status s = ctx.files().status();
  saving_ = false;
  auto_save_ = false;
  if (s.phase == app::Files::Status::Done) {
    ctx.recorder().release();  // the file is on the card: the buffer is free again
    scanned_ = 0;
    env_.reset(kTakeFrames);
    say(ctx, "SAVED");
  } else {
    say(ctx, s.text != nullptr && s.text[0] != '\0' ? s.text : "SAVE FAILED", true);  // the take stays: F6 or Shift + Rec tries again
  }
}

void RecordScreen::discard(app::Context& ctx) noexcept {
  app::Recorder& rec = ctx.recorder();
  if (saving_) {
    say(ctx, "SAVING", true);
    return;
  }
  if (rec.state() != State::Done) {
    say(ctx, take_on(ctx) ? "STOP THE TAKE FIRST" : "NO TAKE TO DISCARD", true);
    return;
  }
  const std::uint64_t now = ctx.uptime_ms();
  if (now >= discard_until_ms_ || discard_until_ms_ == 0u) {  // the first press asks, the second within 5 s drops the take
    discard_until_ms_ = now + kDiscardAskMs;
    say(ctx, "DISCARD? PRESS F7 AGAIN", true);
    return;
  }
  discard_until_ms_ = 0;
  rec.release();
  auto_save_ = false;
  save_wanted_ = false;
  scanned_ = 0;
  env_.reset(kTakeFrames);
  say(ctx, "DISCARDED");
}

// ---- The settings -----------------------------------------------------------------------------------------------------------------------

void RecordScreen::pick_source(app::Context& ctx, int dir) noexcept {
  if (take_on(ctx)) {
    say(ctx, "STOP THE TAKE FIRST", true);
    return;
  }
  app::AudioIn& in = ctx.audio_in();
  const RecSource cur = master_ ? RecSource::Master : source_of(in.source());
  const RecSource next = step_source(cur, dir, in.available(), ctx.radio().available(), ctx.usb_audio().available());
  if (next == cur) {
    if (!in.available()) {
      say(ctx, "NO AUDIO INPUT", true);
    }
    return;
  }
  if (next == RecSource::Master) {
    master_ = true;
    return;
  }
  master_ = false;
  if (in.selected()) {
    gain_ = in.gain();  // the kit's (saved) gain is the one that stays
  }
  if (!in.select(input_source(next), gain_)) {
    say(ctx, "INPUT NOT ANSWERING", true);  // the kit keeps the choice and tries again
  }
}

// `wrap`: a tap of F3 goes round the list; the encoder stops at its ends.
void RecordScreen::pick_bars(app::Context& ctx, int delta, bool wrap) noexcept {
  if (take_on(ctx)) {
    say(ctx, "STOP THE TAKE FIRST", true);
    return;
  }
  bars_index_ = wrap && bars_index_ + 1u >= kBarChoices ? 0u : step_bars_index(bars_index_, delta);
}

void RecordScreen::change_gain(app::Context& ctx, int delta) noexcept {
  const int next = static_cast<int>(gain_) + delta;
  gain_ = static_cast<unsigned>(next < 0 ? 0 : (next > 100 ? 100 : next));
  if (!master_ && ctx.audio_in().selected()) {
    (void)ctx.audio_in().set_gain(gain_);
  }
}

void RecordScreen::change_monitor(app::Context& ctx, int delta) noexcept {
  const int next = static_cast<int>(ctx.audio_in().monitor()) + delta;
  ctx.audio_in().monitor(static_cast<unsigned>(next < 0 ? 0 : (next > 100 ? 100 : next)));
}

// ---- Input ------------------------------------------------------------------------------------------------------------------

void RecordScreen::input(app::Context& ctx, const Input& in) noexcept {
  if (in.kind == core::InputKind::Pad) {
    (void)home_->input(ctx, in);
  } else if (in.kind == core::InputKind::Encoder) {
    turn_input(ctx, in);
  } else if (in.kind == core::InputKind::Button) {
    button_input(ctx, in);
  }
}

void RecordScreen::turn_input(app::Context& ctx, const Input& in) noexcept {
  const int d = in.delta();
  if (d == 0) {
    return;
  }
  if (home_->turn(ctx, d, in.shift, Entry{})) {  // a held step takes the turn (nothing here can lock)
    return;
  }
  // A key held (F2..F5) takes the turn without a latch; else the latched one.
  unsigned target = owner_;
  for (unsigned k = kKeySource; k <= kKeyMonitor; ++k) {
    if ((down_ >> k & 1u) != 0u) {
      target = k;
      break;
    }
  }
  switch (target) {
    case kKeySource:
      pick_source(ctx, d);
      break;
    case kKeyBars:
      pick_bars(ctx, d, false);
      break;
    case kKeyGain:
      change_gain(ctx, in.shift ? d * 5 : d);
      break;
    case kKeyMonitor:
      change_monitor(ctx, in.shift ? d * 5 : d);
      break;
    default:
      break;
  }
  ctx.redraw();
}

void RecordScreen::key_press(app::Context& ctx, unsigned k) noexcept {
  switch (k) {
    case kKeyArm:
      arm_or_stop(ctx, false);
      return;
    case kKeySource:
      owner_ = k;
      pick_source(ctx, 1);
      break;
    case kKeyBars:
      owner_ = k;
      pick_bars(ctx, 1, true);
      break;
    case kKeyGain:
      owner_ = k;
      break;
    case kKeyMonitor:
      owner_ = k;
      ctx.audio_in().monitor(ctx.audio_in().monitor() != 0u ? 0u : app::AudioIn::kMonitorOn);
      break;
    case kKeySave:
      save(ctx);
      break;
    case kKeyDiscard:
      discard(ctx);
      break;
    default:
      break;
  }
  ctx.redraw();
}

void RecordScreen::button_input(app::Context& ctx, const Input& in) noexcept {
  const Button b = in.which();
  if (app::is_fkey(b)) {
    const unsigned k = app::fkey_index(b);
    if (in.type == Input::Type::Press) {
      if (in.shift) {  // the Shift row is empty: the key does nothing, and its release is not an action
        tool_mask_ = static_cast<std::uint8_t>(tool_mask_ | (1u << k));
        ctx.redraw();
        return;
      }
      down_ = static_cast<std::uint8_t>(down_ | (1u << k));
      key_press(ctx, k);
    } else if (in.type == Input::Type::Release) {
      down_ = static_cast<std::uint8_t>(down_ & ~(1u << k));
      tool_mask_ = static_cast<std::uint8_t>(tool_mask_ & ~(1u << k));
      ctx.redraw();
    }
    return;
  }
  if (b == Button::Note || b == Button::Instr || b == Button::Vol || b == Button::Fx) {
    return;  // the colour buttons do nothing on RECORD (UX-303 2.3)
  }
  (void)home_->input(ctx, in);  // the arrows, Copy / Insert / Delete, and the plain Rec (step record)
}

// ---- Update, soft keys and LEDs -----------------------------------------------------------------------------------------------

// The meters, from the main loop's passes (MASTER's rules): the master's peak of the render's last block, and the input's through the kit's peaks (asked only while this screen is up).
void RecordScreen::update(app::Context& ctx) noexcept {
  const dsp::EngineStats s = ctx.engine().stats();
  unsigned fresh[2] = {meter_norm(s.peak_l), meter_norm(s.peak_r)};
  unsigned in_fresh[2] = {0u, 0u};
  if (ctx.audio_in().available()) {
    const dsp::InputPeaks p = ctx.audio_in().peaks();
    in_fresh[0] = meter_norm(p.l);
    in_fresh[1] = meter_norm(p.r);
  }
  const std::uint64_t now = ctx.uptime_ms();
  const std::uint64_t dt = now > meter_ms_ ? now - meter_ms_ : 0u;
  if (dt < kFallStepMs) {
    for (unsigned c = 0; c < 2u; ++c) {
      level_[c] = level_[c] > fresh[c] ? level_[c] : fresh[c];
      peak_[c] = peak_[c] > fresh[c] ? peak_[c] : fresh[c];
      in_level_[c] = in_level_[c] > in_fresh[c] ? in_level_[c] : in_fresh[c];
      in_peak_[c] = in_peak_[c] > in_fresh[c] ? in_peak_[c] : in_fresh[c];
    }
    return;
  }
  meter_ms_ = now;
  const unsigned step = dt > 1000u ? 1000u : static_cast<unsigned>(dt);
  for (unsigned c = 0; c < 2u; ++c) {
    level_[c] = meter_fall(level_[c], fresh[c], step, kLevelFallPerS);
    peak_[c] = meter_fall(peak_[c], fresh[c], step, kPeakFallPerS);
    in_level_[c] = meter_fall(in_level_[c], in_fresh[c], step, kLevelFallPerS);
    in_peak_[c] = meter_fall(in_peak_[c], in_fresh[c], step, kPeakFallPerS);
  }
}

// The Shift row is empty: no labels (the knob row carries the names).
void RecordScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  (void)ctx;
  (void)out;
}

void RecordScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept { home_->leds(ctx, leds); }

void RecordScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  (void)ctx;
  help_texts::fill(h, help_texts::text(help_texts::Id::Record));
  home_->help_pads(h);
}

// ---- The view -----------------------------------------------------------------------------------------------------------------

void RecordScreen::enc_text(char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  unsigned target = owner_;
  for (unsigned k = kKeySource; k <= kKeyMonitor; ++k) {
    if ((down_ >> k & 1u) != 0u) {
      target = k;
      break;
    }
  }
  append(out, cap, kKeyNames[target < 5u ? target : kKeyBars]);
}

void RecordScreen::fill_view(app::Context& ctx, RecordView& view) noexcept {
  app::Recorder& rec = ctx.recorder();
  const State st = rec.state();
  const std::uint32_t mbpm = ctx.engine().tempo();
  const unsigned bars = kBars[bars_index_];
  const bool active = st == State::Waiting || st == State::Recording;
  const bool playing = ctx.engine().playing();
  const unsigned shown_bars = active || st == State::Done ? take_bars_ : bars;

  view.fits = bars_fit(bars, mbpm);
  view.source = source_name(master_, ctx.audio_in().source());
  (void)bars_text(shown_bars, view.bars, sizeof view.bars);
  view.input_meters = !master_ && ctx.audio_in().available();
  for (unsigned c = 0; c < 2u; ++c) {
    view.in[c] = in_level_[c];
    view.in_peak[c] = in_peak_[c];
    view.out[c] = level_[c];
    view.out_peak[c] = peak_[c];
  }
  view.bar[0] = '\0';
  view.line[0] = '\0';
  const std::uint32_t frames = st == State::Idle || st == State::Waiting ? 0u : rec.frames();
  (void)clock_text(frames, engine_rate_hz(), true, view.clock, sizeof view.clock);
  if (saving_) {
    view.state = "SAVING";
    append(view.line, sizeof view.line, "WRITING THE WAV TO THE CARD");
  } else if (st == State::Waiting) {
    view.state = "ARMED";
    append(view.line, sizeof view.line, playing || take_bars_ == 0u ? "STARTS AT THE NEXT BAR" : "PRESS PLAY: STARTS AT BAR 1");
  } else if (st == State::Recording) {
    view.state = "REC";
    if (take_bars_ != 0u) {
      char n[8];
      append(view.bar, sizeof view.bar, "BAR ");
      (void)put_uint(bar_of(frames, take_per_bar_, take_bars_), n, sizeof n);
      append(view.bar, sizeof view.bar, n);
      append(view.bar, sizeof view.bar, " / ");
      (void)put_uint(take_bars_, n, sizeof n);
      append(view.bar, sizeof view.bar, n);
    }
    append(view.line, sizeof view.line, take_bars_ != 0u ? "F1 STOPS IT EARLY" : "F1 STOPS . THE BUFFER FILLS AT 0:30");
  } else if (st == State::Done) {
    view.state = "DONE";
    append(view.line, sizeof view.line, rec.overrun() != 0u ? "BUFFER FULL . F6 SAVE . F7 DISCARD" : "F6 SAVE . F7 DISCARD");
  } else {
    view.state = "IDLE";
    if (!view.fits) {
      char n[8];
      char bpm[12];
      (void)put_uint(bars, n, sizeof n);
      (void)tempo_text(mbpm, bpm, sizeof bpm);
      append(view.line, sizeof view.line, n);
      append(view.line, sizeof view.line, " BARS TOO LONG AT ");
      append(view.line, sizeof view.line, bpm);
      append(view.line, sizeof view.line, " BPM");
    } else if (bars == 0u && !playing) {
      append(view.line, sizeof view.line, "F1 STARTS AT ONCE");
    } else {
      append(view.line, sizeof view.line, playing ? "F1 ARMS: STARTS AT THE NEXT BAR" : "F1 ARMS: PLAY STARTS AT BAR 1");
    }
  }

  const app::Files& files = ctx.files();
  const app::Files::Card card = files.card();
  view.dest_off = card != app::Files::Card::Ready;
  view.dest[0] = '\0';
  switch (card) {
    case app::Files::Card::Ready:
      append(view.dest, sizeof view.dest, "Samples/take_NNN.wav");
      break;
    case app::Files::Card::ReadOnly:
      append(view.dest, sizeof view.dest, "CARD IS WRITE PROTECTED: CANNOT SAVE");
      break;
    case app::Files::Card::Unsupported:
      append(view.dest, sizeof view.dest, "CARD NOT SUPPORTED: CANNOT SAVE");
      break;
    case app::Files::Card::OnComputer:
      append(view.dest, sizeof view.dest, "CARD ON THE COMPUTER: CANNOT SAVE");
      break;
    case app::Files::Card::InUse:
      append(view.dest, sizeof view.dest, "CARD IN USE: CANNOT SAVE");
      break;
    case app::Files::Card::None:
      append(view.dest, sizeof view.dest, "NO CARD: CANNOT SAVE");
      break;
  }

  // The knob row.
  for (view::Knob& k : view.knob) {
    k = view::Knob{};
    k.blank = true;
  }
  const auto box = [&](unsigned i, const char* label, const char* value, const char* unit, float norm) -> view::Knob& {
    static constexpr const char* kFKeys[view::kKnobs] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
    view::Knob& k = view.knob[i];
    k = view::Knob{};
    k.fkey = kFKeys[i];
    k.label = label;
    std::strncpy(k.value, value, sizeof k.value - 1u);
    k.value[sizeof k.value - 1u] = '\0';
    k.unit = unit;
    k.norm = norm;
    k.held = (down_ >> i & 1u) != 0u;
    return k;
  };
  box(kKeyArm, active ? "STOP" : "ARM", view.state, "", -1.0f);
  box(kKeySource, "SOURCE", view.source, "", -1.0f).focus = owner_ == kKeySource;
  char bars_v[8];
  (void)bars_text(bars, bars_v, sizeof bars_v);
  box(kKeyBars, "BARS", bars_v, !view.fits ? "TOO LONG" : (bars != 0u ? "BARS" : ""), -1.0f).focus = owner_ == kKeyBars;
  char num[8];
  (void)put_uint(gain_, num, sizeof num);
  box(kKeyGain, "GAIN", num, "%", static_cast<float>(gain_) / 100.0f).focus = owner_ == kKeyGain;
  const unsigned mon = ctx.audio_in().monitor();
  (void)put_uint(mon, num, sizeof num);
  box(kKeyMonitor, "MONITOR", mon != 0u ? num : "OFF", mon != 0u ? "%" : "", static_cast<float>(mon) / 100.0f).focus = owner_ == kKeyMonitor;
  box(kKeySave, "SAVE", saving_ ? "SAVING" : (st == State::Done ? "READY" : "--"), "", -1.0f);
  box(kKeyDiscard, "DISCARD", discard_until_ms_ != 0u && ctx.uptime_ms() < discard_until_ms_ ? "SURE?" : "--", "", -1.0f);
}

void RecordScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  home_->clamp_cursor();
  frame::StatusView status;
  home_->status(status);
  status.save_failed = rig_->project.failed;
  enc_text(status.enc, sizeof status.enc);
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);

  fill_view(ctx, view_);
  const State st = ctx.recorder().state();
  view::translucent(r, 72, [&] {
    r.panel(left_card(area), Color::Empty);
    r.panel(right_card(area), Color::Empty);
    r.panel(take_panel(area), Color::Empty);
  });
  draw_state_card(r, left_card(area), view_);
  draw_meter_card(r, right_card(area), view_);
  draw_envelope(r, take_panel(area), env_, scanned_, st == State::Idle ? kBars[bars_index_] : take_bars_, st == State::Recording, st == State::Recording || st == State::Done);

  const Rect strip{area.x, area.y + area.h - app::kKnobStripH, area.w, app::kKnobStripH};
  char line[128];
  home_->pads_line(ctx, line, sizeof line);
  frame::draw_pads_line(r, Rect{strip.x, strip.y, strip.w, frame::kPadsLineH}, line);
  view::draw_knobs(r, strip, view_.knob);
}

}  // namespace o3_303
