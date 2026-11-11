# SPDX-License-Identifier: GPL-3.0-or-later
# TRACT: the vocal-tract effect for slot 0's insert 2 after the growl (docs/TRACT-KNOBS-PLAN.md, section 3.2): a wrapper of the shared DSP in
# o3-sdk/extras/tract (unit_tract_dsp, added by units/CMakeLists.txt). The library is `unit_tract`; its header is <o3_303/units/tract/tract.h>.
# Its tests are tests/tract_unit_test.cpp (the app's test suite).
o3sdk_unit(tract SOURCES src/tract.cpp DEPENDS unit_tract_dsp)
# Built for speed like the growl (the image is MinSizeRel): the wrapper's loops are the mix and the mono sum. No fused multiply-add (see the growl).
target_compile_options(unit_tract PRIVATE -O2 -ffp-contract=off)
