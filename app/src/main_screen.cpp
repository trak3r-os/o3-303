// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_screen.h"

#include "frame.h"
#include "knob_strip.h"
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

constexpr std::uint64_t kToolsDelayMs = 600;  // Shift held and the encoder still this long: the tools row
constexpr int kInfoH = 34;
constexpr int kHeroY = frame::kStatusH + kInfoH + 4;  // under the status line and the info strip
constexpr int kHeroH = 236;                           // the motion lane and the PADS line are under it, over the knob strip

constexpr const char* kToolNames[app::kFKeyCount] = {"RANDOM", "MUTATE", "REVERSE", "INVERT", "DOUBLE", "CLEAR", "REDO", "UNDO"};
constexpr const char* kToolHints[app::kFKeyCount] = {"new line", "tweak 25%", "backwards", "flip pitch", "length x2", "all rests", "step forward", "step back"};

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

}  // namespace

// ---- Setup and screen hooks -------------------------------------------------------------------------------------------------

void MainScreen::init(Rig& rig) noexcept {
  // Every piece of state starts afresh: the app object outlives a run in the simulator's tests.
  rig_ = &rig;
  controls_ = &rig.controls;
  live_ = rig.live;
  home_.init(rig);
  keys_ = PageKeys{};
  keys_.set_page(kPageSound);
  roll_ = false;
  win_pattern_ = -1;
  win_lo_ = 0;
  win_hi_ = 0;
  win_roll_ = false;
  enc_latch_ = EncLatch::None;
  master_down_ = false;
  master_used_ = false;
  master_shift_ = false;
  master_down_ms_ = 0;
  nudge_pct_ = 0;
  nudge_base_ = 0;
  tool_mask_ = 0;
  shift_ms_ = 0;
  last_turn_ms_ = 0;
  random_open_ = false;
  random_focus_ = 0;
}

const char* MainScreen::title() const noexcept {
  if (home_.overlay()) {
    return "PATTERNS";
  }
  return roll_ ? "ROLL" : "STEPS";
}

void MainScreen::enter(app::Context& ctx) noexcept {
  home_.enter(ctx);
  master_down_ = false;
}

void MainScreen::leave(app::Context& ctx) noexcept {
  home_.leave(ctx);
  keys_.release_all();
  live_->snap_finish(ctx, true);
  random_open_ = false;
  nudge_end(ctx);  // a tempo nudge ends with the screen: its release would reach another one
  master_down_ = false;
  live_->leave(ctx);  // the actions held end with the screen (their releases would reach another one)
  tool_mask_ = 0;
}

// A seed that differs from press to press and from boot to boot: the sequence of the last one and the time of this press.
std::uint32_t MainScreen::next_seed(app::Context& ctx) noexcept { return live_->next_seed(ctx); }

void MainScreen::update(app::Context& ctx) noexcept {
  home_.update(ctx);
  home_.knob_latched(keys_.focused());  // the lane the drawer shows follows the knob the encoder owns (and the pattern under it)
  // (LiveActions::update, from the app's own, ends the keyboard's transposition with the run and stores a morph's blend.)
}

// ---- Input ------------------------------------------------------------------------------------------------------------------

void MainScreen::input(app::Context& ctx, const Input& in) noexcept {
  if (in.kind == core::InputKind::Pad) {
    (void)home_.input(ctx, in);
  } else if (in.kind == core::InputKind::Encoder) {
    turn_input(ctx, in);
  } else if (in.kind == core::InputKind::Button) {
    button_input(ctx, in);
  }
  home_.sync_step_focus();
}

void MainScreen::turn_input(app::Context& ctx, const Input& in) noexcept {
  if (in.delta() == 0) {
    return;
  }
  last_turn_ms_ = ctx.uptime_ms();
  if (master_down_) {  // hold Master + turn: the tempo nudge (R7)
    master_turn(ctx, in.delta());
    return;
  }
  const Entry& focused = keys_.focused();
  if (home_.turn(ctx, in.delta(), in.shift, focused)) {  // hold a step + the encoder: the lock of the knob (or the step's ratchet or condition)
    keys_.mark_turned();  // with an F key held, its release gives the latch back, as after any hold-and-turn
    return;
  }
  if (enc_latch_ != EncLatch::None && keys_.held() == 0u) {  // Master's tap latched TEMPO or SWING on the encoder
    latch_turn(ctx, in.delta(), in.shift);
    return;
  }
  if (random_open_) {  // the RANDOM popup has the encoder: the setting its key latched
    controls_->step(ctx.engine(), random_entry(random_focus_), in.delta(), in.shift);
    ctx.redraw();
    return;
  }
  if (on_snap_page()) {  // the SNAP page's encoder is the morph
    snap_turn(ctx, in.delta(), in.shift);
    return;
  }
  const std::uint32_t affected = keys_.held() != 0u ? keys_.held() : (1u << keys_.focus());
  const Page& pg = keys_.page();
  (void)keys_.input(ctx, in, *controls_);
  for (unsigned k = 0; k < kKeys; ++k) {  // Rec while the pattern plays: the move is written as motion under the playhead (the home map's)
    if ((affected >> k & 1u) != 0u) {
      home_.turned(ctx, pg.entry[k]);
    }
  }
}

void MainScreen::tool(app::Context& ctx, unsigned index) noexcept {
  seq::Bank& bank = *rig_->bank;
  switch (index) {
    case 0:  // the RANDOM popup (C3): its F8 makes the line
      random_toggle(ctx);
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
  home_.refocus();
}

void MainScreen::button_input(app::Context& ctx, const Input& in) noexcept {
  if (app::is_fkey(in.which())) {
    const unsigned k = app::fkey_index(in.which());
    if (in.type == Input::Type::Press) {
      if (in.shift) {  // Shift + F: the tools row; on the SNAP page it stores the snapshot
        tool_mask_ |= 1u << k;
        if (on_snap_page() && !random_open_) {
          snap_press(ctx, k, true);
        } else {
          tool(ctx, k);
        }
        ctx.redraw();
        return;
      }
      enc_latch_ = EncLatch::None;  // the encoder is the page's again (Master's tap had it on TEMPO or SWING)
      if (random_open_ || on_snap_page()) {  // the popup's keys and the snapshots are not the pages' latches
        tool_mask_ |= 1u << k;
        if (random_open_) {
          random_press(ctx, k);
        } else {
          snap_press(ctx, k, false);
        }
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

  if (in.which() == Button::Pattern && in.type == Input::Type::Release && home_.pattern_tap()) {
    roll_ = !roll_;  // a tap on the screen's own button: the step view and the piano roll
  }
  if (home_.input(ctx, in)) {  // the arrows, Copy / Insert / Delete, Rec, the colour buttons, Pattern
    return;
  }

  if (in.type == Input::Type::Release) {
    if (in.which() == Button::Master) {
      master_release(ctx);
    }
    return;
  }

  switch (in.which()) {
    case Button::Params:
      if (random_open_) {  // the popup closes first
        random_open_ = false;
      } else {
        const bool was_snap = on_snap_page();
        if (in.shift) {  // the SNAP page, and back from it
          keys_.toggle_snap();
        } else {
          keys_.next_page();
        }
        if (was_snap && !on_snap_page()) {
          live_->snap_finish(ctx, true);
        }
      }
      ctx.redraw();
      break;
    case Button::Master:
      master_press(ctx, in.shift);
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
  if (!ctx.panel().shift() || on_snap_page()) {  // Shift + F on the SNAP page stores: no tools row there
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
  // No labels: the knob strip's boxes carry the F-number and the name (S2), so the key row is the keys alone and there is no second label row.
}

void MainScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept { home_.leds(ctx, leds); }

// ---- Drawing ----------------------------------------------------------------------------------------------------------------

// What the encoder moves, as the status line says it ("ENC > F1 CUTOFF"): the F key latched (or held), a latch of Master, the popup's setting, the morph.
void MainScreen::enc_text(app::Context& ctx, char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  if (master_down_) {
    append(out, cap, "TEMPO NUDGE");
  } else if (enc_latch_ != EncLatch::None) {
    append(out, cap, enc_latch_ == EncLatch::Tempo ? "TEMPO" : "SWING");
  } else if (random_open_) {
    char fk[4] = {'F', static_cast<char>('1' + random_focus_), ' ', '\0'};
    append(out, cap, fk);
    append(out, cap, controls_->label(ctx.engine(), random_entry(random_focus_)));
  } else if (on_snap_page()) {
    append(out, cap, "MORPH");
  } else {
    char fk[4] = {'F', static_cast<char>('1' + keys_.focus()), ' ', '\0'};
    append(out, cap, fk);
    append(out, cap, controls_->label(ctx.engine(), keys_.focused()));
  }
}

// The info strip under the status line: the pattern's name, its LEN / SW / GATE / TR, the pattern queued next, and the STEP box (the held or cursor
// step whole) on the right. The keyboard's mode, Rec, the actions that are on and the toast are the status line's.
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
  // The pattern's playback transpose (the SEQ page's TRANSP).
  char tr[8];
  n = 0;
  tr[n++] = p.transpose < 0 ? '-' : '+';
  n += view::fmt_uint(static_cast<unsigned>(p.transpose < 0 ? -p.transpose : p.transpose), tr + n);
  tr[n] = '\0';
  row.field("TR", Face::Data, tr, Color::ChromeDim, Color::ChromeText);
  row.skip(6);
  if (ph.playing && ph.queued >= 0) {
    char next[16] = "NEXT ";
    char qn[4];
    pattern_name(static_cast<unsigned>(ph.queued), qn, sizeof qn);
    append(next, sizeof next, qn);
    row.chip(Face::Label, next, Color::ChromeDim, Color::Chrome);
  }

  // The STEP box, right aligned: "STEP 5: D#2 ACC SLD x2 CUTOFF 420Hz".
  char box[64];
  (void)home_.step_box(ctx, box, sizeof box);
  r.text(strip.x + strip.w - 8 - r.text_width(box, Face::Data), ui::caps_y(theme, Face::Data, strip.y, strip.h), box, Color::ChromeText, Face::Data);
}

// The page tabs in the corner (the page the F keys are on, and the others one PARAMS away), a hint after them, and the state chips.
void MainScreen::draw_tabs(app::Context& ctx, ui::Renderer& r, const Rect& area, const char* hint) noexcept {
  r.panel(area, Color::Pane);
  r.rule(area.x, area.y, area.w, ui::Axis::Horizontal, Color::Empty);
  int tx = area.x + 6;
  for (unsigned i = 0; i < kPageCount; ++i) {
    const char* nm = page_at(i).name;
    const int w = r.text_width(nm, Face::Micro);
    const bool cur = hint == nullptr && i == keys_.page_index();
    r.text(tx, area.y + 3, nm, cur ? Color::Text : Color::Empty, Face::Micro);
    if (cur) {
      r.rule(tx, area.y + 14, w, ui::Axis::Horizontal, Color::Sel);
    }
    tx += w + 12;
  }
  if (hint != nullptr) {
    r.text(tx + 4, area.y + 3, hint, Color::TextDim, Face::Micro);
  }
  draw_state_chips(ctx, r, area);
}

// The knob of an entry: its name, value and unit as the page shows them, the value as the fill.
void MainScreen::fill_knob(app::Context& ctx, unsigned k, const Entry& entry, view::Knob& out) noexcept {
  static constexpr const char* kFKeys[kKeys] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
  dsp::Engine& e = ctx.engine();
  const core::ParamDesc* d = controls_->desc(e, entry);
  out = view::Knob{};
  out.fkey = kFKeys[k];
  if (d == nullptr) {
    out.blank = true;
    return;
  }
  out.label = controls_->label(e, entry);
  (void)controls_->format(e, entry, out.value, sizeof out.value);
  out.unit = controls_->unit_text(e, entry);
  out.norm = core::param_norm(*d, controls_->get(e, entry));
}

void MainScreen::draw_strip(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  const Page& pg = keys_.page();
  draw_tabs(ctx, r, area, nullptr);
  view::Knob knobs[view::kKnobs];
  char label[16];
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    view::Knob& kn = knobs[k];
    fill_knob(ctx, k, pg.entry[k], kn);
    if (kn.blank) {
      continue;
    }
    kn.focus = enc_latch_ == EncLatch::None && k == keys_.focus();
    kn.held = (keys_.held() >> k & 1u) != 0u;
    if (home_.lock_active() && kn.focus) {  // a step is held and the encoder is locking this key on it: its value, in Accent
      char num[4];
      view::fmt_uint(static_cast<unsigned>(home_.lock_step() + 1), num);
      cut("LOCK ", 5, label, sizeof label);
      append(label, sizeof label, num);
      kn.label = label;
      cut(home_.lock_text(), sizeof kn.value - 1u, kn.value, sizeof kn.value);
      kn.unit = "";
      kn.norm = home_.lock_norm();
      kn.locked = true;
    }
  }
  view::draw_knobs(r, area, knobs);
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
  r.text(area.x + 100, area.y + 4, home_.playing() ? "TAP QUEUES THE NEXT PATTERN, SEVERAL TAPS MAKE A CHAIN" : "TAP SELECTS, MORE TAPS CHAIN BEHIND IT",
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
  home_.clamp_cursor();

  // From the top: the status line, the info strip, the step view, the motion lane, the PADS line, the knob strip.
  frame::StatusView status;
  home_.status(status);
  status.save_failed = rig_->project.failed;
  enc_text(ctx, status.enc, sizeof status.enc);
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);
  draw_info(ctx, r, Rect{area.x, area.y + frame::kStatusH, area.w, kInfoH});
  const Rect strip_area{area.x, area.y + area.h - app::kKnobStripH, area.w, app::kKnobStripH};
  const Rect pads_area{area.x, strip_area.y - frame::kPadsLineH, area.w, frame::kPadsLineH};

  if (home_.overlay()) {
    draw_grid(ctx, r, Rect{area.x, area.y + kHeroY, area.w, pads_area.y - (area.y + kHeroY) - 2});
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

    view::Marks m = home_.drawer(roll_);
    const Rect hero{area.x, area.y + kHeroY, area.w, kHeroH};
    m.window = roll_ ? fit_window(wlo, whi, view::lane_height(hero.h, roll_)) : range_window(wlo, whi, view::lane_height(hero.h, roll_));
    view::draw_steps(r, hero, p, m);
    if (pattern_is_empty(p)) {
      const char* a = "EMPTY PATTERN";
      const char* b = "ARM REC AND PLAY THE KEYS, OR HOLD A STEP PAD AND PLAY A KEY";
      const int cx = view::kGridX + view::kGridW / 2;
      const int cy = hero.y + view::kNumbersH + 70;
      r.text(cx - r.text_width(a, Face::Data) / 2, cy, a, Color::TextDim, Face::Data);
      r.text(cx - r.text_width(b, Face::Label) / 2, cy + 26, b, Color::Empty, Face::Label);
    }

    // The motion lane of the knob the encoder owns when a lane drives it (the pattern's lanes table, C2); else the last lane shown.
    const unsigned lane = home_.lane();
    const LaneRef lane_ref = p.lanes[lane];
    const Entry lane_entry = controls_->lane_entry(lane_ref);
    const core::ParamDesc* d = controls_->desc(ctx.engine(), lane_entry);
    const float knob = d != nullptr ? core::param_norm(*d, controls_->get(ctx.engine(), lane_entry)) : 0.0f;
    view::draw_motion(r, Rect{area.x, hero.y + hero.h + 4, area.w, view::kMotionH}, p, m, lane, controls_->lane_name(ctx.engine(), lane_ref), knob,
                      home_.on_lane());
  }

  char line[128];
  home_.pads_line(ctx, line, sizeof line);
  frame::draw_pads_line(r, pads_area, line);
  if (tools_visible(ctx)) {
    draw_tools(ctx, r, strip_area);
  } else if (random_open_) {
    draw_random(ctx, r, strip_area);
  } else if (on_snap_page()) {
    draw_snap(ctx, r, strip_area);
  } else {
    draw_strip(ctx, r, strip_area);
  }
}

}  // namespace o3_303
