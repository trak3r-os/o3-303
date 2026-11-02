// SPDX-License-Identifier: GPL-3.0-or-later
#include "panel.h"

#include <o3sdk/core/placement.h>

#include <new>

namespace o3_303::skin {

namespace {

using o3sdk::eve::Wrap;
using ui::Rect;

constexpr std::int16_t i16(int v) noexcept { return static_cast<std::int16_t>(v); }
constexpr std::uint16_t u16(unsigned v) noexcept { return static_cast<std::uint16_t>(v > 65535u ? 65535u : v); }

}  // namespace

void Panel::clear(const Rect& covered) noexcept {
  n_shapes_ = 0;
  n_texts_ = 0;
  dropped_ = 0;
  skipped_ = 0;
  covered_ = covered;
  sheen_clip_ = Rect{0, 0, ui::kScreenW, ui::kScreenH};
}

bool Panel::hidden(int x, int y, int w, int h) noexcept {
  const int x0 = x > 0 ? x : 0;
  const int y0 = y > 0 ? y : 0;
  const int x1 = x + w < ui::kScreenW ? x + w : ui::kScreenW;
  const int y1 = y + h < ui::kScreenH ? y + h : ui::kScreenH;
  if (x0 >= x1 || y0 >= y1 || x0 < covered_.x || y0 < covered_.y || x1 > covered_.x + covered_.w || y1 > covered_.y + covered_.h) {
    return false;
  }
  ++skipped_;
  return true;
}

Panel::Shape* Panel::add(Kind kind, Layer layer, ui::Rgb rgb, std::uint8_t alpha) noexcept {
  if (n_shapes_ >= kShapes) {
    ++dropped_;
    return nullptr;
  }
  Shape& s = shape_[n_shapes_++];
  s = Shape{0, 0, 0, 0, 0, rgb, alpha, kind, layer, 0, 0};
  return &s;
}

bool Panel::rect(Layer layer, const Rect& r, ui::Rgb rgb, std::uint8_t alpha) noexcept {
  if (covering() && hidden(r.x, r.y, r.w, r.h)) {
    return true;
  }
  Shape* s = add(Kind::Rect, layer, rgb, alpha);
  if (s == nullptr) {
    return false;
  }
  s->a = i16(r.x);
  s->b = i16(r.y);
  s->c = i16(r.w);
  s->d = i16(r.h);
  return true;
}

bool Panel::round_rect(Layer layer, const Rect& r, unsigned radius_q4, ui::Rgb rgb, std::uint8_t alpha) noexcept {
  if (covering() && hidden(r.x, r.y, r.w, r.h)) {
    return true;
  }
  Shape* s = add(Kind::RoundRect, layer, rgb, alpha);
  if (s == nullptr) {
    return false;
  }
  s->a = i16(r.x);
  s->b = i16(r.y);
  s->c = i16(r.w);
  s->d = i16(r.h);
  s->arg = u16(radius_q4);
  return true;
}

bool Panel::sprite(Layer layer, skin_assets::Id id, unsigned cell, int x, int y, ui::Rgb rgb, std::uint8_t alpha) noexcept {
  if (covering()) {
    const skin_assets::Info& size = skin_assets::info(id);
    if (hidden(x, y, size.draw_w, size.draw_h)) {
      return true;
    }
  }
  Shape* s = add(Kind::Sprite, layer, rgb, alpha);
  if (s == nullptr) {
    return false;
  }
  s->a = i16(x);
  s->b = i16(y);
  s->asset = static_cast<std::uint8_t>(id);
  s->cell = static_cast<std::uint8_t>(cell);
  return true;
}

bool Panel::texture(int y, int h, ui::Rgb tint) noexcept {
  if (covering() && hidden(0, y, ui::kScreenW, h)) {
    return true;
  }
  Shape* s = add(Kind::Texture, Layer::Texture, tint, 255);
  if (s == nullptr) {
    return false;
  }
  s->b = i16(y);
  s->d = i16(h);
  return true;
}

bool Panel::point(Layer layer, int cx_q4, int cy_q4, unsigned radius_q4, ui::Rgb rgb, std::uint8_t alpha) noexcept {
  if (covering()) {
    // The disc, and a pixel for its edge.
    const int r_px = static_cast<int>(radius_q4 / 16u) + 1;
    if (hidden(cx_q4 / 16 - r_px - 1, cy_q4 / 16 - r_px - 1, 2 * r_px + 2, 2 * r_px + 2)) {
      return true;
    }
  }
  Shape* s = add(Kind::Point, layer, rgb, alpha);
  if (s == nullptr) {
    return false;
  }
  s->a = i16(cx_q4);
  s->b = i16(cy_q4);
  s->arg = u16(radius_q4);
  return true;
}

bool Panel::line(Layer layer, int x0_q4, int y0_q4, int x1_q4, int y1_q4, unsigned w_q4, ui::Rgb rgb, std::uint8_t alpha) noexcept {
  if (covering()) {
    // The two ends, grown by the line's width (its caps reach that far) and a pixel; the Q4 values divide towards the smaller coordinate (an arithmetic shift).
    const int grow = static_cast<int>(w_q4 / 16u) + 2;
    const int lx = (x0_q4 < x1_q4 ? x0_q4 : x1_q4) >> 4;
    const int ly = (y0_q4 < y1_q4 ? y0_q4 : y1_q4) >> 4;
    const int hx = ((x0_q4 > x1_q4 ? x0_q4 : x1_q4) >> 4) + 1;
    const int hy = ((y0_q4 > y1_q4 ? y0_q4 : y1_q4) >> 4) + 1;
    if (hidden(lx - grow, ly - grow, hx - lx + 2 * grow, hy - ly + 2 * grow)) {
      return true;
    }
  }
  Shape* s = add(Kind::Line, layer, rgb, alpha);
  if (s == nullptr) {
    return false;
  }
  s->a = i16(x0_q4);
  s->b = i16(y0_q4);
  s->c = i16(x1_q4);
  s->d = i16(y1_q4);
  s->arg = u16(w_q4);
  return true;
}

bool Panel::text(int x, int y, const char* s, ui::Color c, ui::Face face) noexcept {
  // (a null string is queued as it always was: Renderer::text takes it; it has no box to hide)
  if (covering() && s != nullptr && hidden(x, y, text_len(s) * face_w(face), face_h(face))) {
    return true;
  }
  if (n_texts_ >= kTexts) {
    ++dropped_;
    return false;
  }
  text_[n_texts_++] = Text{i16(x), i16(y), s, c, face};
  return true;
}

bool Panel::sprite_at(unsigned i, SpriteInfo& out) const noexcept {
  if (i >= n_shapes_ || shape_[i].kind != Kind::Sprite) {
    return false;
  }
  const Shape& s = shape_[i];
  out = SpriteInfo{s.layer, static_cast<skin_assets::Id>(s.asset), s.a, s.b, s.rgb};
  return true;
}

bool Panel::rect_at(unsigned i, RectInfo& out) const noexcept {
  if (i >= n_shapes_ || shape_[i].kind != Kind::Rect) {
    return false;
  }
  const Shape& s = shape_[i];
  out = RectInfo{s.layer, Rect{s.a, s.b, s.c, s.d}, s.rgb};
  return true;
}

bool Panel::round_rect_at(unsigned i, RoundRectInfo& out) const noexcept {
  if (i >= n_shapes_ || shape_[i].kind != Kind::RoundRect) {
    return false;
  }
  const Shape& s = shape_[i];
  out = RoundRectInfo{s.layer, Rect{s.a, s.b, s.c, s.d}, s.rgb};
  return true;
}

bool Panel::text_at(unsigned i, TextInfo& out) const noexcept {
  if (i >= n_texts_) {
    return false;
  }
  const Text& t = text_[i];
  out = TextInfo{t.x, t.y, t.s, t.color, t.face};
  return true;
}

ui::plot::BitmapDesc Panel::texture_desc(int y, int h) noexcept {
  const skin_assets::Info& i = skin_assets::info(skin_assets::Id::TexObsidian);
  const unsigned rows = static_cast<unsigned>(h);
  return {ui::plot::kRamGApp + i.offset + static_cast<unsigned>(y) * i.stride, i.format, i.stride, i.cell_w, static_cast<std::uint16_t>(rows), i.draw_w,
          static_cast<std::uint16_t>(rows), Wrap::Repeat, Wrap::Border};
}

void Panel::draw(ui::plot::Canvas& canvas, const Shape& s) const noexcept {
  switch (s.kind) {
    case Kind::Rect:
      canvas.rect(Rect{s.a, s.b, s.c, s.d}, s.rgb, s.alpha);
      break;
    case Kind::RoundRect:
      canvas.round_rect(Rect{s.a, s.b, s.c, s.d}, s.arg, s.rgb, s.alpha);
      break;
    case Kind::Sprite:
      canvas.bind(skin_assets::desc(static_cast<skin_assets::Id>(s.asset)));
      canvas.sprite(s.cell, s.a, s.b, s.rgb, s.alpha);
      break;
    case Kind::Texture:
      canvas.bind(texture_desc(s.b, s.d));
      canvas.sprite(0, 0, s.b, s.rgb, s.alpha);
      break;
    case Kind::Point:
      canvas.point(s.a, s.b, s.arg, s.rgb, s.alpha);
      break;
    case Kind::Line:
      canvas.line(s.a, s.b, s.c, s.d, s.arg, s.rgb, s.alpha);
      break;
  }
}

// A stable counting sort of the queue's indices by key (a layer, a colour token): one pass to count, the prefix sums, one pass to scatter, so the SDRAM-resident queue is scanned
// twice and not once a layer. `start[k]..start[k + 1]` of `order` are the entries of key k, in the order they were queued.
namespace {

template <unsigned Keys, typename Entry, typename KeyOf>
void bucket(const Entry* entries, unsigned n, KeyOf key_of, std::uint16_t (&order)[Panel::kShapes], unsigned (&start)[Keys + 1]) noexcept {
  for (unsigned k = 0; k <= Keys; ++k) {
    start[k] = 0;
  }
  for (unsigned i = 0; i < n; ++i) {
    ++start[key_of(entries[i]) + 1u];
  }
  for (unsigned k = 0; k < Keys; ++k) {
    start[k + 1] += start[k];
  }
  unsigned next[Keys];
  for (unsigned k = 0; k < Keys; ++k) {
    next[k] = start[k];
  }
  for (unsigned i = 0; i < n; ++i) {
    order[next[key_of(entries[i])]++] = static_cast<std::uint16_t>(i);
  }
}

}  // namespace

void Panel::flush_shapes(ui::plot::Canvas& canvas) const noexcept {
  std::uint16_t order[kShapes];
  unsigned start[kLayerCount + 1];
  bucket<kLayerCount>(shape_, n_shapes_, [](const Shape& s) { return static_cast<unsigned>(s.layer); }, order, start);
  for (unsigned l = 0; l < kLayerCount; ++l) {
    const bool sheen = static_cast<Layer>(l) == Layer::Sheen;
    if (sheen) {
      canvas.scissor(sheen_clip_);
    }
    for (unsigned i = start[l]; i < start[l + 1]; ++i) {
      draw(canvas, shape_[order[i]]);
    }
    if (sheen) {
      canvas.scissor_reset();
    }
  }
}

void Panel::flush_text(ui::Renderer& r) const noexcept {
  static_assert(kTexts <= kShapes, "the shape queue's index array serves the texts");
  std::uint16_t order[kShapes];
  unsigned start[ui::kColorCount + 1];
  bucket<ui::kColorCount>(text_, n_texts_, [](const Text& t) { return static_cast<unsigned>(t.color); }, order, start);
  for (unsigned c = 0; c < ui::kColorCount; ++c) {
    const ui::Color color = static_cast<ui::Color>(c);
    for (unsigned i = start[c]; i < start[c + 1]; ++i) {
      const Text& t = text_[order[i]];
      r.text(t.x, t.y, t.s, color, t.face);
    }
  }
}

void cut_to(char* out, unsigned cap, const char* s, unsigned max) noexcept {
  unsigned n = 0;
  for (; s != nullptr && s[n] != '\0' && n < max && n + 1u < cap; ++n) {
    out[n] = s[n];
  }
  out[n] = '\0';
}

void led(Panel& p, int cx, int cy, bool on, ui::Rgb colour) noexcept {
  constexpr int kHalf = 8;  // the sprite is 16 x 16
  if (on) {
    p.point(Layer::Glow, cx * 16, cy * 16, 12 * 16, colour, pal::kGlowAlpha);
  }
  p.sprite(Layer::Led, skin_assets::Id::Led, 0, cx - kHalf, cy - kHalf, on ? colour : pal::kLedOff);
}

void window(Panel& p, const Rect& r, unsigned radius_px) noexcept {
  p.round_rect(Layer::WindowLip, Rect{r.x - 1, r.y, r.w + 2, r.h + 2}, (radius_px + 1u) * 16u, pal::kWindowLip, pal::kWindowLipAlpha);
  p.round_rect(Layer::WindowFace, Rect{r.x, r.y - 1, r.w, r.h + 1}, radius_px * 16u, pal::kWindow);
}

void rule_box(Panel& p, const Rect& b, int gap_x0, int gap_x1) noexcept {
  p.rect(Layer::Rule, Rect{b.x, b.y, gap_x0 - b.x, 1}, pal::kRule);
  p.rect(Layer::Rule, Rect{gap_x1, b.y, b.x + b.w - gap_x1, 1}, pal::kRule);
  p.rect(Layer::Rule, Rect{b.x, b.y, 1, b.h}, pal::kRule);
  p.rect(Layer::Rule, Rect{b.x + b.w - 1, b.y, 1, b.h}, pal::kRule);
  p.rect(Layer::Rule, Rect{b.x, b.y + b.h - 1, b.w, 1}, pal::kRule);
}

void queue_box(Panel& p, const Rect& box, const char* title) noexcept {
  constexpr int kLegendX = 10;  // the title starts this far in from the box's corner
  const int tw = text_len(title) * face_w(ui::Face::Label);
  rule_box(p, box, box.x + kLegendX - 4, box.x + kLegendX + tw + 4);
  p.text(box.x + kLegendX, box.y - 8, title, ui::Color::ChromeDim, ui::Face::Label);
}

ui::Face fitting(const char* s, int w) noexcept {
  const int n = text_len(s);
  return n * face_w(ui::Face::Data) <= w - 8 ? ui::Face::Data : (n * face_w(ui::Face::Label) <= w - 8 ? ui::Face::Label : ui::Face::Micro);
}

void queue_readout_window(Panel& p, const Rect& win, const char* s, ui::Color ink) noexcept {
  window(p, win, 3);
  const ui::Face face = fitting(s, win.w);
  p.text(win.x + (win.w - text_len(s) * face_w(face)) / 2, win.y + (win.h - face_h(face)) / 2, s, ink, face);
}

void queue_panel(Panel& p, int glass_top, int glass_bottom) noexcept {
  constexpr int kRound = 6;  // the glass's corners: the bands run that far under them, so the corners show the panel
  p.rect(Layer::Base, Rect{0, 0, ui::kScreenW, glass_top + kRound}, pal::kBase);
  p.rect(Layer::Base, Rect{0, glass_bottom - kRound, ui::kScreenW, ui::kScreenH - glass_bottom + kRound}, pal::kBase);
  p.texture(0, glass_top, pal::kTexTint);
  p.texture(glass_bottom, ui::kScreenH - glass_bottom, pal::kTexTint);
  p.set_sheen_clip(Rect{0, 0, ui::kScreenW, glass_top});
  p.line(Layer::Sheen, 120 * 16, -40 * 16, -60 * 16, 200 * 16, 36 * 16, pal::kSheen, pal::kSheenAlpha);
  p.line(Layer::Sheen, 260 * 16, -40 * 16, 60 * 16, 230 * 16, 14 * 16, pal::kSheen, pal::kSheenAlpha);
  p.round_rect(Layer::GlassRim, Rect{0, glass_top, ui::kScreenW, glass_bottom - glass_top}, kRound * 16, pal::kBezel);
  p.round_rect(Layer::GlassFace, Rect{3, glass_top + 3, ui::kScreenW - 6, glass_bottom - glass_top - 6}, (kRound - 1) * 16, pal::kGlass);
}

void queue_panel_full(Panel& p, int header_h) noexcept {
  p.rect(Layer::Base, Rect{0, 0, ui::kScreenW, ui::kScreenH}, pal::kBase);
  p.texture(0, ui::kScreenH, pal::kTexTint);
  p.set_sheen_clip(Rect{0, 0, ui::kScreenW, header_h});
  p.line(Layer::Sheen, 120 * 16, -40 * 16, -60 * 16, 200 * 16, 36 * 16, pal::kSheen, pal::kSheenAlpha);
  p.line(Layer::Sheen, 260 * 16, -40 * 16, 60 * 16, 230 * 16, 14 * 16, pal::kSheen, pal::kSheenAlpha);
}

void draw_ballast(ui::Renderer& r, std::uint32_t words) noexcept {
  if (words < 2u) {
    return;
  }
  ui::plot::Canvas canvas(r);
  canvas.open();
  for (std::uint32_t i = 0; i < words / 2u; ++i) {  // a rect is two words (VERTEX2F pair), and its colour is set once
    canvas.rect(Rect{0, ui::kScreenH - 1, 1, 1}, pal::kBase);
  }
  canvas.close();
}

Panel& panel() noexcept {
  // In the SDRAM, not the SRAM (SRAM_U keeps 32 KiB for the stack and the Panel's 5.4 KB overran that budget) and not the stack: zero-filled O3SDK_BIG storage and a placement
  // new, once (a Panel has non-zero defaults, as ProjectService's scratch). A frame writes it once and reads it once, in the main loop: the SDRAM's latency is nothing there.
  alignas(Panel) O3SDK_BIG static unsigned char storage[sizeof(Panel)];
  static Panel* const g_panel = new (storage) Panel();
  return *g_panel;
}

}  // namespace o3_303::skin
