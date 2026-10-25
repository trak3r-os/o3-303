# SPDX-License-Identifier: GPL-3.0-or-later
# DISTORTION: the five-model insert distortion (docs/M3-PLAN.md), a port of Replicant 303's DistortionUnit (third_party/replicant303/,
# MIT). The library is `unit_distortion`; its header is <o3_303/units/distortion/distortion.h>.
o3sdk_unit(distortion SOURCES src/distortion.cpp)
# The image is built for size (MinSizeRel); the sample loops of this unit are the hot code of the render (docs/M3-PLAN.md "Budgets"), so it is
# built for speed: at -Os the tanh and the oscillator are calls inside the loop.
target_compile_options(unit_distortion PRIVATE -O2)

if(NOT O3SDK_CROSS)
  add_subdirectory(tests)
endif()
