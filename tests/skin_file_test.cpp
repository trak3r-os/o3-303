// SPDX-License-Identifier: GPL-3.0-or-later
// The obsidian skin's FILE PROJECTS and FILE LIBRARY (docs/SKIN-PLAN.md 2.4, SK7): the list's geometry (nine rows of 28 px in the glass, the scroll mark), the autosave line's ink, the F row's slot
// kinds and dim states from the view (a pure function), and the screens end to end on a scratch card with the skin on: the rows and the cursor row, "*" on the current project, the keys dim when the
// screen would refuse them, an armed REVERT or DELETE lit red, NO CARD, the name keyboard in the glass with its own keys, LIBRARY's chrome and theme with the skin on and off, and the fullest list
// frame queued whole (`dropped() == 0`). The pictures are skin_file.txt's.
#include <doctest/doctest.h>

#include "../app/src/file_screen.h"
#include "../app/src/project_service.h"
#include "../app/src/skin/file.h"
#include "../app/src/skin/frow.h"
#include "../app/src/skin/panel.h"
#include "../app/src/vault_screen.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/text_entry.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/sim/harness.h>
#include <o3sdk/ui/keyboard.h>
#include <o3_303/config_keys.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

using namespace o3sdk;
using namespace o3_303;
using app::Button;
namespace fs = std::filesystem;
namespace host = o3sdk::platform::host;

namespace {

bool same(const ui::Rgb& a, const ui::Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
bool inside(const ui::Rect& in, const ui::Rect& out) { return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h; }

// The app on a scratch card (or none), started, with the skin on, and the hands to play it.
struct Session {
  host::ScratchDir dir{"o3303-sk7"};
  host::DirCard card{dir.path()};
  std::unique_ptr<sim::Harness> h;

  explicit Session(bool with_card = true) {
    REQUIRE(dir.ok());
    h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, with_card ? static_cast<host::HostCard*>(&card) : nullptr, "o3_303");
    REQUIRE(h->start());
    app_project().set_autosave_period(1'000);  // (init() at setup made it the default again)
    h->run_ms(300);
  }
  void run(unsigned ms) { h->run_ms(ms); }
  app::Context& ctx() { return h->runtime().context(); }
  void skin(bool on) {
    o3sdk_app().config_set(ctx(), config::kSkin, on ? 1.0f : 0.0f);
    run(60);
  }
  void tap(Button b, unsigned hold = 40) {
    h->press(b);
    h->run_ms(2);
    h->run_ms(hold);
    h->release(b);
    h->run_ms(2);
  }
  void pad(unsigned p) {
    h->pad_down(p);
    h->run_ms(40);
    h->pad_up(p);
    h->run_ms(2);
  }
  void turn(int d) {
    h->turn(d);
    h->run_ms(2);
  }
  void settle(unsigned limit_ms = 3000) {
    for (unsigned i = 0; i < limit_ms && !(app_project().idle() && !ctx().files().busy() && !app_project().request_busy()); ++i) {
      h->run_ms(1);
    }
    h->run_ms(20);
  }
  const char* title() { return ctx().screen()->title(); }
  FileScreen& file() {
    REQUIRE(std::strcmp(title(), "FILE PROJECTS") == 0);
    return *static_cast<FileScreen*>(ctx().screen());
  }
  void to_file() {
    if (std::strcmp(title(), "FILE PROJECTS") != 0) {
      tap(Button::File);
      if (std::strcmp(title(), "FILE PROJECTS") != 0) {
        tap(Button::File);  // it was the library
      }
    }
    run(300);  // the list is read
  }
  // The keyboard's pads for `text` (lower case letters and digits).
  void type_name(const char* text) {
    for (const char* c = text; *c != '\0'; ++c) {
      unsigned key = app::kTextKeys;
      for (unsigned k = 0; k < app::kTextKeys; ++k) {
        if (app::kTextKeysSmall[k] == *c) {
          key = k;
          break;
        }
      }
      REQUIRE(key < app::kTextKeys);
      unsigned p = 0;
      while (app::kTextPadKey[p] != key) {
        ++p;
      }
      pad(p);
    }
  }
  // SAVE AS (`as`) or NEW from FILE with `name`: the keyboard, the name, OK.
  void make(const char* name, bool as = true) {
    to_file();
    tap(as ? Button::F3 : Button::F4);
    REQUIRE(file().naming());
    type_name(name);
    tap(Button::F7);
    settle();
    run(300);
  }
  fs::path path(const std::string& rel) const { return fs::path(dir.path()) / "Apps" / "o3_303" / rel; }
};

// The Panel's last frame, counted.
struct Queue {
  const skin::Panel& q = skin::panel();
  unsigned sprites(skin::Layer layer, skin_assets::Id id) const {
    unsigned n = 0;
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::SpriteInfo sp;
      n += q.sprite_at(i, sp) && sp.layer == layer && sp.id == id ? 1u : 0u;
    }
    return n;
  }
  unsigned round_rects(skin::Layer layer, int w, int h) const {
    unsigned n = 0;
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::RoundRectInfo r;
      n += q.round_rect_at(i, r) && r.layer == layer && r.box.w == w && r.box.h == h ? 1u : 0u;
    }
    return n;
  }
  // The round rects of `layer` in colour `rgb`: the cursor row's band is the one of the Fill layer in the Sel colour.
  bool round_rect(skin::Layer layer, const ui::Rgb& rgb, ui::Rect& out) const {
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::RoundRectInfo r;
      if (q.round_rect_at(i, r) && r.layer == layer && same(r.rgb, rgb)) {
        out = r.box;
        return true;
      }
    }
    return false;
  }
  unsigned rects(skin::Layer layer, int y_from, int y_to) const {
    unsigned n = 0;
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::RectInfo r;
      n += q.rect_at(i, r) && r.layer == layer && r.box.y >= y_from && r.box.y < y_to ? 1u : 0u;
    }
    return n;
  }
  // Is there a plain rect of `layer` exactly at `box`?
  bool has_rect(skin::Layer layer, const ui::Rect& box) const {
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::RectInfo r;
      if (q.rect_at(i, r) && r.layer == layer && r.box.x == box.x && r.box.y == box.y && r.box.w == box.w && r.box.h == box.h) {
        return true;
      }
    }
    return false;
  }
  bool tint_at(skin::Layer layer, skin_assets::Id id, int x, int y, ui::Rgb& out) const {
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::SpriteInfo sp;
      if (q.sprite_at(i, sp) && sp.layer == layer && sp.id == id && sp.x == x && sp.y == y) {
        out = sp.rgb;
        return true;
      }
    }
    return false;
  }
  // The colour token of the text at (x, y); false when there is none.
  bool text_at(int x, int y, ui::Color& out) const {
    for (unsigned i = 0; i < q.texts(); ++i) {
      skin::Panel::TextInfo t;
      if (q.text_at(i, t) && t.x == x && t.y == y) {
        out = t.color;
        return true;
      }
    }
    return false;
  }
  // F key `k`'s action key (the cap sits at x 8 + 100 k, y 405).
  bool key_tint(unsigned k, ui::Rgb& out) const { return tint_at(skin::Layer::ActKey, skin_assets::Id::ActKey, 8 + 100 * static_cast<int>(k), 405, out); }
  void intact() const {
    CHECK(q.dropped() == 0u);
    CHECK(q.shapes() < skin::Panel::kShapes);
    CHECK(q.texts() < skin::Panel::kTexts);
  }
};

}  // namespace

// ---- file.h --------------------------------------------------------------------------------------------------------------------

TEST_CASE("skin file: nine rows of 28 px in the glass between the info line and the PADS line; the scroll mark leaves room at the right") {
  const ui::Rect glass{3, 69, 794, 297};  // inside the rim, above the PADS line
  ui::Rect prev = skin::file_row_box(0, false);
  CHECK(prev.h == 28);
  for (unsigned i = 0; i < skin::kFileRows; ++i) {
    const ui::Rect b = skin::file_row_box(i, false);
    CHECK(inside(b, glass));
    CHECK(b.w == 768);
    CHECK(b.y == 106 + 28 * static_cast<int>(i));
    const ui::Rect m = skin::file_row_box(i, true);
    CHECK(m.x == b.x);
    CHECK(m.w + 8 + 6 == b.w);  // the mark's track and a gap
  }
  CHECK(skin::file_row_box(skin::kFileRows - 1, false).y + 28 <= 366);
  CHECK(skin::kFileRows <= kFileRowsMax);

  // The track is as high as the rows shown; the thumb sits in it, at its top for the first window and its bottom for the last.
  const ui::Rect track = skin::file_scroll_track(9);
  CHECK(track.h == 9 * 28);
  CHECK(track.x == skin::file_row_box(0, true).x + skin::file_row_box(0, true).w + 8);
  const ui::Rect top = skin::file_scroll_thumb(9, 30, 0, 9);
  const ui::Rect bottom = skin::file_scroll_thumb(9, 30, 21, 9);
  CHECK(top.y == track.y);
  CHECK(bottom.y + bottom.h == track.y + track.h);
  CHECK(inside(top, track));
  CHECK(inside(bottom, track));
  CHECK(top.h == bottom.h);
  CHECK(top.h >= 12);
  CHECK(skin::file_scroll_thumb(9, 1000, 0, 9).h == 12);  // never thinner than a handle
  CHECK(skin::file_scroll_thumb(9, 30, 5, 9).y > top.y);
  CHECK(skin::file_scroll_thumb(9, 30, 99, 9).y == bottom.y);  // a first past the end is the end
}

TEST_CASE("skin file: the autosave line's ink follows the last write") {
  CHECK(skin::saved_ink("AUTOSAVED 12 S AGO") == ui::Color::Play);
  CHECK(skin::saved_ink("AUTOSAVED JUST NOW") == ui::Color::Play);
  CHECK(skin::saved_ink("SAVING") == ui::Color::Accent);
  CHECK(skin::saved_ink("NOT SAVED") == ui::Color::Rec);
  CHECK(skin::saved_ink("NOT ON THE CARD YET") == ui::Color::TextDim);
  CHECK(skin::saved_ink("NOTHING WRITTEN YET") == ui::Color::TextDim);
  CHECK(skin::saved_ink("") == ui::Color::TextDim);
}

TEST_CASE("skin file: the F row from the view, LOAD to DELETE and LIBRARY as action keys, F7 empty; dim and armed as the view says") {
  FileView v;
  static constexpr const char* kLabels[8] = {"LOAD", "SAVE", "SAVE AS", "NEW", "REVERT", "DELETE", "", "LIBRARY"};
  for (unsigned k = 0; k < 8; ++k) {
    v.labels[k] = kLabels[k];
  }
  skin::FRowView f;
  skin::fill_file_frow(f, v);
  for (unsigned k = 0; k < 8; ++k) {
    CHECK(f.slot[k].kind == (k == 6 ? skin::SlotKind::Empty : skin::SlotKind::Action));
    CHECK_FALSE(f.slot[k].dim);
    CHECK_FALSE(f.slot[k].lit);
  }
  CHECK(std::strcmp(f.slot[0].name, "LOAD") == 0);
  CHECK(std::strcmp(f.slot[2].name, "SAVE AS") == 0);
  CHECK(std::strcmp(f.slot[7].name, "LIBRARY") == 0);
  CHECK(std::strcmp(f.slot[4].readout, "last save") == 0);
  CHECK(std::strcmp(f.slot[5].readout, "tap twice") == 0);
  CHECK(std::strcmp(f.slot[6].fkey, "F7") == 0);
  CHECK_FALSE(f.shift);

  // Dim (no card, or nothing to load): the bits the screen says; the key stays an action key, F8 is never dim there.
  v.dim = 0x3Fu;
  skin::fill_file_frow(f, v);
  for (unsigned k = 0; k < 8; ++k) {
    CHECK(f.slot[k].dim == (k < 6));
  }
  v.dim = (1u << 0) | (1u << 5);
  skin::fill_file_frow(f, v);
  CHECK(f.slot[0].dim);
  CHECK(f.slot[5].dim);
  CHECK_FALSE(f.slot[1].dim);

  // Armed: the label the screen gives ("REVERT?"), lit red, and "again?" under it.
  v.dim = 0;
  v.labels[4] = "REVERT?";
  v.hot = 1u << 4;
  skin::fill_file_frow(f, v);
  CHECK(f.slot[4].lit);
  CHECK(std::strcmp(f.slot[4].name, "REVERT?") == 0);
  CHECK(std::strcmp(f.slot[4].readout, "again?") == 0);
  CHECK_FALSE(f.slot[5].lit);

  // The keyboard's own keys: TYPE, OK and CANCEL, the rest empty boxes; OK dim while the name is refused.
  FileView n;
  n.naming = true;
  n.labels[0] = "TYPE";
  n.labels[6] = "OK";
  n.labels[7] = "CANCEL";
  n.dim = 1u << 6;
  skin::fill_file_frow(f, n);
  for (unsigned k = 0; k < 8; ++k) {
    CHECK(f.slot[k].kind == (k == 0 || k >= 6 ? skin::SlotKind::Action : skin::SlotKind::Empty));
  }
  CHECK(f.slot[6].dim);
  CHECK_FALSE(f.slot[7].dim);
  CHECK(std::strcmp(f.slot[6].readout, "accept") == 0);
}

// ---- The screens ---------------------------------------------------------------------------------------------------------------

TEST_CASE("skin file: FILE takes no kit chrome and the obsidian theme with the skin on; LIBRARY keeps the kit's chrome and takes the obsidian theme; the skin off changes neither") {
  Session s;
  for (const bool library : {false, true}) {
    s.to_file();
    if (library) {
      s.tap(Button::File);
      REQUIRE(std::strcmp(s.title(), "FILE LIBRARY") == 0);
    }
    app::Context& ctx = s.ctx();
    CHECK(ctx.screen()->chrome(ctx) == app::Chrome::Full);
    CHECK(ctx.screen()->theme(ctx) == nullptr);
    s.skin(true);
    CHECK(ctx.screen()->chrome(ctx) == (library ? app::Chrome::Full : app::Chrome::None));
    CHECK(ctx.screen()->theme(ctx) == &skin::theme());
    s.skin(false);
    CHECK(ctx.screen()->chrome(ctx) == app::Chrome::Full);
    CHECK(ctx.screen()->theme(ctx) == nullptr);
  }
}

TEST_CASE("skin file: an empty card says NO PROJECTS YET, LOAD and DELETE are dim, SAVE AS and NEW are not, F7 is an empty box") {
  Session s;
  s.skin(true);
  s.to_file();
  const Queue q;
  const FileView& v = s.file().last_view();
  CHECK(v.total == 0u);
  CHECK(std::strcmp(v.empty, "NO PROJECTS YET") == 0);
  CHECK(std::strcmp(v.empty_hint, "SAVE AS OR NEW MAKES ONE") == 0);
  CHECK(v.card[0] == '\0');
  CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::ActKey) == 7u);  // F1 to F6 and F8
  CHECK(q.round_rects(skin::Layer::WindowFace, 80, 41) == 1u);           // F7
  ui::Rgb tint{};
  for (unsigned k = 0; k < 8; ++k) {
    if (k == 6) {
      continue;
    }
    REQUIRE(q.key_tint(k, tint));
    CHECK(same(tint, (k == 0 || k == 5) ? skin::pal::kKeyDim : skin::pal::kKey));
  }
  ui::Rect band{};
  CHECK_FALSE(q.round_rect(skin::Layer::Fill, skin::theme().rgb(ui::Color::Sel), band));  // no cursor row
  // The hint under a dim key is as quiet as the key (the Empty token); under a live key it is the dim text. "selected" (F1, dim) and "save point" (F2) are centred in the 92 px windows.
  ui::Color ink{};
  REQUIRE(q.text_at(50 - 8 * 8 / 2, 457 + (22 - 16) / 2, ink));
  CHECK(ink == ui::Color::Empty);
  REQUIRE(q.text_at(150 - 10 * 8 / 2, 457 + (22 - 16) / 2, ink));
  CHECK(ink == ui::Color::TextDim);
  q.intact();
}

TEST_CASE("skin file: with Shift held the F row is eight empty boxes (the Shift row is empty: F keys do nothing), the list stays; the keyboard keeps its keys") {
  Session s;
  s.make("aaa");
  s.skin(true);
  s.to_file();
  const Queue q;
  s.h->press(Button::Shift);
  s.run(60);
  CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::ActKey) == 0u);
  CHECK(q.round_rects(skin::Layer::WindowFace, 80, 41) == 8u);
  CHECK(std::strcmp(s.file().last_view().labels[0], "") == 0);
  ui::Rect band{};
  CHECK(q.round_rect(skin::Layer::Fill, skin::theme().rgb(ui::Color::Sel), band));  // the list is there
  // F1 under Shift does what classic does: nothing (no keyboard, no load).
  s.tap(Button::F3);
  CHECK_FALSE(s.file().naming());
  s.h->release(Button::Shift);
  s.run(60);
  CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::ActKey) == 7u);
  // The keyboard's row is its own, Shift or not.
  s.tap(Button::F3);
  REQUIRE(s.file().naming());
  s.h->press(Button::Shift);
  s.run(60);
  CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::ActKey) == 3u);
  s.h->release(Button::Shift);
  s.tap(Button::F8);
}

TEST_CASE("skin file: nine projects fill the window with no scroll mark; a tenth adds the mark and narrows the rows by its track and gap") {
  Session s;
  s.make("aaa");
  for (unsigned i = 0; i < 8; ++i) {
    char name[8];
    std::snprintf(name, sizeof name, "p%02u", i);
    fs::create_directories(s.path(name));
  }
  s.skin(true);
  s.tap(Button::File);  // LIBRARY and back: the list is read again
  s.tap(Button::File);
  s.run(600);
  FileScreen& f = s.file();
  REQUIRE(app_project().projects() == 9u);
  const Queue q;
  CHECK(f.last_view().shown == 9u);
  ui::Rect band{};
  REQUIRE(q.round_rect(skin::Layer::Fill, skin::theme().rgb(ui::Color::Sel), band));
  CHECK(band.w == skin::file_row_box(f.cursor(), false).w);
  CHECK(q.rects(skin::Layer::Fill, 0, 480) == 0u);  // no thumb
  CHECK_FALSE(q.has_rect(skin::Layer::Rule, skin::file_scroll_track(9)));

  fs::create_directories(s.path("p08"));
  s.tap(Button::File);
  s.tap(Button::File);
  s.run(600);
  REQUIRE(app_project().projects() == 10u);
  CHECK(f.last_view().shown == 9u);
  REQUIRE(q.round_rect(skin::Layer::Fill, skin::theme().rgb(ui::Color::Sel), band));
  CHECK(band.w == skin::file_row_box(f.cursor(), true).w);
  CHECK(band.w == skin::file_row_box(0, false).w - 14);
  CHECK(q.has_rect(skin::Layer::Rule, skin::file_scroll_track(9)));
  CHECK(q.has_rect(skin::Layer::Fill, skin::file_scroll_thumb(9, 10, 0, 9)));
  q.intact();
}

TEST_CASE("skin file: a current project outside the window has no \"*\", and NOW still names it; the thumb follows the window") {
  Session s;
  s.make("zzz");  // the last of the list in order
  for (unsigned i = 0; i < 14; ++i) {
    char name[8];
    std::snprintf(name, sizeof name, "p%02u", i);
    fs::create_directories(s.path(name));
  }
  s.skin(true);
  s.tap(Button::File);
  s.tap(Button::File);
  s.run(600);
  FileScreen& f = s.file();
  REQUIRE(app_project().projects() == 15u);
  s.turn(-60);  // the top: the window starts at 0, zzz is the 15th
  s.run(60);
  const Queue q;
  const FileView v = f.last_view();  // copies: the screen's view is redrawn under every turn
  CHECK(v.first == 0u);
  CHECK(std::strcmp(v.current, "zzz") == 0);
  for (unsigned i = 0; i < v.shown; ++i) {
    CHECK_FALSE(v.row[i].current);
    const ui::Rect b = skin::file_row_box(i, true);
    ui::Color ink{};
    CHECK_FALSE(q.text_at(b.x + b.w - 34, b.y + (b.h - 20) / 2, ink));  // no "*" in any row
  }
  ui::Color ink{};
  REQUIRE(q.text_at(232 + 3 * 8 + 8, 76, ink));  // NOW's value: the current project's name
  CHECK(ink == ui::Color::Text);
  CHECK(q.has_rect(skin::Layer::Fill, skin::file_scroll_thumb(v.shown, v.total, 0, skin::kFileRows)));
  // To the end: the window starts at 6, zzz is in it with its star, the thumb is at the foot of the track.
  s.turn(60);
  s.run(60);
  const FileView e = f.last_view();
  CHECK(e.first == 6u);
  CHECK(e.row[8].current);
  const ui::Rect b = skin::file_row_box(8, true);
  REQUIRE(q.text_at(b.x + b.w - 34, b.y + (b.h - 20) / 2, ink));
  CHECK(ink == ui::Color::SelText);  // the cursor is on it
  const ui::Rect thumb = skin::file_scroll_thumb(e.shown, e.total, e.first, skin::kFileRows);
  CHECK(q.has_rect(skin::Layer::Fill, thumb));
  CHECK(thumb.y + thumb.h == skin::file_scroll_track(e.shown).y + skin::file_scroll_track(e.shown).h);
  // One step up is a window of the same size: first stays at 6 until the cursor leaves it, then the thumb moves up.
  s.turn(-9);
  s.run(60);
  const FileView m = f.last_view();
  CHECK(m.first < e.first + 1u);
  CHECK(q.has_rect(skin::Layer::Fill, skin::file_scroll_thumb(m.shown, m.total, m.first, skin::kFileRows)));
  CHECK(m.first == 5u);
  CHECK(skin::file_scroll_thumb(m.shown, m.total, m.first, skin::kFileRows).y < thumb.y);
}

TEST_CASE("skin file: the rows, the cursor row banded, \"*\" on the current project, and the pads and the encoder move the cursor as they did") {
  Session s;
  s.make("aaa");
  s.make("bbb", false);
  s.make("ccc", false);  // the current project is ccc, the last one made
  s.skin(true);
  s.to_file();
  FileScreen& f = s.file();
  REQUIRE(app_project().projects() == 3u);
  const Queue q;
  const FileView& v = f.last_view();
  CHECK(v.total == 3u);
  CHECK(v.shown == 3u);
  CHECK(v.first == 0u);
  CHECK(std::strcmp(v.empty, "") == 0);
  CHECK(std::strcmp(v.current, "ccc") == 0);
  const char* const names[3] = {"aaa", "bbb", "ccc"};
  for (unsigned i = 0; i < 3; ++i) {
    CHECK(std::strcmp(v.row[i].name, names[i]) == 0);
    CHECK(v.row[i].current == (i == 2));
    CHECK(v.row[i].cursor == (i == f.cursor()));
  }
  // The cursor's band is the one Sel round rect, on its row's box; the "*" is a text at the right of the current row only.
  ui::Rect band{};
  REQUIRE(q.round_rect(skin::Layer::Fill, skin::theme().rgb(ui::Color::Sel), band));
  const ui::Rect want = skin::file_row_box(f.cursor(), false);
  CHECK(band.x == want.x);
  CHECK(band.y == want.y);
  CHECK(band.w == want.w);
  CHECK(band.h == want.h);
  ui::Color ink{};
  const ui::Rect cur = skin::file_row_box(2, false);
  const int star_y = cur.y + (cur.h - 20) / 2;
  REQUIRE(q.text_at(cur.x + cur.w - 34, star_y, ink));
  CHECK(ink == (f.cursor() == 2u ? ui::Color::SelText : ui::Color::Accent));
  CHECK_FALSE(q.text_at(skin::file_row_box(0, false).x + skin::file_row_box(0, false).w - 34, skin::file_row_box(0, false).y + 4, ink));
  CHECK(q.rects(skin::Layer::Fill, 0, 480) == 0u);  // no scroll mark: the list fits

  // The cursor moves with a pad and the encoder, and the band follows.
  s.pad(0);
  s.run(60);
  CHECK(f.cursor() == 0u);
  CHECK(f.last_view().row[0].cursor);
  CHECK_FALSE(f.last_view().row[2].cursor);
  REQUIRE(q.round_rect(skin::Layer::Fill, skin::theme().rgb(ui::Color::Sel), band));
  CHECK(band.y == skin::file_row_box(0, false).y);
  s.turn(1);
  s.run(60);
  CHECK(f.cursor() == 1u);
  REQUIRE(q.round_rect(skin::Layer::Fill, skin::theme().rgb(ui::Color::Sel), band));
  CHECK(band.y == skin::file_row_box(1, false).y);
  q.intact();
}

TEST_CASE("skin file: no card, the glass and the keys say so: NO CARD in red, F1 to F6 dim, LIBRARY still a key") {
  Session s;
  s.make("aaa");
  s.skin(true);
  s.to_file();
  s.h->card_out();
  s.run(3500);  // the card's notice is over
  const Queue q;
  const FileView& v = s.file().last_view();
  CHECK(std::strcmp(v.card, "NO CARD") == 0);
  CHECK(v.total == 0u);
  CHECK(std::strcmp(v.empty, "NO CARD") == 0);
  CHECK(std::strcmp(v.empty_hint, "PUT A WRITABLE CARD IN") == 0);
  CHECK((v.dim & 0x3Fu) == 0x3Fu);
  ui::Rgb tint{};
  for (unsigned k = 0; k < 8; ++k) {
    if (k == 6) {
      continue;
    }
    REQUIRE(q.key_tint(k, tint));
    CHECK(same(tint, k < 6 ? skin::pal::kKeyDim : skin::pal::kKey));
  }
  // The headline is the Display face centred in the list's box, in red.
  ui::Color ink{};
  const int mid = 106 + static_cast<int>(skin::kFileRows) * 28 / 2;
  REQUIRE(q.text_at(16 + 768 / 2 - 7 * 16 / 2, mid - 36, ink));
  CHECK(ink == ui::Color::Rec);
  q.intact();
  s.h->card_in();
  s.run(500);
}

TEST_CASE("skin file: REVERT and DELETE armed are lit red and read REVERT? and DELETE?; moving the cursor drops the second press") {
  Session s;
  s.make("aaa");
  s.make("bbb", false);
  s.skin(true);
  s.to_file();
  FileScreen& f = s.file();
  const Queue q;
  ui::Rgb tint{};
  s.turn(-9);  // aaa; bbb is current
  s.run(60);
  s.tap(Button::F5);
  s.run(60);
  CHECK(f.revert_armed());
  CHECK(std::strcmp(f.last_view().labels[4], "REVERT?") == 0);
  REQUIRE(q.key_tint(4, tint));
  CHECK(same(tint, skin::pal::kFocusFill));
  REQUIRE(q.key_tint(5, tint));
  CHECK(same(tint, skin::pal::kKey));
  s.turn(1);  // a move disarms
  s.run(60);
  s.turn(-1);
  s.run(60);
  CHECK_FALSE(f.revert_armed());
  REQUIRE(q.key_tint(4, tint));
  CHECK(same(tint, skin::pal::kKey));
  s.tap(Button::F6);  // aaa is not the current project: DELETE asks once
  s.run(60);
  CHECK(f.delete_armed());
  CHECK(std::strcmp(f.last_view().labels[5], "DELETE?") == 0);
  REQUIRE(q.key_tint(5, tint));
  CHECK(same(tint, skin::pal::kFocusFill));
  s.turn(1);
  s.run(60);
  CHECK_FALSE(f.delete_armed());
  q.intact();
}

TEST_CASE("skin file: SAVE AS puts the kit's keyboard in the glass with its own keys on the F row, and the list is gone") {
  Session s;
  s.make("aaa");
  s.skin(true);
  s.to_file();
  FileScreen& f = s.file();
  const Queue q;
  s.tap(Button::F3);
  s.run(60);
  REQUIRE(f.naming());
  const FileView& v = f.last_view();
  CHECK(v.naming);
  CHECK(std::strcmp(v.labels[0], "TYPE") == 0);
  CHECK(std::strcmp(v.labels[6], "OK") == 0);
  CHECK(std::strcmp(v.labels[7], "CANCEL") == 0);
  CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::ActKey) == 3u);  // F1, F7, F8
  CHECK(q.round_rects(skin::Layer::WindowFace, 80, 41) == 5u);           // F2 to F6
  ui::Rect band{};
  CHECK_FALSE(q.round_rect(skin::Layer::Fill, skin::theme().rgb(ui::Color::Sel), band));  // no list under the keyboard
  ui::Rgb tint{};
  REQUIRE(q.key_tint(0, tint));
  CHECK(same(tint, skin::pal::kKey));
  REQUIRE(q.key_tint(6, tint));
  CHECK(same(tint, skin::pal::kKey));  // OK: the name is the current project's, which the filter takes
  // The keyboard fits the glass above the PADS line, and the header's PAGE window still names the screen.
  const ui::Rect area = skin::file_keyboard_area();
  const ui::Rect kb = ui::keyboard_size(skin::theme());
  CHECK(kb.w <= area.w);
  CHECK(kb.h <= area.h);
  CHECK(area.y >= 66);
  CHECK(area.y + area.h <= 366);
  // Erase the name: the empty name is refused, OK goes dim.
  s.tap(Button::Delete);
  s.run(60);
  CHECK(f.naming());
  CHECK((f.last_view().dim & (1u << 6)) != 0u);
  REQUIRE(q.key_tint(6, tint));
  CHECK(same(tint, skin::pal::kKeyDim));
  s.tap(Button::F8);  // CANCEL: the list again
  s.run(60);
  CHECK_FALSE(f.naming());
  CHECK(f.last_view().naming == false);
  CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::ActKey) == 7u);
  q.intact();
}

TEST_CASE("skin file: the fullest frame (a window of nine 15 character names, more beyond, the scroll mark, a toast) is queued whole") {
  Session s;
  s.make("aaaaaaaaaaaaaaa");  // 15 characters: the longest a project name has
  for (unsigned i = 0; i < 14; ++i) {
    char name[ProjectInfo::kNameChars];
    std::snprintf(name, sizeof name, "projectnumber%02u", i);  // 15 characters
    fs::create_directories(s.path(name));
  }
  s.skin(true);
  s.tap(Button::File);  // LIBRARY and back: the list is read again
  s.tap(Button::File);
  s.run(600);
  FileScreen& f = s.file();
  REQUIRE(app_project().projects() == 15u);
  s.turn(4);
  s.tap(Button::F6);  // DELETE asks: the toast, the armed key, a cursor mid-list
  s.run(60);
  const Queue q;
  const FileView& v = f.last_view();
  CHECK(v.total == 15u);
  CHECK(v.shown == skin::kFileRows);
  CHECK(std::strlen(v.row[0].name) == 15u);
  ui::Rect band{};
  REQUIRE(q.round_rect(skin::Layer::Fill, skin::theme().rgb(ui::Color::Sel), band));
  CHECK(inside(band, ui::Rect{3, 69, 794, 297}));
  // The scroll mark: its track (Rule) and its thumb (Fill, TextDim) are queued and inside the glass.
  const ui::Rect track = skin::file_scroll_track(v.shown);
  CHECK(q.rects(skin::Layer::Rule, track.y, track.y + 1) >= 1u);
  CHECK(inside(track, ui::Rect{3, 69, 794, 297}));
  CHECK(q.q.dropped() == 0u);
  MESSAGE("FILE worst frame: " << q.q.shapes() << " shapes of " << skin::Panel::kShapes << ", " << q.q.texts() << " texts of " << skin::Panel::kTexts);
  CHECK(q.q.shapes() + 100u <= skin::Panel::kShapes);
  CHECK(q.q.texts() + 60u <= skin::Panel::kTexts);
  // The same with the keyboard up and a 15 character name typed.
  s.tap(Button::F3);
  s.run(60);
  CHECK(f.naming());
  CHECK(q.q.dropped() == 0u);
  MESSAGE("FILE keyboard frame: " << q.q.shapes() << " shapes, " << q.q.texts() << " texts");
  s.tap(Button::F8);
}

TEST_CASE("skin file: LIBRARY with the skin on is the classic layout (no skin bitmap drawn) and within the fallback's 1,500 words") {
  Session s;
  s.skin(true);
  s.to_file();
  s.tap(Button::F8);
  s.run(100);
  REQUIRE(std::strcmp(s.title(), "FILE LIBRARY") == 0);
  const std::size_t uses = s.h->raster().handle_app_uses();
  s.run(100);
  CHECK(s.h->raster().handle_app_uses() == uses);  // not skinned: the app's bitmaps are not drawn
  CHECK(s.h->display().last_frame_words() < 1500u);
  CHECK(s.h->display().peak_frame_words() < 1800u);
  s.tap(Button::File);
}
