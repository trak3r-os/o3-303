# SPDX-License-Identifier: GPL-3.0-or-later
# ACID: the acid voice (docs/M3-PLAN.md), a port of Replicant 303's DSP (third_party/replicant303/, MIT). The library is `unit_acid_voice`;
# its header is <o3_303/units/acid_voice/acid_voice.h>.
o3sdk_unit(acid_voice SOURCES src/acid_voice.cpp)
# The image is built for size (MinSizeRel); the sample loops of this unit are the hot code of the render (docs/M3-PLAN.md "Budgets"), so it is
# built for speed: at -Os the tanh and the oscillator are calls inside the loop.
target_compile_options(unit_acid_voice PRIVATE -O2)

if(NOT O3SDK_CROSS)
  add_subdirectory(tests)
endif()
