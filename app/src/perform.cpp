// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/perform.h>

namespace o3_303 {

namespace {

// `s` appended at `n` in `out` (cap bytes with the terminator); returns the new length.
unsigned put(char* out, unsigned cap, unsigned n, const char* s) noexcept {
  for (; *s != '\0' && n + 1u < cap; ++s) {
    out[n++] = *s;
  }
  return n;
}

unsigned put_uint(char* out, unsigned cap, unsigned n, unsigned v) noexcept {
  char digits[4];
  unsigned count = 0;
  do {
    digits[count++] = static_cast<char>('0' + v % 10u);
    v /= 10u;
  } while (v != 0u && count < sizeof digits);
  while (count != 0u && n + 1u < cap) {
    out[n++] = digits[--count];
  }
  return n;
}

}  // namespace

FreezeCmd FreezeOwner::step(std::uint16_t on, std::uint16_t changed) noexcept {
  FreezeCmd cmd;
  if (owner_ >= 0) {
    if ((on >> static_cast<unsigned>(owner_) & 1u) == 0u) {  // the owner let go: off, with the Mode as it is
      owner_ = -1;
      cmd.set_active = true;
      cmd.active = false;
    }
    return cmd;
  }
  // The more specific modes first; an action that went on in this call (a press, a latch, a reassigned button) takes the unit.
  static constexpr PerfAction kPriority[] = {PerfAction::Roll, PerfAction::Stop, PerfAction::Reverse, PerfAction::Freeze};
  for (const PerfAction a : kPriority) {
    if (((on & changed) & perf_bit(a)) != 0u) {
      owner_ = static_cast<int>(a);
      cmd.set_mode = true;
      cmd.mode = static_cast<unsigned>(perf_freeze_mode(a));
      cmd.set_active = true;
      cmd.active = true;
      break;
    }
  }
  return cmd;
}

unsigned cond_text(std::uint8_t cond, char* out, unsigned cap) noexcept {
  if (out == nullptr || cap == 0u) {
    return 0;
  }
  const Cond c = decode_cond(cond);
  unsigned n = 0;
  switch (c.kind) {
    case CondKind::Probability:
      n = put_uint(out, cap, n, c.a);
      n = put(out, cap, n, "%");
      break;
    case CondKind::Ratio:
      n = put_uint(out, cap, n, c.a);
      n = put(out, cap, n, ":");
      n = put_uint(out, cap, n, c.b);
      break;
    case CondKind::Fill:
      n = put(out, cap, n, "FILL");
      break;
    case CondKind::NotFill:
      n = put(out, cap, n, "!FILL");
      break;
    case CondKind::First:
      n = put(out, cap, n, "FIRST");
      break;
    case CondKind::NotFirst:
      n = put(out, cap, n, "!FIRST");
      break;
    case CondKind::Always:
      n = put(out, cap, n, "ALWAYS");
      break;
  }
  out[n] = '\0';
  return n;
}

}  // namespace o3_303
