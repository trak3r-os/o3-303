# SPDX-License-Identifier: GPL-3.0-or-later
# FREEZE: the beat freeze / stutter for master insert 0 (docs/M3B-PLAN.md, R6). The library is `unit_freeze`; its header is
# <o3_303/units/freeze/freeze.h>.
o3sdk_unit(freeze SOURCES src/freeze.cpp)
# Built for speed like the other sample loops of the render (the image is MinSizeRel).
target_compile_options(unit_freeze PRIVATE -O2)

if(NOT O3SDK_CROSS)
  add_subdirectory(tests)
endif()
