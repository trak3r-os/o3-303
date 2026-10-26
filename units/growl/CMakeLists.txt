# SPDX-License-Identifier: GPL-3.0-or-later
# GROWL: the vowel formant filter and tuned comb for slot 0's insert 1 (docs/M3C-PLAN.md, G). The library is `unit_growl`; its header is
# <o3_303/units/growl/growl.h>.
o3sdk_unit(growl SOURCES src/growl.cpp)
# Built for speed like the other sample loops of the render (the image is MinSizeRel). No fused multiply-add: on the Cortex-M4F a VFMA/VMLA
# takes 3 cycles and a VMUL + VADD 2, and the loop is a chain of such pairs.
target_compile_options(unit_growl PRIVATE -O2 -ffp-contract=off)

if(NOT O3SDK_CROSS)
  add_subdirectory(tests)
endif()
