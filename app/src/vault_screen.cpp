// SPDX-License-Identifier: GPL-3.0-or-later
#include "vault_screen.h"

#include "frame.h"
#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/core/param.h>
#include <o3_303/pad_map.h>
#include <o3_303/step_model.h>

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
constexpr int kTopY = frame::kStatusH + 40;  // the banks, the grid and the details start here (area-relative)
constexpr int kRowH = 19;     // 8 rows
constexpr int kTopH = 8 * kRowH;
constexpr int kHeroH = 176;
constexpr unsigned kToastMs = 2500;

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

const char* load_what_name(LoadWhat what) noexcept {
  switch (what) {
    case LoadWhat::Both: return "BOTH";
    case LoadWhat::Pattern: return "LINE";
    case LoadWhat::Sound: return "SOUND";
  }
  return "BOTH";
}

unsigned library_pads_text(unsigned bank, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  append(out, cap, "SLOTS 0-47 OF ");
  append(out, cap, seq::gen::bank_name(bank % seq::gen::kBanks));
  append(out, cap, " . TAP = LOAD . INSERT = LOAD THE SLOT SHOWN");
  unsigned n = 0;
  while (out[n] != '\0') {
    ++n;
  }
  return n;
}

void VaultScreen::init(Rig& rig) noexcept {
  rig_ = &rig;
  toast_[0] = '\0';
  toast_until_ms_ = 0;
  saving_ = false;
  seen_notice_ = rig.project.notice_seq;
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
  toast_[0] = '\0';
  seen_notice_ = rig_->project.notice_seq;
  ctx.redraw();
}

void VaultScreen::say(app::Context& ctx, const char* text) noexcept {
  unsigned n = 0;
  for (; text[n] != '\0' && n + 1u < sizeof toast_; ++n) {
    toast_[n] = text[n];
  }
  toast_[n] = '\0';
  toast_until_ms_ = ctx.uptime_ms() + kToastMs;
  ctx.redraw();
}

void VaultScreen::update(app::Context& ctx) noexcept {
  if (toast_[0] != '\0' && ctx.uptime_ms() >= toast_until_ms_) {
    toast_[0] = '\0';
    ctx.redraw();
  }
  // A SAVE from here (Shift + File) says SAVING until both files are written, then what became of it.
  if (rig_->project.saving != saving_) {
    saving_ = rig_->project.saving;
    ctx.redraw();
  }
  if (rig_->project.notice_seq != seen_notice_) {
    seen_notice_ = rig_->project.notice_seq;
    say(ctx, rig_->project.notice);
  }
}

void VaultScreen::move_slot(int by) noexcept {
  const int n = static_cast<int>(seq::gen::kSlots);
  slot_ = static_cast<unsigned>((static_cast<int>(slot_) + by % n + n) % n);
  refresh();
}

void VaultScreen::load_slot(app::Context& ctx) noexcept {
  load_preset(ctx.engine(), *rig_, preview_, what_);  // one UNDO; the level and master are not part of a preset
  char text[40] = "LOADED ";
  append(text, sizeof text, preview_.name);
  say(ctx, text);
}

void VaultScreen::input(app::Context& ctx, const Input& in) noexcept {
  if (in.pad_press()) {  // a pad is a slot of the bank (0..47) and loads it
    if (in.pad() < seq::gen::kSlots) {
      slot_ = in.pad();
      refresh();
      load_slot(ctx);
    }
    return;
  }
  if (in.turn()) {
    if (in.shift) {
      const std::uint32_t s = seed_ + static_cast<std::uint32_t>(in.delta());  // wraps: a seed is a number, 0 is the vault's own
      seed_ = s;
      refresh();
      char text[24] = "SEED BY HAND ";
      if (seed_ == 0u) {
        append(text, sizeof text, "VAULT");
      } else {
        append_uint(text, sizeof text, seed_ % 10000u);
      }
      say(ctx, text);
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
    if (!in.shift) {
      bank_ = k;
      refresh();
    } else if (k == 0u) {  // NEW SEED: another take of this slot, different from boot to boot too
      seed_ = seed_ * 1664525u + 1013904223u + static_cast<std::uint32_t>(ctx.uptime_ms()) * 2654435761u + 1u;
      seed_ = seed_ == 0u ? 1u : seed_;
      refresh();
    } else if (k == 1u) {  // LOAD what
      what_ = what_ == LoadWhat::Both ? LoadWhat::Pattern : (what_ == LoadWhat::Pattern ? LoadWhat::Sound : LoadWhat::Both);
      char text[24] = "LOAD ";
      append(text, sizeof text, load_what_name(what_));
      say(ctx, text);
    }
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
    case Button::Insert:
      load_slot(ctx);
      break;
    default:
      return;
  }
  ctx.redraw();
}

void VaultScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  if (ctx.panel().shift()) {  // the Shift row: NEW SEED and LOAD what, the rest empty
    out.label[0] = "NEW SEED";
    static constexpr const char* kLoadKeys[3] = {"LOAD BOTH", "LOAD LINE", "LOAD SOUND"};
    out.label[1] = kLoadKeys[static_cast<unsigned>(what_)];
    return;
  }
  // The bank names, "OFF WORLD" shortened to fit a key.
  static constexpr const char* kKeyNames[seq::gen::kBanks] = {"AMBER", "SUB", "NEON", "ACID", "BLADE", "ROOFTOP", "OFF WRLD", "LAST"};
  for (unsigned i = 0; i < seq::gen::kBanks; ++i) {
    out.label[i] = kKeyNames[i];
  }
  out.hot = static_cast<std::uint8_t>(1u << bank_);
}

void VaultScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  (void)ctx;
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {  // slots 0..47: slot 48..63 has no pad
    leds.set(pad, pad == slot_ ? app::kLedOn : (pad % 8u == 0u ? app::kLedLow : app::kLedDim));
  }
}

void VaultScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  const ui::Theme& theme = r.theme();
  const seq::gen::Preset& pr = preview_;

  // ---- The status line: the encoder's owner (the seed while Shift is held), the toast, a failed autosave ----
  frame::StatusView status;
  status.keys = false;
  std::strcpy(status.enc, ctx.panel().shift() ? "SEED" : "SLOT");
  status.save_failed = rig_->project.failed;
  status.toast = toast_[0] != '\0' ? toast_ : (saving_ ? "SAVING" : "");
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);

  // ---- The strip: the bank's letter large, then bank, slot, seed and what a load takes ----
  const Rect strip{area.x, area.y + frame::kStatusH, area.w, kInfoH};
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
  append(load, sizeof load, load_what_name(what_));
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
  m.window = range_window(lo, hi, view::lane_height(hero.h));
  view::draw_steps(r, hero, pr.pattern, m);

  // ---- The PADS line ----
  char pads[80];
  (void)library_pads_text(bank_, pads, sizeof pads);
  frame::draw_pads_line(r, Rect{area.x, area.y + area.h - frame::kPadsLineH, area.w, frame::kPadsLineH}, pads);
}

}  // namespace o3_303
