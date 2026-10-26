# SPDX-License-Identifier: GPL-3.0-or-later
# GROWL: the vowel formant filter and tuned comb for slot 0's insert 1 (docs/M3C-PLAN.md, G). The library is `unit_growl`; its header is
# <o3_303/units/growl/growl.h>.
o3sdk_unit(growl SOURCES src/growl.cpp)
# Built for speed like the other sample loops of the render (the image is MinSizeRel).
target_compile_options(unit_growl PRIVATE -O2)

if(NOT O3SDK_CROSS)
  add_subdirectory(tests)
endif()
