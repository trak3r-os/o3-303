// SPDX-License-Identifier: GPL-3.0-or-later
#include "vault_screen.h"

#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/core/param.h>
#include <o3_303/pad_map.h>
#include <o3_303/step_model.h>

namespace o3_303 {

namespace {

using namespace o3sdk;
using app::Button;
using app::Input;
using ui::Color;
using ui::Face;
using ui::Rect;

constexpr int kInfoH = 34;
constexpr int kTopY = 40;     // the banks, the grid and the details start here (area-relative)
constexpr int kRowH = 19;     // 8 rows
constexpr int kTopH = 8 * kRowH;
constexpr int kHeroH = 184;

constexpr const char* kLoadNames[3] = {"BOTH", "PATTERN", "SOUND"};
constexpr const char* kDistNames[5] = {"GRIT", "TUBE", "HARD", "FUZZ", "FOLD"};

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
  view::fmt_uint(v, num);
  append(out, cap, num);
}

// A 0..1 fraction as a whole percent.
unsigned pct(float v) noexcept {
  const float p = v * 100.0f + 0.5f;
  return p < 0.0f ? 0u : static_cast<unsigned>(p);
}

// "<label> <value>" appended to `line`, a space first when the line is not empty.
void add_field(char* line, unsigned cap, const char* label, unsigned value) noexcept {
  if (line[0] != '\0') {
    append(line, cap, "  ");
  }
  append(line, cap, label);
  append(line, cap, " ");
  append_uint(line, cap, value);
}

}  // namespace

void VaultScreen::init(Rig& rig, MainScreen& main) noexcept {
  rig_ = &rig;
  main_ = &main;
  bank_ = 0;
  slot_ = 0;
  seed_ = 0;
  what_ = LoadWhat::Both;
  refresh();
}

void VaultScreen::refresh() noexcept {
  if (seed_ == 0u) {
    seq::gen::factory(preview_, bank_, slot_);
  } else {
    seq::gen::generate(preview_, bank_, slot_, seed_);
  }
}

void VaultScreen::enter(app::Context& ctx) noexcept {
  refresh();
  ctx.redraw();
}

void VaultScreen::move_slot(int by) noexcept {
  const int n = static_cast<int>(seq::gen::kSlots);
  slot_ = static_cast<unsigned>((static_cast<int>(slot_) + by % n + n) % n);
  refresh();
}

void VaultScreen::load_and_return(app::Context& ctx) noexcept {
  load_preset(ctx.engine(), *rig_, preview_, what_);
  char text[40] = "LOADED ";
  append(text, sizeof text, preview_.name);
  main_->toast(ctx, text);
  ctx.show(*main_);
}

void VaultScreen::input(app::Context& ctx, const Input& in) noexcept {
  if (in.pad_press()) {
    const unsigned s = in.pad() + (in.shift ? 16u : 0u);
    if (s < seq::gen::kSlots) {
      slot_ = s;
      refresh();
      ctx.redraw();
    }
    return;
  }
  if (in.turn()) {
    if (in.shift) {
      const int d = in.delta();
      const std::uint32_t s = seed_ + static_cast<std::uint32_t>(d);  // wraps: a seed is a number, 0 is the vault's own
      seed_ = s;
      refresh();
    } else {
      move_slot(in.delta());
    }
    ctx.redraw();
    return;
  }
  if (in.type != Input::Type::Press || in.kind != core::InputKind::Button) {
    return;
  }
  unsigned k = 0;
  if (in.fkey_press(k)) {
    bank_ = k;
    refresh();
    ctx.redraw();
    return;
  }
  switch (in.which()) {
    case Button::Left:
      move_slot(-1);
      break;
    case Button::Right:
      move_slot(1);
      break;
    case Button::Up:
      move_slot(-8);
      break;
    case Button::Down:
      move_slot(8);
      break;
    case Button::Rec:  // GEN: a new seed for this slot, different from boot to boot too
      seed_ = seed_ * 1664525u + 1013904223u + static_cast<std::uint32_t>(ctx.uptime_ms()) * 2654435761u + 1u;
      seed_ = seed_ == 0u ? 1u : seed_;
      refresh();
      break;
    case Button::Params:
      what_ = what_ == LoadWhat::Both ? LoadWhat::Pattern : (what_ == LoadWhat::Pattern ? LoadWhat::Sound : LoadWhat::Both);
      break;
    case Button::File:
      load_and_return(ctx);
      return;
    default:
      return;
  }
  ctx.redraw();
}

void VaultScreen::soft_keys(app::Context&, app::SoftKeys& out) noexcept {
  // The bank names, "OFF WORLD" shortened to fit a key.
  static constexpr const char* kKeyNames[seq::gen::kBanks] = {"AMBER", "SUB", "NEON", "ACID", "BLADE", "ROOFTOP", "OFF WRLD", "LAST"};
  for (unsigned i = 0; i < seq::gen::kBanks; ++i) {
    out.label[i] = kKeyNames[i];
  }
  out.hot = static_cast<std::uint8_t>(1u << bank_);
}

void VaultScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  const unsigned base = ctx.panel().shift() ? 16u : 0u;
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    const unsigned s = pad + base;
    if (s >= seq::gen::kSlots) {
      continue;
    }
    leds.set(pad, s == slot_ ? app::kLedOn : (s % 8u == 0u ? app::kLedLow : app::kLedDim));
  }
}

void VaultScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  (void)ctx;
  const ui::Theme& theme = r.theme();
  const seq::gen::Preset& pr = preview_;

  // ---- The strip: the bank's letter large, then bank, slot, seed and what a load takes ----
  const Rect strip{area.x, area.y, area.w, kInfoH};
  ui::strip(r, strip, ui::Edge::Bottom);
  const char letter[2] = {seq::gen::bank_letter(bank_), '\0'};
  r.text(10, ui::caps_y(theme, Face::Display, strip.y, strip.h), letter, Color::ChromeText, Face::Display);
  ui::FieldRow row(r, 40, strip.y + 23);
  row.field("BANK", Face::Data, seq::gen::bank_name(bank_), Color::ChromeDim, Color::ChromeText);
  char slot_text[4] = {static_cast<char>('0' + slot_ / 10u), static_cast<char>('0' + slot_ % 10u), '\0', '\0'};
  row.field("SLOT", Face::Data, slot_text, Color::ChromeDim, Color::ChromeText);
  char seed_text[16] = "VAULT";
  if (seed_ != 0u) {
    seed_text[0] = '\0';
    append_uint(seed_text, sizeof seed_text, seed_ % 10000u);
  }
  row.field("SEED", Face::Data, seed_text, Color::ChromeDim, seed_ != 0u ? Color::Sel : Color::Chrome,
            seed_ != 0u ? Color::SelText : Color::ChromeText);
  char load[16] = "LOAD ";
  append(load, sizeof load, kLoadNames[static_cast<unsigned>(what_)]);
  const int lw = r.text_width(load, Face::Label) + 2 * theme.metrics.gap;
  ui::FieldRow lrow(r, ui::kScreenW - 8 - lw, strip.y + 23);
  lrow.chip(Face::Label, load, Color::ChromeText, Color::Chrome);

  // ---- The banks ----
  const int top = area.y + kTopY;
  for (unsigned b = 0; b < seq::gen::kBanks; ++b) {
    const Rect cell{8, top + static_cast<int>(b) * kRowH, 196, kRowH - 2};
    const bool cur = b == bank_;
    if (cur) {
      r.panel(cell, Color::Sel);
    }
    const char ch[2] = {seq::gen::bank_letter(b), '\0'};
    const int ty = ui::caps_y(theme, Face::Label, cell.y, cell.h);
    r.text(cell.x + 6, ty, ch, cur ? Color::SelText : Color::Text, Face::Label);
    r.text(cell.x + 26, ty, seq::gen::bank_name(b), cur ? Color::SelText : Color::TextDim, Face::Label);
  }

  // ---- The slots: 8 x 8 ----
  const int gx = 216;
  for (unsigned s = 0; s < seq::gen::kSlots; ++s) {
    const Rect cell{gx + static_cast<int>(s % 8u) * 40, top + static_cast<int>(s / 8u) * kRowH, 38, kRowH - 2};
    const bool cur = s == slot_;
    r.panel(cell, cur ? Color::Sel : Color::Empty);
    char num[4] = {static_cast<char>('0' + s / 10u), static_cast<char>('0' + s % 10u), '\0', '\0'};
    r.text(cell.x + (cell.w - 16) / 2, ui::caps_y(theme, Face::Label, cell.y, cell.h), num, cur ? Color::SelText : (s == 0u ? Color::Text : Color::TextDim),
           Face::Label);
  }

  // ---- The details of the slot ----
  const int dx = 552;
  char name[27];
  unsigned n = 0;
  for (; pr.name[n] != '\0' && n < sizeof name - 1u; ++n) {
    name[n] = pr.name[n];
  }
  name[n] = '\0';
  r.text(dx, top, name, Color::Text, Face::Data);
  const seq::gen::Patch& pa = pr.patch;
  char line[40];
  int ly = top + 26;
  const auto put = [&](Color c) {
    r.text(dx, ly, line, c, Face::Label);
    ly += 18;
    line[0] = '\0';
  };
  line[0] = '\0';
  add_field(line, sizeof line, "TEMPO", static_cast<unsigned>(pa.tempo + 0.5f));
  add_field(line, sizeof line, "SWING", pct(pa.swing));
  put(Color::TextDim);
  append(line, sizeof line, pa.waveform == 0 ? "SAW" : "SQR");
  add_field(line, sizeof line, "CUT", static_cast<unsigned>(pa.cutoff + 0.5f));
  add_field(line, sizeof line, "RES", pct(pa.resonance));
  put(Color::TextDim);
  add_field(line, sizeof line, "ENV", pct(pa.env_mod));
  add_field(line, sizeof line, "DEC", static_cast<unsigned>(pa.decay * 1000.0f + 0.5f));
  add_field(line, sizeof line, "ACC", pct(pa.accent));
  put(Color::TextDim);
  append(line, sizeof line, "DIST ");
  append(line, sizeof line, pa.dist_active ? kDistNames[static_cast<unsigned>(pa.dist_type) % 5u] : "OFF");
  add_field(line, sizeof line, "DRV", pct(pa.dist_drive));
  add_field(line, sizeof line, "TONE", pct(pa.dist_tone));
  put(Color::TextDim);

  // ---- The pattern, as on the 303 screen ----
  const Rect hero{area.x, top + kTopH + 6, area.w, kHeroH};
  int lo = 36;
  int hi = 48;
  if (!pitch_range(pr.pattern, lo, hi)) {
    lo = 36;
    hi = 48;
  }
  view::Marks m;
  m.window = range_window(lo, hi, view::lane_height(hero.h, false));
  view::draw_steps(r, hero, pr.pattern, m);

  // ---- What the keys do: one line, the keys bright and what they do dim ----
  static constexpr const char* kHelp[][2] = {{"F1-F8", "BANK"}, {"ENC", "SLOT"},  {"SHIFT+ENC", "SEED"}, {"PADS", "SLOT"},
                                            {"REC", "GEN"},    {"PARAMS", "WHAT"}, {"FILE", "LOAD"},      {"PATTERN", "BACK"}};
  const int hy = area.y + area.h - 20;
  r.rule(area.x, hy - 3, area.w, ui::Axis::Horizontal, Color::Empty);
  int hx = 10;
  for (const auto& pair : kHelp) {
    r.text(hx, hy, pair[0], Color::Text, Face::Label);
    hx += r.text_width(pair[0], Face::Label) + 8;
    r.text(hx, hy, pair[1], Color::TextDim, Face::Label);
    hx += r.text_width(pair[1], Face::Label) + 16;
  }
}

}  // namespace o3_303
