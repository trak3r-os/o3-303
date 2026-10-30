// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_screen.h"

#include "frame.h"
#include "knob_strip.h"
#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/core/param.h>
#include <o3sdk/core/scales.h>
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
constexpr int kGenH = 22;                             // PATTERN SETUP's second info line (the generator): the drawer is that much shorter

constexpr const char* kToolNames[app::kFKeyCount] = {"RANDOM", "MUTATE", "REVERSE", "INVERT", "DOUBLE", "CLEAR", "REDO", "UNDO"};
constexpr const char* kToolHints[app::kFKeyCount] = {"new line", "tweak 25%", "backwards", "flip pitch", "length x2", "all rests", "step forward", "step back"};
// PATTERN SETUP's Shift row (UX-303 5.2): F5..F7 are free.
constexpr const char* kSetupToolNames[app::kFKeyCount] = {"COPY PAT", "PASTE PAT", "DUP>NEXT", "CLEAR LOCKS", "-", "-", "-", "UNDO"};
constexpr const char* kSetupToolHints[app::kFKeyCount] = {"to the clipboard", "over this one", "into the next", "keep the notes", "", "", "", "step back"};
// (the F key row has room for eight characters; the tools strip says them whole)
constexpr const char* kSetupKeyLabels[app::kFKeyCount] = {"COPY PAT", "PASTE", "DUP>NEXT", "CLR LOCK", "-", "-", "-", "UNDO"};
constexpr unsigned kToolCopyPat = 0;
constexpr unsigned kToolPastePat = 1;
constexpr unsigned kToolDupNext = 2;
constexpr unsigned kToolClearLocks = 3;
constexpr unsigned kToolUndo = 7;

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

// ---- The scale on the info lines -------------------------------------------------------------------------------------------

void scale_value_text(unsigned scale, bool locked, bool short_form, char* out, unsigned cap) noexcept {
  unsigned n = 0;
  const unsigned max = short_form ? 3u : core::kScaleNameChars;
  if (locked && scale < core::kScaleCount) {
    for (; n < max && core::kScaleNames[scale][n] != '\0' && n + 1u < cap; ++n) {
      const char c = core::kScaleNames[scale][n];
      out[n] = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    }
    while (n != 0u && out[n - 1u] == ' ') {  // the table pads a name to its field ("Phrygian ")
      --n;
    }
  }
  if (n == 0u && cap > 2u) {  // no scale locked: the keyboard is chromatic
    out[n++] = '-';
    out[n++] = '-';
  }
  out[n] = '\0';
}

// ---- Setup and screen hooks -------------------------------------------------------------------------------------------------

void MainScreen::init(Rig& rig) noexcept {
  // Every piece of state starts afresh: the app object outlives a run in the simulator's tests.
  rig_ = &rig;
  controls_ = &rig.controls;
  live_ = rig.live;
  home_.init(rig);
  keys_ = PageKeys{};
  keys_.set_page(kPageSound);
  setup_ = false;
  setup_keys_ = PageKeys{};
  setup_keys_.set_table(&setup_page(), kSetupPageCount);
  setup_keys_.set_page(0);
  win_pattern_ = -1;
  win_lo_ = 0;
  win_hi_ = 0;
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
  return setup_ ? "PATTERN SETUP" : "STEPS";
}

void MainScreen::enter(app::Context& ctx) noexcept {
  home_.enter(ctx);
  setup_ = false;  // Pattern from another screen goes HOME (UX-303 D6)
  setup_keys_.release_all();
  master_down_ = false;
}

void MainScreen::leave(app::Context& ctx) noexcept {
  home_.leave(ctx);
  keys_.release_all();
  setup_keys_.release_all();
  enc_latch_ = EncLatch::None;  // Master's latch of TEMPO / SWING ends with the screen: back again, the encoder is the page's
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
  home_.knob_latched(fkeys().focused());  // the lane the drawer shows follows the knob the encoder owns (and the pattern under it)
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
  const Entry& focused = fkeys().focused();
  if (home_.turn(ctx, in.delta(), in.shift, focused)) {  // hold a step + the encoder: the lock of the knob (or the step's ratchet or condition)
    fkeys().mark_turned();  // with an F key held, its release gives the latch back, as after any hold-and-turn
    return;
  }
  if (enc_latch_ != EncLatch::None && fkeys().held() == 0u) {  // Master's tap latched TEMPO or SWING on the encoder
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
  PageKeys& fk = fkeys();
  const std::uint32_t affected = fk.held() != 0u ? fk.held() : (1u << fk.focus());
  const Page& pg = fk.page();
  (void)fk.input(ctx, in, *controls_);
  for (unsigned k = 0; k < kKeys; ++k) {  // Rec while the pattern plays: the move is written as motion under the playhead (the home map's)
    if ((affected >> k & 1u) != 0u) {
      home_.turned(ctx, pg.entry[k]);
      if (pg.entry[k].item == Item::Length) {  // a LENGTH step changed the line under the cursor and its page
        home_.refocus();
      }
    }
  }
}

void MainScreen::tool(app::Context& ctx, unsigned index) noexcept {
  if (setup_ && index != kToolUndo) {  // (UNDO is the same on both rows: it falls through to the switch below)
    setup_tool(ctx, index);
    return;
  }
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

// ---- PATTERN SETUP ----------------------------------------------------------------------------------------------------------

// A tap of Pattern: HOME and PATTERN SETUP trade places. The F keys held let go (their releases would reach the other page), a snapshot being morphed
// is finished, the RANDOM popup closes; the encoder's latch of the page being left stays as it was.
void MainScreen::setup_toggle(app::Context& ctx) noexcept {
  if (!setup_ && on_snap_page()) {
    live_->snap_finish(ctx, true);
  }
  fkeys().release_all();
  random_open_ = false;
  setup_ = !setup_;
  fkeys().release_all();
  ctx.redraw();
}

// DUP>NEXT: the pattern copied into the next slot, which is selected (queued while the pattern plays). One UNDO (Bank::replace).
void MainScreen::duplicate_to_next(app::Context& ctx) noexcept {
  seq::Bank& bank = *rig_->bank;
  if (sel() + 1u >= kPatternCount) {  // the last pattern has no slot after it: nothing wraps round to overwrite A01
    toast(ctx, "NO SLOT AFTER THE LAST PATTERN");
    return;
  }
  const unsigned next = sel() + 1u;
  (void)bank.replace(next, pat());
  char name[4];
  char text[24] = "DUPLICATED TO ";
  pattern_name(next, name, sizeof name);
  append(text, sizeof text, name);
  if (ctx.engine().playing()) {
    (void)rig_->seq->queue(next);
  } else {
    bank.select(next);
  }
  toast(ctx, text);
}

// Shift + F1..F4 on PATTERN SETUP (F5..F7 are free, F8 UNDO is HOME's).
void MainScreen::setup_tool(app::Context& ctx, unsigned index) noexcept {
  seq::Bank& bank = *rig_->bank;
  switch (index) {
    case kToolCopyPat:
      bank.copy_pattern(sel());
      toast(ctx, "PATTERN COPIED");
      break;
    case kToolPastePat:
      toast(ctx, bank.paste_pattern(sel()) ? "PATTERN PASTED" : "NOTHING COPIED");
      break;
    case kToolDupNext:
      duplicate_to_next(ctx);
      break;
    case kToolClearLocks:  // every lane of this pattern, the notes stay: one UNDO
      toast(ctx, bank.clear_motion(sel()) ? "LOCKS CLEARED" : "NO LOCKS TO CLEAR");
      break;
    default:
      break;
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
        home_.release_row();  // but the encoder is theirs (the setting, the morph), not a drawer row's
        tool_mask_ |= 1u << k;
        if (random_open_) {
          random_press(ctx, k);
        } else {
          snap_press(ctx, k, false);
        }
        return;
      }
      if (fkeys().page().entry[k].item != Item::Blank) {
        home_.release_row();  // an F key gives the encoder back to the knob (UX-303 2.3); an empty box does nothing
      }
      (void)fkeys().input(ctx, in, *controls_);
    } else if (in.type == Input::Type::Release) {
      if ((tool_mask_ >> k & 1u) != 0u) {
        tool_mask_ &= ~(1u << k);
        return;
      }
      (void)fkeys().input(ctx, in, *controls_);
    }
    return;
  }

  if (in.which() == Button::Pattern && in.type == Input::Type::Release && home_.pattern_tap()) {
    setup_toggle(ctx);  // a tap on the screen's own button: HOME and PATTERN SETUP
  }
  if (home_.input(ctx, in)) {  // the arrows, Copy / Insert / Delete, Rec, the colour buttons (the drawer rows), Pattern
    if (home_.row() != DrawerRow::None) {  // a row has the encoder: it is not Master's latch nor the popup's
      enc_latch_ = EncLatch::None;
      random_open_ = false;
    }
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
        if (on_snap_page()) {
          home_.release_row();  // the SNAP page's encoder is the morph
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
      out.label[k] = setup_ ? kSetupKeyLabels[k] : kToolNames[k];
      if (tool_dim(k)) {
        out.dim = static_cast<std::uint8_t>(out.dim | (1u << k));
      }
    }
    return;
  }
  // No labels: the knob strip's boxes carry the F-number and the name (S2), so the key row is the keys alone and there is no second label row.
}

const char* MainScreen::tool_name(unsigned k) const noexcept { return (setup_ ? kSetupToolNames : kToolNames)[k]; }
const char* MainScreen::tool_hint(unsigned k) const noexcept { return (setup_ ? kSetupToolHints : kToolHints)[k]; }

// A tool that has nothing to do now: REDO / UNDO with nothing to step to, PASTE PAT with nothing copied, and the free keys of SETUP's row.
bool MainScreen::tool_dim(unsigned k) const noexcept {
  const seq::Bank& bank = *rig_->bank;
  if (setup_) {
    return k == kToolPastePat ? !bank.has_pattern_clip() : (k == kToolUndo ? !bank.can_undo() : (k > kToolClearLocks && k < kToolUndo));
  }
  return (k == 6u && !bank.can_redo()) || (k == 7u && !bank.can_undo());
}

void MainScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept { home_.leds(ctx, leds); }

// ---- Drawing ----------------------------------------------------------------------------------------------------------------

// What the encoder moves, as the status line says it ("ENC > F1 CUTOFF"): a row of the drawer, the F key latched (or held), a latch of Master, the popup's setting, the morph.
void MainScreen::enc_text(app::Context& ctx, char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  if (master_down_) {
    append(out, cap, "TEMPO NUDGE");
  } else if (home_.row() != DrawerRow::None) {
    home_.row_text(ctx, out, cap);
  } else if (enc_latch_ != EncLatch::None) {
    append(out, cap, enc_latch_ == EncLatch::Tempo ? "TEMPO" : "SWING");
  } else if (random_open_) {
    char fk[4] = {'F', static_cast<char>('1' + random_focus_), ' ', '\0'};
    append(out, cap, fk);
    append(out, cap, controls_->label(ctx.engine(), random_entry(random_focus_)));
  } else if (on_snap_page()) {
    append(out, cap, "MORPH");
  } else {
    char fk[4] = {'F', static_cast<char>('1' + fkeys().focus()), ' ', '\0'};
    append(out, cap, fk);
    append(out, cap, controls_->label(ctx.engine(), fkeys().focused()));
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
  // The STEP box is right aligned and has the first claim on the strip; the scale is left out when it would run into the box or the NEXT chip.
  char box[64];
  box[0] = '\0';
  if (!setup_) {
    (void)home_.step_box(ctx, box, sizeof box);
  }
  const int box_x = strip.x + strip.w - 8 - r.text_width(box, Face::Data);
  char next[16] = "";
  if (ph.playing && ph.queued >= 0) {
    char qn[4];
    pattern_name(static_cast<unsigned>(ph.queued), qn, sizeof qn);
    cut("NEXT ", 5, next, sizeof next);
    append(next, sizeof next, qn);
  }
  if (!setup_) {  // the keyboard's scale lock (SETUP's F5 sets it; SETUP's own second line shows the generator's scale)
    const bool locked = home_.scale_locked();
    char sc[24];
    scale_value_text(rig_->wiring.gen.scale, locked, false, sc, sizeof sc);
    const int next_w = next[0] != '\0' ? r.text_width(next, Face::Label) + 22 : 0;
    const bool full = row.x() + r.text_width("SCALE", Face::Label) + r.text_width(sc, Face::Data) + 24 + next_w <= box_x - 12;
    if (full) {
      row.field("SCALE", Face::Data, sc, Color::ChromeDim, Color::ChromeText);
    } else if (locked) {  // a scale that locks the keyboard is always shown: short, when the full field does not fit
      scale_value_text(rig_->wiring.gen.scale, true, true, sc, sizeof sc);
      row.field("SC", Face::Data, sc, Color::ChromeDim, Color::ChromeText);
    }
  }
  row.skip(6);
  if (next[0] != '\0') {
    row.chip(Face::Label, next, Color::ChromeDim, Color::Chrome);
  }

  if (setup_) {  // PATTERN SETUP: the generator's settings (project-wide, D1) on a line of their own, where HOME has the STEP box
    draw_info_generator(ctx, r, Rect{area.x, area.y + kInfoH, area.w, kGenH});
    return;
  }
  // The STEP box, right aligned: "STEP 5: D#2 ACC SLD x2 CUTOFF 420Hz".
  r.text(box_x, ui::caps_y(theme, Face::Data, strip.y, strip.h), box, Color::ChromeText, Face::Data);
}

// PATTERN SETUP's second info line: the generator's settings, which are the whole project's (D1): the scale (the keyboard is locked to it once the player
// has chosen one), the density, the accent and slide chances.
void MainScreen::draw_info_generator(app::Context& ctx, ui::Renderer& r, const Rect& strip) noexcept {
  (void)ctx;
  ui::strip(r, strip, ui::Edge::Bottom);
  const seq::GenParams& g = rig_->wiring.gen;
  ui::FieldRow row(r, 8, strip.y + 17);
  row.put(Face::Label, "GENERATOR", Color::ChromeText);
  row.put(Face::Label, " (WHOLE PROJECT)", Color::ChromeDim);
  row.skip(40);
  char sc[24];
  scale_value_text(g.scale, home_.scale_locked(), false, sc, sizeof sc);  // "--" until the player chooses one: the keyboard is chromatic (NEW uses it anyway)
  row.field("SCALE", Face::Data, sc, Color::ChromeDim, Color::ChromeText);
  const unsigned pct[3] = {g.density, g.accent_pct, g.slide_pct};
  static constexpr const char* kNames[3] = {"DENSITY", "ACC", "SLD"};
  for (unsigned i = 0; i < 3; ++i) {
    char v[8];
    const unsigned n = view::fmt_uint(pct[i], v);
    v[n] = '%';
    v[n + 1u] = '\0';
    row.field(kNames[i], Face::Data, v, Color::ChromeDim, Color::ChromeText);
  }
}

// The page tabs in the corner (the page the F keys are on, and the others one PARAMS away), a hint after them, and the state chips.
void MainScreen::draw_tabs(app::Context& ctx, ui::Renderer& r, const Rect& area, const char* hint) noexcept {
  r.panel(area, Color::Pane);
  r.rule(area.x, area.y, area.w, ui::Axis::Horizontal, Color::Empty);
  int tx = area.x + 6;
  if (setup_ && hint == nullptr) {  // PATTERN SETUP has the one page
    const char* nm = setup_page().name;
    r.text(tx, area.y + 3, "PATTERN", Color::Empty, Face::Micro);
    r.text(tx + r.text_width("PATTERN", Face::Micro) + 6, area.y + 3, nm, Color::Text, Face::Micro);
    r.rule(tx + r.text_width("PATTERN", Face::Micro) + 6, area.y + 14, r.text_width(nm, Face::Micro), ui::Axis::Horizontal, Color::Sel);
    draw_state_chips(ctx, r, area);
    return;
  }
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
  const PageKeys& fk = fkeys();
  const Page& pg = fk.page();
  draw_tabs(ctx, r, area, nullptr);
  view::Knob knobs[view::kKnobs];
  char label[16];
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    view::Knob& kn = knobs[k];
    fill_knob(ctx, k, pg.entry[k], kn);
    if (kn.blank) {
      continue;
    }
    kn.focus = enc_latch_ == EncLatch::None && (home_.row() == DrawerRow::None || home_.row() == DrawerRow::Lock) && k == fk.focus();
    kn.held = (fk.held() >> k & 1u) != 0u;
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
    const bool none = tool_dim(k);
    const Rect box{cell.x + 3, cell.y + 16, cell.w - 6, cell.h - 18};
    r.panel(box, Color::Empty);
    r.text(box.x + 4, box.y + 1, tool_name(k), none ? Color::TextDim : Color::Text, Face::Label);
    r.text(box.x + 4, box.y + 22, tool_hint(k), Color::TextDim, Face::Micro);
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

  const int hero_y = area.y + kHeroY + (setup_ ? kGenH : 0);  // PATTERN SETUP's second info line takes the drawer's top
  if (home_.overlay()) {
    draw_grid(ctx, r, Rect{area.x, hero_y, area.w, pads_area.y - hero_y - 2});
  } else {
    // The pitch window: the notes of the pattern, remembered while they are entered.
    int lo = 36;
    int hi = 48;
    if (!pitch_range(p, lo, hi)) {
      lo = 36;
      hi = 48;
    }
    if (win_pattern_ != static_cast<int>(sel())) {
      win_pattern_ = static_cast<int>(sel());
      win_lo_ = lo;
      win_hi_ = hi;
    }
    win_lo_ = lo < win_lo_ ? lo : win_lo_;
    win_hi_ = hi > win_hi_ ? hi : win_hi_;

    view::Marks m = home_.drawer(ctx);
    const Rect hero{area.x, hero_y, area.w, kHeroH - (setup_ ? kGenH : 0)};
    m.window = range_window(win_lo_, win_hi_, view::lane_height(hero.h));
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
