// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/snapshot.h>

namespace o3_303 {

void SnapBank::clear() noexcept {
  *this = SnapBank{};
}

void SnapBank::store(unsigned slot, const float* values) noexcept {
  if (slot >= kSnapSlots || values == nullptr) {
    return;
  }
  for (unsigned i = 0; i < kSnapValues; ++i) {
    slot_[slot][i] = values[i];
  }
  used_[slot] = true;
}

bool SnapBank::recall(unsigned slot, const float* live) noexcept {
  if (!used(slot)) {
    return false;
  }
  if (last_ < 0) {  // the first recall: the morph runs back to the sound as it was
    for (unsigned i = 0; i < kSnapValues; ++i) {
      from_[i] = live != nullptr ? live[i] : slot_[slot][i];
    }
    previous_ = -1;
  } else if (static_cast<unsigned>(last_) != slot) {
    for (unsigned i = 0; i < kSnapValues; ++i) {
      from_[i] = to_[i];  // the end of the morph so far was the snapshot as it was when recalled
    }
    previous_ = last_;
  }
  for (unsigned i = 0; i < kSnapValues; ++i) {
    to_[i] = slot_[slot][i];
  }
  last_ = static_cast<int>(slot);
  pos_ = kMorphSteps;
  return true;
}

unsigned SnapBank::turn(int detents, bool coarse) noexcept {
  const int step = detents * (coarse ? static_cast<int>(kMorphCoarse) : 1);
  int p = static_cast<int>(pos_) + step;
  p = p < 0 ? 0 : (p > static_cast<int>(kMorphSteps) ? static_cast<int>(kMorphSteps) : p);
  pos_ = static_cast<unsigned>(p);
  return pos_;
}

float SnapBank::value(unsigned target, bool discrete) const noexcept {
  if (target >= kSnapValues) {
    return 0.0f;
  }
  if (discrete) {
    return pos_ * 2u < kMorphSteps ? from_[target] : to_[target];
  }
  const float t = static_cast<float>(pos_) / static_cast<float>(kMorphSteps);
  return from_[target] + (to_[target] - from_[target]) * t;
}

}  // namespace o3_303
