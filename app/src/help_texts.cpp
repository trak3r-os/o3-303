// SPDX-License-Identifier: GPL-3.0-or-later
#include "help_texts.h"

#include <o3_303/pad_map.h>
#include <o3sdk/app/text_entry.h>

namespace o3_303::help_texts {

namespace {

using namespace o3sdk;
using app::Button;
using app::Help;
using app::HelpButton;
using app::HelpPadGroup;
using ui::Color;

// ---- The buttons --------------------------------------------------------------------------------------------------------------------------
//
// One table a screen, built at compile time: the keys the app takes on every screen (o3_303_app.cpp: Play, the screen buttons, Master, File, Shift + Rec), worded for the screen they are
// listed on, then what the screen itself does with the others. A button the screen does nothing with is not listed: the kit draws it dimmed, and that is true. Config is the kit's, Shift has the
// kit's text, and Song does nothing until SG2.

constexpr unsigned kMaxButtons = 25;  // the 25 buttons that are not soft keys

struct Buttons {
  HelpButton row[kMaxButtons] = {};
  std::uint8_t n = 0;
  constexpr void add(Button b, const char* text, const char* shift = nullptr) noexcept {
    if (text != nullptr || shift != nullptr) {
      row[n] = HelpButton{b, text, shift};
      ++n;
    }
  }
};

enum class Where : std::uint8_t { Home, Setup, Perform, PerformSetup, Instrument, Master, File, Naming, Library, Record, Hooks };

constexpr void add_globals(Buttons& b, Where w) noexcept {
  const bool perform = w == Where::Perform || w == Where::PerformSetup;
  const bool home_map = w == Where::Home || w == Where::Setup || w == Where::Instrument || w == Where::Master || w == Where::Record;
  b.add(Button::Play, "PLAY / STOP", "RESTART FROM STEP 1");
  // Rec: the home map's step record (a press turns write / record mode on and off); PERFORM's Rec is the same switch (it records the ladder and the knob while the pattern plays);
  // elsewhere it does nothing alone. Shift + Rec is the audio take, from every screen.
  b.add(Button::Rec, home_map ? "WRITE / RECORD MODE ON / OFF" : (perform ? "RECORD KNOB + LADDER MOVES" : nullptr), "ARM / STOP AN AUDIO TAKE");
  b.add(Button::Pattern, w == Where::Home ? "TAP: SETUP. HOLD: PATTERNS" : (w == Where::Setup ? "TAP: HOME. HOLD: PATTERNS" : "BACK TO THE STEPS"));
  b.add(Button::Params, w == Where::Instrument ? "NEXT PAGE: VOICE, FX, MODS" : "OPEN INSTRUMENT");
  b.add(Button::Performance, w == Where::Perform ? "PERFORM SETUP" : (w == Where::PerformSetup ? "BACK TO PERFORM" : "OPEN PERFORM"));
  b.add(Button::Master, w == Where::Master ? "TEMPO. HOLD + TURN: NUDGE" : "MASTER. HOLD + TURN: NUDGE", "TAP TEMPO");
  b.add(Button::File, w == Where::File ? "FILE LIBRARY" : (w == Where::Library ? "FILE PROJECTS" : (w == Where::Naming ? "DROP THE NAME, THEN LIBRARY" : "OPEN FILE PROJECTS")), "SAVE THE PROJECT");
  b.add(Button::SampleRec, w == Where::Record ? nullptr : "OPEN RECORD");
  const char* hooks = w == Where::Hooks ? nullptr : "OPEN HOOKS";
  b.add(Button::SampleLoad, hooks);
  b.add(Button::SamplePlay, hooks);
  b.add(Button::SampleEdit, hooks);
}

// The home map's edit buttons (home_map.h): the cursor, the note of the cursor step (or of the held steps), Copy / Insert / Delete.
constexpr void add_edit(Buttons& b) noexcept {
  b.add(Button::Copy, "COPY THE CURSOR STEP", "PASTE ON THE CURSOR STEP");
  b.add(Button::Insert, "INSERT A REST AT THE CURSOR", "CURSOR TO STEP 1");
  b.add(Button::Delete, "CLEAR THE STEP", "REMOVE STEP, PULL LEFT");
  b.add(Button::Left, "CURSOR LEFT", "ROTATE THE LINE LEFT");
  b.add(Button::Right, "CURSOR RIGHT", "ROTATE THE LINE RIGHT");
  b.add(Button::Up, "NOTE UP A SEMITONE", "NOTE UP AN OCTAVE");
  b.add(Button::Down, "NOTE DOWN A SEMITONE", "NOTE DOWN AN OCTAVE");
}

// Note, Instr, Vol and Fx on the screens with the drawer: each gives the encoder one row of it.
constexpr void add_rows(Buttons& b) noexcept {
  b.add(Button::Note, "ENCODER ON THE NOTE ROW");
  b.add(Button::Instr, "ENCODER ON THE GATE ROW");
  b.add(Button::Vol, "ENCODER ON THE PLAYS ROW");
  b.add(Button::Fx, "ENCODER ON THE LOCK ROW");
}

// PERFORM: the four colour buttons are live actions, the ones PERFORM SETUP assigned.
constexpr void add_live(Buttons& b) noexcept {
  b.add(Button::Note, "HOLD: NOTE ACTION (SETUP)", "LATCH IT");
  b.add(Button::Instr, "HOLD: INSTR ACTION (SETUP)", "LATCH IT");
  b.add(Button::Vol, "HOLD: VOL ACTION (SETUP)", "LATCH IT");
  b.add(Button::Fx, "HOLD: FX ACTION (SETUP)", "LATCH IT");
}

constexpr Buttons make_home(Where w) noexcept {
  Buttons b;
  add_globals(b, w);
  add_edit(b);
  add_rows(b);
  return b;
}
constexpr Buttons make_home_map_only(Where w) noexcept {  // MASTER and RECORD: the colour buttons do nothing
  Buttons b;
  add_globals(b, w);
  add_edit(b);
  return b;
}
constexpr Buttons make_perform(Where w) noexcept {
  Buttons b;
  add_globals(b, w);
  add_live(b);
  return b;
}
constexpr Buttons make_file() noexcept {
  Buttons b;
  add_globals(b, Where::File);
  b.add(Button::Up, "LIST CURSOR UP");
  b.add(Button::Down, "LIST CURSOR DOWN");
  return b;
}
constexpr Buttons make_naming() noexcept {
  Buttons b;
  add_globals(b, Where::Naming);
  b.add(Button::Delete, "BACKSPACE");
  b.add(Button::Left, "FOCUS LEFT");
  b.add(Button::Right, "FOCUS RIGHT");
  b.add(Button::Up, "FOCUS UP");
  b.add(Button::Down, "FOCUS DOWN");
  return b;
}
constexpr Buttons make_library() noexcept {
  Buttons b;
  add_globals(b, Where::Library);
  b.add(Button::Insert, "LOAD THE SLOT SHOWN");
  b.add(Button::Left, "SLOT - 1");
  b.add(Button::Right, "SLOT + 1");
  b.add(Button::Up, "SLOT - 8");
  b.add(Button::Down, "SLOT + 8");
  return b;
}
constexpr Buttons make_hooks() noexcept {
  Buttons b;
  add_globals(b, Where::Hooks);
  return b;
}

constexpr Buttons kHomeButtons = make_home(Where::Home);
constexpr Buttons kSetupButtons = make_home(Where::Setup);
constexpr Buttons kPerformButtons = make_perform(Where::Perform);
constexpr Buttons kPerformSetupButtons = make_perform(Where::PerformSetup);
constexpr Buttons kInstrumentButtons = make_home(Where::Instrument);
constexpr Buttons kMasterButtons = make_home_map_only(Where::Master);
constexpr Buttons kFileButtons = make_file();
constexpr Buttons kNamingButtons = make_naming();
constexpr Buttons kLibraryButtons = make_library();
constexpr Buttons kRecordButtons = make_home_map_only(Where::Record);
constexpr Buttons kHooksButtons = make_hooks();

// ---- The pad groups -----------------------------------------------------------------------------------------------------------------------

// The home map. FILT on swaps the keys for the two ladders (a fourth table would say the same with other words: only the third group differs).
constexpr HelpPadGroup kHomeGroups[] = {
    {"STEPS", "TAP: SELECT. HOLD + KEY: ITS NOTE", Color::Sel},
    {"MODIFIERS", "ACC SLIDE TIE REST, OCT-/+, TRNSP, FILT", Color::Accent},
    {"KEYS", "PLAY THE VOICE. TRNSP: TRANSPOSE", Color::Instr},
};
constexpr HelpPadGroup kHomeFiltGroups[] = {
    kHomeGroups[0],
    kHomeGroups[1],
    {"LADDERS", "ROW 3 CUTOFF, ROW 4 RESO: 12 PLACES", Color::Instr},
};

constexpr HelpPadGroup kPerformGroups[] = {
    {"ACTIONS", "HOLD A PAD FOR ITS ACTION. SHIFT: LATCH", Color::Accent},
    {"ALL OFF", "LETS EVERY LATCHED ACTION GO", Color::Rec},
    {"SNAPSHOTS", "TAP: RECALL, THEN MORPH. SHIFT: STORE", Color::Sel},
    {"VARIATIONS", "NEW LINE, VARY, UNDO, REDO", Color::Vol},
    {"LADDER", "12 PLACES OF THE LATCHED KNOB", Color::Fx},
    {"KEYS", "PLAY THE VOICE; TRANSPOSE IF TRNSP IS ON (HOME)", Color::Instr},
};
constexpr HelpPadGroup kLibraryGroups[] = {
    {"SLOTS", "TAP: LOAD THAT SLOT OF THE BANK", Color::Sel},
};
constexpr HelpPadGroup kListGroups[] = {
    {"PROJECTS", "TAP: SELECT THE PROJECT OF THAT ROW", Color::Sel},
};
constexpr HelpPadGroup kKeyboardGroups[] = {
    {"KEYBOARD", "A PAD TYPES THE KEY UNDER IT", Color::Sel},
};

constexpr std::uint8_t count_of(std::size_t n) noexcept { return static_cast<std::uint8_t>(n); }

// ---- The soft keys' texts -----------------------------------------------------------------------------------------------------------------

#define NONE nullptr

// The sound knobs: HOME, PERFORM and INSTRUMENT VOICE.
#define SOUND_KEYS                                                                                                                                    \
  {"FILTER CUTOFF", "FILTER RESONANCE", "ENVELOPE DEPTH", "ENVELOPE DECAY", "ACCENT AMOUNT", "WAVE: SAW / SQR", "TUNING IN SEMITONES", "VOICE LEVEL"}

// ---- The screens --------------------------------------------------------------------------------------------------------------------------

constexpr Text kTexts[kIdCount] = {
    // Home
    {"HOME",
     "WRITE AND PLAY THE LINE WHILE IT RUNS. TAP A STEP PAD TO SELECT IT, OR HOLD IT AND PRESS A KEY FOR ITS NOTE. F1-F8 ARE THE SOUND KNOBS: TAP ONE, THEN TURN THE ENCODER.",
     SOUND_KEYS,
     {},
     "KNOB OR ROW; SHIFT = COARSE",
     kHomeButtons.row,
     kHomeButtons.n,
     kHomeGroups,
     count_of(sizeof kHomeGroups / sizeof kHomeGroups[0]),
     "HOLD PATTERN: THE 48 PADS ARE THE PATTERNS. REC: KEYS WRITE. FILT: ROWS 3-4 ARE THE LADDERS."},
    // Setup
    {"SETUP",
     "THIS PATTERN'S SETTINGS (F1-F4) AND THE GENERATOR'S (F5-F8, FOR THE WHOLE PROJECT). THE STEPS AND THE PADS WORK AS ON HOME. TAP PATTERN TO GO BACK.",
     {"PATTERN LENGTH", "PATTERN SWING", "GATE: NOTE LENGTH", "PATTERN TRANSPOSE", "SCALE: LOCKS THE KEYS", "GENERATOR DENSITY", "GENERATOR ACCENT CHANCE", "GENERATOR SLIDE CHANCE"},
     {},
     "KNOB OR ROW; SHIFT = COARSE",
     kSetupButtons.row,
     kSetupButtons.n,
     kHomeGroups,
     count_of(sizeof kHomeGroups / sizeof kHomeGroups[0]),
     "HOLD PATTERN: THE 48 PADS ARE THE PATTERNS. REC: KEYS WRITE. FILT: ROWS 3-4 ARE THE LADDERS."},
    // Perform
    {"PERFORM",
     "PLAY WITHOUT A MENU: THE 48 PADS ARE LIVE. ROW 1 ACTIONS, ROW 2 SNAPSHOTS AND VARIATIONS, ROW 3 A LADDER OF THE LATCHED KNOB, ROW 4 KEYS. THE PATTERN KEEPS PLAYING.",
     SOUND_KEYS,
     {},
     "KNOB; MORPH AFTER A RECALL",
     kPerformButtons.row,
     kPerformButtons.n,
     kPerformGroups,
     count_of(sizeof kPerformGroups / sizeof kPerformGroups[0]),
     "WOBBLE ON: ROW 3 PICKS ITS RATE. A LATCHED ACTION STAYS ON WHEN YOU LEAVE THE SCREEN."},
    // PerformSetup
    {"PERFORM SETUP",
     "WHAT THE FOUR COLOUR BUTTONS DO (F1-F4), WHAT FILL PLAYS (F5), THE FREEZE LENGTH (F6), THE ROLL MODE (F7) AND THE STOP TIME (F8). THE PADS STAY PERFORM'S.",
     {"NOTE BUTTON'S ACTION", "INSTR BUTTON'S ACTION", "VOL BUTTON'S ACTION", "FX BUTTON'S ACTION", "WHAT FILL PLAYS", "FREEZE LENGTH", "ROLL RATE OR BUILD", "STOP TIME"},
     {},
     "VALUE; SHIFT = COARSE",
     kPerformSetupButtons.row,
     kPerformSetupButtons.n,
     kPerformGroups,
     count_of(sizeof kPerformGroups / sizeof kPerformGroups[0]),
     "WOBBLE ON: ROW 3 PICKS ITS RATE. A LATCHED ACTION STAYS ON WHEN YOU LEAVE THE SCREEN."},
    // Voice
    {"VOICE",
     "THE WHOLE SOUND IN VIEW. THE EIGHT KNOBS ARE THE VOICE: TAP AN F KEY TO LATCH IT, THEN TURN. PARAMS GOES TO THE NEXT PAGE. SHIFT + F KEY: MIDI LEARN.",
     SOUND_KEYS,
     {},
     "KNOB OR ROW; SHIFT = COARSE",
     kInstrumentButtons.row,
     kInstrumentButtons.n,
     kHomeGroups,
     count_of(sizeof kHomeGroups / sizeof kHomeGroups[0]),
     "REC: KEYS WRITE. FILT: ROWS 3-4 ARE THE LADDERS."},
    // Fx
    {"FX",
     "THE DISTORTION (F1-F4) AND THE DELAY (F5-F8). TAP AN F KEY TO LATCH IT, THEN TURN. PARAMS GOES TO THE NEXT PAGE. SHIFT + F KEY: MIDI LEARN WHERE IT CAN.",
     {"DISTORTION DRIVE", "DISTORTION TONE", "DISTORTION MIX", "DISTORTION MODEL", "DELAY TIME", "DELAY FEEDBACK", "DELAY SEND LEVEL", "DELAY TONE"},
     {},
     "KNOB OR ROW; SHIFT = COARSE",
     kInstrumentButtons.row,
     kInstrumentButtons.n,
     kHomeGroups,
     count_of(sizeof kHomeGroups / sizeof kHomeGroups[0]),
     "REC: KEYS WRITE. FILT: ROWS 3-4 ARE THE LADDERS."},
    // Mods
    {"MODS",
     "THE GROWL (F1-F4) AND THE WOBBLE (F5-F8). TAP AN F KEY TO LATCH IT, THEN TURN. PARAMS GOES TO THE NEXT PAGE. SHIFT + F KEY: MIDI LEARN WHERE IT CAN.",
     {"GROWL VOWEL", "GROWL FORMANT Q", "GROWL MIX", "GROWL COMB MIX", "WOBBLE RATE", "WOBBLE SHAPE", "WOBBLE DEPTH", "WOBBLE TARGET"},
     {},
     "KNOB OR ROW; SHIFT = COARSE",
     kInstrumentButtons.row,
     kInstrumentButtons.n,
     kHomeGroups,
     count_of(sizeof kHomeGroups / sizeof kHomeGroups[0]),
     "REC: KEYS WRITE. FILT: ROWS 3-4 ARE THE LADDERS."},
    // Master
    {"MASTER",
     "TEMPO AND OUTPUT. THE ENCODER STARTS ON F1 TEMPO. F2 TAPS THE TEMPO, F3 NUDGES IT WHILE HELD, F4 IS THE MASTER LEVEL. THE STEPS AND PADS WORK AS ON HOME.",
     {"TEMPO IN BPM", "TAP 2 TO 4 TIMES", "HOLD + TURN: NUDGE 3 %", "MASTER LEVEL", NONE, NONE, NONE, NONE},
     {},
     "VALUE; SHIFT = COARSE",
     kMasterButtons.row,
     kMasterButtons.n,
     kHomeGroups,
     count_of(sizeof kHomeGroups / sizeof kHomeGroups[0]),
     "REC: KEYS WRITE. FILT: ROWS 3-4 ARE THE LADDERS."},
    // File
    {"FILE",
     "THE PROJECTS ON THE CARD. PICK ONE WITH THE ENCODER, UP / DOWN OR A PAD, THEN LOAD IT. REVERT AND DELETE WANT A SECOND PRESS WITHIN 5 SECONDS.",
     {"THE SELECTED PROJECT", "SAME AS SHIFT + FILE", "SAVE UNDER A NEW NAME", "NEW EMPTY PROJECT", "BACK TO THE LAST SAVE", "NEVER THE CURRENT ONE", NONE, "THE GENERATED LINES"},
     {},
     "MOVE THE LIST CURSOR",
     kFileButtons.row,
     kFileButtons.n,
     kListGroups,
     count_of(sizeof kListGroups / sizeof kListGroups[0]),
     "THE FIRST PADS ARE THE LIST'S VISIBLE ROWS, TOP TO BOTTOM."},
    // FileNaming
    {"FILE NAME",
     "TYPE A NAME. THE ENCODER OR THE ARROWS MOVE THE FOCUS, F1 OR A PAD TYPES THE KEY, DELETE ERASES, F7 ACCEPTS AND F8 CANCELS.",
     {"THE KEY IN FOCUS", NONE, NONE, NONE, NONE, NONE, "ACCEPT THE NAME", "DROP THE NAME"},
     {},
     "MOVE THE KEY FOCUS",
     kNamingButtons.row,
     kNamingButtons.n,
     kKeyboardGroups,
     count_of(sizeof kKeyboardGroups / sizeof kKeyboardGroups[0]),
     "THE PADS ARE THE ON-SCREEN KEYBOARD, ROW FOR ROW."},
    // Library
    {"LIBRARY",
     "512 GENERATED LINES IN 8 BANKS OF 64. PICK A BANK (F1-F8) AND A SLOT; A TAP LOADS IT INTO THE CURRENT PATTERN AND SOUND, ONE UNDO. IT NEVER LEAVES THIS SCREEN.",
     {"BANK A", "BANK B", "BANK C", "BANK D", "BANK E", "BANK F", "BANK G: OFF WORLD", "BANK H"},
     {"NEW SEED", NONE, NONE, NONE, NONE, NONE, NONE, NONE},
     "SLOT (SHIFT: SEED)",
     kLibraryButtons.row,
     kLibraryButtons.n,
     kLibraryGroups,
     count_of(sizeof kLibraryGroups / sizeof kLibraryGroups[0]),
     "SLOTS 48-63 HAVE NO PAD: PICK THEM WITH THE ENCODER OR ARROWS, THEN INSERT LOADS THE SLOT SHOWN."},
    // Record
    {"RECORD",
     "RECORD THE JAM. A TAKE OF THE MASTER OR THE INPUT STARTS AT THE NEXT BAR AND ENDS AFTER ITS BARS (FREE: AT F1). SAVE WRITES A WAV TO THE CARD.",
     {"STARTS AT THE NEXT BAR", "TAP: NEXT SOURCE", "TAP: 1 2 4 8 FREE", "INPUT GAIN", "INPUT MONITOR", "WAV TO THE CARD", "PRESS TWICE", NONE},
     {},
     "VALUE; SHIFT = COARSE",
     kRecordButtons.row,
     kRecordButtons.n,
     kHomeGroups,
     count_of(sizeof kHomeGroups / sizeof kHomeGroups[0]),
     "REC: KEYS WRITE. FILT: ROWS 3-4 ARE THE LADDERS."},
    // Hooks
    {"HOOKS",
     "HOOKS ARRIVES WITH M4. UNTIL THEN THIS SCREEN IS A PLACEHOLDER; THE SAMPLE RECORDER (SAMPLE REC) WORKS NOW.",
     {},
     {},
     nullptr,
     kHooksButtons.row,
     kHooksButtons.n,
     nullptr,
     0,
     nullptr},
};

#undef SOUND_KEYS
#undef NONE

// ---- Pad labels -----------------------------------------------------------------------------------------------------------------------------

constexpr const char* kStepNumbers[64] = {"1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  "10", "11", "12", "13", "14", "15", "16",
                                          "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32",
                                          "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43", "44", "45", "46", "47", "48",
                                          "49", "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "60", "61", "62", "63", "64"};
constexpr const char* kSlotNumbers[48] = {"00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12", "13", "14", "15",
                                          "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31",
                                          "32", "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43", "44", "45", "46", "47"};

// Row 1 of PERFORM, columns 1..11: the order of the pads (perform.h kPerformActionOrder), the short names the screen's lamps carry; column 12 is ALL OFF.
constexpr const char* kActionPads[11] = {"MUTE", "ACC", "SLD", "OCT+", "FILL", "THRW", "FRZ", "ROLL", "STOP", "REV", "WOB"};
constexpr const char* kVariationPads[4] = {"NEW", "VARY", "UNDO", "REDO"};
constexpr const char* kLadderPads[app::kPadCols] = {"MIN", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "MAX"};
constexpr const char* kPerformKeyPads[app::kPadCols] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr const char* kModifierPads[2][4] = {{"ACC", "SLIDE", "TIE", "REST"}, {"OCT-", "OCT+", "TRNSP", "FILT"}};

constexpr unsigned kRows = app::kPadRows;
constexpr unsigned kCols = app::kPadCols;

}  // namespace

const Text& text(Id id) noexcept {
  const unsigned i = static_cast<unsigned>(id);
  return kTexts[i < kIdCount ? i : 0u];
}

void fill(Help& h, const Text& t) noexcept {
  h.summary = t.summary;
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    h.fkey[k] = t.fkey[k];
    h.fkey_shift[k] = t.fkey_shift[k];
  }
  h.buttons = t.buttons;
  h.button_count = t.button_count;
  h.encoder = t.encoder;
  h.groups = t.groups;
  h.group_count = t.group_count;
  h.pads = t.pads;
}

void fill_home_pads(Help& h, const HomePads& s, KeyNames& names) noexcept {
  // The step pads: the 16 steps of the page shown, dark past the pattern's length.
  for (unsigned k = 0; k < kPageSteps; ++k) {
    const unsigned pad = step_pad(k);
    const unsigned step = s.view_page * 16u + k;
    h.pad_group[pad] = step < s.length ? 1 : 0;
    h.pad_label[pad] = step < 64u ? kStepNumbers[step] : nullptr;
  }
  // The modifier block: ACC SLIDE TIE REST over OCT- OCT+ TRNSP FILT.
  app::help_pads(h, 0, 8, 1, 11, 2, &kModifierPads[0][0]);
  // The keyboard (or, with FILT on, the two ladders).
  for (unsigned i = 0; i < kHomeKeys; ++i) {
    const unsigned pad = 24u + i;
    if (s.filt) {
      h.pad_group[pad] = 3;
      h.pad_label[pad] = app::pad_row(pad) == 2u ? "CUT" : "RES";
      continue;
    }
    // The name is the kit's ("C#-1", C2 is MIDI 36: the bottom left key at octave 0); outside 0..127 or outside the locked scale: dark, nothing happens.
    if (((s.out_of_scale >> i) & 1u) != 0u || !app::help_note_label(names[i], key_note(pad, s.octave))) {
      h.pad_group[pad] = 0;
      h.pad_label[pad] = nullptr;
      continue;
    }
    h.pad_group[pad] = 3;
    h.pad_label[pad] = names[i];
  }
  if (s.filt) {
    h.groups = kHomeFiltGroups;
    h.group_count = count_of(sizeof kHomeFiltGroups / sizeof kHomeFiltGroups[0]);
  }
}

void fill_perform_pads(Help& h) noexcept {
  app::help_pads(h, 0, 0, 0, 10, 1, kActionPads);
  app::help_pads(h, 0, 11, 0, 11, 2);
  h.pad_label[app::pad_at(0, 11)] = "OFF";
  app::help_pads(h, 1, 0, 1, 7, 3, kStepNumbers);  // the snapshots 1..8
  app::help_pads(h, 1, 8, 1, 11, 4, kVariationPads);
  app::help_pads(h, 2, 0, 2, 11, 5, kLadderPads);
  app::help_pads(h, 3, 0, 3, 11, 6, kPerformKeyPads);
}

void fill_library_pads(Help& h) noexcept { app::help_pads(h, 0, 0, kRows - 1, kCols - 1, 1, kSlotNumbers); }

void fill_list_pads(Help& h, unsigned rows) noexcept {
  for (unsigned pad = 0; pad < app::kPadCount && pad < rows; ++pad) {
    h.pad_group[pad] = 1;
  }
}

// The name keyboard's key labels, one per key of the kit's layout (text_entry.h kTextKeysBig, upper case like every label): a one-character string, DEL, CAPS or SPACE. The two keys a file
// name refuses ('@' and '+', FILE PROJECTS uses only Kind::FileName) have none and their pads stay dark.
struct KeyLabels {
  char text[app::kTextKeys][2];
  constexpr KeyLabels() : text{} {
    for (unsigned k = 0; k < app::kTextKeys; ++k) {
      text[k][0] = app::kTextKeysBig[k];
    }
  }
};
constexpr KeyLabels kKeyLabels{};

const char* key_label(unsigned key) noexcept {
  if (key == app::kTextKeyBackspace) {
    return "DEL";
  }
  if (key == app::kTextKeyCaps) {
    return "CAPS";
  }
  if (key == app::kTextKeySpace) {
    return "SPACE";
  }
  const char c = app::kTextKeysBig[key];
  return (c == '@' || c == '+') ? nullptr : kKeyLabels.text[key];
}

void fill_keyboard_pads(Help& h) noexcept {
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    const char* label = key_label(app::kTextPadKey[pad]);
    h.pad_label[pad] = label;
    h.pad_group[pad] = label != nullptr ? 1 : 0;
  }
}

}  // namespace o3_303::help_texts
