// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The skin's layer queue (docs/SKIN-PLAN.md section 7, as the spike's Panel). A skinned screen queues its shapes (rects, round rects, sprites of the skin's bitmaps, points, lines)
// and its live text, in any order; flush_shapes() then draws them into one ui::plot::Canvas section one LAYER at a time, in the order of Layer, so the chip's state (colour, point
// size, bitmap bind) changes once a layer and not once a shape, which is what keeps a frame under its display-list budget (section 4.3). flush_text() draws the texts after the
// shapes, outside the section, through Renderer::text() in the frame's theme, one colour token at a time. No shape sits over text.
//
// The queue is a fixed array (in the SDRAM on the unit, see panel()) (kShapes of 18 bytes, kTexts of 12: the header and the F row use about 120 shapes and 70 texts between them): a full queue drops what does not fit and counts it (dropped()), which the tests hold at
// zero; nothing is allocated. Main loop only. The text pointers must stay valid until flush_text(): the caller's members or literals.

#include "skin.h"

#include <o3_303/skin_assets.h>
#include <o3sdk/ui/plot/canvas.h>
#include <o3sdk/ui/renderer.h>

#include <cstdint>

namespace o3_303::skin {

// The order a frame's shapes are drawn in: the panel under everything, then what sits on it from the back to the front.
enum class Layer : std::uint8_t {
  Base,        // rects of the panel's colour (the header and the F row, and the corners the glass rounds)
  Texture,     // the tile, REPEAT in x, one band each (texture())
  Sheen,       // translucent lines over the header, scissored (set_sheen_clip)
  GlassRim,    // the bezel of the glass
  GlassFace,   // the glass inside it
  Rule,        // silkscreen rules
  WindowLip,   // the lit edge under every display and window
  HeldRim,     // an F key that is down: an amber rim a little larger than its readout window, under the window's face
  WindowFace,  // their recesses
  Fill,        // the focused readout's red fill, over its face
  Tab,         // the F-number's tab on a slot's label line
  Outline,     // a one pixel frame over a window (a message that says something did not happen)
  Switch,      // the WAVE switch: track, lever and its light, in the order queued
  ActKey,      // the action keys (sprites)
  KnobScale,   // the knobs' ticks, then their skirts, then their caps, each part for every knob (one bitmap bind a layer)
  KnobBody,
  KnobCap,
  Pointer,     // the knobs' pointers (lines)
  Glow,        // the halo of a lit LED (a point)
  Led,         // LED sprites
  Seg,         // 7-segment cells
  SegDot,      // their decimal points (points)
  Logo,        // the wordmark
  kCount,
};
inline constexpr unsigned kLayerCount = static_cast<unsigned>(Layer::kCount);

class Panel {
 public:
  static constexpr unsigned kShapes = 256;
  static constexpr unsigned kTexts = 96;

  // The queue empty again, the sheen's scissor the whole screen.
  void clear() noexcept;
  // Where the Sheen layer's lines are cut (the header).
  void set_sheen_clip(const ui::Rect& r) noexcept { sheen_clip_ = r; }

  // Each returns false (and counts a drop) when the queue is full.
  bool rect(Layer layer, const ui::Rect& r, ui::Rgb rgb, std::uint8_t alpha = 255) noexcept;
  bool round_rect(Layer layer, const ui::Rect& r, unsigned radius_q4, ui::Rgb rgb, std::uint8_t alpha = 255) noexcept;
  bool sprite(Layer layer, skin_assets::Id id, unsigned cell, int x, int y, ui::Rgb rgb, std::uint8_t alpha = 255) noexcept;
  // The texture over rows [y, y + h) of the screen: the tile's rows y.. drawn 800 wide, tinted (one bitmap bind of its own).
  bool texture(int y, int h, ui::Rgb tint) noexcept;
  bool point(Layer layer, int cx_q4, int cy_q4, unsigned radius_q4, ui::Rgb rgb, std::uint8_t alpha = 255) noexcept;
  bool line(Layer layer, int x0_q4, int y0_q4, int x1_q4, int y1_q4, unsigned w_q4, ui::Rgb rgb, std::uint8_t alpha = 255) noexcept;
  // Live text with its top-left at (x, y) in the theme's `face`, colour token `c`. `s` stays valid until flush_text().
  bool text(int x, int y, const char* s, ui::Color c, ui::Face face) noexcept;

  // Every queued shape, layer by layer, into `canvas` (whose section the caller has open()ed, and closes).
  void flush_shapes(ui::plot::Canvas& canvas) const noexcept;
  // Every queued text, one colour token at a time (the order of ui::Color).
  void flush_text(ui::Renderer& r) const noexcept;

  unsigned shapes() const noexcept { return n_shapes_; }
  unsigned texts() const noexcept { return n_texts_; }
  unsigned dropped() const noexcept { return dropped_; }

  // The texture's bitmap for the band [y, y + h): the app's tile starting at row y.
  static ui::plot::BitmapDesc texture_desc(int y, int h) noexcept;

 private:
  enum class Kind : std::uint8_t { Rect, RoundRect, Sprite, Texture, Point, Line };
  struct Shape {
    std::int16_t a, b, c, d;     // Rect: x y w h | Sprite, Texture: x y 0 h | Point: cx cy 0 0 (1/16 px) | Line: x0 y0 x1 y1 (1/16 px)
    std::uint16_t arg;           // the radius (RoundRect, Point) or the width (Line) in 1/16 px
    ui::Rgb rgb;
    std::uint8_t alpha;
    Kind kind;
    Layer layer;
    std::uint8_t asset;          // Sprite: skin_assets::Id
    std::uint8_t cell;
  };
  struct Text {
    std::int16_t x, y;
    const char* s;
    ui::Color color;
    ui::Face face;
  };

  Shape* add(Kind kind, Layer layer, ui::Rgb rgb, std::uint8_t alpha) noexcept;
  void draw(ui::plot::Canvas& canvas, const Shape& s) const noexcept;

  Shape shape_[kShapes];
  Text text_[kTexts];
  unsigned n_shapes_ = 0;
  unsigned n_texts_ = 0;
  unsigned dropped_ = 0;
  ui::Rect sheen_clip_{0, 0, ui::kScreenW, ui::kScreenH};
};

// At most `max` characters of `s` (null: none) into `out`, whose `cap` includes the NUL.
void cut_to(char* out, unsigned cap, const char* s, unsigned max) noexcept;

// Parts the header and the F row share. An LED at its centre (cx, cy): the sprite, tinted `colour` when it is lit and the socket's own red-black when it is not, and the glow
// behind a lit one. A display or window: the lit lip under it and the recess a pixel higher (a shadow on top), corners of `radius_px`.
void led(Panel& p, int cx, int cy, bool on, ui::Rgb colour) noexcept;
void window(Panel& p, const ui::Rect& r, unsigned radius_px) noexcept;

// The panel itself, for a screen whose glass runs from `glass_top` to `glass_bottom` (docs/SKIN-PLAN.md 2.1; HOME: 66 and 384): the base and the texture in the bands above and below
// the glass, the sheen over the header, and the glass with its rim. The glass is the frame's CLEAR colour (skin::theme()'s Pane), so only its rim and the bands are queued.
void queue_panel(Panel& p, int glass_top, int glass_bottom) noexcept;

// The ballast of `skin ballast N` (skin.h): `words` words of one-pixel rects in a Canvas section of their own, to be called last in a frame's draw().
void draw_ballast(ui::Renderer& r, std::uint32_t words) noexcept;

// The one queue of the program, in the SDRAM (a few KB that a frame reuses). The skinned screens draw through it, one frame at a time.
Panel& panel() noexcept;

}  // namespace o3_303::skin
