// SPDX-License-Identifier: GPL-3.0-or-later
// AutosaveClock (app/include/o3_303/autosave.h, docs/UX-303.md 8.4): live.o3a is due one period after the LAST change and at most 60 s after the FIRST
// unsaved one, OFF never is, and a save's start empties the wait.
#include <doctest/doctest.h>

#include <o3_303/autosave.h>

using o3_303::AutosaveClock;

TEST_CASE("autosave clock: nothing is due without a change") {
  AutosaveClock c;
  CHECK_FALSE(c.pending());
  CHECK_FALSE(c.due(0));
  CHECK_FALSE(c.due(1'000'000));
}

TEST_CASE("autosave clock: due one period after the last change, not before") {
  AutosaveClock c;
  CHECK(c.period() == 10'000u);
  c.changed(5'000);
  CHECK(c.pending());
  CHECK_FALSE(c.due(5'000));
  CHECK_FALSE(c.due(14'999));
  CHECK(c.due(15'000));
  CHECK(c.due(99'999));  // stays due until a save starts
}

TEST_CASE("autosave clock: a later change moves the due time, up to the cap") {
  AutosaveClock c;
  c.changed(0);
  c.changed(8'000);   // 8 s: the period restarts
  CHECK_FALSE(c.due(17'999));
  CHECK(c.due(18'000));

  // Changes every 5 s never leave a 10 s gap: the cap saves at 60 s after the first.
  AutosaveClock d;
  for (std::uint64_t t = 0; t < 60'000; t += 5'000) {
    d.changed(t);
    CHECK_FALSE(d.due(t));
  }
  CHECK_FALSE(d.due(59'999));
  CHECK(d.due(60'000));
}

TEST_CASE("autosave clock: a save's start empties the wait and the next change starts a new cap") {
  AutosaveClock c;
  c.changed(0);
  c.started();
  CHECK_FALSE(c.pending());
  CHECK_FALSE(c.due(1'000'000));
  c.changed(100'000);  // the first change after a save starts the cap's clock
  CHECK_FALSE(c.due(109'999));
  c.changed(150'000);
  CHECK_FALSE(c.due(159'999));
  CHECK(c.due(160'000));  // the period after the last, 60 s after the first is 160 s
}

TEST_CASE("autosave clock: the period is the setting; OFF is never due and keeps what waits") {
  AutosaveClock c;
  c.set_period(30'000);
  c.changed(0);
  CHECK_FALSE(c.due(29'999));
  CHECK(c.due(30'000));

  c.set_period(0);  // OFF
  CHECK_FALSE(c.due(1'000'000));
  CHECK(c.pending());  // the change is still counted
  c.set_period(10'000);
  CHECK(c.due(1'000'000));  // turned on again: what waited is saved
}

TEST_CASE("autosave clock: reset forgets what waited") {
  AutosaveClock c;
  c.changed(0);
  c.reset();
  CHECK_FALSE(c.pending());
  CHECK_FALSE(c.due(100'000));
}

TEST_CASE("autosave clock: the cap counts from the first change even when the period is long") {
  AutosaveClock c;
  c.set_period(30'000);
  c.changed(0);
  c.changed(25'000);
  c.changed(50'000);  // 30 s of quiet never comes
  CHECK_FALSE(c.due(59'999));
  CHECK(c.due(60'000));
}
