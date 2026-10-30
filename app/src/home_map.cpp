// SPDX-License-Identifier: GPL-3.0-or-later
#include "home_map.h"

#include <o3sdk/app/context.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>
#include <o3_303/perform.h>
#include <o3_303/seq/ops.h>
#include <o3_303/step_model.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <initializer_list>

namespace o3_303 {

namespace {

using namespace o3sdk;
using app::Button;
using app::Input;

constexpr std::uint64_t kToastMs = 1400;

constexpr unsigned role_index(PadRole r) noexcept { return static_cast<unsigned>(r); }

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

// `s` appended to the text in `out` (cap bytes with the terminator).
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

void append_uint(char* out, unsigned cap, unsigned v) noexcept {
  char num[12];
  num[view::fmt_uint(v, num)] = '\0';
  append(out, cap, num);
}

// The coalescing byte a gesture over `count` steps gives every one of its Bank edits: one value for all, so the turns are one undo step (Bank::kJoinOwn: a single
// step keeps its own, as the keys do).
unsigned join_of(const unsigned* steps, unsigned count) noexcept { return count > 1u ? 0x80u | (steps[0] & 0x3Fu) : seq::Bank::kJoinOwn; }

int wrap(int v, int n) noexcept { return n > 0 ? ((v % n) + n) % n : 0; }

// "C2-B3": the keyboard's two octaves at shift `octave` (C2 is the bottom left pad at 0).
void keys_range(int octave, char* out, unsigned cap) noexcept {
  out[0] = '\0';
  for (int i = 0; i < 2; ++i) {
    const int o = 2 + octave + (i == 0 ? 0 : 1);
    append(out, cap, i == 0 ? "C" : "-B");
    if (o < 0) {
      append(out, cap, "-");
    }
    append_uint(out, cap, static_cast<unsigned>(o < 0 ? -o : o));
  }
}

const char* attribute_name(PadRole role) noexcept {
  switch (role) {
    case PadRole::Acc:
      return "ACC";
    case PadRole::Slide:
      return "SLIDE";
    case PadRole::Tie:
      return "TIE";
    default:
      return "REST";
  }
}

}  // namespace

// ---- The PADS line ----------------------------------------------------------------------------------------------------------

void pads_line_text(const PadsLineState& s, char* out, unsigned cap) noexcept {
  out[0] = '\0';
  if (s.overlay) {
    append(out, cap, "48 PATTERNS . TAP = SELECT (PLAYING: QUEUE) . COPY PAT, DUP>NEXT, CLEAR LOCKS: PATTERN SETUP");
    return;
  }
  if (s.attribute != PadRole::None) {
    append(out, cap, "STEPS TOGGLE ");
    append(out, cap, attribute_name(s.attribute));
    return;
  }
  if (s.step_held != 0u && !s.filt) {
    append(out, cap, "KEY = NOTE OF STEP ");
    append_uint(out, cap, s.step_held);
    append(out, cap, " . ACC SLIDE TIE REST TOGGLE IT");
    if (s.turn[0] != '\0') {
      append(out, cap, " . TURN = ");
      append(out, cap, s.turn);
    }
    return;
  }
  append(out, cap, "1-16 STEPS . ACC SLIDE TIE REST . OCT- OCT+ TRNSP FILT . ");
  if (s.filt) {
    append(out, cap, "ROW 3 CUTOFF . ROW 4 RESO");
  } else if (s.rec) {
    append(out, cap, s.playing ? "KEYS RECORD AT THE PLAYHEAD" : "KEYS WRITE AND ADVANCE");
  } else if (s.trnsp) {
    append(out, cap, "KEYS TRANSPOSE THE LINE");
  } else {
    char range[16];
    keys_range(s.octave, range, sizeof range);
    append(out, cap, "KEYS ");
    append(out, cap, range);
    append(out, cap, " PLAY THE VOICE");
  }
}

// ---- Setup and hooks --------------------------------------------------------------------------------------------------------

void HomeMap::init(Rig& rig) noexcept {
  rig_ = &rig;
  controls_ = &rig.controls;
  live_ = rig.live;
  rec_ = false;
  filt_ = false;
  playing_ = false;
  octave_ = 0;
  cursor_ = 0;
  view_page_ = 0;
  last_play_page_ = -1;
  lane_ = 0;
  on_lane_ = false;
  knob_ = Entry{};
  lock_knob_ = Entry{Item::Voice, static_cast<std::uint16_t>(AcidVoice::Cutoff), nullptr};
  row_ = DrawerRow::None;
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
  pattern_down_ = false;
  overlay_ = false;
  chain_started_ = false;
  pattern_used_ = false;
  pattern_down_ms_ = 0;
  toast_[0] = '\0';
  toast_until_ms_ = 0;
  toast_off_ = false;
  lock_active_ = false;
  lock_step_ = 0;
  lock_norm_ = 0.0f;
  lock_text_[0] = '\0';
}

void HomeMap::enter(app::Context& ctx) noexcept {
  for (auto& s : held_step_) {
    s = -1;
  }
  for (bool& d : mod_down_) {
    d = false;
  }
  pattern_down_ = false;
  overlay_ = false;
  row_ = DrawerRow::None;  // a screen shown again: the encoder is the knob's (the ENC text says so)
  refocus();
  ctx.redraw();
}

void HomeMap::leave(app::Context& ctx) noexcept {
  for (unsigned i = 0; i < app::kPadCount; ++i) {
    if (key_note_[i] >= 0) {
      key_release(ctx, i);
    }
  }
  audition_off(ctx);
  pattern_down_ = false;
  overlay_ = false;
}

// ---- Model helpers ----------------------------------------------------------------------------------------------------------

void HomeMap::mark_held_used() noexcept {
  for (unsigned k = 0; k < kPageSteps; ++k) {
    if (held_step_[k] >= 0) {
      step_used_[k] = true;
    }
  }
}

bool HomeMap::any_step_held() const noexcept {
  for (int s : held_step_) {
    if (s >= 0) {
      return true;
    }
  }
  return false;
}

std::uint32_t HomeMap::held_mask() const noexcept {
  std::uint32_t m = 0;
  for (unsigned k = 0; k < kPageSteps; ++k) {
    if (held_step_[k] >= 0) {
      m |= 1u << k;
    }
  }
  return m;
}

unsigned HomeMap::first_held_step() const noexcept {
  for (int s : held_step_) {
    if (s >= 0) {
      return static_cast<unsigned>(s) + 1u;
    }
  }
  return 0;
}

// The step flag of the TR-style modifier held now (ACC, SLIDE, TIE or REST), 0 when none.
std::uint8_t HomeMap::modifier_flag_down() const noexcept {
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

PadRole HomeMap::attribute_down() const noexcept {
  for (PadRole role : {PadRole::Acc, PadRole::Slide, PadRole::Tie, PadRole::Rest}) {
    if (mod_down_[role_index(role)]) {
      return role;
    }
  }
  return PadRole::None;
}

void HomeMap::clamp_cursor() noexcept {
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

void HomeMap::move_cursor(app::Context& ctx, int to) noexcept {
  cursor_ = to;
  clamp_cursor();
  view_page_ = cursor_ / 16;
  ctx.redraw();
}

// The step the RATCHET and COND entries show and edit: the first held step, else the cursor's.
void HomeMap::sync_step_focus() noexcept {
  int s = cursor_;
  for (int h : held_step_) {
    if (h >= 0) {
      s = h;
      break;
    }
  }
  rig_->wiring.step_focus = static_cast<unsigned>(s < 0 ? 0 : s);
}

void HomeMap::toast(app::Context& ctx, const char* text, bool off) noexcept {
  cut(text, sizeof toast_ - 1u, toast_, sizeof toast_);
  toast_off_ = off;
  toast_until_ms_ = ctx.uptime_ms() + kToastMs;
  ctx.redraw();
}

// With a scale chosen on the SEQ page the keyboard keeps to it: the other pads are dark and do nothing. Until the player chooses one
// (the generator's default scale is not a choice) the keyboard is chromatic as it always was; "No scale" and "Chromatic" have every note.
bool HomeMap::scale_locked() const noexcept {
  const Wiring& w = rig_->wiring;
  return w.scale_set && w.gen.scale < core::kScaleCount && core::kScaleDef[w.gen.scale] != 0xFFFu;
}

bool HomeMap::scale_locked_out(int note) const noexcept {
  return scale_locked() && !in_scale(note, rig_->wiring.gen.scale, rig_->wiring.gen.root);
}

// The lane of `p` that drives what `entry` shows (pattern.h's lanes table), -1 when the entry is not a lane target or no lane points at it.
int HomeMap::lane_of(const Entry& entry, const Pattern& p) const noexcept {
  LaneRef ref{kLaneNone, 0};
  if (!controls_->lane_ref(entry, ref)) {
    return -1;
  }
  return seq::find_lane(p, ref.target, ref.id);
}

void HomeMap::knob_latched(const Entry& knob) noexcept {
  knob_ = knob;
  LaneRef ref{kLaneNone, 0};
  if (controls_->lane_ref(knob, ref)) {
    lock_knob_ = knob;
  }
  const int lane = lane_of(knob, pat());
  on_lane_ = lane >= 0;
  if (on_lane_) {
    lane_ = static_cast<unsigned>(lane);
  }
  if (lane_ >= kMotionLanes) {
    lane_ = 0;
  }
}

// ---- update -----------------------------------------------------------------------------------------------------------------

void HomeMap::update(app::Context& ctx) noexcept {
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
  sync_step_focus();
}

// ---- The keyboard -----------------------------------------------------------------------------------------------------------

void HomeMap::audition_on(app::Context& ctx, unsigned note, std::uint8_t step_flags) noexcept {
  audition_off(ctx);
  const std::uint8_t f = note_flags(step_flags);
  (void)ctx.engine().note_on(rig_->wiring.voice, static_cast<std::uint8_t>(note), (f & dsp::kNoteAccent) != 0u ? 127 : 100, f);
  audition_note_ = static_cast<int>(note);
}

void HomeMap::audition_off(app::Context& ctx) noexcept {
  if (audition_note_ >= 0) {
    (void)ctx.engine().note_off(rig_->wiring.voice, static_cast<std::uint8_t>(audition_note_));
    audition_note_ = -1;
  }
}

void HomeMap::key_press(app::Context& ctx, unsigned pad) noexcept {
  const int note = key_note(pad, octave_);
  if (note < 0 || scale_locked_out(note)) {  // not a key, or out of the scale the keyboard is locked to (R8): the pad is dark and does nothing
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
  } else if (playing_ && live_->trnsp()) {
    // TRNSP: the keyboard transposes the running pattern; the bottom left C is no transposition.
    int t = note - kLowestNote;
    t = t < -36 ? -36 : (t > 36 ? 36 : t);
    live_->set_live_transpose(t);
    sound = false;
  }
  if (sound) {
    key_note_[pad] = note;
    const std::uint8_t nf = note_flags(flags);
    (void)ctx.engine().note_on(rig_->wiring.voice, static_cast<std::uint8_t>(note), (nf & dsp::kNoteAccent) != 0u ? 127 : 100, nf);
  }
  ctx.redraw();
}

void HomeMap::key_release(app::Context& ctx, unsigned pad) noexcept {
  if (key_note_[pad] >= 0) {
    (void)ctx.engine().note_off(rig_->wiring.voice, static_cast<std::uint8_t>(key_note_[pad]));
    key_note_[pad] = -1;
  }
}

// FILT on: row 3 is Cutoff, row 4 Reso: 12 places on the knob, the first pad its minimum and the last its maximum. A tap jumps there through a
// 30 ms slew (so a jump does not click and a p-lock or a motion point on the step wins over it). Rec while the pattern plays writes the jump
// under the playhead like a turn.
void HomeMap::ladder_press(app::Context& ctx, unsigned pad) noexcept {
  const unsigned row = app::pad_row(pad);
  const unsigned col = app::pad_col(pad);
  const std::uint8_t id = row == 2u ? static_cast<std::uint8_t>(AcidVoice::Cutoff) : static_cast<std::uint8_t>(AcidVoice::Reso);
  const core::ParamDesc* d = ctx.engine().param_desc(rig_->wiring.voice, id);
  if (d == nullptr) {
    return;
  }
  const float value = core::param_denorm(*d, ladder_norm(col));
  (void)rig_->seq->slew(kLaneVoice, id, value, kLadderSlewMs);
  if (recording_motion()) {
    const seq::Playhead ph = rig_->seq->playhead();
    const unsigned pp = ph.playing && ph.pattern >= 0 ? static_cast<unsigned>(ph.pattern) : sel();
    const int lane = rig_->bank->claim_lane(pp, kLaneVoice, id);
    if (lane >= 0) {
      (void)rig_->seq->record_motion(static_cast<unsigned>(lane), value);
    } else {
      toast(ctx, kLanesUsedToast, true);
    }
  }
  const int shown = seq::find_lane(pat(), kLaneVoice, id);
  if (shown >= 0) {
    lane_ = static_cast<unsigned>(shown);  // the motion lane under the step view follows the parameter the hand is on
  }
  ctx.redraw();
}

// ---- The step pads and the modifier block -----------------------------------------------------------------------------------

void HomeMap::toggle_flag_on_held_or_cursor(app::Context& ctx, std::uint8_t flag) noexcept {
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

void HomeMap::step_press(app::Context& ctx, unsigned k) noexcept {
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

// A modifier pad pressed and released with nothing else done: ACC and SLIDE toggle on the cursor step; TIE and REST do the same,
// except that with REC armed they are entries: stopped, written on the cursor, which advances; playing, written at the playhead's step.
void HomeMap::modifier_tap(app::Context& ctx, PadRole role) noexcept {
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
  if ((flag == kStepTie || flag == kStepRest) && rec_ && playing_) {  // punched in live, like a key: at the playhead's step
    const seq::Playhead ph = rig_->seq->playhead();
    if (ph.playing && ph.step >= 0 && ph.pattern >= 0) {
      const unsigned pp = static_cast<unsigned>(ph.pattern);
      const unsigned st = static_cast<unsigned>(ph.step);
      (void)rig_->bank->write_advance(pp, st, rig_->bank->pattern(pp).steps[st].note, flag);
    }
    ctx.redraw();
    return;
  }
  (void)rig_->bank->toggle_flag(sel(), static_cast<unsigned>(cursor_), flag);
  ctx.redraw();
}

void HomeMap::choose_pattern(app::Context& ctx, unsigned index) noexcept {
  if (index >= kPatternCount) {
    return;
  }
  if (!chain_started_) {
    chain_started_ = true;
    (void)rig_->seq->clear_queue();
    if (!playing_) {
      rig_->bank->select(index);  // stopped: the first tap selects at once, further taps chain behind it
      refocus();
      return;
    }
  }
  (void)rig_->seq->queue(index);  // the chain is the sequencer's: it shows in chain() within a block
  ctx.redraw();
}

void HomeMap::pad_input(app::Context& ctx, const Input& in) noexcept {
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
      case PadRole::OctDown:
      case PadRole::OctUp:
        mod_down_[ri] = false;  // (held only for the LED)
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
        toggle_flag_on_held_or_cursor(ctx, role == PadRole::Acc ? kStepAccent : (role == PadRole::Slide ? kStepSlide : (role == PadRole::Tie ? kStepTie : kStepRest)));
        mod_used_[ri] = true;
      }
      break;
    case PadRole::OctDown:
    case PadRole::OctUp:
      mod_down_[ri] = true;  // (held only for the LED; the ratchet gesture that was hold a step + OCT+ / OCT- is the PLAYS row's now, UX-303 D24)
      octave_ = clamp_octave(octave_ + (role == PadRole::OctUp ? 1 : -1));
      break;
    case PadRole::Trnsp:
      live_->set_trnsp(!live_->trnsp());
      if (!live_->trnsp()) {
        live_->set_live_transpose(0);  // off: the keys play the voice and the line is as written again
      }
      break;
    case PadRole::Filt:
      filt_ = !filt_;
      break;
    case PadRole::Key:
      if (filt_) {  // FILT on: rows 3 and 4 are the CUTOFF and RESO ladders
        ladder_press(ctx, pad);
      } else {  // (a latched WOBBLE does not touch the keys here: its rate picker is PERFORM's row 3, UX-303 5.3 and 6.1 rule 3)
        key_press(ctx, pad);
      }
      break;
    case PadRole::None:
      break;
  }
  ctx.redraw();
}

// ---- Turns ------------------------------------------------------------------------------------------------------------------

// Hold a step + the encoder on the RATCHET or COND entry: every held step moves by the detents from where it stands (a ratchet along x1 .. x4, a
// condition along the encoder's walk, seq::step_cond); one undo step for a run of turns on a step (the Bank joins them).
void HomeMap::step_entry_turn(app::Context& ctx, const Entry& entry, int detents, bool coarse) noexcept {
  mark_held_used();
  for (int s : held_step_) {
    if (s >= 0) {
      controls_->step_at(ctx.engine(), entry, static_cast<unsigned>(s), detents, coarse);
    }
  }
  const unsigned first = rig_->wiring.step_focus;
  char text[24] = "STEP ";
  append_uint(text, sizeof text, first + 1u);
  append(text, sizeof text, entry.item == Item::Ratchet ? " RATCHET " : " COND ");
  char value[16];
  (void)controls_->format(ctx.engine(), entry, value, sizeof value);
  append(text, sizeof text, value);
  toast(ctx, text);
}

// The lock of `entry` on each of `steps`, moved `detents` from where it stands (the knob's own value when the step has none), its lane claimed first
// (D2: one undoable edit), scaled with the target's own descriptor (a unit's parameter) or range (the mixer). Turned below the minimum, a lock is removed
// ("--"); a step with none stays without.
HomeMap::LockResult HomeMap::lock_steps(app::Context& ctx, const Entry& entry, const unsigned* steps, unsigned count, int detents, bool coarse) noexcept {
  LaneRef ref{kLaneNone, 0};
  const core::ParamDesc* d = controls_->desc(ctx.engine(), entry);
  if (d == nullptr || !controls_->lane_ref(entry, ref)) {
    return LockResult::NoTarget;
  }
  const int n = coarse ? detents * static_cast<int>(d->coarse) : detents;
  const unsigned join = join_of(steps, count);
  for (unsigned i = 0; i < count; ++i) {
    const unsigned s = steps[i];
    const int have = seq::find_lane(pat(), ref.target, ref.id);
    const std::uint8_t cur = have >= 0 ? step_lock_point(pat(), static_cast<unsigned>(have), s) : kNoLock;
    const float base = controls_->get(ctx.engine(), entry);
    std::uint8_t pt = 0;
    bool removed = false;
    if (cur == kNoLock) {
      const float v = core::param_step(*d, base, n);
      if (n < 0 && v >= base) {  // the knob is at its minimum already: nothing below it to lock
        continue;
      }
      pt = controls_->to_point(ctx.engine(), entry, v);
    } else if (cur == 0u && n < 0) {  // below the minimum: no lock
      removed = true;
    } else {
      pt = controls_->to_point(ctx.engine(), entry, core::param_step(*d, controls_->from_point(ctx.engine(), entry, cur), n));
    }
    if (removed) {
      (void)rig_->bank->unlock_step(sel(), ref.target, ref.id, s, join);
    } else {
      const int lane = rig_->bank->lock_step(sel(), ref.target, ref.id, s, pt, join);
      if (lane < 0) {  // all eight lanes are in use by other parameters (D2)
        toast(ctx, kLanesUsedToast, true);
        return LockResult::LanesFull;
      }
      lane_ = static_cast<unsigned>(lane);
      on_lane_ = true;
    }
    lock_active_ = any_step_held();
    lock_step_ = static_cast<int>(s);
    lock_norm_ = removed ? 0.0f : static_cast<float>(pt) / 254.0f;
    lock_text_[0] = '\0';
    if (removed) {
      append(lock_text_, sizeof lock_text_, "--");
    } else {
      lock_value_text(ctx, entry, pt, lock_text_, sizeof lock_text_);
    }
  }
  ctx.redraw();
  return LockResult::Done;
}

// Hold a step + the encoder on an entry a lane can drive: a parameter lock on each held step.
bool HomeMap::lock_turn(app::Context& ctx, const Entry& entry, int detents, bool coarse) noexcept {
  unsigned steps[kPageSteps];
  unsigned count = 0;
  for (int s : held_step_) {
    if (s >= 0) {
      steps[count++] = static_cast<unsigned>(s);
    }
  }
  LaneRef ref{kLaneNone, 0};
  if (!controls_->lane_ref(entry, ref)) {
    return false;
  }
  mark_held_used();
  return lock_steps(ctx, entry, steps, count, detents, coarse) != LockResult::NoTarget;
}

// "420Hz": the value of motion point `point` of `entry` and the first three letters of its unit.
void HomeMap::lock_value_text(app::Context& ctx, const Entry& entry, std::uint8_t point, char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  const core::ParamDesc* d = controls_->desc(ctx.engine(), entry);
  if (d == nullptr) {
    return;
  }
  char v[16];
  (void)core::param_format(*d, controls_->from_point(ctx.engine(), entry, point), v, sizeof v);
  append(out, cap, v);
  char unit[8];
  cut(d->unit_text, 3, unit, sizeof unit);
  append(out, cap, unit);
}

// ---- The drawer rows (UX-303 2.3): what the colour buttons latch and what the encoder then walks ----------------------------------

unsigned HomeMap::row_steps(unsigned (&out)[kPageSteps]) noexcept {
  unsigned n = 0;
  for (int s : held_step_) {
    if (s >= 0) {
      out[n++] = static_cast<unsigned>(s);
    }
  }
  if (n == 0u) {
    clamp_cursor();
    out[n++] = static_cast<unsigned>(cursor_);
  } else {
    mark_held_used();  // the held steps took the turn: their release does not select
  }
  return n;
}

// A tap of Note / Instr / Vol / Fx: the encoder edits that row from now on, until an F key gives it back to the knob.
void HomeMap::latch_row(app::Context& ctx, DrawerRow row) noexcept {
  row_ = row;
  ctx.redraw();
}

void HomeMap::row_text(app::Context& ctx, char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  switch (row_) {
    case DrawerRow::Note:
      append(out, cap, "NOTE");
      break;
    case DrawerRow::Gate:
      append(out, cap, "GATE");
      break;
    case DrawerRow::Plays:
      append(out, cap, "PLAYS");
      break;
    case DrawerRow::Lock:
      append(out, cap, "LOCK ");
      append(out, cap, controls_->label(ctx.engine(), lock_knob_));
      break;
    case DrawerRow::None:
      break;
  }
}

void HomeMap::row_turn(app::Context& ctx, int detents, bool coarse) noexcept {
  unsigned steps[kPageSteps];
  const unsigned count = row_steps(steps);
  const unsigned join = join_of(steps, count);
  seq::Bank& bank = *rig_->bank;
  char text[48] = "STEP ";
  append_uint(text, sizeof text, steps[0] + 1u);
  switch (row_) {
    case DrawerRow::Note: {  // the semitone; Shift the octave
      const int d = detents * (coarse ? 12 : 1);
      const int low = static_cast<int>(kRowNoteLow);
      const int high = static_cast<int>(kRowNoteHigh);
      for (unsigned i = 0; i < count; ++i) {
        const int was = pat().steps[steps[i]].note;
        int n = was + d;
        if (was < low) {  // outside C1..C5 a turn moves the note by its own amount, toward the range only (never into the other side of it)
          n = d > 0 ? (n > high ? high : n) : was;
        } else if (was > high) {
          n = d < 0 ? (n < low ? low : n) : was;
        } else {
          n = n < low ? low : (n > high ? high : n);
        }
        (void)bank.set_note(sel(), steps[i], static_cast<unsigned>(n), join);
      }
      char name[8];
      (void)note_name(pat().steps[steps[0]].note, name, sizeof name);
      append(text, sizeof text, " NOTE ");
      append(text, sizeof text, name);
      break;
    }
    case DrawerRow::Gate:
      for (unsigned i = 0; i < count; ++i) {
        const int at = static_cast<int>(seq::gate_list_index(pat().steps[steps[i]].flags)) + detents;
        const int top = static_cast<int>(seq::kGateListCount) - 1;
        (void)bank.set_gate_list(sel(), steps[i], static_cast<unsigned>(at < 0 ? 0 : (at > top ? top : at)), join);
      }
      append(text, sizeof text, " GATE ");
      append(text, sizeof text, gate_name(seq::gate_list_index(pat().steps[steps[0]].flags)));
      break;
    case DrawerRow::Plays:  // the list of D8, Shift four places
      for (unsigned i = 0; i < count; ++i) {
        const Step& st = pat().steps[steps[i]];
        (void)bank.set_plays(sel(), steps[i], seq::plays_step(st.flags, st.cond, detents * (coarse ? 4 : 1)), join);
      }
      append(text, sizeof text, " PLAYS ");
      {
        char plays[12];
        (void)plays_text(pat().steps[steps[0]].flags, pat().steps[steps[0]].cond, plays, sizeof plays);
        append(text, sizeof text, plays);
      }
      break;
    case DrawerRow::Lock:
      if (lock_steps(ctx, lock_knob_, steps, count, detents, coarse) != LockResult::Done) {  // (the lanes-used toast is up)
        return;
      }
      append(text, sizeof text, " LOCK ");
      append(text, sizeof text, controls_->label(ctx.engine(), lock_knob_));
      append(text, sizeof text, " ");
      {
        const int lane = lane_of(lock_knob_, pat());
        const std::uint8_t pt = lane >= 0 ? step_lock_point(pat(), static_cast<unsigned>(lane), steps[0]) : kNoLock;
        if (pt == kNoLock) {
          append(text, sizeof text, "--");
        } else {
          char v[16];
          lock_value_text(ctx, lock_knob_, pt, v, sizeof v);
          append(text, sizeof text, v);
        }
      }
      break;
    case DrawerRow::None:
      return;
  }
  toast(ctx, text);
}

bool HomeMap::turn(app::Context& ctx, int detents, bool coarse, const Entry& knob) noexcept {
  playing_ = ctx.engine().playing();
  if (row_ != DrawerRow::None) {  // a colour button latched a row: it takes the turn, on the held steps or the cursor's
    row_turn(ctx, detents, coarse);
    return true;
  }
  if (!any_step_held()) {
    return false;
  }
  if (Controls::is_step_entry(knob)) {  // hold a step + the encoder on RATCHET or COND
    step_entry_turn(ctx, knob, detents, coarse);
    return true;
  }
  return lock_turn(ctx, knob, detents, coarse);  // hold a step + the encoder: the lock of the knob, on the lane that drives it
}

void HomeMap::turned(app::Context& ctx, const Entry& knob) noexcept {
  playing_ = ctx.engine().playing();
  if (!recording_motion()) {
    return;
  }
  // Rec while the pattern plays: the new value is written as motion under the playhead, on the lane of the playing pattern (claimed when none drives it).
  LaneRef ref{kLaneNone, 0};
  if (!controls_->lane_ref(knob, ref)) {
    return;
  }
  const seq::Playhead ph = rig_->seq->playhead();
  const unsigned pp = ph.playing && ph.pattern >= 0 ? static_cast<unsigned>(ph.pattern) : sel();
  const int lane = rig_->bank->claim_lane(pp, ref.target, ref.id);
  if (lane < 0) {
    toast(ctx, kLanesUsedToast, true);
    return;
  }
  lane_ = static_cast<unsigned>(lane);
  (void)rig_->seq->record_motion(static_cast<unsigned>(lane), controls_->to_lane(knob, controls_->get(ctx.engine(), knob)));
}

// ---- Buttons ----------------------------------------------------------------------------------------------------------------

// Hold ACC (or SLIDE) + left / right: that layer of the pattern alone moves one step (R3); the notes and everything else stay.
void HomeMap::rotate_layers(app::Context& ctx, int direction) noexcept {
  const bool acc = mod_down_[role_index(PadRole::Acc)];
  const bool slide = mod_down_[role_index(PadRole::Slide)];
  if (acc) {
    (void)rig_->bank->rotate_layer(sel(), seq::Layer::Accent, direction);
    mod_used_[role_index(PadRole::Acc)] = true;  // the release is no toggle of the cursor step's accent
  }
  if (slide) {
    (void)rig_->bank->rotate_layer(sel(), seq::Layer::Slide, direction);
    mod_used_[role_index(PadRole::Slide)] = true;
  }
  const char* what = acc && slide ? "ACCENTS AND SLIDES" : (acc ? "ACCENTS" : "SLIDES");
  char text[40];
  cut(what, 20, text, sizeof text);
  append(text, sizeof text, direction > 0 ? " ROTATED RIGHT" : " ROTATED LEFT");
  toast(ctx, text);
}

// Left / Right: the cursor (across the pages); ACC or SLIDE held: that layer one step; Shift: the whole pattern one step. Up / Down: the note of the
// cursor step, or of every held step, a semitone; Shift: an octave.
void HomeMap::arrow(app::Context& ctx, Button which, bool shift) noexcept {
  if (which == Button::Left || which == Button::Right) {
    const int d = which == Button::Right ? 1 : -1;
    if (mod_down_[role_index(PadRole::Acc)] || mod_down_[role_index(PadRole::Slide)]) {  // hold ACC / SLIDE + left / right (R3)
      rotate_layers(ctx, d);
    } else if (shift) {
      (void)rig_->bank->rotate(sel(), d);
      toast(ctx, d > 0 ? "ROTATED RIGHT" : "ROTATED LEFT");
    } else {
      move_cursor(ctx, wrap(cursor_ + d, static_cast<int>(length())));
    }
    return;
  }
  const int d = (which == Button::Up ? 1 : -1) * (shift ? 12 : 1);
  const auto move = [&](unsigned step) {
    const int n = static_cast<int>(pat().steps[step].note) + d;
    if (n >= 0 && n <= 127) {
      (void)rig_->bank->set_note(sel(), step, static_cast<unsigned>(n));
    }
  };
  if (any_step_held()) {  // hold a step + up / down: the same on the held steps
    mark_held_used();
    for (int s : held_step_) {
      if (s >= 0) {
        move(static_cast<unsigned>(s));
      }
    }
  } else {
    move(static_cast<unsigned>(cursor_));
  }
  ctx.redraw();
}

void HomeMap::copy_buttons(app::Context& ctx, Button which, bool shift) noexcept {
  seq::Bank& bank = *rig_->bank;
  if (pattern_down_) {  // Pattern held: the pads are the patterns, and Copy / Insert / Delete are PATTERN SETUP's (COPY PAT, DUP>NEXT, CLEAR LOCKS): nothing here
    pattern_used_ = true;
    toast(ctx, "COPY PAT, DUP>NEXT, CLEAR LOCKS: PATTERN SETUP", true);
    return;
  }
  switch (which) {
    case Button::Copy:
      if (shift) {
        toast(ctx, bank.paste_step(sel(), static_cast<unsigned>(cursor_)) ? "STEP PASTED" : "NOTHING COPIED");
      } else {
        bank.copy_step(sel(), static_cast<unsigned>(cursor_));
        toast(ctx, "STEP COPIED");
      }
      break;
    case Button::Insert:
      if (shift) {  // Shift + Insert (Home): the cursor to step 1
        move_cursor(ctx, 0);
      } else {
        (void)bank.insert_step(sel(), static_cast<unsigned>(cursor_));
      }
      break;
    default: {  // Delete
      if (shift) {  // Shift + Delete (Backspace): the cursor step goes and the rest is pulled left
        (void)bank.delete_step(sel(), static_cast<unsigned>(cursor_));
        break;
      }
      // Delete: the step is cleared whole (a rest, no ratchet, no condition, no locks), the held steps' or the cursor's, as one edit each.
      const auto clear = [&](unsigned s) {
        (void)bank.edit(sel(), [&](Pattern& p) {
          p.steps[s].flags = kStepRest;
          p.steps[s].cond = kCondAlways;
          seq::clear_step_motion(p, s);
        });
      };
      if (any_step_held()) {
        mark_held_used();
        for (int s : held_step_) {
          if (s >= 0) {
            clear(static_cast<unsigned>(s));
          }
        }
      } else {
        clear(static_cast<unsigned>(cursor_));
      }
      break;
    }
  }
  clamp_cursor();
  ctx.redraw();
}

bool HomeMap::input(app::Context& ctx, const Input& in) noexcept {
  playing_ = ctx.engine().playing();
  if (in.kind == core::InputKind::Pad) {
    pad_input(ctx, in);
    sync_step_focus();
    return true;
  }
  if (in.kind != core::InputKind::Button) {
    return false;
  }
  const Button which = in.which();
  bool taken = true;
  if (in.type == Input::Type::Release) {
    switch (which) {
      case Button::Note:
      case Button::Instr:
      case Button::Vol:
      case Button::Fx:  // (a row is latched at the press; the live actions of these buttons are PERFORM's)
        break;
      case Button::Pattern:
        taken = pattern_down_;
        if (pattern_down_) {
          pattern_down_ = false;
          overlay_ = false;
          chain_started_ = false;
          ctx.redraw();
        }
        break;
      default:
        taken = false;
        break;
    }
  } else {
    switch (which) {
      case Button::Rec:
        rec_ = !rec_;
        ctx.redraw();
        break;
      case Button::Left:
      case Button::Right:
      case Button::Up:
      case Button::Down:
        arrow(ctx, which, in.shift);
        break;
      case Button::Note:
        latch_row(ctx, DrawerRow::Note);
        break;
      case Button::Instr:
        latch_row(ctx, DrawerRow::Gate);
        break;
      case Button::Vol:
        latch_row(ctx, DrawerRow::Plays);
        break;
      case Button::Fx:  // Shift + the button is the same as the button alone (D22): Shift matters for the turn
        latch_row(ctx, DrawerRow::Lock);
        break;
      case Button::Copy:
      case Button::Insert:
      case Button::Delete:
        copy_buttons(ctx, which, in.shift);
        break;
      case Button::Pattern:
        pattern_down_ = true;
        overlay_ = false;
        chain_started_ = false;
        pattern_used_ = false;
        pattern_down_ms_ = ctx.uptime_ms();
        break;
      default:
        taken = false;
        break;
    }
  }
  if (taken) {
    sync_step_focus();
  }
  return taken;
}

// ---- LEDs -------------------------------------------------------------------------------------------------------------------

// FILT on: rows 3 and 4 draw where Cutoff and Reso stand, a bar from the first pad to the position, the position itself at full level.
void HomeMap::ladder_leds(app::Context& ctx, app::PadLeds& leds) const noexcept {
  for (unsigned row = 2; row < app::kPadRows; ++row) {
    const std::uint16_t id = row == 2u ? static_cast<std::uint16_t>(AcidVoice::Cutoff) : static_cast<std::uint16_t>(AcidVoice::Reso);
    const core::ParamDesc* d = ctx.engine().param_desc(rig_->wiring.voice, id);
    const unsigned pos = d != nullptr ? ladder_col(core::param_norm(*d, ctx.engine().param(rig_->wiring.voice, id))) : 0u;
    for (unsigned col = 0; col < kLadderSteps; ++col) {
      leds.set(app::pad_at(row, col), col < pos ? app::kLedMid : (col == pos ? app::kLedOn : app::kLedOff));
    }
  }
}

void HomeMap::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
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
    } else if (info.kind == Cell::Note) {
      level = info.accent ? app::kLedLow : app::kLedDim;
    } else if (info.kind == Cell::Tie) {
      level = app::kLedDim;
    }
    if (playing_here && ph.step == static_cast<int>(s)) {
      level = app::kLedOn;
    }
    if (static_cast<int>(s) == cursor_ && flag == 0u) {
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

  // The modifier block: low; mid when the cursor's step has the attribute, on while held. OCT- and OCT+ low, on while held; TRNSP and FILT low, on when active.
  const Step& cs = p.steps[cursor_ < static_cast<int>(len) ? cursor_ : 0];
  const auto mod_level = [&](PadRole role, bool lit) -> std::uint8_t {
    return mod_down_[role_index(role)] ? app::kLedOn : (lit ? app::kLedMid : app::kLedLow);
  };
  leds.set(role_pad(PadRole::Acc), mod_level(PadRole::Acc, (cs.flags & kStepAccent) != 0u));
  leds.set(role_pad(PadRole::Slide), mod_level(PadRole::Slide, (cs.flags & kStepSlide) != 0u));
  leds.set(role_pad(PadRole::Tie), mod_level(PadRole::Tie, (cs.flags & kStepTie) != 0u));
  leds.set(role_pad(PadRole::Rest), mod_level(PadRole::Rest, (cs.flags & kStepRest) != 0u));
  leds.set(role_pad(PadRole::OctDown), mod_down_[role_index(PadRole::OctDown)] ? app::kLedOn : app::kLedLow);
  leds.set(role_pad(PadRole::OctUp), mod_down_[role_index(PadRole::OctUp)] ? app::kLedOn : app::kLedLow);
  leds.set(role_pad(PadRole::Trnsp), live_->trnsp() ? app::kLedOn : app::kLedLow);
  leds.set(role_pad(PadRole::Filt), filt_ ? app::kLedOn : app::kLedLow);

  if (filt_) {  // rows 3 and 4 are the ladders, drawn as bars
    ladder_leds(ctx, leds);
    return;
  }

  // The keyboard: naturals dim, sharps off, the note of the cursor's step mid (of the playhead's, full level); out of the scale it is locked to, dark.
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
    if (scale_locked_out(n)) {
      leds.set(pad, app::kLedOff);
      continue;
    }
    // The piano drawn with one colour: naturals dim, sharps off. With a scale chosen, lit means playable: every pad of the scale is dim.
    std::uint8_t level = is_natural(static_cast<unsigned>(n)) || scale_locked() ? app::kLedDim : app::kLedOff;
    if (playing_ && live_->trnsp() && !rec_ && n == kLowestNote + live_->live_transpose()) {
      level = app::kLedMid;  // the key the running pattern is transposed by
    }
    if (n == current) {
      level = playing_here ? app::kLedOn : app::kLedMid;
    }
    if (ctx.panel().pad_held(pad)) {
      level = app::kLedOn;
    }
    leds.set(pad, level);
  }
}

// ---- What the screen reads --------------------------------------------------------------------------------------------------

view::Marks HomeMap::drawer(app::Context& ctx) const noexcept {
  const seq::Playhead ph = rig_->seq->playhead();
  const Pattern& p = pat();
  const unsigned len = length();
  view::Marks m;
  m.page = view_page_;
  m.cursor = cursor_;
  m.playhead = ph.playing && ph.pattern == static_cast<int>(sel()) ? ph.step : -1;
  m.playpoint = ph.point;
  m.held = held_mask();
  m.row = row_;
  const std::uint8_t flag = modifier_flag_down();
  const int lane = lane_of(lock_knob_, p);  // the LOCK row: the lock knob's lane
  for (unsigned k = 0; k < kPageSteps; ++k) {
    const unsigned s = static_cast<unsigned>(view_page_) * 16u + k;
    if (s >= len) {
      break;
    }
    if (flag != 0u && (p.steps[s].flags & flag) != 0u) {
      m.emphasis |= 1u << k;
    }
    const std::uint8_t pt = lane >= 0 ? step_lock_point(p, static_cast<unsigned>(lane), s) : kNoLock;
    if (pt != kNoLock) {
      char text[16];
      lock_value_text(ctx, lock_knob_, pt, text, sizeof text);
      cut(text, view::kLockChars - 1u, m.lock[k], view::kLockChars);
    }
  }
  m.focus = Controls::is_step_entry(knob_) ? static_cast<int>(rig_->wiring.step_focus) : -1;  // the step RATCHET / COND edit
  return m;
}

unsigned HomeMap::step_box(app::Context& ctx, char* out, unsigned cap) const noexcept {
  const Pattern& p = pat();
  const unsigned len = length();
  unsigned s = first_held_step() != 0u ? first_held_step() - 1u : static_cast<unsigned>(cursor_ < 0 ? 0 : cursor_);
  s = s < len ? s : len - 1u;
  (void)step_text(p, s, out, cap);
  // The lock of the LOCK row's knob on this step.
  const int lane = lane_of(lock_knob_, p);
  const std::uint8_t pt = lane >= 0 ? step_lock_point(p, static_cast<unsigned>(lane), s) : kNoLock;
  if (pt != kNoLock) {
    char v[16];
    lock_value_text(ctx, lock_knob_, pt, v, sizeof v);
    append(out, cap, " ");
    append(out, cap, controls_->label(ctx.engine(), lock_knob_));
    append(out, cap, " ");
    append(out, cap, v);
  }
  unsigned n = 0;
  while (n < cap && out[n] != '\0') {
    ++n;
  }
  return n;
}

void HomeMap::status(frame::StatusView& out) const noexcept {
  out.trnsp = live_->trnsp();
  out.octave = octave_;
  out.rec = rec_;
  out.actions = live_->on();
  out.toast = toast_;
  out.toast_off = toast_off_;
}

void HomeMap::pads_line(app::Context& ctx, char* out, unsigned cap) const noexcept {
  PadsLineState s;
  s.overlay = overlay_;
  s.playing = playing_;
  s.rec = rec_;
  s.trnsp = live_->trnsp();
  s.filt = filt_;
  s.octave = octave_;
  s.step_held = first_held_step();
  s.attribute = attribute_down();
  char turn[24];
  turn[0] = '\0';
  if (s.step_held != 0u) {
    if (row_ != DrawerRow::None) {
      row_text(ctx, turn, sizeof turn);
    } else if (Controls::is_step_entry(knob_)) {
      cut(knob_.item == Item::Ratchet ? "RATCHET" : "COND", 7, turn, sizeof turn);
    } else {
      LaneRef ref{kLaneNone, 0};
      if (controls_->lane_ref(knob_, ref)) {
        cut("LOCK ", 5, turn, sizeof turn);
        append(turn, sizeof turn, controls_->label(ctx.engine(), knob_));
      }
    }
  }
  s.turn = turn;
  pads_line_text(s, out, cap);
}

}  // namespace o3_303
