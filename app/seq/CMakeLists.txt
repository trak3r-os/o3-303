# SPDX-License-Identifier: GPL-3.0-or-later
# The sequencer library `o3_303_seq` (docs/M3-PLAN.md, the seq slice): the bank of patterns, the editing operations with undo, the
# clocked hook that plays them on the voice, and GEN with the vault. Depends on o3sdk's dsp and core only (no ui). Headers are
# <o3_303/seq/*.h>; they include <o3_303/pattern.h>, which lives one directory up (app/include).
o3sdk_library(o3_303_seq
  SOURCES src/ops.cpp src/history.cpp src/bank.cpp src/sequencer.cpp src/gen.cpp
  DEPENDS dsp core)
target_include_directories(o3_303_seq PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
# Single precision everywhere (the cross build is checked for software double helpers); no errno from the maths functions.
target_compile_options(o3_303_seq PRIVATE -fsingle-precision-constant -fno-math-errno)
target_compile_options(o3_303_seq_hdrcheck PRIVATE -fsingle-precision-constant)
# gen.cpp: no fused multiply-add, so the generator rounds as the host oracle does (a pattern never depends on a contraction).
set_source_files_properties(src/gen.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)

if(NOT O3SDK_CROSS)
  add_subdirectory(tests)
endif()
