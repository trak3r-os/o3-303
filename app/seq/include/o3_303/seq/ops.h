// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The editing operations: pure functions on a Pattern (o3_303/pattern.h), no state, no allocation, safe on any copy. The project's
// undo history and the render-side handoff are the Bank's job (bank.h): call these through Bank::edit() to edit a project pattern.
//
// Conventions. `step` is 0..kMaxSteps-1 and is clamped; an operation that works on "the pattern" works on the steps below
// `length` (steps above it are hidden data, kept, and come back when the length grows). Motion moves with its steps (four points a
// step). "Playing step" = a step that is neither rest nor tie.

#include <o3sdk/core/param.h>
#include <o3_303/pattern.h>

#include <cstdint>

namespace o3_303::seq {

inline constexpr std::uint8_t kMaxNote = 127;
inline constexpr std::uint8_t kMaxPoint = 254;  // a motion point is 0..kMaxPoint; kNoLock (255) means "the knob's value"

// ---- One step ---------------------------------------------------------------------------------------------------------------

// The note, clamped to 0..127. The flags are untouched (a rest stays a rest).
void set_note(Pattern& p, unsigned step, unsigned note) noexcept;
// One flag bit on or off. REST and TIE exclude each other: turning one on turns the other off.
void set_flag(Pattern& p, unsigned step, std::uint8_t flag, bool on) noexcept;
void toggle_flag(Pattern& p, unsigned step, std::uint8_t flag) noexcept;
bool has_flag(const Pattern& p, unsigned step, std::uint8_t flag) noexcept;
// REC entry: the step takes `note` and exactly `flags` (kStepAccent | kStepSlide, or kStepRest / kStepTie for those entries; for
// those two the note is kept), with no ratchet and no condition. Returns the next cursor: step + 1, or 0 at the end of the length.
unsigned write_advance(Pattern& p, unsigned step, unsigned note, std::uint8_t flags) noexcept;

// Insert: the steps from `at` to the end of the length move one later (the last falls off) and `at` becomes a rest.
// Delete: the steps after `at` move one earlier and the last step of the length becomes a rest. Motion moves with them.
void insert_step(Pattern& p, unsigned at) noexcept;
void delete_step(Pattern& p, unsigned at) noexcept;

// A step with its parameter locks (the step clipboard).
struct StepClip {
  Step step;
  std::uint8_t motion[kMotionLanes][kMotionPointsPerStep];
};
StepClip copy_step(const Pattern& p, unsigned step) noexcept;
void paste_step(Pattern& p, unsigned step, const StepClip& clip) noexcept;

// ---- The pattern ------------------------------------------------------------------------------------------------------------

// Rotates the steps (and their motion) inside the length by `steps`: positive moves them later (to the right), the end wraps to the
// start.
void rotate(Pattern& p, int steps) noexcept;
// Moves the note of every step of the length by `semitones`, rests and ties included (a rest keeps the pitch it will have when it is
// written again). false (nothing changed) when any step of the length would leave 0..127, so the intervals survive. This changes the
// notes; the playback transpose of the pattern is set_transpose().
bool transpose_notes(Pattern& p, int semitones) noexcept;
// The steps (with their flags and motion points) in reverse order inside the length.
void reverse(Pattern& p) noexcept;
// Pitch mirror: every playing note becomes lowest + highest - note, so the pattern keeps its register.
void invert(Pattern& p) noexcept;
// Length x 2, the steps and motion copied into the new half. false (unchanged) at 64.
bool double_length(Pattern& p) noexcept;
// Every step a rest (no ratchet, no condition), no motion; length, swing, gate, transpose and the lanes stay. (o3_303::clear() is the
// fresh pattern instead.)
void clear_pattern(Pattern& p) noexcept;

// 8, 16, 32 or 64; false (unchanged) for anything else.
bool set_length(Pattern& p, unsigned length) noexcept;
void set_swing(Pattern& p, int percent) noexcept;      // 0..kMaxSwing
void set_gate(Pattern& p, int percent) noexcept;       // 5..100
void set_transpose(Pattern& p, int semitones) noexcept;  // -24..+24, applied at playback

// ---- Motion and parameter locks ---------------------------------------------------------------------------------------------

// A point (0..kMaxPoint, or kNoLock) of `lane` (0..7: the voice's ids 0..7) at `step`, quarter `point` (0..3).
void set_motion_point(Pattern& p, unsigned lane, unsigned step, unsigned point, std::uint8_t value) noexcept;
// A p-lock: all four points of the step.
void set_plock(Pattern& p, unsigned lane, unsigned step, std::uint8_t value) noexcept;
void clear_plock(Pattern& p, unsigned lane, unsigned step) noexcept;  // the step's four points of the lane
void clear_step_motion(Pattern& p, unsigned step) noexcept;           // every lane of the step
void clear_lane_motion(Pattern& p, unsigned lane) noexcept;           // the whole lane
void clear_motion(Pattern& p) noexcept;                               // all lanes
bool step_has_motion(const Pattern& p, unsigned step) noexcept;
bool lane_has_motion(const Pattern& p, unsigned lane) noexcept;

// A point and a parameter value, through the descriptor's curve (a knob position): 0 = min, kMaxPoint = max. Enum descriptors snap
// to their entries.
std::uint8_t value_to_point(const o3sdk::core::ParamDesc& d, float value) noexcept;
float point_to_value(const o3sdk::core::ParamDesc& d, std::uint8_t point) noexcept;

// ---- Generators -------------------------------------------------------------------------------------------------------------

struct GenParams {
  std::uint8_t scale = 18;        // index into o3sdk's core::kScaleDef (core/scales.h): 18 = Penta Min
  std::uint8_t root = 0;          // pitch class, 0 (C) .. 11
  std::uint8_t low = 24;          // note range of the result (C1 .. C3)
  std::uint8_t high = 48;
  std::uint8_t density = 75;      // percent of steps that sound
  std::uint8_t accent_pct = 25;   // percent of sounding steps with accent
  std::uint8_t slide_pct = 20;    // percent of sounding steps that slide
};

// A new line over the length: it replaces the flags and notes of those steps (ties, ratchets and conditions go; step 0 always
// sounds); motion, the lanes and the steps above the length are untouched. Deterministic from the seed.
void random_fill(Pattern& p, const GenParams& g, std::uint32_t seed) noexcept;
// Changes about `amount_pct` percent of the steps of the length: a new note from the scale, a rest toggled by the density, a
// re-rolled accent or slide.
void mutate(Pattern& p, const GenParams& g, std::uint32_t seed, unsigned amount_pct) noexcept;

}  // namespace o3_303::seq
