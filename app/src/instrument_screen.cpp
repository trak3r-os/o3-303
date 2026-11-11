// SPDX-License-Identifier: GPL-3.0-or-later
#include "instrument_screen.h"

#include "help_texts.h"
#include "skin/header.h"
#include "skin/instrument.h"
#include "skin/skin.h"
#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>
#include <o3sdk/hal/cycles.h>
#include <o3sdk/ui/components.h>
#include <o3_303/step_model.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/tract_vowels.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/tract/tract.h>
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

// ---- The entries of the read-out rows (the pages' key tables are pages.h's: kFxPage, kNotesPage, kTonePage, kGrowlPage, kWobblePage) -----------------------------------------------

constexpr Entry dist(Distortion::Param id, const char* label = nullptr) noexcept {
  return Entry{Item::Dist, static_cast<std::uint16_t>(id), label};
}
constexpr Entry delay(units::Delay::Param id, const char* label = nullptr) noexcept {
  return Entry{Item::Delay, static_cast<std::uint16_t>(id), label};
}
constexpr Entry growl(Growl::Param id, const char* label) noexcept { return Entry{Item::Growl, static_cast<std::uint16_t>(id), label}; }
constexpr Entry tract(TractFx::Param id, const char* label) noexcept { return Entry{Item::Tract, static_cast<std::uint16_t>(id), label}; }
constexpr Entry wobble(WobbleField field, const char* label) noexcept { return Entry{Item::Wobble, static_cast<std::uint16_t>(field), label}; }
constexpr Entry app_item(Item item, const char* label = nullptr) noexcept { return Entry{item, 0, label}; }

constexpr Entry voice(AcidVoice::Param id, const char* label = nullptr) noexcept { return Entry{Item::Voice, static_cast<std::uint16_t>(id), label}; }

// ---- The read-out sections ---------------------------------------------------------------------------------------------------------------------

enum Section : unsigned { kSecVoice = 1, kSecDist = 2, kSecDelay = 4, kSecGrowl = 8, kSecWobble = 16, kSecModded = 32, kSecNotes = 64, kSecTone = 128, kSecTract = 256 };
constexpr unsigned kSectionCount = 9;

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

constexpr SectionDef kSections[kSectionCount] = {
    {"VOICE", 4, {{"CUTOFF", voice(AcidVoice::Cutoff)}, {"RESO", voice(AcidVoice::Reso)}, {"ENV MOD", voice(AcidVoice::EnvMod)}, {"DECAY", voice(AcidVoice::Decay)}}},
    {"DISTORTION", 4, {{"MODEL", app_item(Item::DistMode)}, {"DRIVE", dist(Distortion::Drive)}, {"TONE", dist(Distortion::Tone)}, {"MIX", dist(Distortion::Mix)}}},
    {"DELAY", 4, {{"TIME", delay(units::Delay::Time)}, {"FB", delay(units::Delay::Feedback)}, {"SEND", app_item(Item::SendA)}, {"TONE", delay(units::Delay::Tone)}}},
    {"GROWL", 4, {{"VOWEL", growl(Growl::Vowel, nullptr)}, {"FMT Q", growl(Growl::FormantQ, nullptr)}, {"CMB MIX", growl(Growl::CombMix, nullptr)}, {"GRL MIX", growl(Growl::Mix, nullptr)}}},
    {"WOBBLE", 4, {{"RATE", wobble(kWobbleRate, nullptr)}, {"DEPTH", wobble(kWobbleDepth, nullptr)}, {"TGT", wobble(kWobbleTarget, nullptr)}, {"WOB", Entry{}, true}}},
    {"MODDED", 4, {{"ACC DEC", voice(AcidVoice::AccDecay)}, {"SLIDE", voice(AcidVoice::Slide)}, {"SUB", voice(AcidVoice::SubOsc)}, {"MODS", app_item(Item::Mods)}}},
    {"NOTES", 4, {{"SOFT ATK", voice(AcidVoice::SoftAtk)}, {"ACC DEC", voice(AcidVoice::AccDecay)}, {"SLIDE", voice(AcidVoice::Slide)}, {"SWEEP", voice(AcidVoice::AccSweep)}}},
    {"TONE", 4, {{"TRACK", voice(AcidVoice::Tracking)}, {"FM", voice(AcidVoice::FilterFm)}, {"DRIVE", voice(AcidVoice::Overdrive)}, {"MUFFLER", voice(AcidVoice::Muffler)}}},
    {"TRACT", 4, {{"VOWEL", tract(TractFx::Vowel, nullptr)}, {"RES", tract(TractFx::Res, nullptr)}, {"SIZE", tract(TractFx::Size, nullptr)}, {"TRC MIX", tract(TractFx::Mix, nullptr)}}},
};
static_assert(kSecVoice == 1u << 0 && kSecDist == 1u << 1 && kSecDelay == 1u << 2 && kSecGrowl == 1u << 3 && kSecWobble == 1u << 4 && kSecModded == 1u << 5 && kSecNotes == 1u << 6 &&
                  kSecTone == 1u << 7 && kSecTract == 1u << 8,
              "the sections are in the mask's order");
static_assert(kSectionCount <= 8u * sizeof(unsigned), "the sections fit a mask");

// ---- The groups (docs/MODS-PLAN.md 5.1) -----------------------------------------------------------------------------------------------------------

// What each page shows as read-outs: the sections whose knobs it does not draw large (MODS-PLAN 5.1.1: VOICE, the anchor of the sound, and the page's closest neighbours).
constexpr const char* kInstrumentTitles[2] = {"INSTRUMENT VOICE", "INSTRUMENT FX"};
constexpr const char* kInstrumentTabs[2] = {"VOICE", "FX"};
constexpr const Page* kInstrumentPages[2] = {nullptr, &kFxPage};
constexpr unsigned kInstrumentShown[2] = {kSecDist | kSecDelay | kSecModded, kSecVoice | kSecModded};
constexpr help_texts::Id kInstrumentHelp[2] = {help_texts::Id::Voice, help_texts::Id::Fx};

constexpr const char* kModdedTitles[2] = {"MODDED NOTES", "MODDED TONE"};
constexpr const char* kModdedTabs[2] = {"NOTES", "TONE"};
constexpr const Page* kModdedPages[2] = {&kNotesPage, &kTonePage};
constexpr unsigned kModdedShown[2] = {kSecVoice | kSecTone, kSecVoice | kSecNotes};
constexpr help_texts::Id kModdedHelp[2] = {help_texts::Id::ModdedNotes, help_texts::Id::ModdedTone};

// Sample Play's group (A2): TRACT first, GROWL on the second press, as Sample Load flips MODDED's two pages. Both show VOICE and the wobble (it swings either one's VOWEL).
constexpr const char* kGrowlTitles[2] = {"TRACT", "GROWL"};
constexpr const Page* kGrowlPages[2] = {&kTractPage, &kGrowlPage};
constexpr unsigned kGrowlShown[2] = {kSecVoice | kSecWobble, kSecVoice | kSecWobble};
constexpr help_texts::Id kGrowlHelp[2] = {help_texts::Id::Tract, help_texts::Id::Growl};
constexpr unsigned kTractPageIndex = 0;  // the page of kGrowlGroup that is the TRACT (the pads are its vowels)

constexpr const char* kWobbleTitles[1] = {"WOBBLE"};
constexpr const Page* kWobblePages[1] = {&kWobblePage};
constexpr unsigned kWobbleShown[1] = {kSecVoice | kSecGrowl};
constexpr help_texts::Id kWobbleHelp[1] = {help_texts::Id::Wobble};

constexpr InstrumentGroup kInstrumentGroup = {2, kInstrumentTitles, kInstrumentTabs, kInstrumentPages, kInstrumentShown, kInstrumentHelp,
                                              "PARAMS AGAIN = NEXT PAGE   SHIFT + F = MIDI LEARN"};
constexpr InstrumentGroup kModdedGroup = {2, kModdedTitles, kModdedTabs, kModdedPages, kModdedShown, kModdedHelp, "SHIFT + SAMPLE LOAD = MODS  SHIFT + F = LEARN"};
constexpr InstrumentGroup kGrowlGroup = {2, kGrowlTitles, kGrowlTitles, kGrowlPages, kGrowlShown, kGrowlHelp, "SAMPLE PLAY AGAIN = GROWL / TRACT   SHIFT + F = LEARN"};
constexpr InstrumentGroup kWobbleGroup = {1, kWobbleTitles, kWobbleTitles, kWobblePages, kWobbleShown, kWobbleHelp, "RUNS WHILE THE WOB ACTION IS ON"};
static_assert(kInstrumentGroup.count <= kMaxGroupPages && kModdedGroup.count <= kMaxGroupPages && kGrowlGroup.count <= kMaxGroupPages && kWobbleGroup.count <= kMaxGroupPages,
              "a group has at most kMaxGroupPages pages");

// The drawing's geometry (the area is the screen's, under the kit's header): the read-outs under the status line, then the drawer's two lines, then the page's knobs large at the bottom, directly
// above the kit's knob strip and PADS line (next to the F keys, as HOME's F row is; BACKLOG 2). The knobs and the drawer hang from the bottom of the area; the hint sits above the drawer.
constexpr int kBigH = 112;
constexpr int kSecY = frame::kStatusH + 4;
constexpr int kSecH = 100;
constexpr int kCellRowH = 24;
constexpr int kDrawerH = 14 + kCellRowH + 2 + kCellRowH;  // the step numbers, NOTE, GATE
constexpr int kHintH = 18;
int big_y(const Rect& area) noexcept { return area.h - app::kKnobStripH - 4 - kBigH; }
int drawer_y(const Rect& area) noexcept { return big_y(area) - 4 - kDrawerH; }

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
  return Rect{area.x + static_cast<int>(k) * cell_w + 3, area.y + big_y(area), cell_w - 6, kBigH};
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
        const int w = knobs[k].lit ? b.w : fill_w(knobs[k].norm, b.w);
        if (!knobs[k].blank && view::key_kind(knobs[k]) != KeyKind::Switch && knobs[k].focus == (pass == 1) && w > 0) {
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
    if (view::key_kind(knobs[k]) == KeyKind::Action) {  // its name is in the key below
      continue;
    }
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
      if (view::key_kind(knobs[k]) != KeyKind::Knob) {  // the chip of a switch and the key of an action (knob_strip.h): the same faces as the strip's
        if (pass == 0) {
          (view::key_kind(knobs[k]) == KeyKind::Switch ? view::draw_switch_face : view::draw_action_face)(r, b, b.y + 22, b.h - 38, knobs[k]);
        }
        continue;
      }
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
  const int y_numbers = area.y + drawer_y(area);
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
  const bool heard_gate = view.heard_accent || view.heard_slide;  // the latched mod is heard on accents or slides: the GATE row's A and S light
  r.text(area.x + 8, ui::caps_y(theme, Face::Label, y_gate, kCellRowH), "GATE", gate_owned ? Color::SelText : (heard_gate ? Color::Accent : Color::TextDim), Face::Label);
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
    const bool lit = mark(k) == 0 && ((view.heard_accent && c.accent) || (view.heard_slide && c.slide));
    r.text(cell_x(k) + (view::kCellW - 2 - r.text_width(c.gate, Face::Data)) / 2, ui::caps_y(theme, Face::Data, y_gate, kCellRowH), c.gate, lit ? Color::Accent : ink,
           Face::Data);
  }
}

// The action keys (docs/MODS-PLAN.md 5.3, 5.5; Controls::kind says which entries are): a key that runs at its press and is lit while it is on, not a knob the encoder latches. F8 of MODDED is STOCK, F8 of WOBBLE the WOB action.
bool is_action(const dsp::Engine& e, const Controls& controls, const Entry& entry) noexcept { return controls.kind(e, entry) == KeyKind::Action; }

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
      c.accent = info.accent;
      c.slide = info.slide;
      c.on = true;
    } else if (info.kind == Cell::Tie) {
      cut("~", 1, c.note, sizeof c.note);
      cut("~", 1, c.gate, sizeof c.gate);
      c.on = true;
    } else {
      cut("--", 2, c.note, sizeof c.note);
      cut(".", 1, c.gate, sizeof c.gate);
    }
    view.cells = k + 1u;
  }
}

const InstrumentGroup& instrument_group() noexcept { return kInstrumentGroup; }
const InstrumentGroup& modded_group() noexcept { return kModdedGroup; }
const InstrumentGroup& growl_group() noexcept { return kGrowlGroup; }
const InstrumentGroup& wobble_group() noexcept { return kWobbleGroup; }

void fill_sections(const dsp::Engine& e, const Controls& controls, bool wobble_on, const InstrumentGroup& group, unsigned page, InstrumentView& view) noexcept {
  view.sections = 0;
  if (page >= group.count) {
    return;
  }
  for (unsigned s = 0; s < kSectionCount && view.sections < InstrumentView::kMaxSections; ++s) {
    if ((group.shown[page] >> s & 1u) == 0u) {
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
    case Item::Tract:
      unit = wiring.tract;
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

void InstrumentScreen::init(Rig& rig, HomeMap& home, const InstrumentGroup& group) noexcept {
  // Every piece of state starts afresh: the app object outlives a run in the simulator's tests.
  rig_ = &rig;
  controls_ = &rig.controls;
  live_ = rig.live;
  home_ = &home;
  group_ = &group;
  for (unsigned i = 0; i < kMaxGroupPages; ++i) {
    own_[i] = PageKeys{};
    if (i < group.count && group.pages[i] != nullptr) {
      own_[i].set_table(group.pages[i], 1);
    }
  }
  page_ = 0;
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
  page_ = 0;
}

void InstrumentScreen::next_page() noexcept {
  release_keys();
  home_->release_row();  // the page has its own knobs: the encoder is the knob's again
  page_ = (page_ + 1u) % group_->count;
}

const char* InstrumentScreen::title() const noexcept { return group_->titles[page_]; }

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
  home_->knob_latched(keys().focused(), keys().held() != 0u);  // the lane the drawer shows follows the knob the encoder owns
}

// ---- Input ------------------------------------------------------------------------------------------------------------------

void InstrumentScreen::input(app::Context& ctx, const Input& in) noexcept {
  if (in.kind == core::InputKind::Pad) {
    pad_input(ctx, in);
  } else if (in.kind == core::InputKind::Encoder) {
    turn_input(ctx, in);
  } else if (in.kind == core::InputKind::Button) {
    button_input(ctx, in);
  }
}

// The pads. On the TRACT page they are its own (A2) unless Pattern is held (the pattern overlay stays the home map's): row 3's first five are the live vowels A E I O U, a tap writes
// the VOWEL knob (F1's read-out follows, and Rec while the pattern plays records it as it does a turn). A release always goes to the home map (a step or a key that was held when the page
// opened lets go). Everywhere else the home map has every pad.
void InstrumentScreen::pad_input(app::Context& ctx, const Input& in) noexcept {
  if (!tract_page() || home_->overlay() || !in.pad_press()) {
    (void)home_->input(ctx, in);
    return;
  }
  const int vowel = tract_vowel_pad(in.pad());
  if (vowel >= 0) {
    const Entry entry = Entry{Item::Tract, static_cast<std::uint16_t>(TractFx::Vowel), nullptr};
    controls_->set(ctx.engine(), entry, tract_vowel_point(static_cast<unsigned>(vowel)));
    home_->turned(ctx, entry);
    ctx.redraw();
  }
  // LINE SLICE (TRACT-KNOBS-PLAN 11.6): rows 0 to 2 of this page (pad_row 0..2) are the TRACT pattern's pads and do nothing until then; the rest of row 3 neither.
}

void InstrumentScreen::turn_input(app::Context& ctx, const Input& in) noexcept {
  if (in.delta() == 0) {
    return;
  }
  PageKeys& pk = keys();
  if (home_->turn(ctx, pk.lock_detents(ctx, *controls_, pk.focused(), in.delta(), in.shift), in.shift, pk.focused())) {  // hold a step + the encoder: the lock of the knob (or the step's ratchet or condition)
    pk.mark_turned();  // with an F key held, its release gives the latch back, as after any hold-and-turn
    return;
  }
  const std::uint32_t affected = pk.held() != 0u ? pk.held() : (1u << pk.focus());
  const Page& pg = pk.page();
  for (unsigned k = 0; k < kKeys && rig_->wiring.stock; ++k) {  // a player who reaches for a mod wants to hear it (MODS-PLAN 5.3): STOCK ends before the turn lands (a lock, a lane or a CC does not end it)
    if ((affected >> k & 1u) != 0u && is_mod(pg.entry[k])) {
      toggle_stock(*rig_, ctx);
    }
  }
  for (unsigned k = 0; k < kKeys; ++k) {  // a TARGET of VOWEL with the growl off, or of MOUTH with the tract off, would be silent: refused, with a toast (B5a, A2); with only the tract on, a turn up from CUTOFF lands on MOUTH. A project or a snapshot can still hold it
    const Entry& en = pg.entry[k];
    if ((affected >> k & 1u) == 0u || en.item != Item::Wobble || en.id != kWobbleTarget || in.delta() <= 0) {
      continue;
    }
    const std::uint8_t target = rig_->wiring.wobble.target;  // the turn goes up to the next one: CUTOFF to VOWEL needs the growl, VOWEL to MOUTH the tract
    const bool growl_off = target == kWobbleCutoff && controls_->get(ctx.engine(), Entry{Item::GrowlOn, 0, nullptr}) < 0.5f;
    const bool tract_off = target == kWobbleVowel && controls_->get(ctx.engine(), Entry{Item::TractOn, 0, nullptr}) < 0.5f;
    if (growl_off && controls_->get(ctx.engine(), Entry{Item::TractOn, 0, nullptr}) >= 0.5f) {  // VOWEL needs the growl but MOUTH has the tract: the turn goes past VOWEL to MOUTH
      controls_->set(ctx.engine(), en, static_cast<float>(kWobbleMouth));
      ctx.redraw();
      return;
    }
    if (growl_off || tract_off) {
      home_->toast(ctx, growl_off ? "GROWL IS OFF" : "TRACT IS OFF", true);
      return;
    }
  }
  (void)pk.input(ctx, in, *controls_);
  for (unsigned k = 0; k < kKeys; ++k) {  // Rec while the pattern plays: the move is written as motion under the playhead (the home map's)
    if ((affected >> k & 1u) != 0u) {
      home_->turned(ctx, pg.entry[k]);
    }
  }
}

void InstrumentScreen::button_input(app::Context& ctx, const Input& in) noexcept {
  if (Input held_turn; keys().arrow_turn(in, held_turn)) {  // Left / Right with an F key held: its knob by 1/20 of the range, as a turn
    turn_input(ctx, held_turn);
    keys().end_press();
    return;
  }
  if (in.repeat) {  // a held arrow's repeat with no F key held: nothing (Left and Right keep their single press)
    return;
  }
  if (app::is_fkey(in.which())) {
    const unsigned k = app::fkey_index(in.which());
    if (in.type == Input::Type::Press) {
      if (in.shift) {  // the Shift row is MIDI learn: the key arms its knob, and its release is not the page's
        tool_mask_ |= 1u << k;
        learn_key(ctx, k);
        ctx.redraw();
        return;
      }
      if (is_action(ctx.engine(), *controls_, keys().page().entry[k])) {  // F8 STOCK / WOB: it runs at the press, and the encoder and the latch stay where they were
        run_action(ctx, keys().page().entry[k]);
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
      if (is_action(ctx.engine(), *controls_, keys().page().entry[k])) {  // its press did the work; the release has no latch to give back
        return;
      }
      (void)keys().input(ctx, in, *controls_);
    }
    return;
  }
  (void)home_->input(ctx, in);  // the arrows, Copy / Insert / Delete, Rec, the colour buttons (the drawer rows)
}

// An action key's press: STOCK flips the voice's switch (the same gesture as Shift + Sample Loader), WOB latches or releases PERFORM's WOB action (LiveActions::toggle_latch).
void InstrumentScreen::run_action(app::Context& ctx, const Entry& entry) noexcept {
  if (entry.item == Item::Mods) {
    toggle_stock(*rig_, ctx);
  } else if (entry.item == Item::GrowlOn || entry.item == Item::TractOn) {
    controls_->set(ctx.engine(), entry, controls_->get(ctx.engine(), entry) >= 0.5f ? 0.0f : 1.0f);
  } else {
    live_->toggle_latch(ctx, PerfAction::Wobble);
  }
  ctx.redraw();
}

// Whether an action key is on now: STOCK's mirror, or the WOB action latched (a held one cannot be here: a screen change lets it go, and no colour button holds it on this screen).
bool InstrumentScreen::action_on(const dsp::Engine& e, const Entry& entry) const noexcept {
  if (entry.item == Item::Mods) {
    return !rig_->wiring.stock;  // lit while the mods are heard
  }
  if (entry.item == Item::GrowlOn || entry.item == Item::TractOn) {
    return controls_->get(e, entry) >= 0.5f;
  }
  return live_->latched(PerfAction::Wobble);
}

// ---- Soft keys and LEDs -----------------------------------------------------------------------------------------------------

// The Shift row (UX-303 5.5, D15): LEARN for each knob that is a unit parameter (lit while this key's learn is waiting for its controller), "-" for the others; nothing
// while Shift is up (the knob row carries the names).
void InstrumentScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  if (!ctx.panel().shift() || !page_learnable()) {  // a page with nothing to learn (WOBBLE) has no Shift row, not eight "-" (U8a)
    return;
  }
  for (unsigned k = 0; k < kKeys; ++k) {
    const Learn l = learn_of(ctx, k);
    if (l == Learn::No) {
      out.label[k] = "-";
      out.dim = static_cast<std::uint8_t>(out.dim | (1u << k));
      continue;
    }
    out.label[k] = "LEARN";
    if (l == Learn::Armed) {
      out.hot = static_cast<std::uint8_t>(out.hot | (1u << k));
    }
  }
}

bool InstrumentScreen::page_learnable() const noexcept {
  const Page& pg = keys().page();
  for (unsigned k = 0; k < kKeys; ++k) {
    dsp::UnitId unit = dsp::kNoUnit;
    std::uint16_t param = 0;
    if (learn_target(rig_->wiring, pg.entry[k], unit, param)) {
      return true;
    }
  }
  return false;
}

// What the Shift row says of key `k`: LEARN where the knob is a unit parameter (waiting for its controller while this key's learn is armed), "-" where it is not.
Learn InstrumentScreen::learn_of(const app::Context& ctx, unsigned k) const noexcept {
  if (!ctx.panel().shift() || !page_learnable()) {
    return Learn::None;
  }
  dsp::UnitId unit = dsp::kNoUnit;
  std::uint16_t param = 0;
  if (!learn_target(rig_->wiring, keys().page().entry[k], unit, param)) {
    return Learn::No;
  }
  return learn_armed_ && learn_page_ == page_ && learn_key_ == k && ctx.midi().learning() ? Learn::Armed : Learn::Can;
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

// On the TRACT page the pads are the live vowels (pad_input): the vowel that sounds is lit (the nearest one between two), the other four dim, the rest dark. Pattern held: the home map's overlay.
void InstrumentScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  if (!tract_page() || home_->overlay()) {
    home_->leds(ctx, leds);
    return;
  }
  leds.clear();
  const unsigned now = tract_nearest_vowel(controls_->get(ctx.engine(), Entry{Item::Tract, static_cast<std::uint16_t>(TractFx::Vowel), nullptr}));
  for (unsigned i = 0; i < kTractVowelCount; ++i) {
    leds.set(3u, i, i == now ? app::kLedOn : app::kLedDim);
  }
}

bool InstrumentScreen::tract_page() const noexcept { return group_ == &kGrowlGroup && page_ == kTractPageIndex; }

// HELP: the page's texts, LEARN on the keys of the Shift row that soft_keys() labels so (learn_target(): the unit's own parameters), and the home map's pads.
void InstrumentScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  (void)ctx;
  help_texts::fill(h, help_texts::text(group_->help[page_]));
  const Page& pg = keys().page();
  for (unsigned k = 0; k < kKeys; ++k) {
    dsp::UnitId unit = dsp::kNoUnit;
    std::uint16_t param = 0;
    h.fkey_shift[k] = learn_target(rig_->wiring, pg.entry[k], unit, param) ? "LEARN" : nullptr;
  }
  if (tract_page()) {  // the TRACT page's pads are the live vowels (a held Pattern, the overlay, is not shown in HELP)
    help_texts::fill_tract_pads(h);
    return;
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

void InstrumentScreen::pads_line(app::Context& ctx, char* out, unsigned cap) const noexcept {
  if (tract_page() && !home_->overlay()) {
    cut("LIVE VOWELS . BOTTOM ROW A E I O U . A TAP SETS THE VOWEL KNOB", cap - 1u, out, cap);
    return;
  }
  home_->pads_line(ctx, out, cap);
}

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
    if (is_action(e, *controls_, pg.entry[k])) {  // a key and not a knob (fill_knob gave it no travel: norm below 0 is the Action kind), its state ON / OFF as the hint, lit while on
      kn.lit = action_on(e, pg.entry[k]);
      cut(kn.lit ? "ON" : "OFF", sizeof kn.value - 1u, kn.value, sizeof kn.value);
      kn.unit = "";
      continue;
    }
    kn.focus = k == pk.focus() && (home_->row() == DrawerRow::None || home_->row() == DrawerRow::Lock);
    kn.held = (pk.held() >> k & 1u) != 0u;
    if (home_->lock_active() && kn.focus) {  // a step is held and the encoder is locking this key on it: its value, in Accent
      apply_lock(view, kn, home_->lock_step(), home_->lock_text(), home_->lock_norm());
    }
  }

  view.page = page_;
  view.tab_count = group_->count;
  view.tabs = group_->tabs;
  view.hint = group_->hint;
  if (ctx.panel().shift() && !page_learnable()) {  // WOBBLE's knobs are the screen's own settings, not unit parameters (U8a)
    view.hint = "NOT LEARNABLE";
  } else if (group_ == &kGrowlGroup && page_ == kTractPageIndex && controls_->get(e, Entry{Item::TractOn, 0, nullptr}) < 0.5f) {  // an effect that is off says how to bring it back (B5a)
    view.hint = "TRACT OFF: F8";
  } else if (group_ == &kGrowlGroup && page_ != kTractPageIndex && controls_->get(e, Entry{Item::GrowlOn, 0, nullptr}) < 0.5f) {
    view.hint = "GROWL OFF: F8";
  } else if (group_ == &kWobbleGroup && !live_->wobble_on()) {
    view.hint = "WOB OFF: F8";
  } else if (group_ == &kModdedGroup && rig_->wiring.stock) {
    view.hint = "MODS OFF: F8";
  } else if (is_mod(pk.focused())) {  // the latched mod says when it is heard (an "OFF: F8" hint above has priority)
    const help_texts::HeardOn h = help_texts::heard_on(pk.focused().id);
    const char* line = help_texts::heard_line(pk.focused().id, controls_->get(e, voice(AcidVoice::Accent)) <= 0.0f);
    if (line[0] != '\0') {
      view.hint = line;
      view.heard = true;
      view.heard_accent = h.accent;
      view.heard_slide = h.slide;
    }
  }
  for (unsigned k = 0; k < kKeys; ++k) {
    view.learn[k] = learn_of(ctx, k);
  }
  fill_sections(e, *controls_, live_->wobble_on(), *group_, page_, view);

  const seq::Bank& bank = *rig_->bank;
  view.marks = home_->drawer(ctx);
  fill_cells(bank.pattern(bank.selected()), static_cast<unsigned>(view.marks.page) * 16u, view);
}

app::Chrome InstrumentScreen::chrome(const app::Context& ctx) const noexcept { return skin::active(ctx) ? app::Chrome::None : app::Chrome::Full; }

const ui::Theme* InstrumentScreen::theme(const app::Context& ctx) const noexcept { return skin::active(ctx) ? &skin::theme() : nullptr; }

void InstrumentScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  if (skin::active(ctx)) {
    draw_skinned(ctx, r);
  } else {
    draw_classic(ctx, r, area);
  }
}

// The obsidian drawing (skin/instrument.cpp, mockup 06): the same view the classic drawing takes, filled the same way, and the header's status as the classic status line takes it.
void InstrumentScreen::draw_skinned(app::Context& ctx, ui::Renderer& r) noexcept {
  const std::uint32_t t0 = o3sdk::hal::cycles();
  home_->clamp_cursor();
  frame::StatusView status;
  home_->status(status);
  status.save_failed = rig_->project.failed;
  status.no_autosave = !rig_->project.autosave;
  enc_text(ctx, status.enc, sizeof status.enc);
  InstrumentView view;
  fill_view(ctx, view);
  skin::HeaderInputs in;
  in.title = title();
  in.pattern = rig_->bank->selected();
  in.tempo_mbpm = ctx.engine().tempo();
  in.playing = rig_->seq->playhead().playing;
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    in.perf[b] = rig_->wiring.perf[b];
  }
  in.now_ms = ctx.uptime_ms();
  in.covered = ctx.strip_area();
  char line[128];
  pads_line(ctx, line, sizeof line);
  skin::draw_instrument(r, view, status, in, line);
  constexpr std::uint32_t kCyclesPerUs = 180;  // the MK66's clock; the host's hal::cycles() is scaled to it
  skin::note_frame(static_cast<std::uint32_t>(r.words()), (o3sdk::hal::cycles() - t0) / kCyclesPerUs, ctx.uptime_ms());
}

void InstrumentScreen::draw_classic(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  home_->clamp_cursor();
  frame::StatusView status;
  home_->status(status);
  status.save_failed = rig_->project.failed;
  status.no_autosave = !rig_->project.autosave;
  enc_text(ctx, status.enc, sizeof status.enc);
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);

  InstrumentView view;
  fill_view(ctx, view);
  draw_big_knobs(r, area, view.knob);
  draw_sections(r, area, view);
  draw_drawer(r, area, view);
  if (std::strstr(view.hint, "OFF: F8") != nullptr || view.heard) {  // an effect that is off says how to bring it back (B5a), the latched mod when it is heard (BACKLOG 14.3); the other hints are the skin's
    r.text(area.x + 8, area.y + drawer_y(area) - kHintH, view.hint, Color::Accent, Face::Label);
  }

  const Rect strip{area.x, area.y + area.h - app::kKnobStripH, area.w, app::kKnobStripH};
  char line[128];
  pads_line(ctx, line, sizeof line);
  frame::draw_pads_line(r, Rect{strip.x, strip.y, strip.w, frame::kPadsLineH}, line);
  view::draw_knobs(r, strip, view.knob);
}

}  // namespace o3_303
