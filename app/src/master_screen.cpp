// SPDX-License-Identifier: GPL-3.0-or-later
#include "master_screen.h"

#include "help_texts.h"
#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/dsp/engine.h>
#include <o3sdk/ui/components.h>
#include <o3_303/perform.h>

#include <cmath>
#include <cstring>

namespace o3_303 {

namespace {

using namespace o3sdk;
using app::Button;
using app::Input;
using ui::Color;
using ui::Face;
using ui::Rect;

// F1 TEMPO and F4 MASTER on the keys the latch can reach; F2 TAP and F3 NUDGE are the screen's own (blank here, so the keys pass them by).
constexpr Page kPage = {"MASTER", {Entry{Item::Tempo, 0, nullptr}, Entry{}, Entry{}, Entry{Item::Master, 0, nullptr}, Entry{}, Entry{}, Entry{}, Entry{}}};

constexpr unsigned kKeyTempo = 0;
constexpr unsigned kKeyTap = 1;
constexpr unsigned kKeyNudge = 2;

// The meters: the level falls a whole scale in half a second, the peak tick in two.
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

// The drawing's geometry (the area is the screen's, under the kit's header).
constexpr int kCardY = frame::kStatusH + 8;
constexpr int kGap = 8;
constexpr int kLeftW = 440;

// The cards fill the room between the status line and the knob strip.
int card_h(const Rect& area) noexcept { return area.h - kCardY - app::kKnobStripH - kGap; }
Rect left_card(const Rect& area) noexcept { return Rect{area.x + kGap, area.y + kCardY, kLeftW, card_h(area)}; }
Rect right_card(const Rect& area) noexcept {
  return Rect{area.x + kGap + kLeftW + kGap, area.y + kCardY, area.w - kLeftW - 3 * kGap, card_h(area)};
}

// The tempo card: the tempo large, the nudge, the clock.
void draw_tempo_card(ui::Renderer& r, const Rect& card, const MasterView& view) noexcept {
  const ui::Theme& theme = r.theme();
  const int x = card.x + 16;
  r.text(x, card.y + 10, "TEMPO", Color::TextDim, Face::Label);
  r.text(x, ui::caps_y(theme, Face::Display, card.y + 30, 40), view.tempo, Color::Text, Face::Display);
  const int bpm_x = x + static_cast<int>(std::strlen(view.tempo)) * 16 + 8;
  r.text(bpm_x, ui::caps_y(theme, Face::Label, card.y + 30, 40), "BPM", Color::TextDim, Face::Label);
  if (view.taps[0] != '\0') {  // the tap tempo's measure: how many taps it has so far
    char chip[16] = "TAP ";
    append(chip, sizeof chip, view.taps);
    const int w = r.text_width(chip, Face::Label) + 12;
    const int cx = card.x + card.w - 16 - w;
    r.panel(Rect{cx, card.y + 12, w, 18}, Color::Sel);
    r.text(cx + 6, ui::caps_y(theme, Face::Label, card.y + 12, 18), chip, Color::SelText, Face::Label);
  }
  const int nudge_y = card.y + card.h / 2 - 6;
  const int clock_y = nudge_y + 56;
  r.text(x, nudge_y + 4, "NUDGE", Color::TextDim, Face::Label);
  r.text(x + 96, ui::caps_y(theme, Face::Data, nudge_y, 24), view.nudge, view.nudged ? Color::Accent : Color::Text, Face::Data);
  r.text(x, clock_y + 4, "CLOCK", Color::TextDim, Face::Label);
  r.text(x + 96, ui::caps_y(theme, Face::Data, clock_y, 24), view.clock, Color::Text, Face::Data);
  const int hint_x = x + 96 + r.text_width(view.clock, Face::Data) + 12;
  r.text(hint_x, ui::caps_y(theme, Face::Micro, clock_y, 24), "(MIDI: CONFIG)", Color::TextDim, Face::Micro);
}

// The output card: the master's peaks, left and right, on a dB scale.
void draw_output_card(ui::Renderer& r, const Rect& card, const MasterView& view) noexcept {
  const ui::Theme& theme = r.theme();
  const int x = card.x + 16;
  r.text(x, card.y + 10, "MASTER", Color::TextDim, Face::Label);
  static constexpr const char* kSide[2] = {"L", "R"};
  const int mx = x + 24;
  const int mw = card.w - 16 - 24 - 16;
  const int mh = (card.h - 40 - 40) / 2 - 12;  // two meters with a gap, and the scale under them
  for (unsigned c = 0; c < 2u; ++c) {
    const int y = card.y + 40 + static_cast<int>(c) * (mh + 12);
    r.text(x, ui::caps_y(theme, Face::Data, y, mh), kSide[c], Color::Text, Face::Data);
    ui::meter(r, Rect{mx, y, mw, mh}, view.level[c], view.peak[c], kMeterMax, Color::ChromeDim, Color::Pane, Color::Play, Color::Text);
  }
  // The scale under them: -48, -24 and 0 dB at their places.
  const int ty = card.y + 40 + 2 * (mh + 12);
  r.text(mx, ty, "-48", Color::TextDim, Face::Micro);
  r.text(mx + mw / 2 - 9, ty, "-24", Color::TextDim, Face::Micro);
  r.text(mx + mw - 18, ty, "0 dB", Color::TextDim, Face::Micro);
}

}  // namespace

// ---- The view logic ---------------------------------------------------------------------------------------------------------------------------

unsigned tempo_text(std::uint32_t mbpm, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  const unsigned tenths = static_cast<unsigned>((static_cast<std::uint64_t>(mbpm) + 50u) / 100u);
  out[0] = '\0';
  char whole[12];
  (void)put_uint(tenths / 10u, whole, sizeof whole);
  append(out, cap, whole);
  const char frac[3] = {'.', static_cast<char>('0' + tenths % 10u), '\0'};
  append(out, cap, frac);
  return static_cast<unsigned>(std::strlen(out));
}

unsigned nudge_text(int pct, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  const int p = clamp_nudge(pct);
  out[0] = '\0';
  const char sign[2] = {p < 0 ? '-' : '+', '\0'};
  append(out, cap, sign);
  char digits[4];
  (void)put_uint(static_cast<unsigned>(p < 0 ? -p : p), digits, sizeof digits);
  append(out, cap, digits);
  append(out, cap, " %");
  return static_cast<unsigned>(std::strlen(out));
}

unsigned meter_norm(float peak) noexcept {
  if (!(peak > 0.0f)) {  // silence, and a NaN
    return 0;
  }
  const float db = 20.0f * std::log10(peak);
  if (db <= kMeterFloorDb) {
    return 0;
  }
  if (db >= 0.0f) {
    return kMeterMax;
  }
  return static_cast<unsigned>((db - kMeterFloorDb) / -kMeterFloorDb * static_cast<float>(kMeterMax) + 0.5f);
}

unsigned meter_fall(unsigned held, unsigned now, unsigned dt_ms, unsigned fall_per_s) noexcept {
  const std::uint64_t drop = static_cast<std::uint64_t>(fall_per_s) * dt_ms / 1000u;
  const unsigned fell = drop >= held ? 0u : held - static_cast<unsigned>(drop);
  return now > fell ? now : fell;
}

void fill_tap_knob(unsigned taps, view::Knob& out) noexcept {
  out = view::Knob{};
  out.fkey = "F2";
  out.label = "TAP";
  if (taps == 0u) {
    std::strcpy(out.value, "--");
  } else {
    (void)put_uint(taps, out.value, sizeof out.value);
  }
  out.unit = taps == 1u ? "TAP" : "TAPS";
  out.norm = -1.0f;  // an action: no fill
}

void fill_nudge_knob(int pct, bool held, view::Knob& out) noexcept {
  out = view::Knob{};
  out.fkey = "F3";
  out.label = "NUDGE";
  (void)nudge_text(pct, out.value, sizeof out.value);
  out.unit = "";
  out.norm = (static_cast<float>(clamp_nudge(pct)) + static_cast<float>(kNudgeMaxPct)) / static_cast<float>(2 * kNudgeMaxPct);
  out.held = held;
}

// ---- Setup and screen hooks ------------------------------------------------------------------------------------------------------------------

void MasterScreen::init(Rig& rig, HomeMap& home) noexcept {
  // Every piece of state starts afresh: the app object outlives a run in the simulator's tests.
  rig_ = &rig;
  controls_ = &rig.controls;
  home_ = &home;
  keys_ = PageKeys{};
  keys_.set_table(&kPage, 1);
  master_down_ = false;
  master_used_ = false;
  master_ms_ = 0;
  nudge_pct_ = 0;
  nudge_base_ = 0;
  restore_pending_ = false;
  rig.tempo_base_mbpm = 0;
  f3_down_ = false;
  tap_tempo_.reset();
  tool_mask_ = 0;
  level_[0] = level_[1] = 0;
  peak_[0] = peak_[1] = 0;
  meter_ms_ = 0;
  view_ = MasterView{};
}

void MasterScreen::enter(app::Context& ctx) noexcept {
  home_->enter(ctx);
  keys_.set_table(&kPage, 1);  // the encoder enters latched on TEMPO (D5)
  f3_down_ = false;
  tool_mask_ = 0;
  meter_ms_ = ctx.uptime_ms();
  ctx.redraw();
}

void MasterScreen::leave(app::Context& ctx) noexcept {
  home_->leave(ctx);
  keys_.release_all();
  f3_down_ = false;
  if (!master_down_) {  // F3's nudge ends with the screen (its release would reach another one); the button's goes on until its own release
    nudge_end(ctx);
  }
  tool_mask_ = 0;
}

// The meters, from the main loop's passes: the peak of the render's last block (the engine keeps no longer one: a peak between two passes can be missed),
// taken on every pass, with a fall in steps of kFallStepMs (the fall is whole units: a step of a millisecond would round it away).
void MasterScreen::update(app::Context& ctx) noexcept {
  home_->knob_latched(keys_.focused());
  const dsp::EngineStats s = ctx.engine().stats();
  const unsigned fresh[2] = {meter_norm(s.peak_l), meter_norm(s.peak_r)};
  const std::uint64_t now = ctx.uptime_ms();
  const std::uint64_t dt = now > meter_ms_ ? now - meter_ms_ : 0u;
  if (dt < kFallStepMs) {
    for (unsigned c = 0; c < 2u; ++c) {
      level_[c] = level_[c] > fresh[c] ? level_[c] : fresh[c];
      peak_[c] = peak_[c] > fresh[c] ? peak_[c] : fresh[c];
    }
    return;
  }
  meter_ms_ = now;
  const unsigned step = dt > 1000u ? 1000u : static_cast<unsigned>(dt);
  for (unsigned c = 0; c < 2u; ++c) {
    level_[c] = meter_fall(level_[c], fresh[c], step, kLevelFallPerS);
    peak_[c] = meter_fall(peak_[c], fresh[c], step, kPeakFallPerS);
  }
}

// ---- The global Master ------------------------------------------------------------------------------------------------------------------

void MasterScreen::master_press(app::Context& ctx, bool shift) noexcept {
  master_down_ = true;
  master_used_ = shift;
  master_ms_ = ctx.uptime_ms();
  if (shift) {
    tap(ctx);  // Shift + Master: one tap of the tap tempo, from any screen; the release opens nothing
  }
}

bool MasterScreen::master_turn(app::Context& ctx, int detents) noexcept {
  if (!master_down_) {
    return false;
  }
  master_used_ = true;
  nudge_turn(ctx, detents);
  return true;
}

void MasterScreen::master_release(app::Context& ctx) noexcept {
  if (!master_down_) {
    return;
  }
  master_down_ = false;
  if (!f3_down_) {
    nudge_end(ctx);
  }
  if (!master_used_ && ctx.uptime_ms() - master_ms_ < kHoldMs) {  // a tap: MASTER, the encoder on TEMPO
    open(ctx);
  }
  ctx.redraw();
}

void MasterScreen::open(app::Context& ctx) noexcept {
  if (ctx.screen() == this) {
    keys_.set_table(&kPage, 1);  // already here: the encoder goes back to TEMPO
    ctx.redraw();
  } else {
    ctx.show(*this);
  }
}

void MasterScreen::tap(app::Context& ctx) noexcept {
  if (ctx.engine().following()) {  // a MIDI clock leads: the own tempo is silent, a tap would say nothing true
    home_->toast(ctx, "MIDI CLOCK LEADS", true);
    ctx.redraw();
    return;
  }
  service(ctx);
  std::uint32_t mbpm = 0;
  char text[24] = "";
  if (tap_tempo_.tap(ctx.uptime_ms(), mbpm)) {
    if (nudge_pct_ != 0) {  // a tap under a nudge: the tapped tempo is the new base, and the nudge goes on from it
      nudge_base_ = mbpm;
      rig_->tempo_base_mbpm = mbpm;
      (void)ctx.engine().set_tempo(nudged_clamped(nudge_base_, nudge_pct_));
    } else if (ctx.engine().set_tempo(mbpm)) {
      forget_base();  // (it also stands in for a restore that had not reached the render)
    }
    char bpm[12];
    (void)tempo_text(mbpm, bpm, sizeof bpm);
    append(text, sizeof text, "TAP ");
    append(text, sizeof text, bpm);
  } else {
    append(text, sizeof text, "TAP");  // the first of a measure: nothing to say yet
  }
  home_->toast(ctx, text);
  ctx.redraw();
}

std::uint32_t MasterScreen::nudged_clamped(std::uint32_t base, int pct) noexcept {
  const std::uint32_t mbpm = nudged_tempo(base, pct);
  return mbpm < dsp::kMinTempoMilliBpm ? static_cast<std::uint32_t>(dsp::kMinTempoMilliBpm)
                                       : (mbpm > dsp::kMaxTempoMilliBpm ? static_cast<std::uint32_t>(dsp::kMaxTempoMilliBpm) : mbpm);
}

void MasterScreen::forget_base() noexcept {
  restore_pending_ = false;
  rig_->tempo_base_mbpm = 0;
}

// The tempo moved `detents` percent from where the nudge began (one percent a detent, at most three either way). While a MIDI clock leads the own
// tempo is silent: nothing moves and the toast says why. A full command ring refuses the change: the nudge is dropped and the base is put back as
// soon as the ring has room (service()).
void MasterScreen::nudge_turn(app::Context& ctx, int detents) noexcept {
  if (ctx.engine().following()) {
    home_->toast(ctx, "MIDI CLOCK LEADS", true);
    ctx.redraw();
    return;
  }
  service(ctx);
  if (restore_pending_) {
    return;  // the last restore has not reached the render yet
  }
  const int next = clamp_nudge(nudge_pct_ + detents);
  if (next == nudge_pct_) {
    return;
  }
  const std::uint32_t base = nudge_pct_ == 0 ? ctx.engine().own_tempo() : nudge_base_;  // not a MIDI master's: the release puts this back as the own tempo
  nudge_base_ = base;
  rig_->tempo_base_mbpm = base;  // a save during the nudge keeps the base (project.cpp gather())
  if (!ctx.engine().set_tempo(nudged_clamped(base, next))) {
    nudge_pct_ = 0;
    restore_pending_ = true;  // the engine stored the value it refused: put the base back
    ctx.redraw();
    return;
  }
  nudge_pct_ = next;
  char text[16] = "NUDGE ";
  char pct[8];
  (void)nudge_text(nudge_pct_, pct, sizeof pct);
  append(text, sizeof text, pct);
  home_->toast(ctx, text);  // the other screens' status lines say it
  ctx.redraw();
}

// The base tempo back (the release of Master or of F3, or the screen going away with F3 held).
void MasterScreen::nudge_end(app::Context& ctx) noexcept {
  if (nudge_pct_ != 0) {
    nudge_pct_ = 0;
    if (ctx.engine().set_tempo(nudge_base_)) {
      forget_base();
    } else {
      restore_pending_ = true;  // a full command ring: the render keeps the nudged tempo until service() puts the base back
    }
    ctx.redraw();
  }
}

// Every pass of the main loop, whatever screen is up (the app calls it): a restore that the command ring refused is tried again.
void MasterScreen::service(app::Context& ctx) noexcept {
  if (restore_pending_ && ctx.engine().set_tempo(nudge_base_)) {
    forget_base();
    ctx.redraw();
  }
}

// ---- Input ------------------------------------------------------------------------------------------------------------------

void MasterScreen::input(app::Context& ctx, const Input& in) noexcept {
  if (in.kind == core::InputKind::Pad) {
    (void)home_->input(ctx, in);
  } else if (in.kind == core::InputKind::Encoder) {
    turn_input(ctx, in);
  } else if (in.kind == core::InputKind::Button) {
    button_input(ctx, in);
  }
}

void MasterScreen::turn_input(app::Context& ctx, const Input& in) noexcept {
  if (in.delta() == 0) {
    return;
  }
  if (f3_down_) {  // F3 held: the nudge, as with Master held
    nudge_turn(ctx, in.delta());
    return;
  }
  if (home_->turn(ctx, in.delta(), in.shift, keys_.focused())) {  // a held step takes the turn (a lock the tempo cannot have: the home map says so)
    keys_.mark_turned();
    return;
  }
  (void)keys_.input(ctx, in, *controls_);
}

void MasterScreen::button_input(app::Context& ctx, const Input& in) noexcept {
  const Button b = in.which();
  if (app::is_fkey(b)) {
    const unsigned k = app::fkey_index(b);
    if (in.type == Input::Type::Press) {
      if (in.shift) {  // the Shift row is empty: the key does nothing, and its release is not the page's
        tool_mask_ |= 1u << k;
        ctx.redraw();
        return;
      }
      if (k == kKeyTap) {
        tap(ctx);
        return;
      }
      if (k == kKeyNudge) {
        f3_down_ = true;
        ctx.redraw();
        return;
      }
    } else if (in.type == Input::Type::Release) {
      if ((tool_mask_ >> k & 1u) != 0u) {
        tool_mask_ &= ~(1u << k);
        return;
      }
      if (k == kKeyNudge) {
        f3_down_ = false;
        if (!master_down_) {
          nudge_end(ctx);
        }
        ctx.redraw();
        return;
      }
      if (k == kKeyTap) {
        return;
      }
    }
    (void)keys_.input(ctx, in, *controls_);
    return;
  }
  if (b == Button::Note || b == Button::Instr || b == Button::Vol || b == Button::Fx) {
    return;  // the colour buttons do nothing on MASTER (UX-303 2.3)
  }
  (void)home_->input(ctx, in);  // the arrows, Copy / Insert / Delete, Rec
}

// ---- Soft keys and LEDs -----------------------------------------------------------------------------------------------------

// The Shift row is empty: no labels (the knob row carries the names).
void MasterScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  (void)ctx;
  (void)out;
}

void MasterScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept { home_->leds(ctx, leds); }

// HELP: MASTER's texts (the Shift row is empty) and the home map's pads.
void MasterScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  (void)ctx;
  help_texts::fill(h, help_texts::text(help_texts::Id::Master));
  home_->help_pads(h);
}

// ---- The view -----------------------------------------------------------------------------------------------------------------

// "F1 TEMPO": the key the encoder drives; "F3 NUDGE" while it is held, "NUDGE" while the Master button is.
void MasterScreen::enc_text(app::Context& ctx, char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  if (f3_down_) {
    append(out, cap, "F3 NUDGE");
    return;
  }
  if (master_down_) {
    append(out, cap, "TEMPO NUDGE");
    return;
  }
  char fk[4] = {'F', static_cast<char>('1' + keys_.focus()), ' ', '\0'};
  append(out, cap, fk);
  append(out, cap, controls_->label(ctx.engine(), keys_.focused()));
}

void MasterScreen::fill_view(app::Context& ctx, MasterView& view) noexcept {
  const dsp::Engine& e = ctx.engine();
  const Page& pg = keys_.page();
  for (unsigned k = 0; k < kKeys; ++k) {
    view::Knob& kn = view.knob[k];
    if (k == kKeyTap) {
      fill_tap_knob(tap_tempo_.count_at(ctx.uptime_ms()), kn);
    } else if (k == kKeyNudge) {
      fill_nudge_knob(nudge_pct_, f3_down_, kn);
    } else {
      frame::fill_knob(ctx, *controls_, k, pg.entry[k], kn);
    }
    if (kn.blank) {
      continue;
    }
    kn.focus = k == keys_.focus() && k != kKeyTap && k != kKeyNudge;
    if (k != kKeyNudge) {
      kn.held = (keys_.held() >> k & 1u) != 0u;
    }
  }
  (void)tempo_text(e.tempo(), view.tempo, sizeof view.tempo);
  (void)nudge_text(nudge_pct_, view.nudge, sizeof view.nudge);
  view.nudged = nudge_pct_ != 0;
  view.clock = e.following() ? "MIDI" : "INTERNAL";
  view.taps[0] = '\0';
  const unsigned taps = tap_tempo_.count_at(ctx.uptime_ms());  // a stale measure shows nothing
  if (taps != 0u) {
    (void)put_uint(taps, view.taps, sizeof view.taps);
  }
  for (unsigned c = 0; c < 2u; ++c) {
    view.level[c] = level_[c];
    view.peak[c] = peak_[c];
  }
}

void MasterScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  home_->clamp_cursor();
  frame::StatusView status;
  home_->status(status);
  status.save_failed = rig_->project.failed;
  enc_text(ctx, status.enc, sizeof status.enc);
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);

  fill_view(ctx, view_);
  view::translucent(r, 72, [&] {
    r.panel(left_card(area), Color::Empty);
    r.panel(right_card(area), Color::Empty);
  });
  draw_tempo_card(r, left_card(area), view_);
  draw_output_card(r, right_card(area), view_);

  const Rect strip{area.x, area.y + area.h - app::kKnobStripH, area.w, app::kKnobStripH};
  char line[128];
  home_->pads_line(ctx, line, sizeof line);
  frame::draw_pads_line(r, Rect{strip.x, strip.y, strip.w, frame::kPadsLineH}, line);
  view::draw_knobs(r, strip, view_.knob);
}

}  // namespace o3_303
