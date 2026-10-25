// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_screen.h"

#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>
#include <o3_303/seq/ops.h>

#include <cstring>

namespace o3_303 {

namespace {

using namespace o3sdk;
using app::Button;
using app::Input;
using ui::Color;
using ui::Face;
using ui::Rect;

constexpr std::uint64_t kHoldMs = 300;      // Pattern held this long: the 48 pads are the patterns
constexpr std::uint64_t kToastMs = 1400;
constexpr std::uint64_t kToolsDelayMs = 600;  // Shift held and the encoder still this long: the tools row
constexpr int kInfoH = 34;
constexpr int kHeroY = 38;
constexpr int kHeroH = 272;

constexpr unsigned role_index(PadRole r) noexcept { return static_cast<unsigned>(r); }

constexpr const char* kToolNames[app::kFKeyCount] = {"RANDOM", "MUTATE", "REVERSE", "INVERT", "DOUBLE", "CLEAR", "REDO", "UNDO"};
constexpr const char* kToolHints[app::kFKeyCount] = {"new line", "tweak 25%", "backwards", "flip pitch", "length x2", "all rests", "step forward", "step back"};
constexpr const char* kPerfNames[4] = {"MUTE", "ACC", "SLD", "THROW"};

std::uint8_t note_flags(std::uint8_t step_flags) noexcept {
  std::uint8_t f = 0;
  if ((step_flags & kStepAccent) != 0u) {
    f = static_cast<std::uint8_t>(f | dsp::kNoteAccent);
  }
  if ((step_flags & kStepSlide) != 0u) {
    f = static_cast<std::uint8_t>(f | dsp::kNoteSlide);
  }
  return f;
}

// `s` cut to `max` characters into `out` (cap > max).
void cut(const char* s, unsigned max, char* out, unsigned cap) noexcept {
  unsigned n = 0;
  for (; s != nullptr && s[n] != '\0' && n < max && n + 1u < cap; ++n) {
    out[n] = s[n];
  }
  out[n] = '\0';
}

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

int wrap(int v, int n) noexcept { return n > 0 ? ((v % n) + n) % n : 0; }

}  // namespace

// ---- Setup and screen hooks -------------------------------------------------------------------------------------------------

void MainScreen::init(Rig& rig) noexcept {
  // Every piece of state starts afresh: the app object outlives a run in the simulator's tests.
  rig_ = &rig;
  controls_ = &rig.controls;
  keys_ = PageKeys{};
  keys_.set_page(kPageSound);
  rec_ = false;
  keys_live_ = false;
  roll_ = false;
  motion_latched_ = false;
  playing_ = false;
  octave_ = 0;
  cursor_ = 0;
  view_page_ = 0;
  last_play_page_ = -1;
  was_playing_ = false;
  lane_ = 0;
  seed_ = 0x303u;
  live_trn_ = 0;
  win_pattern_ = -1;
  win_lo_ = 0;
  win_hi_ = 0;
  win_roll_ = false;
  for (unsigned k = 0; k < kPageSteps; ++k) {
    held_step_[k] = -1;
    step_used_[k] = false;
  }
  for (unsigned i = 0; i < kRoles; ++i) {
    mod_down_[i] = false;
    mod_used_[i] = false;
    mod_down_ms_[i] = 0;
  }
  for (auto& n : key_note_) {
    n = -1;
  }
  audition_note_ = -1;
  for (unsigned i = 0; i < kPerfCount; ++i) {
    perf_held_[i] = false;
    perf_latched_[i] = false;
  }
  tool_mask_ = 0;
  shift_ms_ = 0;
  last_turn_ms_ = 0;
  pattern_down_ = false;
  overlay_ = false;
  chain_started_ = false;
  pattern_down_ms_ = 0;
  copy_is_pattern_ = false;
  toast_[0] = '\0';
  toast_until_ms_ = 0;
  lock_active_ = false;
  lock_step_ = 0;
  lock_norm_ = 0.0f;
  lock_text_[0] = '\0';
}

const char* MainScreen::title() const noexcept {
  if (overlay_) {
    return "PATTERNS";
  }
  return roll_ ? "ROLL" : "STEPS";
}

void MainScreen::enter(app::Context& ctx) noexcept {
  for (auto& s : held_step_) {
    s = -1;
  }
  for (bool& d : mod_down_) {
    d = false;
  }
  pattern_down_ = false;
  overlay_ = false;
  clamp_cursor();
  view_page_ = cursor_ / 16;
  ctx.redraw();
}

void MainScreen::leave(app::Context& ctx) noexcept {
  for (unsigned i = 0; i < app::kPadCount; ++i) {
    if (key_note_[i] >= 0) {
      key_release(ctx, i);
    }
  }
  audition_off(ctx);
  keys_.release_all();
  for (bool& h : perf_held_) {
    h = false;
  }
  apply_perf(ctx);
  tool_mask_ = 0;
  pattern_down_ = false;
  overlay_ = false;
}

void MainScreen::mark_held_used() noexcept {
  for (unsigned k = 0; k < kPageSteps; ++k) {
    if (held_step_[k] >= 0) {
      step_used_[k] = true;
    }
  }
}

bool MainScreen::any_step_held() const noexcept {
  for (int s : held_step_) {
    if (s >= 0) {
      return true;
    }
  }
  return false;
}

std::uint32_t MainScreen::held_mask() const noexcept {
  std::uint32_t m = 0;
  for (unsigned k = 0; k < kPageSteps; ++k) {
    if (held_step_[k] >= 0) {
      m |= 1u << k;
    }
  }
  return m;
}

// The step flag of the TR-style modifier held now (ACC, SLIDE, TIE or REST), 0 when none.
std::uint8_t MainScreen::modifier_flag_down() const noexcept {
  if (mod_down_[role_index(PadRole::Acc)]) {
    return kStepAccent;
  }
  if (mod_down_[role_index(PadRole::Slide)]) {
    return kStepSlide;
  }
  if (mod_down_[role_index(PadRole::Tie)]) {
    return kStepTie;
  }
  if (mod_down_[role_index(PadRole::Rest)]) {
    return kStepRest;
  }
  return 0;
}

// A seed that differs from press to press and from boot to boot: the sequence of the last one and the time of this press.
std::uint32_t MainScreen::next_seed(app::Context& ctx) noexcept {
  seed_ = seed_ * 1664525u + 1013904223u + static_cast<std::uint32_t>(ctx.uptime_ms()) * 2654435761u;
  return seed_;
}

void MainScreen::clamp_cursor() noexcept {
  const int len = static_cast<int>(length());
  if (cursor_ >= len) {
    cursor_ = len - 1;
  }
  if (cursor_ < 0) {
    cursor_ = 0;
  }
  const int last_page = (len - 1) / 16;  // the length may have shrunk under the page being shown
  if (view_page_ > last_page) {
    view_page_ = last_page;
  }
  if (view_page_ < 0) {
    view_page_ = 0;
  }
}

void MainScreen::move_cursor(app::Context& ctx, int to) noexcept {
  cursor_ = to;
  clamp_cursor();
  view_page_ = cursor_ / 16;
  ctx.redraw();
}

void MainScreen::toast(app::Context& ctx, const char* text) noexcept {
  cut(text, sizeof toast_ - 1u, toast_, sizeof toast_);
  toast_until_ms_ = ctx.uptime_ms() + kToastMs;
  ctx.redraw();
}

void MainScreen::update(app::Context& ctx) noexcept {
  playing_ = ctx.engine().playing();
  const seq::Playhead ph = rig_->seq->playhead();
  if (playing_ && ph.playing) {
    // The screen follows the pattern being played, and its page.
    if (ph.pattern >= 0 && static_cast<unsigned>(ph.pattern) != sel()) {
      rig_->bank->select(static_cast<unsigned>(ph.pattern));
      clamp_cursor();
    }
    if (ph.step >= 0 && ph.step / 16 != last_play_page_) {
      last_play_page_ = ph.step / 16;
      view_page_ = last_play_page_;
    }
  } else {
    last_play_page_ = -1;
  }
  if (was_playing_ && !playing_) {
    rig_->seq->set_live_transpose(0);  // the keyboard's transposition ends with the run
    live_trn_ = 0;
  }
  was_playing_ = playing_;
  if (pattern_down_ && !overlay_ && ctx.uptime_ms() - pattern_down_ms_ >= kHoldMs) {
    overlay_ = true;
    chain_started_ = false;
    ctx.redraw();
  }
  if (lock_active_ && !any_step_held()) {
    lock_active_ = false;
  }
  if (toast_[0] != '\0' && ctx.uptime_ms() >= toast_until_ms_) {
    toast_[0] = '\0';
  }
}

// ---- Input ------------------------------------------------------------------------------------------------------------------

void MainScreen::input(app::Context& ctx, const Input& in) noexcept {
  playing_ = ctx.engine().playing();
  if (in.kind == core::InputKind::Pad) {
    pad_input(ctx, in);
  } else if (in.kind == core::InputKind::Encoder) {
    turn_input(ctx, in);
  } else if (in.kind == core::InputKind::Button) {
    button_input(ctx, in);
  }
}

void MainScreen::audition_on(app::Context& ctx, unsigned note, std::uint8_t step_flags) noexcept {
  audition_off(ctx);
  const std::uint8_t f = note_flags(step_flags);
  (void)ctx.engine().note_on(rig_->wiring.voice, static_cast<std::uint8_t>(note), (f & dsp::kNoteAccent) != 0u ? 127 : 100, f);
  audition_note_ = static_cast<int>(note);
}

void MainScreen::audition_off(app::Context& ctx) noexcept {
  if (audition_note_ >= 0) {
    (void)ctx.engine().note_off(rig_->wiring.voice, static_cast<std::uint8_t>(audition_note_));
    audition_note_ = -1;
  }
}

void MainScreen::toggle_flag_on_held_or_cursor(app::Context& ctx, std::uint8_t flag) noexcept {
  if (any_step_held()) {
    mark_held_used();
    for (int s : held_step_) {
      if (s >= 0) {
        (void)rig_->bank->toggle_flag(sel(), static_cast<unsigned>(s), flag);
      }
    }
  } else {
    (void)rig_->bank->toggle_flag(sel(), static_cast<unsigned>(cursor_), flag);
  }
  ctx.redraw();
}

void MainScreen::step_press(app::Context& ctx, unsigned k) noexcept {
  const int s = view_page_ * 16 + static_cast<int>(k);
  if (static_cast<unsigned>(s) >= length()) {
    return;
  }
  held_step_[k] = s;
  step_used_[k] = false;
  // A modifier held alone: the step pads are its TR-style switches.
  const std::uint8_t flag = modifier_flag_down();
  if (flag != 0u) {
    step_used_[k] = true;
    (void)rig_->bank->toggle_flag(sel(), static_cast<unsigned>(s), flag);
    const PadRole role = flag == kStepAccent ? PadRole::Acc : (flag == kStepSlide ? PadRole::Slide : (flag == kStepTie ? PadRole::Tie : PadRole::Rest));
    mod_used_[role_index(role)] = true;
    ctx.redraw();
    return;
  }
  if (mod_down_[role_index(PadRole::Motion)]) {
    // MOTION held: the step pads show the steps with automation; a tap on one clears that step's locks of the lane shown (the cursor
    // stays, nothing sounds). Delete with MOTION held clears the whole lane.
    mod_used_[role_index(PadRole::Motion)] = true;
    step_used_[k] = true;
    if (step_has_motion(pat(), lane_, static_cast<unsigned>(s))) {
      (void)rig_->bank->clear_plock(sel(), lane_, static_cast<unsigned>(s));
      toast(ctx, "STEP MOTION CLEARED");
    }
    ctx.redraw();
    return;
  }
  // The cursor goes to the step when the pad is let go without having been used (a tap): hold + key or modifier leaves it where it is.
  if (!playing_) {
    const Step& st = pat().steps[s];
    if ((st.flags & kStepRest) == 0u && (st.flags & kStepTie) == 0u) {
      int n = static_cast<int>(st.note) + pat().transpose;
      n = n < 0 ? 0 : (n > 127 ? 127 : n);
      audition_on(ctx, static_cast<unsigned>(n), st.flags);
    }
  }
}

void MainScreen::modifier_with_steps(app::Context& ctx, PadRole role) noexcept {
  std::uint8_t flag = 0;
  switch (role) {
    case PadRole::Acc:
      flag = kStepAccent;
      break;
    case PadRole::Slide:
      flag = kStepSlide;
      break;
    case PadRole::Tie:
      flag = kStepTie;
      break;
    case PadRole::Rest:
      flag = kStepRest;
      break;
    default:
      return;
  }
  toggle_flag_on_held_or_cursor(ctx, flag);
}

// A modifier pad pressed and released with nothing else done: ACC and SLIDE toggle on the cursor step; TIE and REST do the same,
// except that with REC armed and the transport stopped they are entries (written on the cursor, which advances).
void MainScreen::modifier_tap(app::Context& ctx, PadRole role) noexcept {
  std::uint8_t flag = 0;
  switch (role) {
    case PadRole::Acc:
      flag = kStepAccent;
      break;
    case PadRole::Slide:
      flag = kStepSlide;
      break;
    case PadRole::Tie:
      flag = kStepTie;
      break;
    case PadRole::Rest:
      flag = kStepRest;
      break;
    default:
      return;
  }
  if ((flag == kStepTie || flag == kStepRest) && rec_ && !playing_) {
    const unsigned next = rig_->bank->write_advance(sel(), static_cast<unsigned>(cursor_), pat().steps[cursor_].note, flag);
    move_cursor(ctx, static_cast<int>(next));
    return;
  }
  (void)rig_->bank->toggle_flag(sel(), static_cast<unsigned>(cursor_), flag);
  ctx.redraw();
}

void MainScreen::key_press(app::Context& ctx, unsigned pad) noexcept {
  const int note = key_note(pad, octave_);
  if (note < 0) {
    return;
  }
  std::uint8_t flags = 0;
  if (mod_down_[role_index(PadRole::Acc)]) {
    flags = static_cast<std::uint8_t>(flags | kStepAccent);
    mod_used_[role_index(PadRole::Acc)] = true;
  }
  if (mod_down_[role_index(PadRole::Slide)]) {
    flags = static_cast<std::uint8_t>(flags | kStepSlide);
    mod_used_[role_index(PadRole::Slide)] = true;
  }
  const unsigned un = static_cast<unsigned>(note);
  bool sound = true;
  if (any_step_held()) {
    // hold a step + a key: that step's note, the cursor stays.
    mark_held_used();
    for (int s : held_step_) {
      if (s >= 0) {
        (void)rig_->bank->set_note(sel(), static_cast<unsigned>(s), un);
        if ((pat().steps[s].flags & (kStepRest | kStepTie)) != 0u) {
          (void)rig_->bank->set_flag(sel(), static_cast<unsigned>(s), kStepRest, false);
          (void)rig_->bank->set_flag(sel(), static_cast<unsigned>(s), kStepTie, false);
        }
      }
    }
  } else if (rec_ && !playing_) {
    const unsigned next = rig_->bank->write_advance(sel(), static_cast<unsigned>(cursor_), un, flags);
    move_cursor(ctx, static_cast<int>(next));
  } else if (rec_ && playing_) {
    const seq::Playhead ph = rig_->seq->playhead();
    if (ph.playing && ph.step >= 0 && ph.pattern >= 0) {
      (void)rig_->bank->write_advance(static_cast<unsigned>(ph.pattern), static_cast<unsigned>(ph.step), un, flags);
    }
  } else if (playing_ && !keys_live_) {
    // The keyboard transposes the running pattern: the bottom left C is no transposition.
    int t = note - kLowestNote;
    t = t < -36 ? -36 : (t > 36 ? 36 : t);
    live_trn_ = t;
    rig_->seq->set_live_transpose(t);
    sound = false;
  }
  if (sound) {
    key_note_[pad] = note;
    const std::uint8_t nf = note_flags(flags);
    (void)ctx.engine().note_on(rig_->wiring.voice, static_cast<std::uint8_t>(note), (nf & dsp::kNoteAccent) != 0u ? 127 : 100, nf);
  }
  ctx.redraw();
}

void MainScreen::key_release(app::Context& ctx, unsigned pad) noexcept {
  if (key_note_[pad] >= 0) {
    (void)ctx.engine().note_off(rig_->wiring.voice, static_cast<std::uint8_t>(key_note_[pad]));
    key_note_[pad] = -1;
  }
}

void MainScreen::choose_pattern(app::Context& ctx, unsigned index) noexcept {
  if (index >= kPatternCount) {
    return;
  }
  if (!chain_started_) {
    chain_started_ = true;
    (void)rig_->seq->clear_queue();
    if (!playing_) {
      rig_->bank->select(index);  // stopped: the first tap selects at once, further taps chain behind it
      clamp_cursor();
      view_page_ = cursor_ / 16;
      return;
    }
  }
  (void)rig_->seq->queue(index);  // the chain is the sequencer's: it shows in chain() within a block
  ctx.redraw();
}

void MainScreen::pad_input(app::Context& ctx, const Input& in) noexcept {
  const unsigned pad = in.pad();
  const PadRole role = pad_role(pad);
  const unsigned ri = role_index(role);
  const bool down = in.pad_press();

  if (!down) {
    switch (role) {
      case PadRole::Step: {
        const int k = pad_page_step(pad);
        if (k >= 0) {
          if (held_step_[k] >= 0 && !step_used_[k] && !overlay_) {
            move_cursor(ctx, held_step_[k]);  // a tap: the step is selected
          }
          held_step_[k] = -1;
        }
        if (!any_step_held()) {
          audition_off(ctx);
        }
        break;
      }
      case PadRole::Key:
        key_release(ctx, pad);
        break;
      case PadRole::Acc:
      case PadRole::Slide:
      case PadRole::Tie:
      case PadRole::Rest:
        if (mod_down_[ri]) {
          mod_down_[ri] = false;
          // A tap does the modifier's own job; held longer it was only a look at the steps that have it.
          if (!mod_used_[ri] && !overlay_ && ctx.uptime_ms() - mod_down_ms_[ri] < kHoldMs) {
            modifier_tap(ctx, role);
          }
          mod_used_[ri] = false;
        }
        break;
      case PadRole::Motion:
        if (mod_down_[ri]) {
          mod_down_[ri] = false;
          if (!mod_used_[ri] && !overlay_ && ctx.uptime_ms() - mod_down_ms_[ri] < kHoldMs) {
            motion_latched_ = !motion_latched_;
            toast(ctx, motion_latched_ ? "MOTION REC ON" : "MOTION REC OFF");
          }
          mod_used_[ri] = false;
        }
        break;
      default:
        break;
    }
    ctx.redraw();
    return;
  }

  if (overlay_ || pattern_down_) {  // Pattern held: the 48 pads are the patterns
    overlay_ = true;
    choose_pattern(ctx, pad);
    ctx.redraw();
    return;
  }

  switch (role) {
    case PadRole::Step: {
      const int k = pad_page_step(pad);
      if (k >= 0) {
        step_press(ctx, static_cast<unsigned>(k));
      }
      break;
    }
    case PadRole::Acc:
    case PadRole::Slide:
    case PadRole::Tie:
    case PadRole::Rest:
      mod_down_[ri] = true;
      mod_used_[ri] = false;
      mod_down_ms_[ri] = ctx.uptime_ms();
      if (any_step_held()) {  // hold a step + ACC / SLIDE / TIE / REST: that attribute toggles on it
        modifier_with_steps(ctx, role);
        mod_used_[ri] = true;
      }
      break;
    case PadRole::OctDown:
    case PadRole::OctUp: {
      const int d = role == PadRole::OctUp ? 1 : -1;
      if (any_step_held()) {  // hold a step + OCT: the step's note moves an octave
        mark_held_used();
        for (int s : held_step_) {
          if (s >= 0) {
            const int n = static_cast<int>(pat().steps[s].note) + 12 * d;
            if (n >= 0 && n <= 127) {
              (void)rig_->bank->set_note(sel(), static_cast<unsigned>(s), static_cast<unsigned>(n));
            }
          }
        }
      } else {
        octave_ = clamp_octave(octave_ + d);
      }
      break;
    }
    case PadRole::Keys:
      keys_live_ = !keys_live_;
      toast(ctx, keys_live_ ? "KEYS PLAY THE VOICE" : "KEYS TRANSPOSE");
      break;
    case PadRole::Motion:
      mod_down_[ri] = true;
      mod_used_[ri] = false;
      mod_down_ms_[ri] = ctx.uptime_ms();
      break;
    case PadRole::Key:
      key_press(ctx, pad);
      break;
    case PadRole::None:
      break;
  }
  ctx.redraw();
}

void MainScreen::plock_turn(app::Context& ctx, int detents, bool coarse) noexcept {
  unsigned lane = 0;
  if (!controls_->lane(keys_.focused(), lane)) {
    return;
  }
  const dsp::UnitId voice = rig_->wiring.voice;
  const core::ParamDesc* d = ctx.engine().param_desc(voice, static_cast<std::uint16_t>(lane));
  if (d == nullptr) {
    return;
  }
  lane_ = lane;
  mark_held_used();
  const int n = coarse ? detents * static_cast<int>(d->coarse) : detents;
  for (unsigned k = 0; k < kPageSteps; ++k) {
    if (held_step_[k] < 0) {
      continue;
    }
    const unsigned s = static_cast<unsigned>(held_step_[k]);
    const std::uint8_t cur = pat().motion[lane][s * kMotionPointsPerStep];
    const float v = cur == kNoLock ? ctx.engine().param(voice, static_cast<std::uint16_t>(lane)) : seq::point_to_value(*d, cur);
    const std::uint8_t pt = seq::value_to_point(*d, core::param_step(*d, v, n));
    (void)rig_->bank->set_plock(sel(), lane, s, pt);
    lock_active_ = true;
    lock_step_ = static_cast<int>(s);
    lock_norm_ = static_cast<float>(pt) / 254.0f;
    (void)core::param_format(*d, seq::point_to_value(*d, pt), lock_text_, sizeof lock_text_);
    char unit[8];
    cut(d->unit_text, 3, unit, sizeof unit);
    append(lock_text_, sizeof lock_text_, unit);
  }
  ctx.redraw();
}

void MainScreen::turn_input(app::Context& ctx, const Input& in) noexcept {
  if (in.delta() == 0) {
    return;
  }
  last_turn_ms_ = ctx.uptime_ms();
  unsigned lane = 0;
  if (any_step_held() && controls_->lane(keys_.focused(), lane)) {  // hold a step + encoder: a p-lock
    keys_.mark_turned();  // with an F key held, its release gives the latch back, as after any hold-and-turn
    plock_turn(ctx, in.delta(), in.shift);
    return;
  }
  const std::uint32_t affected = keys_.held() != 0u ? keys_.held() : (1u << keys_.focus());
  const Page& pg = keys_.page();
  (void)keys_.input(ctx, in, *controls_);
  if (playing_ && (rec_ || motion_latched_)) {
    // Recording: an encoder move on a SOUND-page key is written as motion under the playhead.
    for (unsigned k = 0; k < kKeys; ++k) {
      if ((affected >> k & 1u) != 0u && controls_->lane(pg.entry[k], lane)) {
        lane_ = lane;
        (void)rig_->seq->record_motion(lane, ctx.engine().param(rig_->wiring.voice, pg.entry[k].id));
      }
    }
  }
}

void MainScreen::tool(app::Context& ctx, unsigned index) noexcept {
  seq::Bank& bank = *rig_->bank;
  switch (index) {
    case 0:
      (void)bank.random_fill(sel(), rig_->wiring.gen, next_seed(ctx));
      toast(ctx, "RANDOM LINE");
      break;
    case 1:
      (void)bank.mutate(sel(), rig_->wiring.gen, next_seed(ctx), 25);
      toast(ctx, "MUTATED");
      break;
    case 2:
      (void)bank.reverse(sel());
      toast(ctx, "REVERSED");
      break;
    case 3:
      (void)bank.invert(sel());
      toast(ctx, "INVERTED");
      break;
    case 4:
      toast(ctx, bank.double_length(sel()) ? "DOUBLED" : "ALREADY 64 STEPS");
      break;
    case 5:
      (void)bank.clear_pattern(sel());
      toast(ctx, "PATTERN CLEARED");
      break;
    case 6: {
      const int i = bank.redo();
      if (i >= 0) {
        bank.select(static_cast<unsigned>(i));
      }
      toast(ctx, i >= 0 ? "REDO" : "NOTHING TO REDO");
      break;
    }
    default: {
      const int i = bank.undo();
      if (i >= 0) {
        bank.select(static_cast<unsigned>(i));
      }
      toast(ctx, i >= 0 ? "UNDO" : "NOTHING TO UNDO");
      break;
    }
  }
  clamp_cursor();
  view_page_ = cursor_ / 16;
}

void MainScreen::perf_press(app::Context& ctx, Perf which, bool shift) noexcept {
  if (shift) {
    perf_latched_[which] = !perf_latched_[which];
  } else {
    perf_held_[which] = true;
  }
  apply_perf(ctx);
}

void MainScreen::perf_release(app::Context& ctx, Perf which) noexcept {
  perf_held_[which] = false;
  apply_perf(ctx);
}

void MainScreen::apply_perf(app::Context& ctx) noexcept {
  const auto on = [&](Perf p) { return perf_held_[p] || perf_latched_[p]; };
  rig_->seq->set_perf_flag(seq::kPerfMute, on(kMute));
  rig_->seq->set_perf_flag(seq::kPerfAccAll, on(kAccAll));
  rig_->seq->set_perf_flag(seq::kPerfSlideAll, on(kSlideAll));
  rig_->wiring.throw_on = on(kThrow);
  controls_->apply_send(ctx.engine());
  ctx.redraw();
}

void MainScreen::copy_buttons(app::Context& ctx, Button which, bool shift) noexcept {
  seq::Bank& bank = *rig_->bank;
  switch (which) {
    case Button::Copy:
      if (pattern_down_) {
        bank.copy_pattern(sel());
        copy_is_pattern_ = true;
        toast(ctx, "PATTERN COPIED");
      } else if (shift) {
        if (copy_is_pattern_) {
          toast(ctx, bank.paste_pattern(sel()) ? "PATTERN PASTED" : "NOTHING COPIED");
        } else {
          toast(ctx, bank.paste_step(sel(), static_cast<unsigned>(cursor_)) ? "STEP PASTED" : "NOTHING COPIED");
        }
      } else {
        bank.copy_step(sel(), static_cast<unsigned>(cursor_));
        copy_is_pattern_ = false;
        toast(ctx, "STEP COPIED");
      }
      break;
    case Button::Insert:
      if (shift) {
        if (sel() + 1u >= kPatternCount) {  // the last pattern has no slot after it: nothing wraps round to overwrite A01
          toast(ctx, "NO SLOT AFTER THE LAST PATTERN");
          break;
        }
        const unsigned next = sel() + 1u;
        (void)bank.replace(next, pat());
        char name[4];
        char text[24] = "DUPLICATED TO ";
        pattern_name(next, name, sizeof name);
        append(text, sizeof text, name);
        if (playing_) {
          (void)rig_->seq->queue(next);
        } else {
          bank.select(next);
        }
        toast(ctx, text);
      } else {
        (void)bank.insert_step(sel(), static_cast<unsigned>(cursor_));
      }
      break;
    default: {  // Delete
      if (mod_down_[role_index(PadRole::Motion)]) {
        mod_used_[role_index(PadRole::Motion)] = true;
        if (shift) {
          (void)bank.clear_motion(sel());
          toast(ctx, "ALL MOTION CLEARED");
        } else {
          (void)bank.clear_lane_motion(sel(), lane_);
          toast(ctx, "MOTION CLEARED");
        }
      } else if (shift) {
        (void)bank.clear_pattern(sel());
        toast(ctx, "PATTERN CLEARED");
      } else if (any_step_held()) {
        mark_held_used();
        for (int s : held_step_) {
          if (s >= 0) {
            (void)bank.set_flag(sel(), static_cast<unsigned>(s), kStepRest, true);
            (void)bank.clear_step_motion(sel(), static_cast<unsigned>(s));
          }
        }
      } else {
        (void)bank.set_flag(sel(), static_cast<unsigned>(cursor_), kStepRest, true);
        (void)bank.clear_step_motion(sel(), static_cast<unsigned>(cursor_));
      }
      break;
    }
  }
  clamp_cursor();
  ctx.redraw();
}

void MainScreen::button_input(app::Context& ctx, const Input& in) noexcept {
  if (app::is_fkey(in.which())) {
    const unsigned k = app::fkey_index(in.which());
    if (in.type == Input::Type::Press) {
      if (in.shift) {  // Shift + F: the tools row
        tool_mask_ |= 1u << k;
        tool(ctx, k);
        ctx.redraw();
        return;
      }
      (void)keys_.input(ctx, in, *controls_);
    } else if (in.type == Input::Type::Release) {
      if ((tool_mask_ >> k & 1u) != 0u) {
        tool_mask_ &= ~(1u << k);
        return;
      }
      (void)keys_.input(ctx, in, *controls_);
    }
    return;
  }

  if (in.type == Input::Type::Release) {
    switch (in.which()) {
      case Button::Note:
        perf_release(ctx, kMute);
        break;
      case Button::Instr:
        perf_release(ctx, kAccAll);
        break;
      case Button::Vol:
        perf_release(ctx, kSlideAll);
        break;
      case Button::Fx:
        perf_release(ctx, kThrow);
        break;
      case Button::Pattern:
        if (pattern_down_) {
          if (!overlay_) {
            roll_ = !roll_;  // a tap on the screen's own button: the step view and the piano roll
          }
          pattern_down_ = false;
          overlay_ = false;
          chain_started_ = false;
          ctx.redraw();
        }
        break;
      default:
        break;
    }
    return;
  }

  switch (in.which()) {
    case Button::Params:
      if (in.shift) {
        keys_.prev_page();
      } else {
        keys_.next_page();
      }
      ctx.redraw();
      break;
    case Button::Rec:
      rec_ = !rec_;
      ctx.redraw();
      break;
    case Button::Left:
    case Button::Right: {
      const int d = in.which() == Button::Right ? 1 : -1;
      if (in.shift) {
        (void)rig_->bank->rotate(sel(), d);
        toast(ctx, d > 0 ? "ROTATED RIGHT" : "ROTATED LEFT");
      } else {
        move_cursor(ctx, wrap(cursor_ + d, static_cast<int>(length())));
      }
      break;
    }
    case Button::Up:
    case Button::Down: {
      const int d = in.which() == Button::Up ? 1 : -1;
      if (in.shift) {
        (void)rig_->bank->set_transpose(sel(), pat().transpose + d);
      } else if (any_step_held()) {  // hold a step + UP / DOWN: that step's note, a semitone (M3b: the octave)
        mark_held_used();
        for (int s : held_step_) {
          if (s >= 0) {
            const int n = static_cast<int>(pat().steps[s].note) + d;
            if (n >= 0 && n <= 127) {
              (void)rig_->bank->set_note(sel(), static_cast<unsigned>(s), static_cast<unsigned>(n));
            }
          }
        }
      } else {
        const int n = static_cast<int>(pat().steps[cursor_].note) + d;
        if (n >= 0 && n <= 127) {
          (void)rig_->bank->set_note(sel(), static_cast<unsigned>(cursor_), static_cast<unsigned>(n));
        }
      }
      ctx.redraw();
      break;
    }
    case Button::Note:
      perf_press(ctx, kMute, in.shift);
      break;
    case Button::Instr:
      perf_press(ctx, kAccAll, in.shift);
      break;
    case Button::Vol:
      perf_press(ctx, kSlideAll, in.shift);
      break;
    case Button::Fx:
      perf_press(ctx, kThrow, in.shift);
      break;
    case Button::Copy:
    case Button::Insert:
    case Button::Delete:
      copy_buttons(ctx, in.which(), in.shift);
      break;
    case Button::Pattern:
      pattern_down_ = true;
      overlay_ = false;
      chain_started_ = false;
      pattern_down_ms_ = ctx.uptime_ms();
      break;
    case Button::Shift:
      shift_ms_ = ctx.uptime_ms();
      ctx.redraw();
      break;
    default:
      break;
  }
}

// ---- Soft keys and LEDs -----------------------------------------------------------------------------------------------------

// The tools row (and its labels on the F keys) shows while Shift is held and the encoder is still: a coarse Shift + turn keeps the name
// and the value of the parameter on the screen. The tools themselves work from the moment Shift is down.
bool MainScreen::tools_visible(const app::Context& ctx) const noexcept {
  if (!ctx.panel().shift()) {
    return false;
  }
  const std::uint64_t since = shift_ms_ > last_turn_ms_ ? shift_ms_ : last_turn_ms_;
  return ctx.uptime_ms() - since >= kToolsDelayMs;
}

void MainScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  if (tools_visible(ctx)) {
    for (unsigned k = 0; k < app::kFKeyCount; ++k) {
      out.label[k] = kToolNames[k];
    }
    if (!rig_->bank->can_redo()) {
      out.dim = static_cast<std::uint8_t>(out.dim | (1u << 6));
    }
    if (!rig_->bank->can_undo()) {
      out.dim = static_cast<std::uint8_t>(out.dim | (1u << 7));
    }
    return;
  }
  const Page& pg = keys_.page();
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    out.label[k] = controls_->label(ctx.engine(), pg.entry[k]);
    if (pg.entry[k].item == Item::Blank) {
      out.dim = static_cast<std::uint8_t>(out.dim | (1u << k));
    }
  }
  out.hot = static_cast<std::uint8_t>(1u << keys_.focus());
}

void MainScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  clamp_cursor();
  const seq::Playhead ph = rig_->seq->playhead();
  const bool blink = app::PadLeds::blink(ctx.now(), 500);

  if (overlay_) {
    const seq::ChainView chain = rig_->seq->chain();
    for (unsigned i = 0; i < kPatternCount; ++i) {
      std::uint8_t level = pattern_is_empty(rig_->bank->pattern(i)) ? app::kLedOff : app::kLedDim;
      if (i == sel()) {
        level = app::kLedMid;
      }
      for (unsigned c = 0; c < chain.length; ++c) {
        if (chain.pattern[c] == i) {
          level = static_cast<int>(c) == chain.next_entry && blink ? app::kLedOn : app::kLedLow;
        }
      }
      if (ph.playing && ph.pattern == static_cast<int>(i)) {
        level = app::kLedOn;
      }
      leds.set(i, level);
    }
    return;
  }

  const Pattern& p = pat();
  const unsigned len = length();
  const std::uint8_t flag = modifier_flag_down();
  const bool motion_view = mod_down_[role_index(PadRole::Motion)];
  const bool playing_here = ph.playing && ph.pattern == static_cast<int>(sel());
  for (unsigned k = 0; k < kPageSteps; ++k) {
    const unsigned s = static_cast<unsigned>(view_page_) * 16u + k;
    if (s >= len) {
      continue;
    }
    const StepInfo info = describe(p, s);
    std::uint8_t level = app::kLedOff;
    if (flag != 0u) {  // a modifier held alone: the steps that have it
      level = (p.steps[s].flags & flag) != 0u ? app::kLedMid : app::kLedOff;
    } else if (motion_view) {  // MOTION held: the steps with automation
      level = step_has_motion(p, lane_, s) ? app::kLedMid : app::kLedOff;
    } else if (info.kind == Cell::Note) {
      level = info.accent ? app::kLedLow : app::kLedDim;
    } else if (info.kind == Cell::Tie) {
      level = app::kLedDim;
    }
    if (playing_here && ph.step == static_cast<int>(s)) {
      level = app::kLedOn;
    }
    if (static_cast<int>(s) == cursor_ && flag == 0u && !motion_view) {
      if (playing_here && ph.step == static_cast<int>(s)) {
        level = blink ? app::kLedOn : app::kLedMid;  // the playhead stays visible on the cursor's step
      } else {
        level = blink ? app::kLedOn : (info.kind != Cell::Rest ? app::kLedDim : app::kLedOff);
      }
    }
    if (held_step_[k] >= 0) {
      level = app::kLedOn;
    }
    leds.set(step_pad(k), level);
  }

  // The modifier block: what the cursor's step has, brighter while held.
  const Step& cs = p.steps[cursor_ < static_cast<int>(len) ? cursor_ : 0];
  const auto mod_level = [&](PadRole role, bool lit) -> std::uint8_t {
    return mod_down_[role_index(role)] ? app::kLedOn : (lit ? app::kLedMid : app::kLedDim);
  };
  leds.set(role_pad(PadRole::Acc), mod_level(PadRole::Acc, (cs.flags & kStepAccent) != 0u));
  leds.set(role_pad(PadRole::Slide), mod_level(PadRole::Slide, (cs.flags & kStepSlide) != 0u));
  leds.set(role_pad(PadRole::Tie), mod_level(PadRole::Tie, (cs.flags & kStepTie) != 0u));
  leds.set(role_pad(PadRole::Rest), mod_level(PadRole::Rest, (cs.flags & kStepRest) != 0u));
  leds.set(role_pad(PadRole::OctDown), octave_ < 0 ? app::kLedMid : app::kLedDim);
  leds.set(role_pad(PadRole::OctUp), octave_ > 0 ? app::kLedMid : app::kLedDim);
  leds.set(role_pad(PadRole::Keys), keys_live_ ? app::kLedMid : app::kLedDim);
  leds.set(role_pad(PadRole::Motion), mod_down_[role_index(PadRole::Motion)] ? app::kLedOn : (motion_latched_ ? app::kLedMid : app::kLedDim));

  // The keyboard: naturals dim, sharps off, the current note of the pattern at full level.
  int current = -1;
  const int ref = playing_here ? ph.step : cursor_;
  if (ref >= 0 && static_cast<unsigned>(ref) < len) {
    const StepInfo info = describe(p, static_cast<unsigned>(ref));
    if (info.kind != Cell::Rest) {
      current = info.note;
    }
  }
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    const int n = key_note(pad, octave_);
    if (n < 0) {
      continue;
    }
    std::uint8_t level = is_natural(static_cast<unsigned>(n)) ? app::kLedDim : app::kLedOff;
    if (playing_ && !keys_live_ && !rec_ && n == kLowestNote + live_trn_) {
      level = app::kLedMid;  // the key the running pattern is transposed by
    }
    if (n == current) {
      level = app::kLedOn;
    }
    if (ctx.panel().pad_held(pad)) {
      level = app::kLedOn;
    }
    leds.set(pad, level);
  }
}

// ---- Drawing ----------------------------------------------------------------------------------------------------------------

void MainScreen::draw_info(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  const Rect strip{area.x, area.y, area.w, kInfoH};
  ui::strip(r, strip, ui::Edge::Bottom);
  const ui::Theme& theme = r.theme();
  const Pattern& p = pat();
  const seq::Playhead ph = rig_->seq->playhead();

  char name[4];
  pattern_name(sel(), name, sizeof name);
  r.text(8, ui::caps_y(theme, Face::Display, strip.y, strip.h), name, Color::ChromeText, Face::Display);

  const int baseline = strip.y + 23;
  ui::FieldRow row(r, 68, baseline);
  if (toast_[0] != '\0') {
    row.chip(Face::Data, toast_, Color::Sel, Color::SelText);
    row.skip(12);
  } else {
    char v[8];
    view::fmt_uint(length(), v);
    row.field("LEN", Face::Data, v, Color::ChromeDim, Color::ChromeText);
    char sw[8];
    unsigned n = view::fmt_uint(p.swing, sw);
    sw[n++] = '%';
    sw[n] = '\0';
    row.field("SW", Face::Data, sw, Color::ChromeDim, Color::ChromeText);
    char g[8];
    n = view::fmt_uint(p.gate, g);
    g[n++] = '%';
    g[n] = '\0';
    row.field("GATE", Face::Data, g, Color::ChromeDim, Color::ChromeText);
    // The pattern's playback transpose (the SEQ page's TRANSP and Shift + UP / DOWN).
    char tr[8];
    n = 0;
    tr[n++] = p.transpose < 0 ? '-' : '+';
    n += view::fmt_uint(static_cast<unsigned>(p.transpose < 0 ? -p.transpose : p.transpose), tr + n);
    tr[n] = '\0';
    row.field("TR", Face::Data, tr, Color::ChromeDim, p.transpose != 0 ? Color::ChromeText : Color::ChromeDim);
  }
  if (rec_) {
    row.chip(Face::Label, "REC", Color::Rec, Color::SelText);
    row.skip(6);
  }
  if (keys_live_) {
    row.chip(Face::Label, "KEYS", Color::ChromeText, Color::Chrome);
    row.put(Face::Label, "TRNSP", Color::ChromeDim);
  } else {
    row.put(Face::Label, "KEYS", Color::ChromeDim);
    row.skip(8);
    char t[12] = "TRNSP";
    if (live_trn_ != 0) {
      char num[8];
      view::fmt_uint(static_cast<unsigned>(live_trn_ < 0 ? -live_trn_ : live_trn_), num);
      append(t, sizeof t, live_trn_ < 0 ? " -" : " +");
      append(t, sizeof t, num);
    }
    row.chip(Face::Label, t, Color::ChromeText, Color::Chrome);
  }
  row.skip(6);
  if (ph.playing && ph.queued >= 0) {
    char next[16] = "NEXT ";
    char qn[4];
    pattern_name(static_cast<unsigned>(ph.queued), qn, sizeof qn);
    append(next, sizeof next, qn);
    row.chip(Face::Label, next, Color::ChromeDim, Color::Chrome);
  }

  // The performance chips, right aligned: lit while the button is down, filled in Sel when latched.
  int total = 0;
  for (unsigned i = 0; i < 4; ++i) {
    total += r.text_width(kPerfNames[i], Face::Label) + 2 * theme.metrics.gap + 6;
  }
  ui::FieldRow perf(r, ui::kScreenW - 6 - total, baseline);
  for (unsigned i = 0; i < 4; ++i) {
    if (perf_latched_[i]) {
      perf.chip(Face::Label, kPerfNames[i], Color::Sel, Color::SelText);
    } else if (perf_held_[i]) {
      perf.chip(Face::Label, kPerfNames[i], Color::Accent, Color::AccentText);
    } else {
      perf.skip(theme.metrics.gap);
      perf.put(Face::Label, kPerfNames[i], Color::ChromeDim);
      perf.skip(theme.metrics.gap);
    }
    perf.skip(6);
  }
  (void)ctx;
}

void MainScreen::draw_strip(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  const Page& pg = keys_.page();
  dsp::Engine& e = ctx.engine();
  const int cell_w = area.w / static_cast<int>(app::kFKeyCount);
  r.panel(area, Color::Pane);
  r.rule(area.x, area.y, area.w, ui::Axis::Horizontal, Color::Empty);

  // The page tabs in the corner: the page the F keys are on, and the others one PARAMS away.
  int tx = area.x + 6;
  for (unsigned i = 0; i < kPageCount; ++i) {
    const char* nm = page_at(i).name;
    const int w = r.text_width(nm, Face::Micro);
    const bool cur = i == keys_.page_index();
    r.text(tx, area.y + 3, nm, cur ? Color::Text : Color::Empty, Face::Micro);
    if (cur) {
      r.rule(tx, area.y + 14, w, ui::Axis::Horizontal, Color::Sel);
    }
    tx += w + 12;
  }

  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    const Rect cell{area.x + static_cast<int>(k) * cell_w, area.y + 1, cell_w, area.h - 1};
    if (k != 0u) {
      r.rule(cell.x, cell.y + 16, cell.h - 20, ui::Axis::Vertical, Color::Empty);
    }
    const Entry& entry = pg.entry[k];
    const core::ParamDesc* d = controls_->desc(e, entry);
    if (d == nullptr) {
      continue;
    }
    const bool focus = k == keys_.focus();
    const bool held = (keys_.held() >> k & 1u) != 0u;
    const bool locked = lock_active_ && focus;
    const Rect box{cell.x + 3, cell.y + 16, cell.w - 6, cell.h - 18};
    if (focus) {
      r.panel(box, Color::Sel);
    } else if (held) {
      r.frame(box, Color::Accent);
    }
    char label[16];
    if (locked) {
      char num[4];
      view::fmt_uint(static_cast<unsigned>(lock_step_ + 1), num);
      cut("LOCK ", 5, label, sizeof label);
      append(label, sizeof label, num);
    } else {
      cut(controls_->label(e, entry), static_cast<unsigned>((box.w - 6) / 8), label, sizeof label);
    }
    r.text(box.x + 4, box.y + 1, label, focus ? Color::SelText : Color::TextDim, Face::Label);

    char value[24];
    if (locked) {
      cut(lock_text_, sizeof value - 1u, value, sizeof value);
    } else {
      (void)controls_->format(e, entry, value, sizeof value);
      append(value, sizeof value, controls_->unit_text(e, entry));
    }
    char shown[16];
    cut(value, static_cast<unsigned>((box.w - 6) / 10), shown, sizeof shown);
    r.text(box.x + 4, box.y + 17, shown, locked ? Color::Accent : (focus ? Color::SelText : Color::Text), Face::Data);

    const float norm = locked ? lock_norm_ : core::param_norm(*d, controls_->get(e, entry));
    const auto filled = static_cast<unsigned>(norm * 1000.0f + 0.5f);
    ui::bar(r, Rect{box.x + 4, box.y + box.h - 7, box.w - 8, 4}, filled, 1000u, focus ? Color::SelText : Color::Empty,
            locked ? Color::Accent : (focus ? Color::Accent : Color::Instr));
  }
}

void MainScreen::draw_tools(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  (void)ctx;
  const int cell_w = area.w / static_cast<int>(app::kFKeyCount);
  r.panel(area, Color::Pane);
  r.rule(area.x, area.y, area.w, ui::Axis::Horizontal, Color::Empty);
  r.text(area.x + 6, area.y + 3, "TOOLS", Color::Text, Face::Micro);
  r.text(area.x + 56, area.y + 3, "SHIFT + F1..F8", Color::Empty, Face::Micro);
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    const Rect cell{area.x + static_cast<int>(k) * cell_w, area.y + 1, cell_w, area.h - 1};
    if (k != 0u) {
      r.rule(cell.x, cell.y + 16, cell.h - 20, ui::Axis::Vertical, Color::Empty);
    }
    const bool none = (k == 6u && !rig_->bank->can_redo()) || (k == 7u && !rig_->bank->can_undo());
    const Rect box{cell.x + 3, cell.y + 16, cell.w - 6, cell.h - 18};
    r.panel(box, Color::Empty);
    r.text(box.x + 4, box.y + 1, kToolNames[k], none ? Color::TextDim : Color::Text, Face::Label);
    r.text(box.x + 4, box.y + 22, kToolHints[k], Color::TextDim, Face::Micro);
  }
}

void MainScreen::draw_grid(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  (void)ctx;
  const ui::Theme& theme = r.theme();
  const seq::Playhead ph = rig_->seq->playhead();
  const seq::ChainView chain = rig_->seq->chain();
  r.text(area.x + 10, area.y + 4, "PATTERNS", Color::Text, Face::Label);
  r.text(area.x + 100, area.y + 4, playing_ ? "TAP QUEUES THE NEXT PATTERN, SEVERAL TAPS MAKE A CHAIN" : "TAP SELECTS, MORE TAPS CHAIN BEHIND IT",
         Color::TextDim, Face::Label);
  const int x0 = area.x + 10;
  const int y0 = area.y + 28;
  const int cw = 65;
  const int ch = (area.h - 32) / 4;

  // The display list is short (2048 words) and 48 cells with their texts would not fit if each were drawn whole: the cells are drawn
  // in passes by colour (one colour word a group, not one a cell), the empty ones as one translucent slab each.
  enum Kind : std::uint8_t { kEmpty, kFilled, kChosen, kPlaying };
  Kind kind[kPatternCount];
  Rect cell[kPatternCount];
  for (unsigned i = 0; i < kPatternCount; ++i) {
    cell[i] = Rect{x0 + static_cast<int>(i % 12u) * cw, y0 + static_cast<int>(i / 12u) * ch, cw - 4, ch - 4};
    const bool empty = pattern_is_empty(rig_->bank->pattern(i));
    kind[i] = ph.playing && ph.pattern == static_cast<int>(i) ? kPlaying : (i == sel() ? kChosen : (empty ? kEmpty : kFilled));
  }
  view::translucent(r, 56, [&] {
    for (unsigned i = 0; i < kPatternCount; ++i) {
      if (kind[i] == kEmpty) {
        r.panel(cell[i], Color::Empty);
      }
    }
  });
  static constexpr Color kFill[4] = {Color::Pane, Color::Empty, Color::Sel, Color::Play};
  static constexpr Color kInk[4] = {Color::TextDim, Color::Text, Color::SelText, Color::PlayText};
  for (unsigned k = kFilled; k <= kPlaying; ++k) {
    for (unsigned i = 0; i < kPatternCount; ++i) {
      if (kind[i] == k) {
        r.panel(cell[i], kFill[k]);
      }
    }
  }
  for (unsigned k = kEmpty; k <= kPlaying; ++k) {
    for (unsigned i = 0; i < kPatternCount; ++i) {
      if (kind[i] == k) {
        char name[4];
        pattern_name(i, name, sizeof name);
        r.text(cell[i].x + 6, cell[i].y + 5, name, kInk[k], Face::Data);
      }
    }
  }
  // The notes of each pattern as one bar under its name (and its length when it is not 16).
  unsigned notes[kPatternCount];
  unsigned lengths[kPatternCount];
  for (unsigned i = 0; i < kPatternCount; ++i) {
    const Pattern& p = rig_->bank->pattern(i);
    lengths[i] = p.length != 0u ? p.length : kDefaultLength;
    notes[i] = 0;
    if (kind[i] != kEmpty) {
      for (unsigned s = 0; s < lengths[i]; ++s) {
        notes[i] += (p.steps[s].flags & kStepRest) == 0u ? 1u : 0u;
      }
    }
  }
  for (unsigned i = 0; i < kPatternCount; ++i) {
    if (kind[i] != kEmpty) {
      r.panel(Rect{cell[i].x + 6, cell[i].y + cell[i].h - 12, cell[i].w - 12, 4}, Color::Pane);
    }
  }
  static constexpr Color kBar[4] = {Color::Pane, Color::Instr, Color::SelText, Color::PlayText};
  for (unsigned k = kFilled; k <= kPlaying; ++k) {
    for (unsigned i = 0; i < kPatternCount; ++i) {
      if (kind[i] == k) {
        r.panel(Rect{cell[i].x + 6, cell[i].y + cell[i].h - 12, (cell[i].w - 12) * static_cast<int>(notes[i]) / static_cast<int>(lengths[i]), 4},
                kBar[k]);
      }
    }
  }
  for (unsigned k = kFilled; k <= kPlaying; ++k) {
    for (unsigned i = 0; i < kPatternCount; ++i) {
      if (kind[i] == k && lengths[i] != kDefaultLength) {
        char ln[4];
        view::fmt_uint(lengths[i], ln);
        r.text(cell[i].x + cell[i].w - 6 - r.text_width(ln, Face::Micro), cell[i].y + 8, ln, kInk[k], Face::Micro);
      }
    }
  }
  // The chain: a frame round each pattern in it and its order numbers (every place the pattern has in it).
  for (unsigned i = 0; i < kPatternCount; ++i) {
    char order[12];
    order[0] = '\0';
    bool coming = false;
    for (unsigned c = 0; c < chain.length; ++c) {
      if (chain.pattern[c] == i) {
        char num[4];
        view::fmt_uint(c + 1u, num);
        if (order[0] != '\0') {
          append(order, sizeof order, " ");
        }
        append(order, sizeof order, num);
        coming = coming || (chain.next_entry >= 0 && static_cast<int>(c) >= chain.next_entry);
      }
    }
    if (order[0] == '\0') {
      continue;
    }
    const Color edge = coming ? Color::Accent : Color::TextDim;
    r.frame(cell[i], edge);
    const int bw = r.text_width(order, Face::Label) + 8;
    r.panel(Rect{cell[i].x + cell[i].w - bw, cell[i].y + cell[i].h - 20, bw, 20}, edge);
    r.text(cell[i].x + cell[i].w - bw + 4, ui::caps_y(theme, Face::Label, cell[i].y + cell[i].h - 20, 20), order, Color::AccentText, Face::Label);
  }
}

void MainScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  const Pattern& p = pat();
  const seq::Playhead ph = rig_->seq->playhead();
  const unsigned len = length();
  clamp_cursor();

  draw_info(ctx, r, area);
  const Rect strip_area{area.x, area.y + area.h - app::kKnobStripH, area.w, app::kKnobStripH};

  if (overlay_) {
    draw_grid(ctx, r, Rect{area.x, area.y + kHeroY, area.w, strip_area.y - (area.y + kHeroY) - 2});
  } else {
    // The pitch window: the notes of the pattern, remembered while they are entered; the roll shows at least two octaves.
    int lo = 36;
    int hi = 48;
    if (!pitch_range(p, lo, hi)) {
      lo = 36;
      hi = 48;
    }
    if (win_pattern_ != static_cast<int>(sel()) || win_roll_ != roll_) {
      win_pattern_ = static_cast<int>(sel());
      win_roll_ = roll_;
      win_lo_ = lo;
      win_hi_ = hi;
    }
    win_lo_ = lo < win_lo_ ? lo : win_lo_;
    win_hi_ = hi > win_hi_ ? hi : win_hi_;
    int wlo = win_lo_;
    int whi = win_hi_;
    if (roll_ && whi - wlo < 24) {
      const int mid = (wlo + whi) / 2;
      wlo = mid - 12;
      whi = mid + 12;
    }

    view::Marks m;
    m.page = view_page_;
    m.cursor = cursor_;
    m.playhead = ph.playing && ph.pattern == static_cast<int>(sel()) ? ph.step : -1;
    m.playpoint = ph.point;
    m.held = held_mask();
    m.roll = roll_;
    const std::uint8_t flag = modifier_flag_down();
    const bool motion_view = mod_down_[role_index(PadRole::Motion)];
    for (unsigned k = 0; k < kPageSteps; ++k) {
      const unsigned s = static_cast<unsigned>(view_page_) * 16u + k;
      if (s >= len) {
        break;
      }
      if ((flag != 0u && (p.steps[s].flags & flag) != 0u) || (motion_view && step_has_motion(p, lane_, s))) {
        m.emphasis |= 1u << k;
      }
    }
    const Rect hero{area.x, area.y + kHeroY, area.w, kHeroH};
    m.window = fit_window(wlo, whi, view::lane_height(hero.h, roll_));
    view::draw_steps(r, hero, p, m);
    if (pattern_is_empty(p)) {
      const char* a = "EMPTY PATTERN";
      const char* b = "ARM REC AND PLAY THE KEYS, OR HOLD A STEP PAD AND PLAY A KEY";
      const int cx = view::kGridX + view::kGridW / 2;
      const int cy = hero.y + view::kNumbersH + 70;
      r.text(cx - r.text_width(a, Face::Data) / 2, cy, a, Color::TextDim, Face::Data);
      r.text(cx - r.text_width(b, Face::Label) / 2, cy + 26, b, Color::Empty, Face::Label);
    }

    // The motion lane of the focused SOUND-page key (the last one when the page is another).
    unsigned lane = lane_;
    const bool on_sound = controls_->lane(keys_.focused(), lane);
    if (on_sound) {
      lane_ = lane;
    }
    const Entry lane_entry{Item::Voice, static_cast<std::uint16_t>(lane_), nullptr};
    const core::ParamDesc* d = controls_->desc(ctx.engine(), lane_entry);
    const float knob = d != nullptr ? core::param_norm(*d, controls_->get(ctx.engine(), lane_entry)) : 0.0f;
    view::draw_motion(r, Rect{area.x, hero.y + hero.h + 4, area.w, view::kMotionH}, p, m, lane_,
                      d != nullptr ? d->name : "", knob, on_sound);
  }

  if (tools_visible(ctx)) {
    draw_tools(ctx, r, strip_area);
  } else {
    draw_strip(ctx, r, strip_area);
  }
}

}  // namespace o3_303
