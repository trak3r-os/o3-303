// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The skin's layer queue (docs/SKIN-PLAN.md section 7, as the spike's Panel). A skinned screen queues its shapes (rects, round rects, sprites of the skin's bitmaps, points, lines)
// and its live text, in any order; flush_shapes() then draws them into one ui::plot::Canvas section one LAYER at a time, in the order of Layer, so the chip's state (colour, point
// size, bitmap bind) changes once a layer and not once a shape, which is what keeps a frame under its display-list budget (section 4.3). flush_text() draws the texts after the
// shapes, outside the section, through Renderer::text() in the frame's theme, one colour token at a time. No shape sits over text.
//
// The queue is a fixed array (in the SDRAM on the unit, see panel()) (kShapes of 18 bytes, kTexts of 12: the header and the F row use about 120 shapes and 70 texts between them; PERFORM's worst frame, all eleven actions on and the four buttons latched, with a toast, queues 108 texts and 202 shapes): a full queue drops what does not fit and counts it (dropped()), which the tests hold at
// zero; nothing is allocated. Main loop only. The text pointers must stay valid until flush_text(): the caller's members or literals.

#include "skin.h"

#include <o3_303/skin_assets.h>
#include <o3sdk/ui/plot/canvas.h>
#include <o3sdk/ui/renderer.h>

#include <cstdint>
#include <cstring>

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
  MeterOff,    // MASTER's and RECORD's LED-segment bars: the segments that are dark (one dim colour), over their window's recess
  MeterOn,     // ... the lit ones, a pass a zone (green, then amber)
  Wave,        // RECORD's envelope: the take's peaks as columns over the glass (one colour, red while it records, green once it is done)
  Switch,      // the WAVE switch: track, lever and its light, in the order queued
  ActKey,      // the action keys and PERFORM's lamps (sprites)
  KnobScale,   // the knobs' ticks, then their skirts, then their caps, each part for every knob (one bitmap bind a layer)
  KnobBody,
  KnobCap,
  Pointer,     // the knobs' pointers (lines)
  Glow,        // the halo of a lit LED (a point)
  Led,         // LED sprites
  KeyBody,     // INSTRUMENT's step keys: the body (tinted), then its light (white) and its shadow (black), each for every key (one bitmap bind)
  KeyLight,
  KeyDark,
  Seg,         // 7-segment cells
  SegDot,      // their decimal points (points)
  Logo,        // the wordmark
  kCount,
};
inline constexpr unsigned kLayerCount = static_cast<unsigned>(Layer::kCount);

class Panel {
 public:
  static constexpr unsigned kShapes = 384;  // INSTRUMENT's worst frame queues 3xx of them (its strip alone: 48 LEDs and glows, 48 key sprites)
  static constexpr unsigned kTexts = 192;   // PERFORM queues its 48 lamp labels beside the header and the F row's ~70; INSTRUMENT about 140

  // The queue empty again, the sheen's scissor the whole screen. `covered` is where the kit will draw its opaque system strip over this frame (Context::strip_area(), empty when none is up):
  // a shape or a text that lies wholly inside it, taken as far as it is on the screen, is not queued at all (skipped() counts them), because nothing of it would be seen. A shape that is
  // partly outside is queued whole. The queue then holds what is visible, and the frame under a strip costs what the strip adds to the shapes it does not cover.
  void clear(const ui::Rect& covered = ui::Rect{0, 0, 0, 0}) noexcept;
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

  // For the tests: shape `i` of the queue, when it is a sprite (false otherwise): what is queued is what is drawn.
  struct SpriteInfo {
    Layer layer;
    skin_assets::Id id;
    int x;
    int y;
    ui::Rgb rgb;
  };
  bool sprite_at(unsigned i, SpriteInfo& out) const noexcept;
  // ... and shape `i` when it is a plain rect (a rule, a bar's segment, an envelope column).
  struct RectInfo {
    Layer layer;
    ui::Rect box;
    ui::Rgb rgb;
  };
  bool rect_at(unsigned i, RectInfo& out) const noexcept;
  // ... and shape `i` when it is a round rect (a ring, a window, a tab): where and in what colour.
  struct RoundRectInfo {
    Layer layer;
    ui::Rect box;
    ui::Rgb rgb;
  };
  bool round_rect_at(unsigned i, RoundRectInfo& out) const noexcept;
  // ... and text `i` of the queue: where, what, in which colour token and face. `s` points into the buffers of the frame's drawing function (its locals): it dangles once the frame is
  // drawn. A test compares it by address or place and never dereferences it.
  struct TextInfo {
    int x;
    int y;
    const char* s;
    ui::Color color;
    ui::Face face;
  };
  bool text_at(unsigned i, TextInfo& out) const noexcept;

  unsigned shapes() const noexcept { return n_shapes_; }
  unsigned texts() const noexcept { return n_texts_; }
  unsigned dropped() const noexcept { return dropped_; }
  unsigned skipped() const noexcept { return skipped_; }  // shapes and texts left out for lying under the strip (clear())

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

  bool covering() const noexcept { return covered_.w > 0 && covered_.h > 0; }  // a strip is up: the boxes below are worth working out
  // True (and counted) when the box `x y w h`, cut to the screen, is wholly inside the covered rect: the caller queues nothing.
  bool hidden(int x, int y, int w, int h) noexcept;
  Shape* add(Kind kind, Layer layer, ui::Rgb rgb, std::uint8_t alpha) noexcept;
  void draw(ui::plot::Canvas& canvas, const Shape& s) const noexcept;

  Shape shape_[kShapes];
  Text text_[kTexts];
  unsigned n_shapes_ = 0;
  unsigned n_texts_ = 0;
  unsigned dropped_ = 0;
  unsigned skipped_ = 0;
  ui::Rect covered_{0, 0, 0, 0};
  ui::Rect sheen_clip_{0, 0, ui::kScreenW, ui::kScreenH};
};

// At most `max` characters of `s` (null: none) into `out`, whose `cap` includes the NUL.
void cut_to(char* out, unsigned cap, const char* s, unsigned max) noexcept;

// Parts the header and the F row share. An LED at its centre (cx, cy): the sprite, tinted `colour` when it is lit and the socket's own red-black when it is not, and the glow
// behind a lit one. A display or window: the lit lip under it and the recess a pixel higher (a shadow on top), corners of `radius_px`.
void led(Panel& p, int cx, int cy, bool on, ui::Rgb colour) noexcept;
void window(Panel& p, const ui::Rect& r, unsigned radius_px) noexcept;

// The faces' cell sizes (the kit's: Display 16x32, Data 10x20, Label 8x16, Micro 6x12) and a string's length, for the screens that lay text out by hand.
constexpr int face_w(ui::Face f) noexcept { return f == ui::Face::Display ? 16 : (f == ui::Face::Data ? 10 : (f == ui::Face::Label ? 8 : 6)); }
constexpr int face_h(ui::Face f) noexcept { return f == ui::Face::Display ? 32 : (f == ui::Face::Data ? 20 : (f == ui::Face::Label ? 16 : 12)); }
inline int text_len(const char* s) noexcept { return static_cast<int>(std::strlen(s)); }

// A box with its title in the gap of its top rule (the legend starts 10 px in from the corner).
void queue_box(Panel& p, const ui::Rect& box, const char* title) noexcept;
// The biggest of the Data, Label and Micro faces in which `s` fits a window `w` px wide with a margin of 4 px each side.
ui::Face fitting(const char* s, int w) noexcept;
// A readout: its window (corners of 3 px) and the text centred in it in the face `fitting()` picks.
void queue_readout_window(Panel& p, const ui::Rect& win, const char* s, ui::Color ink) noexcept;

// The panel itself, for a screen whose glass runs from `glass_top` to `glass_bottom` (docs/SKIN-PLAN.md 2.1; HOME: 66 and 384): the base and the texture in the bands above and below
// the glass, the sheen over the header, and the glass with its rim. The glass is the frame's CLEAR colour (skin::theme()'s Pane), so only its rim and the bands are queued.
void queue_panel(Panel& p, int glass_top, int glass_bottom) noexcept;

// The panel of a screen with no glass (INSTRUMENT, mockup 06): the base and the texture over the whole height, and the sheen over the header (y < `header_h`).
void queue_panel_full(Panel& p, int header_h) noexcept;

// A section's box with its title in a gap of the top rule: the rule's five pieces (a one pixel line round `b`, the top one broken from `gap_x0` to `gap_x1` for the title, which the caller
// draws). INSTRUMENT's sections, MASTER's and RECORD's boxes.
void rule_box(Panel& p, const ui::Rect& b, int gap_x0, int gap_x1) noexcept;

// The ballast of `skin ballast N` (skin.h): `words` words of one-pixel rects in a Canvas section of their own, to be called last in a frame's draw().
void draw_ballast(ui::Renderer& r, std::uint32_t words) noexcept;

// The one queue of the program, in the SDRAM (a few KB that a frame reuses). The skinned screens draw through it, one frame at a time.
Panel& panel() noexcept;

}  // namespace o3_303::skin
