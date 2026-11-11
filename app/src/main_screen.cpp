// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_screen.h"

#include "frame.h"
#include "help_texts.h"
#include "knob_strip.h"
#include "step_view.h"
#include "skin/frow.h"
#include "skin/header.h"
#include "skin/panel.h"
#include "skin/skin.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/core/param.h>
#include <o3sdk/core/scales.h>
#include <o3sdk/dsp/engine.h>
#include <o3sdk/hal/cycles.h>
#include <o3sdk/ui/plot/canvas.h>
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

constexpr int kInfoH = 34;
constexpr int kHeroY = frame::kStatusH + kInfoH + 4;  // under the status line and the info strip
constexpr int kHeroH = 252;                           // the motion lane is under it, over the knob strip (the PADS line is the strip's top line)
constexpr int kGenH = 22;                             // PATTERN SETUP's second info line (the generator): the drawer is that much shorter

// The obsidian frame (docs/SKIN-PLAN.md 2.1): the glass between the header (y 0-66) and the F row's band (y 384-480), its content inset by the rim. The step view is shorter than
// classic's 252 px, because the status line is the header's but the glass is 318 px: info strip, step view, motion lane and the PADS line fill it.
constexpr int kGlassBottom = 384;
constexpr int kGlassX = 5;
constexpr int kGlassW = ui::kScreenW - 2 * kGlassX;
constexpr int kPadsY = kGlassBottom - frame::kPadsLineH - 2;   // the PADS line: the glass's last 16 px (above its rim)
constexpr int kSkinHeroH = 208;

// The Shift row (UX-303 5.1, 5.2 and 7), drawn on the soft keys while Shift is held (the F key row has room for eight characters).
constexpr const char* kHomeKeyLabels[app::kFKeyCount] = {"NEW", "VARY", "RHYTHM", "NOTES", "REVERSE", "CLEAR", "REDO", "UNDO"};
// PATTERN SETUP's: F5 is ROOT (the generator's root, on the encoder until an F key is pressed), F6 is free, F7 and F8 are REDO and UNDO as on HOME.
constexpr const char* kSetupKeyLabels[app::kFKeyCount] = {"COPY PAT", "PASTE", "DUP>NEXT", "CLR LOCK", "ROOT", "-", "REDO", "UNDO"};
// What each Shift key does, in a few words under it on the obsidian skin's F row (the classic strip has no room). "" none.
constexpr const char* kHomeKeyHints[app::kFKeyCount] = {"new line", "25 %", "same notes", "same beat", "backwards", "empty line", "step fwd", "step back"};
constexpr const char* kSetupKeyHints[app::kFKeyCount] = {"to clip", "from clip", "to next", "motion", "key of scale", "", "step fwd", "step back"};
constexpr unsigned kToolNew = 0;
constexpr unsigned kToolVary = 1;
constexpr unsigned kToolRhythm = 2;
constexpr unsigned kToolNotes = 3;
constexpr unsigned kToolReverse = 4;
constexpr unsigned kToolClear = 5;
constexpr unsigned kToolRedo = 6;
constexpr unsigned kVaryPct = 25;  // VARY: today's MUTATE at 25 % (UX-303 7)
constexpr unsigned kToolCopyPat = 0;
constexpr unsigned kToolPastePat = 1;
constexpr unsigned kToolDupNext = 2;
constexpr unsigned kToolClearLocks = 3;
constexpr unsigned kToolRoot = 4;
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

void scale_value_text(unsigned scale, bool locked, bool short_form, char* out, unsigned cap, int root) noexcept {
  unsigned n = 0;
  if (locked && scale < core::kScaleCount && root >= 0 && cap > 4u) {  // the root first: "D PHRYGIAN"
    n = root_name(static_cast<unsigned>(root), out, cap);
    out[n++] = ' ';
  }
  const unsigned head = n;
  const unsigned max = head + (short_form ? 3u : core::kScaleNameChars);
  if (locked && scale < core::kScaleCount) {
    for (unsigned i = 0; n < max && core::kScaleNames[scale][i] != '\0' && n + 1u < cap; ++n, ++i) {
      const char c = core::kScaleNames[scale][i];
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
  setup_ = false;
  setup_keys_ = PageKeys{};
  setup_keys_.set_table(&setup_page(), kSetupPageCount);
  setup_keys_.set_page(0);
  win_pattern_ = -1;
  win_lo_ = 0;
  win_hi_ = 0;
  tool_mask_ = 0;
  root_enc_ = false;  // ROOT lasts until an F key is pressed or the screen is left
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
}

void MainScreen::leave(app::Context& ctx) noexcept {
  home_.leave(ctx);
  rig_->sound_keys.release_all();  // the shared SOUND latch stays, the keys held let go (their releases would reach another screen)
  setup_keys_.release_all();
  live_->snap_finish(ctx, true);
  live_->leave(ctx);  // the actions held end with the screen (their releases would reach another one)
  tool_mask_ = 0;
}

// A seed that differs from press to press and from boot to boot: the sequence of the last one and the time of this press.
std::uint32_t MainScreen::next_seed(app::Context& ctx) noexcept { return live_->next_seed(ctx); }

void MainScreen::update(app::Context& ctx) noexcept {
  home_.update(ctx);
  home_.knob_latched(fkeys().focused(), fkeys().held() != 0u);  // the lane the drawer shows follows the knob the encoder owns (and the pattern under it)
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
}

void MainScreen::turn_input(app::Context& ctx, const Input& in) noexcept {
  if (in.delta() == 0) {
    return;
  }
  const Entry& focused = fkeys().focused();
  if (home_.turn(ctx, fkeys().lock_detents(ctx, *controls_, focused, in.delta(), in.shift), in.shift, focused)) {  // hold a step + the encoder: the lock of the knob (or the step's ratchet or condition)
    fkeys().mark_turned();  // with an F key held, its release gives the latch back, as after any hold-and-turn
    return;
  }
  if (root_owns()) {  // Shift + F5 gave the encoder the root
    seq::GenParams& g = rig_->wiring.gen;
    g.root = static_cast<std::uint8_t>(root_step(g.root, in.delta()));
    ctx.redraw();
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

// The Shift row, at the press of Shift + F: PATTERN SETUP's own on SETUP (REDO and UNDO are the same on both), HOME's otherwise.
void MainScreen::tool(app::Context& ctx, unsigned index) noexcept {
  if (setup_ && index != kToolUndo && index != kToolRedo) {
    setup_tool(ctx, index);
    return;
  }
  home_tool(ctx, index);
}

// HOME's Shift row (UX-303 5.1 and 7): NEW VARY RHYTHM NOTES REVERSE CLEAR REDO UNDO. Each edit is one UNDO step of the pattern shown (Bank::edit); one that
// changes nothing (REVERSE of a palindrome, NOTES of a line with nothing sounding, CLEAR of an empty one) says so and leaves no step. RHYTHM of an empty line
// draws a one-pitch line and is a step: RHYTHM then NOTES starts a line from nothing.
void MainScreen::home_tool(app::Context& ctx, unsigned index) noexcept {
  seq::Bank& bank = *rig_->bank;
  const seq::GenParams& gen = rig_->wiring.gen;
  switch (index) {
    case kToolNew:  // a new line over the length with the project's generator settings and a new seed
      toast(ctx, bank.random_fill(sel(), gen, next_seed(ctx)) ? "NEW LINE" : "NO CHANGE", false);
      break;
    case kToolVary:
      toast(ctx, bank.mutate(sel(), gen, next_seed(ctx), kVaryPct) ? "VARIED" : "NO CHANGE", false);
      break;
    case kToolRhythm:  // the pitches stay, the rests, accents and slides are drawn again
      toast(ctx, bank.regen_rhythm(sel(), gen, next_seed(ctx)) ? "NEW RHYTHM" : "NO CHANGE", false);
      break;
    case kToolNotes:  // the rhythm and the flags stay, the pitches are drawn again in the scale
      toast(ctx, bank.regen_notes(sel(), gen, next_seed(ctx)) ? "NEW NOTES" : "NO CHANGE", false);
      break;
    case kToolReverse:
      toast(ctx, bank.reverse(sel()) ? "REVERSED" : "NO CHANGE", false);
      break;
    case kToolClear:
      toast(ctx, bank.clear_pattern(sel()) ? "PATTERN CLEARED" : "NO CHANGE", false);
      break;
    case kToolRedo:
      toast(ctx, undo_step(*rig_, true) ? "REDO" : "NOTHING TO REDO");
      break;
    default:
      toast(ctx, undo_step(*rig_, false) ? "UNDO" : "NOTHING TO UNDO");
      break;
  }
  home_.refocus();
}

// ---- PATTERN SETUP ----------------------------------------------------------------------------------------------------------

// A tap of Pattern: HOME and PATTERN SETUP trade places. The F keys held let go (their releases would reach the other page); the encoder's latch of the
// page being left stays as it was.
void MainScreen::setup_toggle(app::Context& ctx) noexcept {
  fkeys().release_all();
  setup_ = !setup_;
  root_enc_ = false;
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

// Shift + F1..F5 on PATTERN SETUP (F6 is free; F7 REDO and F8 UNDO are HOME's).
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
    case kToolRoot:  // the encoder is the generator's root (C to B, no wrap) until an F key is pressed
      home_.release_row();  // a latched row had the encoder: ROOT takes it (a held step still keeps the turn, as for any knob)
      root_enc_ = true;
      break;
    default:
      break;
  }
  home_.refocus();
}

void MainScreen::button_input(app::Context& ctx, const Input& in) noexcept {
  if (Input held_turn; fkeys().arrow_turn(in, held_turn)) {  // Left / Right with an F key held: its knob by 1/20 of the range, as a turn
    turn_input(ctx, held_turn);
    fkeys().end_press();
    return;
  }
  if (in.repeat) {  // a held arrow's repeat with no F key held: nothing (Left and Right keep their single press)
    return;
  }
  if (app::is_fkey(in.which())) {
    const unsigned k = app::fkey_index(in.which());
    if (in.type == Input::Type::Press) {
      if (in.shift) {  // Shift + F: the Shift row, at once; the key's release is not the page's
        tool_mask_ |= 1u << k;
        tool(ctx, k);
        ctx.redraw();
        return;
      }
      if (fkeys().page().entry[k].item != Item::Blank) {
        home_.release_row();  // an F key gives the encoder back to the knob (UX-303 2.3); an empty box does nothing
        root_enc_ = false;    // ... and ROOT's owner gives it back to SCALE
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
    if (home_.row() != DrawerRow::None) {
      root_enc_ = false;  // a drawer row latched: it owns the encoder, not ROOT
    }
    return;
  }
  if (in.which() == Button::Shift) {  // the soft keys change with it
    ctx.redraw();
  }
}

// ---- Soft keys and LEDs -----------------------------------------------------------------------------------------------------

// While Shift is held the eight soft keys are the Shift row, at once (UX-303 2.4): the knob strip stays up, so a coarse Shift + turn keeps its values.
void MainScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  if (!ctx.panel().shift()) {
    return;  // no labels: the knob strip's boxes carry the F-number and the name, so the key row is the keys alone
  }
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    out.label[k] = setup_ ? kSetupKeyLabels[k] : kHomeKeyLabels[k];
    if (tool_dim(k)) {
      out.dim = static_cast<std::uint8_t>(out.dim | (1u << k));
    }
  }
}

// A tool that has nothing to do now: REDO / UNDO with nothing to step to, PASTE PAT with nothing copied, and the free keys of SETUP's row.
bool MainScreen::tool_dim(unsigned k) const noexcept {
  const seq::Bank& bank = *rig_->bank;
  if (setup_) {
    return k == kToolPastePat ? !bank.has_pattern_clip() : (k == kToolUndo ? !bank.can_undo() : (k == kToolRedo ? !bank.can_redo() : k > kToolRoot));
  }
  return (k == kToolRedo && !bank.can_redo()) || (k == kToolUndo && !bank.can_undo());
}

void MainScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept { home_.leds(ctx, leds); }

// HELP: the texts of the page shown, the Shift row's labels (the very ones soft_keys() gives while Shift is held; "-" is an empty key and says nothing), and the home map's pads.
void MainScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  (void)ctx;
  help_texts::fill(h, help_texts::text(setup_ ? help_texts::Id::Setup : help_texts::Id::Home));
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    const char* label = setup_ ? kSetupKeyLabels[k] : kHomeKeyLabels[k];
    h.fkey_shift[k] = label[0] == '-' ? nullptr : label;
  }
  home_.help_pads(h);
}

// ---- Drawing ----------------------------------------------------------------------------------------------------------------

// What the encoder moves, as the status line says it ("ENC > F1 CUTOFF"): a row of the drawer, or the F key latched (or held).
void MainScreen::enc_text(app::Context& ctx, char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  if (root_owns()) {
    append(out, cap, "ROOT");
  } else if (home_.row() != DrawerRow::None) {
    home_.row_text(ctx, out, cap);
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
  // The pattern's playback transpose (PATTERN SETUP's TRANSP).
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
  // Under the skin Shift held says what the F row is, where the STEP box is: a red chip.
  static constexpr const char* kBanner = "SHIFT > PATTERN TOOLS, ALL UNDO";
  const bool banner = !setup_ && ctx.panel().shift() && skin::active(ctx);
  const int banner_w = r.text_width(kBanner, Face::Label) + 16;
  const int box_x = strip.x + strip.w - 8 - (banner ? banner_w : r.text_width(box, Face::Data));
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
    scale_value_text(rig_->wiring.gen.scale, locked, false, sc, sizeof sc, static_cast<int>(rig_->wiring.gen.root));
    const int next_w = next[0] != '\0' ? r.text_width(next, Face::Label) + 22 : 0;
    const bool full = row.x() + r.text_width("SCALE", Face::Label) + r.text_width(sc, Face::Data) + 24 + next_w <= box_x - 12;
    if (full) {
      row.field("SCALE", Face::Data, sc, Color::ChromeDim, Color::ChromeText);
    } else if (locked) {  // a scale that locks the keyboard is always shown: short, when the full field does not fit
      scale_value_text(rig_->wiring.gen.scale, true, true, sc, sizeof sc, static_cast<int>(rig_->wiring.gen.root));
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
  if (banner) {
    const Rect chip{box_x, strip.y + 6, banner_w, 22};
    r.panel(chip, Color::Rec);
    r.text(chip.x + 8, ui::caps_y(theme, Face::Label, chip.y, chip.h), kBanner, Color::SelText, Face::Label);
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
  scale_value_text(g.scale, home_.scale_locked(), false, sc, sizeof sc, static_cast<int>(g.root));  // "--" until the player chooses one: the keyboard is chromatic (NEW uses it anyway)
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

// The F keys as the page shows them: the eight knobs, with the one the encoder owns, the keys that are down, and a p-lock being written.
void MainScreen::fill_page_knobs(app::Context& ctx, view::Knob (&knobs)[view::kKnobs], char (&lock_label)[16]) noexcept {
  const PageKeys& fk = fkeys();
  const Page& pg = fk.page();
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    view::Knob& kn = knobs[k];
    frame::fill_knob(ctx, *controls_, k, pg.entry[k], kn);
    if (kn.blank) {
      continue;
    }
    kn.focus = (home_.row() == DrawerRow::None || home_.row() == DrawerRow::Lock) && k == fk.focus();
    if (setup_ && pg.entry[k].item == Item::Scale) {  // SCALE shows its root beside the name; ROOT takes the box while the encoder has it
      char rn[4];
      const unsigned root = rig_->wiring.gen.root;
      (void)root_name(root, rn, sizeof rn);
      if (root_owns()) {
        kn.label = "ROOT";
        cut(rn, sizeof kn.value - 1u, kn.value, sizeof kn.value);
        kn.unit = "";
        kn.norm = static_cast<float>(root < 11u ? root : 11u) / 11.0f;
      } else if (home_.scale_locked()) {
        scale_value_text(rig_->wiring.gen.scale, true, false, kn.value, sizeof kn.value, static_cast<int>(root));
      }
    }
    kn.held = (fk.held() >> k & 1u) != 0u;
    if (home_.lock_active() && kn.focus) {  // a step is held and the encoder is locking this key on it: its value, in Accent
      char num[4];
      view::fmt_uint(static_cast<unsigned>(home_.lock_step() + 1), num);
      cut("LOCK ", 5, lock_label, sizeof lock_label);
      append(lock_label, sizeof lock_label, num);
      kn.label = lock_label;
      cut(home_.lock_text(), sizeof kn.value - 1u, kn.value, sizeof kn.value);
      kn.unit = "";
      kn.norm = home_.lock_norm();
      kn.locked = true;
    }
  }
}

// The Shift row as eight action keys (the obsidian skin's F row while Shift is held): the labels soft_keys() gives, a hint under each, dim where the tool has nothing to do. A "-" is an empty box.
void MainScreen::fill_shift_row(view::Knob (&knobs)[view::kKnobs]) const noexcept {
  static constexpr const char* kFKeys[app::kFKeyCount] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    view::Knob& kn = knobs[k];
    kn = view::Knob{};
    kn.fkey = kFKeys[k];
    const char* label = setup_ ? kSetupKeyLabels[k] : kHomeKeyLabels[k];
    if (label[0] == '-') {
      kn.blank = true;
      continue;
    }
    kn.label = label;
    cut(setup_ ? kSetupKeyHints[k] : kHomeKeyHints[k], sizeof kn.value - 1u, kn.value, sizeof kn.value);
    kn.norm = -1.0f;  // an action key
    kn.dim = tool_dim(k);
  }
}

void MainScreen::draw_strip(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  view::Knob knobs[view::kKnobs];
  char label[16];
  fill_page_knobs(ctx, knobs, label);
  view::draw_knobs(r, area, knobs);
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

// What a frame is laid out on: the info strip, the step view's top and height (PATTERN SETUP's generator line takes the drawer's top), the pattern grid's bottom, the PADS line and
// the knob strip. Classic: from the status line down inside the kit's content area. Skinned: inside the glass of the panel (docs/SKIN-PLAN.md 2.1).
struct MainScreen::Body {
  Rect info;
  int x;
  int w;
  int hero_y;
  int hero_h;
  int grid_bottom;
  Rect pads;
  Rect strip;
  bool skinned;
};

// The obsidian frame's shapes and texts, drawn before the glass content: the panel, the header from the screen's StatusView and the context, then the one Canvas section and the live
// texts (docs/SKIN-PLAN.md section 5, N6: shapes by layer, then text).
void MainScreen::draw_skin_frame(app::Context& ctx, ui::Renderer& r) noexcept {
  skin::Panel& panel = skin::panel();
  panel.clear(ctx.strip_area());
  skin::queue_panel(panel, skin::kHeaderH, kGlassBottom);

  frame::StatusView status;
  home_.status(status);
  status.save_failed = rig_->project.failed;
  status.no_autosave = !rig_->project.autosave;
  enc_text(ctx, status.enc, sizeof status.enc);
  char shift_lock_label[16];
  if (ctx.panel().shift() && home_.row() == DrawerRow::None) {  // Shift + a turn is the coarse step, and the Shift row hides the knobs: the ENC window carries the value
    view::Knob focused[view::kKnobs];
    fill_page_knobs(ctx, focused, shift_lock_label);
    const view::Knob& kn = focused[fkeys().focus()];
    if (!kn.blank) {
      skin::enc_with_value(status.enc, sizeof status.enc, kn.label, kn.value, kn.unit);
    }
  }
  skin::HeaderInputs in;
  in.title = title();
  in.pattern = sel();
  in.tempo_mbpm = ctx.engine().tempo();
  in.playing = rig_->seq->playhead().playing;
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    in.perf[b] = rig_->wiring.perf[b];
  }
  in.now_ms = ctx.uptime_ms();
  skin::HeaderView view;
  skin::fill_header(view, status, in);
  skin::draw_header(panel, view);

  // The F row: the page's knobs, or the Shift row as action keys while Shift is held. Both stay valid until the texts are flushed below.
  view::Knob knobs[view::kKnobs];
  char lock_label[16];
  const bool shift = ctx.panel().shift();
  if (shift) {
    fill_shift_row(knobs);
  } else {
    fill_page_knobs(ctx, knobs, lock_label);
  }
  skin::FRowView frow;
  skin::fill_frow(frow, knobs, shift);
  skin::draw_frow(panel, frow);

  ui::plot::Canvas canvas(r);
  canvas.open();
  panel.flush_shapes(canvas);
  canvas.close();
  panel.flush_text(r);
}

void MainScreen::draw_body(app::Context& ctx, ui::Renderer& r, const Body& b) noexcept {
  const Pattern& p = pat();
  draw_info(ctx, r, b.info);

  const int hero_y = b.hero_y + (setup_ ? kGenH : 0);  // PATTERN SETUP's second info line takes the drawer's top
  if (home_.overlay()) {
    draw_grid(ctx, r, Rect{b.x, hero_y, b.w, b.grid_bottom - hero_y});
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
    m.button_colours = b.skinned;
    const Rect hero{b.x, hero_y, b.w, b.hero_h - (setup_ ? kGenH : 0)};
    m.window = range_window(win_lo_, win_hi_, view::lane_height(hero.h));
    view::draw_steps(r, hero, p, m);
    if (pattern_is_empty(p)) {
      const char* a = "EMPTY PATTERN";
      const char* bl = "ARM REC, PLAY AND HOLD THE KEYS";  // U1: a held key is a long note
      const char* b2 = "OR HOLD A STEP PAD AND PLAY A KEY";
      const int cx = view::kGridX + view::kGridW / 2;
      const int cy = hero.y + view::kNumbersH + 70;
      r.text(cx - r.text_width(a, Face::Data) / 2, cy, a, Color::TextDim, Face::Data);
      r.text(cx - r.text_width(bl, Face::Label) / 2, cy + 26, bl, Color::Empty, Face::Label);
      r.text(cx - r.text_width(b2, Face::Label) / 2, cy + 44, b2, Color::Empty, Face::Label);
    }

    // The motion lane of the knob the encoder owns when a lane drives it (the pattern's lanes table, C2); else the last lane shown.
    const unsigned lane = home_.lane();
    const LaneRef lane_ref = p.lanes[lane];
    const Entry lane_entry = controls_->lane_entry(lane_ref);
    const core::ParamDesc* d = controls_->desc(ctx.engine(), lane_entry);
    const float knob = d != nullptr ? core::param_norm(*d, controls_->get(ctx.engine(), lane_entry)) : 0.0f;
    view::draw_motion(r, Rect{b.x, hero.y + hero.h + 4, b.w, view::kMotionH}, p, m, lane, controls_->lane_name(ctx.engine(), lane_ref), knob,
                      home_.on_lane());
  }

  char line[128];
  home_.pads_line(ctx, line, sizeof line);
  frame::draw_pads_line(r, b.pads, line);
  if (!b.skinned) {
    draw_strip(ctx, r, b.strip);  // the skin's F row was queued with its frame
  }
}

// ---- The skin's switches: the kit asks the screen every frame ------------------------------------------------------------------

app::Chrome MainScreen::chrome(const app::Context& ctx) const noexcept { return skin::active(ctx) ? app::Chrome::None : app::Chrome::Full; }

const ui::Theme* MainScreen::theme(const app::Context& ctx) const noexcept { return skin::active(ctx) ? &skin::theme() : nullptr; }

void MainScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  home_.clamp_cursor();
  Body b;
  const bool skinned = skin::active(ctx);
  const std::uint32_t t0 = skinned ? o3sdk::hal::cycles() : 0u;
  if (skinned) {
    // The panel is the whole 800 x 480; the glass is y 66 to 384 and holds today's content from the info strip down (the status line is the header's); the F row is on the panel's
    // lower band, queued with the frame.
    draw_skin_frame(ctx, r);
    b.info = Rect{kGlassX, skin::kHeaderH + 3, kGlassW, kInfoH};
    b.x = kGlassX;
    b.w = kGlassW;
    b.hero_y = b.info.y + kInfoH + 4;
    b.hero_h = kSkinHeroH;
    b.grid_bottom = kPadsY - 2;
    b.pads = Rect{kGlassX + 3, kPadsY, kGlassW - 6, frame::kPadsLineH};
    b.strip = Rect{};
    b.skinned = true;
  } else {
    // From the top: the status line, the info strip, the step view, the motion lane, the PADS line, the knob strip.
    frame::StatusView status;
    home_.status(status);
    status.save_failed = rig_->project.failed;
    status.no_autosave = !rig_->project.autosave;
    enc_text(ctx, status.enc, sizeof status.enc);
    frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);
    const Rect strip_area{area.x, area.y + area.h - app::kKnobStripH, area.w, app::kKnobStripH};
    b.info = Rect{area.x, area.y + frame::kStatusH, area.w, kInfoH};
    b.x = area.x;
    b.w = area.w;
    b.hero_y = area.y + kHeroY;
    b.hero_h = kHeroH;
    b.grid_bottom = strip_area.y - 2;
    b.pads = Rect{strip_area.x, strip_area.y, strip_area.w, frame::kPadsLineH};  // the knob row's top line, as on every screen with a knob row
    b.strip = strip_area;
    b.skinned = false;
  }
  draw_body(ctx, r, b);
  if (skinned) {
    skin::draw_ballast(r, skin::ballast());  // `skin ballast N` (SU1): after everything, so the list is that much longer
    constexpr std::uint32_t kCyclesPerUs = 180;  // the MK66's clock; the host's hal::cycles() is scaled to it
    skin::note_frame(static_cast<std::uint32_t>(r.words()), (o3sdk::hal::cycles() - t0) / kCyclesPerUs, ctx.uptime_ms());
  }
}

}  // namespace o3_303
