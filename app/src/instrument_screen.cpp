// SPDX-License-Identifier: GPL-3.0-or-later
#include "instrument_screen.h"

#include "help_texts.h"
#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>
#include <o3sdk/ui/components.h>
#include <o3_303/step_model.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/growl/growl.h>
#include <o3sdk/units/delay/delay.h>

#include <cstring>

namespace o3_303 {

namespace {

using namespace o3sdk;
using app::Button;
using app::Input;
using ui::Color;
using ui::Face;
using ui::Rect;

// ---- The pages (existing Items only; the SOUND page is the global table's, on Rig::sound_keys) -----------------------------------------------

constexpr Entry dist(Distortion::Param id, const char* label = nullptr) noexcept {
  return Entry{Item::Dist, static_cast<std::uint16_t>(id), label};
}
constexpr Entry delay(units::Delay::Param id, const char* label = nullptr) noexcept {
  return Entry{Item::Delay, static_cast<std::uint16_t>(id), label};
}
constexpr Entry growl(Growl::Param id, const char* label) noexcept { return Entry{Item::Growl, static_cast<std::uint16_t>(id), label}; }
constexpr Entry wobble(WobbleField field, const char* label) noexcept { return Entry{Item::Wobble, static_cast<std::uint16_t>(field), label}; }
constexpr Entry app_item(Item item, const char* label = nullptr) noexcept { return Entry{item, 0, label}; }

// FX and MODS: the table index is the page's number minus one (VOICE has none). FMT MIX, CMB TUNE, CMB FB, WOB RESET and DLY MODE are Config's page DEEP.
constexpr Page kPages[kInstrumentPageCount - 1u] = {
    {"FX",
     {dist(Distortion::Drive), dist(Distortion::Tone), dist(Distortion::Mix, "MIX"), app_item(Item::DistMode, "MODEL"),
      delay(units::Delay::Time, "DLY TIME"), delay(units::Delay::Feedback, "DLY FB"), app_item(Item::SendA, "DLY SEND"),
      delay(units::Delay::Tone, "DLY TONE")}},
    {"MODS",
     {growl(Growl::Vowel, "VOWEL"), growl(Growl::FormantQ, "FMT Q"), growl(Growl::Mix, "GRL MIX"), growl(Growl::CombMix, "CMB MIX"),
      wobble(kWobbleRate, "WOB RATE"), wobble(kWobbleShape, "WOB SHAPE"), wobble(kWobbleDepth, "WOB DEPTH"), wobble(kWobbleTarget, "WOB TGT")}},
};

// ---- The read-out sections ---------------------------------------------------------------------------------------------------------------------

enum Section : unsigned { kSecVoice = 1, kSecDist = 2, kSecDelay = 4, kSecMods = 8 };

struct ReadRow {
  const char* label;
  Entry entry;           // Item::Blank with `wobble`: the wobble's state (ON / OFF)
  bool wobble = false;
};

struct SectionDef {
  const char* title;
  unsigned rows;
  ReadRow row[InstrumentSection::kRows];
};

constexpr Entry voice(AcidVoice::Param id) noexcept { return Entry{Item::Voice, static_cast<std::uint16_t>(id), nullptr}; }

constexpr SectionDef kSections[4] = {
    {"VOICE", 4, {{"CUTOFF", voice(AcidVoice::Cutoff)}, {"RESO", voice(AcidVoice::Reso)}, {"ENV MOD", voice(AcidVoice::EnvMod)}, {"DECAY", voice(AcidVoice::Decay)}}},
    {"DISTORTION", 4, {{"MODEL", app_item(Item::DistMode)}, {"DRIVE", dist(Distortion::Drive)}, {"TONE", dist(Distortion::Tone)}, {"MIX", dist(Distortion::Mix)}}},
    {"DELAY", 4, {{"TIME", delay(units::Delay::Time)}, {"FB", delay(units::Delay::Feedback)}, {"SEND", app_item(Item::SendA)}, {"TONE", delay(units::Delay::Tone)}}},
    {"MODS", 4, {{"VOWEL", growl(Growl::Vowel, nullptr)}, {"GRL", growl(Growl::Mix, nullptr)}, {"WOB RATE", wobble(kWobbleRate, nullptr)}, {"WOB", Entry{}, true}}},
};
static_assert(kSecVoice == 1u << 0 && kSecDist == 1u << 1 && kSecDelay == 1u << 2 && kSecMods == 1u << 3, "the sections are in the mask's order");

// The sections each page shows as read-outs: the ones whose knobs it does not draw large.
constexpr unsigned kShown[kInstrumentPageCount] = {
    kSecDist | kSecDelay | kSecMods,  // VOICE
    kSecVoice | kSecMods,             // FX
    kSecVoice | kSecDist | kSecDelay, // MODS
};

// The drawing's geometry (the area is the screen's, under the kit's header): the page's knobs large, the read-outs, the drawer's two lines.
constexpr int kBigY = frame::kStatusH + 4;
constexpr int kBigH = 112;
constexpr int kSecY = kBigY + kBigH + 8;
constexpr int kSecH = 100;
constexpr int kDrawerY = kSecY + kSecH + 8;
constexpr int kCellRowH = 24;

constexpr const char* kTitles[kInstrumentPageCount] = {"INSTRUMENT VOICE", "INSTRUMENT FX", "INSTRUMENT MODS"};

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

int fill_w(float norm, int w) noexcept {
  const float n = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
  return static_cast<int>(n * static_cast<float>(w) + 0.5f);
}

// ---- The drawing of an InstrumentView ----------------------------------------------------------------------------------------------------------

Rect big_box(const Rect& area, unsigned k) noexcept {
  const int cell_w = area.w / static_cast<int>(view::kKnobs);
  return Rect{area.x + static_cast<int>(k) * cell_w + 3, area.y + kBigY, cell_w - 6, kBigH};
}

// The eight knobs of the page, large: the F-number and the name over the value, the value as a bar along the bottom of the box and as the fill.
void draw_big_knobs(ui::Renderer& r, const Rect& area, const view::Knob (&knobs)[view::kKnobs]) noexcept {
  const ui::Theme& theme = r.theme();
  view::translucent(r, 72, [&] {
    for (unsigned k = 0; k < view::kKnobs; ++k) {
      if (!knobs[k].blank) {
        r.panel(big_box(area, k), Color::Empty);
      }
    }
  });
  for (int pass = 0; pass < 2; ++pass) {  // pass 0 the values, pass 1 the encoder's knob (the stronger fill)
    view::translucent(r, pass == 0 ? 120 : 225, [&] {
      for (unsigned k = 0; k < view::kKnobs; ++k) {
        const Rect b = big_box(area, k);
        const int w = fill_w(knobs[k].norm, b.w);
        if (!knobs[k].blank && knobs[k].focus == (pass == 1) && w > 0) {
          r.panel(Rect{b.x, b.y + b.h - 12, w, 12}, Color::Sel);
        }
      }
    });
  }
  for (unsigned k = 0; k < view::kKnobs; ++k) {
    if (knobs[k].blank) {
      continue;
    }
    const Rect b = big_box(area, k);
    if (knobs[k].held) {
      r.frame(b, Color::Accent);
    } else if (knobs[k].focus) {
      r.frame(b, Color::Instr);
    }
    r.text(b.x + 5, b.y + 4, knobs[k].fkey, Color::TextDim, Face::Micro);
    char label[16];
    cut(knobs[k].label, 15, label, sizeof label);
    const int room = b.w - 28;
    const Face lf = r.text_width(label, Face::Label) <= room ? Face::Label : Face::Micro;  // a long name takes the small face
    r.text(b.x + 24, ui::caps_y(theme, lf, b.y, 18), label, Color::Text, lf);
  }
  for (int pass = 0; pass < 2; ++pass) {  // the values: pass 1 the locked ones, in Accent
    for (unsigned k = 0; k < view::kKnobs; ++k) {
      if (knobs[k].blank || knobs[k].locked != (pass == 1)) {
        continue;
      }
      const Rect b = big_box(area, k);
      const int room = b.w - 10;
      char text[16];
      cut(knobs[k].value, 15, text, sizeof text);
      unsigned len = static_cast<unsigned>(std::strlen(text));
      const Face face = static_cast<int>(len) * 16 <= room ? Face::Display : Face::Data;
      const int cw = face == Face::Display ? 16 : 10;
      while (len > 0u && static_cast<int>(len) * cw > room) {
        text[--len] = '\0';
      }
      r.text(b.x + 5, ui::caps_y(theme, face, b.y + 24, 50), text, pass == 1 ? Color::Accent : Color::Text, face);
      const int unit_w = static_cast<int>(std::strlen(knobs[k].unit)) * 6;
      const int used = static_cast<int>(len) * cw;
      if (unit_w > 0 && used + 4 + unit_w <= room) {
        r.text(b.x + 5 + used + 4, b.y + b.h - 30, knobs[k].unit, Color::TextDim, Face::Micro);
      }
    }
  }
}

// The read-out sections side by side: a title, then each line as a name and its value.
void draw_sections(ui::Renderer& r, const Rect& area, const InstrumentView& view) noexcept {
  if (view.sections == 0u) {
    return;
  }
  const int gap = 8;
  const int w = (area.w - gap * static_cast<int>(view.sections + 1u)) / static_cast<int>(view.sections);
  view::translucent(r, 72, [&] {
    for (unsigned s = 0; s < view.sections; ++s) {
      r.panel(Rect{area.x + gap + static_cast<int>(s) * (w + gap), area.y + kSecY, w, kSecH}, Color::Empty);
    }
  });
  for (unsigned s = 0; s < view.sections; ++s) {
    const InstrumentSection& sec = view.section[s];
    const int x = area.x + gap + static_cast<int>(s) * (w + gap) + 8;
    r.text(x, area.y + kSecY + 4, sec.title, Color::TextDim, Face::Label);
    for (unsigned i = 0; i < sec.count; ++i) {
      const int col = static_cast<int>(i % 2u);
      const int row = static_cast<int>(i / 2u);
      const int cx = x + col * (w - 16) / 2;
      const int cy = area.y + kSecY + 26 + row * 36;
      r.text(cx, cy, sec.label[i], Color::TextDim, Face::Micro);
      r.text(cx, cy + 12, sec.value[i], Color::Text, Face::Data);
    }
  }
}

// The drawer's two lines under the page of 16 steps: NOTE and GATE, on the step pads' columns, the cursor, the playhead and the held steps marked.
void draw_drawer(ui::Renderer& r, const Rect& area, const InstrumentView& view) noexcept {
  const ui::Theme& theme = r.theme();
  const int y_numbers = area.y + kDrawerY;
  const int y_note = y_numbers + 14;
  const int y_gate = y_note + kCellRowH + 2;
  // The caption of the row the encoder owns is highlighted (a colour button latched it).
  const bool note_owned = view.marks.row == DrawerRow::Note;
  const bool gate_owned = view.marks.row == DrawerRow::Gate;
  if (note_owned) {
    r.panel(Rect{area.x + 4, y_note, view::kGridX - 8, kCellRowH}, Color::Sel);
  }
  if (gate_owned) {
    r.panel(Rect{area.x + 4, y_gate, view::kGridX - 8, kCellRowH}, Color::Sel);
  }
  r.text(area.x + 8, ui::caps_y(theme, Face::Label, y_note, kCellRowH), "NOTE", note_owned ? Color::SelText : Color::TextDim, Face::Label);
  r.text(area.x + 8, ui::caps_y(theme, Face::Label, y_gate, kCellRowH), "GATE", gate_owned ? Color::SelText : Color::TextDim, Face::Label);
  const auto cell_x = [&](unsigned k) { return area.x + view::kGridX + static_cast<int>(k) * view::kCellW; };
  const auto mark = [&](unsigned k) -> int {  // 0 plain, 1 cursor, 2 playhead
    const int step = view.marks.page * 16 + static_cast<int>(k);
    return step == view.marks.playhead ? 2 : (step == view.marks.cursor ? 1 : 0);
  };
  for (unsigned k = 0; k < view.cells; ++k) {  // the grounds: a pass for each colour
    const int m = mark(k);
    const Rect cr[2] = {Rect{cell_x(k), y_note, view::kCellW - 2, kCellRowH}, Rect{cell_x(k), y_gate, view::kCellW - 2, kCellRowH}};
    for (const Rect& c : cr) {
      if (m == 0) {
        continue;
      }
      r.panel(c, m == 2 ? Color::Play : Color::Sel);
    }
  }
  view::translucent(r, 56, [&] {
    for (unsigned k = 0; k < view.cells; ++k) {
      if (mark(k) == 0) {
        r.panel(Rect{cell_x(k), y_note, view::kCellW - 2, kCellRowH}, Color::Empty);
        r.panel(Rect{cell_x(k), y_gate, view::kCellW - 2, kCellRowH}, Color::Empty);
      }
    }
  });
  for (unsigned k = 0; k < view.cells; ++k) {
    if ((view.marks.held >> k & 1u) != 0u) {
      r.frame(Rect{cell_x(k), y_note, view::kCellW - 2, 2 * kCellRowH + 2}, Color::Accent);
    }
    char num[4];
    view::fmt_uint(static_cast<unsigned>(view.marks.page * 16) + k + 1u, num);
    r.text(cell_x(k) + 2, y_numbers, num, Color::TextDim, Face::Micro);
    const Color ink = mark(k) == 0 ? Color::Text : (mark(k) == 2 ? Color::PlayText : Color::SelText);
    const InstrumentCell& c = view.cell[k];
    r.text(cell_x(k) + (view::kCellW - 2 - r.text_width(c.note, Face::Data)) / 2, ui::caps_y(theme, Face::Data, y_note, kCellRowH), c.note, ink,
           Face::Data);
    r.text(cell_x(k) + (view::kCellW - 2 - r.text_width(c.gate, Face::Data)) / 2, ui::caps_y(theme, Face::Data, y_gate, kCellRowH), c.gate, ink,
           Face::Data);
  }
}

}  // namespace

// ---- The view logic ---------------------------------------------------------------------------------------------------------------------------

void fill_cells(const Pattern& p, unsigned first, InstrumentView& view) noexcept {
  const unsigned len = p.length != 0u ? p.length : kDefaultLength;
  view.cells = 0;
  for (unsigned k = 0; k < 16u; ++k) {
    const unsigned s = first + k;
    if (s >= len) {
      break;
    }
    InstrumentCell& c = view.cell[k];
    c = InstrumentCell{};
    const StepInfo info = describe(p, s);
    if (info.kind == Cell::Note) {
      (void)note_name(info.note, c.note, sizeof c.note);
      cut(info.accent && info.slide ? "AS" : (info.accent ? "A" : (info.slide ? "S" : "-")), 2, c.gate, sizeof c.gate);
    } else if (info.kind == Cell::Tie) {
      cut("~", 1, c.note, sizeof c.note);
      cut("~", 1, c.gate, sizeof c.gate);
    } else {
      cut("--", 2, c.note, sizeof c.note);
      cut(".", 1, c.gate, sizeof c.gate);
    }
    view.cells = k + 1u;
  }
}

void fill_sections(const dsp::Engine& e, const Controls& controls, bool wobble_on, unsigned page, InstrumentView& view) noexcept {
  view.sections = 0;
  if (page >= kInstrumentPageCount) {
    return;
  }
  for (unsigned s = 0; s < 4u; ++s) {
    if ((kShown[page] >> s & 1u) == 0u) {
      continue;
    }
    InstrumentSection& sec = view.section[view.sections++];
    const SectionDef& def = kSections[s];
    sec.title = def.title;
    sec.count = def.rows;
    for (unsigned i = 0; i < def.rows; ++i) {
      const ReadRow& row = def.row[i];
      sec.label[i] = row.label;
      sec.value[i][0] = '\0';
      if (row.wobble) {
        cut(wobble_on ? "ON" : "OFF", 11, sec.value[i], sizeof sec.value[i]);
        continue;
      }
      char text[24];
      (void)controls.format(e, row.entry, text, sizeof text);
      append(text, sizeof text, controls.unit_text(e, row.entry));
      cut(text, 11, sec.value[i], sizeof sec.value[i]);
    }
  }
}

void apply_lock(InstrumentView& view, view::Knob& kn, int step, const char* text, float norm) noexcept {
  char num[4];
  view::fmt_uint(static_cast<unsigned>(step + 1), num);
  view.lock_label[0] = '\0';
  append(view.lock_label, sizeof view.lock_label, "LOCK ");
  append(view.lock_label, sizeof view.lock_label, num);
  kn.label = view.lock_label;
  cut(text, sizeof kn.value - 1u, kn.value, sizeof kn.value);
  kn.unit = "";
  kn.norm = norm;
  kn.locked = true;
}

bool learn_target(const Wiring& wiring, const Entry& entry, dsp::UnitId& unit, std::uint16_t& param) noexcept {
  switch (entry.item) {
    case Item::Voice:
      unit = wiring.voice;
      break;
    case Item::Dist:
      unit = wiring.dist;
      break;
    case Item::Delay:
      unit = wiring.delay;
      break;
    case Item::Growl:
      unit = wiring.growl;
      break;
    case Item::Freeze:
      unit = wiring.freeze;
      break;
    default:
      return false;  // VOLUME, DLY SEND, MODEL, the wobble ...: app values, not a unit's parameter [D15]
  }
  param = entry.id;
  return unit != dsp::kNoUnit;
}

// ---- Setup and screen hooks ------------------------------------------------------------------------------------------------------------------

void InstrumentScreen::init(Rig& rig, HomeMap& home) noexcept {
  // Every piece of state starts afresh: the app object outlives a run in the simulator's tests.
  rig_ = &rig;
  controls_ = &rig.controls;
  live_ = rig.live;
  home_ = &home;
  for (unsigned i = 0; i < kInstrumentPageCount - 1u; ++i) {
    own_[i] = PageKeys{};
    own_[i].set_table(&kPages[i], 1);
  }
  page_ = kInstVoice;
  tool_mask_ = 0;
  learn_armed_ = false;
  learn_page_ = 0;
  learn_key_ = 0;
}

void InstrumentScreen::release_keys() noexcept {
  rig_->sound_keys.release_all();
  for (PageKeys& k : own_) {
    k.release_all();
  }
}

void InstrumentScreen::open_first_page() noexcept {
  release_keys();  // a key held across the change would stay held in the keys it was pressed on
  home_->release_row();
  page_ = kInstVoice;
}

void InstrumentScreen::next_page() noexcept {
  release_keys();
  home_->release_row();  // the page has its own knobs: the encoder is the knob's again
  page_ = (page_ + 1u) % kInstrumentPageCount;
}

const char* InstrumentScreen::title() const noexcept { return kTitles[page_]; }

void InstrumentScreen::enter(app::Context& ctx) noexcept {
  home_->enter(ctx);
  tool_mask_ = 0;
}

void InstrumentScreen::leave(app::Context& ctx) noexcept {
  learn_drop(ctx);
  home_->leave(ctx);
  release_keys();
  live_->snap_finish(ctx, true);
  live_->leave(ctx);  // the actions held end with the screen (their releases would reach another one); the latched ones stay on (D4)
  tool_mask_ = 0;
}

// (The app's update() runs the home map's, as it does for every screen but HOME: the pattern followed, the toast, the lock readout.)
void InstrumentScreen::update(app::Context& ctx) noexcept {
  if (learn_armed_ && (!ctx.midi().learning() || learn_page_ != page_)) {
    learn_drop(ctx);  // a controller bound it (the kit said so), or the page changed under it
  }
  home_->knob_latched(keys().focused());  // the lane the drawer shows follows the knob the encoder owns
}

// ---- Input ------------------------------------------------------------------------------------------------------------------

void InstrumentScreen::input(app::Context& ctx, const Input& in) noexcept {
  if (in.kind == core::InputKind::Pad) {
    (void)home_->input(ctx, in);
  } else if (in.kind == core::InputKind::Encoder) {
    turn_input(ctx, in);
  } else if (in.kind == core::InputKind::Button) {
    button_input(ctx, in);
  }
}

void InstrumentScreen::turn_input(app::Context& ctx, const Input& in) noexcept {
  if (in.delta() == 0) {
    return;
  }
  PageKeys& pk = keys();
  if (home_->turn(ctx, in.delta(), in.shift, pk.focused())) {  // hold a step + the encoder: the lock of the knob (or the step's ratchet or condition)
    pk.mark_turned();  // with an F key held, its release gives the latch back, as after any hold-and-turn
    return;
  }
  const std::uint32_t affected = pk.held() != 0u ? pk.held() : (1u << pk.focus());
  const Page& pg = pk.page();
  (void)pk.input(ctx, in, *controls_);
  for (unsigned k = 0; k < kKeys; ++k) {  // Rec while the pattern plays: the move is written as motion under the playhead (the home map's)
    if ((affected >> k & 1u) != 0u) {
      home_->turned(ctx, pg.entry[k]);
    }
  }
}

void InstrumentScreen::button_input(app::Context& ctx, const Input& in) noexcept {
  if (app::is_fkey(in.which())) {
    const unsigned k = app::fkey_index(in.which());
    if (in.type == Input::Type::Press) {
      if (in.shift) {  // the Shift row is MIDI learn: the key arms its knob, and its release is not the page's
        tool_mask_ |= 1u << k;
        learn_key(ctx, k);
        ctx.redraw();
        return;
      }
      if (keys().page().entry[k].item != Item::Blank) {
        home_->release_row();  // an F key gives the encoder back to the knob (UX-303 2.3); an empty box does nothing
      }
      (void)keys().input(ctx, in, *controls_);
    } else if (in.type == Input::Type::Release) {
      if ((tool_mask_ >> k & 1u) != 0u) {
        tool_mask_ &= ~(1u << k);
        return;
      }
      (void)keys().input(ctx, in, *controls_);
    }
    return;
  }
  (void)home_->input(ctx, in);  // the arrows, Copy / Insert / Delete, Rec, the colour buttons (the drawer rows)
}

// ---- Soft keys and LEDs -----------------------------------------------------------------------------------------------------

// The Shift row (UX-303 5.5, D15): LEARN for each knob that is a unit parameter (lit while this key's learn is waiting for its controller), "-" for the others; nothing
// while Shift is up (the knob row carries the names).
void InstrumentScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  if (!ctx.panel().shift()) {
    return;
  }
  const Page& pg = keys().page();
  for (unsigned k = 0; k < kKeys; ++k) {
    dsp::UnitId unit = dsp::kNoUnit;
    std::uint16_t param = 0;
    if (!learn_target(rig_->wiring, pg.entry[k], unit, param)) {
      out.label[k] = "-";
      out.dim = static_cast<std::uint8_t>(out.dim | (1u << k));
      continue;
    }
    out.label[k] = "LEARN";
    if (learn_armed_ && learn_page_ == page_ && learn_key_ == k && ctx.midi().learning()) {
      out.hot = static_cast<std::uint8_t>(out.hot | (1u << k));
    }
  }
}

// Shift + F`k`: arms MIDI learn for the knob (the next control change binds to it; again on the same key cancels). A knob that is not a unit parameter says so.
void InstrumentScreen::learn_key(app::Context& ctx, unsigned k) noexcept {
  const Entry& entry = keys().page().entry[k];
  if (entry.item == Item::Blank) {
    return;
  }
  char text[40];
  text[0] = '\0';
  dsp::UnitId unit = dsp::kNoUnit;
  std::uint16_t param = 0;
  const char* label = controls_->label(ctx.engine(), entry);
  if (!learn_target(rig_->wiring, entry, unit, param)) {
    append(text, sizeof text, "CANNOT LEARN ");
    append(text, sizeof text, label);
    home_->toast(ctx, text, true);
    return;
  }
  if (learn_armed_ && learn_page_ == page_ && learn_key_ == k && ctx.midi().learning()) {
    learn_drop(ctx);
    home_->toast(ctx, "LEARN OFF");
    return;
  }
  if (!ctx.midi().learn(unit, param)) {
    append(text, sizeof text, "CANNOT LEARN ");
    append(text, sizeof text, label);
    home_->toast(ctx, text, true);
    return;
  }
  learn_armed_ = true;
  learn_page_ = page_;
  learn_key_ = k;
  append(text, sizeof text, "LEARN ");
  append(text, sizeof text, label);
  append(text, sizeof text, ": MOVE A KNOB");
  home_->toast(ctx, text);
}

// This screen's arm ends (left, page changed, cancelled): the kit's learn is cancelled if it still waits.
void InstrumentScreen::learn_drop(app::Context& ctx) noexcept {
  if (learn_armed_ && ctx.midi().learning()) {
    ctx.midi().learn_cancel();
  }
  learn_armed_ = false;
}

void InstrumentScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept { home_->leds(ctx, leds); }

// HELP: the page's texts, LEARN on the keys of the Shift row that soft_keys() labels so (learn_target(): the unit's own parameters), and the home map's pads.
void InstrumentScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  (void)ctx;
  help_texts::fill(h, help_texts::text(page_ == kInstFx ? help_texts::Id::Fx : (page_ == kInstMods ? help_texts::Id::Mods : help_texts::Id::Voice)));
  const Page& pg = keys().page();
  for (unsigned k = 0; k < kKeys; ++k) {
    dsp::UnitId unit = dsp::kNoUnit;
    std::uint16_t param = 0;
    h.fkey_shift[k] = learn_target(rig_->wiring, pg.entry[k], unit, param) ? "LEARN" : nullptr;
  }
  home_->help_pads(h);
}

// ---- The view -----------------------------------------------------------------------------------------------------------------

// "F1 CUTOFF": the key the encoder drives, or the row of the drawer a colour button latched ("GATE", "LOCK CUTOFF").
void InstrumentScreen::enc_text(app::Context& ctx, char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  if (home_->row() != DrawerRow::None) {
    home_->row_text(ctx, out, cap);
    return;
  }
  const PageKeys& pk = keys();
  char fk[4] = {'F', static_cast<char>('1' + pk.focus()), ' ', '\0'};
  append(out, cap, fk);
  append(out, cap, controls_->label(ctx.engine(), pk.focused()));
}

void InstrumentScreen::pads_line(app::Context& ctx, char* out, unsigned cap) const noexcept { home_->pads_line(ctx, out, cap); }

void InstrumentScreen::fill_view(app::Context& ctx, InstrumentView& view) noexcept {
  const dsp::Engine& e = ctx.engine();
  const PageKeys& pk = keys();
  const Page& pg = pk.page();

  for (unsigned k = 0; k < kKeys; ++k) {
    view::Knob& kn = view.knob[k];
    frame::fill_knob(ctx, *controls_, k, pg.entry[k], kn);
    if (kn.blank) {
      continue;
    }
    kn.focus = k == pk.focus() && (home_->row() == DrawerRow::None || home_->row() == DrawerRow::Lock);
    kn.held = (pk.held() >> k & 1u) != 0u;
    if (home_->lock_active() && kn.focus) {  // a step is held and the encoder is locking this key on it: its value, in Accent
      apply_lock(view, kn, home_->lock_step(), home_->lock_text(), home_->lock_norm());
    }
  }

  fill_sections(e, *controls_, live_->wobble_on(), page_, view);

  const seq::Bank& bank = *rig_->bank;
  view.marks = home_->drawer(ctx);
  fill_cells(bank.pattern(bank.selected()), static_cast<unsigned>(view.marks.page) * 16u, view);
}

void InstrumentScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  home_->clamp_cursor();
  frame::StatusView status;
  home_->status(status);
  status.save_failed = rig_->project.failed;
  enc_text(ctx, status.enc, sizeof status.enc);
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);

  InstrumentView view;
  fill_view(ctx, view);
  draw_big_knobs(r, area, view.knob);
  draw_sections(r, area, view);
  draw_drawer(r, area, view);

  const Rect strip{area.x, area.y + area.h - app::kKnobStripH, area.w, app::kKnobStripH};
  char line[128];
  pads_line(ctx, line, sizeof line);
  frame::draw_pads_line(r, Rect{strip.x, strip.y, strip.w, frame::kPadsLineH}, line);
  view::draw_knobs(r, strip, view.knob);
}

}  // namespace o3_303
