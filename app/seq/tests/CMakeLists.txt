# SPDX-License-Identifier: GPL-3.0-or-later
# The tests of o3_303_seq (doctest, host only): every *_test.cpp here, one executable, one ctest per source file. They link the two
# units (tb303, distortion) only to read their parameter descriptors: GEN's values are checked against the units' own ranges.
#
# The GEN oracle: Replicant 303's ORIGINAL Presets/PresetBank.cpp (third_party/replicant303, read-only, pinned d5369be) is compiled
# here, unchanged, against a minimal stand-in for the few JUCE names it uses (juce_shim/). It is third-party code: built apart,
# without the project's strict warnings, and linked into the tests only.
set(REPLICANT_DIR ${PROJECT_SOURCE_DIR}/third_party/replicant303)
add_library(o3_303_seq_oracle STATIC ${REPLICANT_DIR}/Presets/PresetBank.cpp)
target_include_directories(o3_303_seq_oracle SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/juce_shim ${REPLICANT_DIR})
target_compile_options(o3_303_seq_oracle PRIVATE -w)

file(GLOB test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
o3sdk_test_suite(o3_303_seq_tests SOURCES ${test_sources} DEPENDS o3_303_seq o3_303_seq_oracle unit_tb303 unit_distortion unit_growl)
