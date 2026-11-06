# SPDX-License-Identifier: GPL-3.0-or-later
# Host tests of the ACID unit (doctest, o3sdk_test_suite): one ctest per *_test.cpp. The oracle test builds the pinned Open303
# files (third_party/open303/DSPCode, MIT) on the host and measures the voice's ladder, cutoff law, amplifier and output chain against them.
file(GLOB test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)

# The original, as it is (it is not ours: no strict warnings, doubles allowed). Host only. Upstream needs <climits> and <cstring> that its
# headers do not include: they are forced in rather than editing the vendored files.
set(OPEN303_DSP "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/open303/DSPCode")
file(GLOB OPEN303_SOURCES CONFIGURE_DEPENDS ${OPEN303_DSP}/rosic_*.cpp ${OPEN303_DSP}/GlobalFunctions.cpp)
add_library(acid_voice_open303_ref STATIC ${OPEN303_SOURCES})
target_include_directories(acid_voice_open303_ref SYSTEM PUBLIC ${OPEN303_DSP})
target_compile_options(acid_voice_open303_ref PRIVATE -w "SHELL:-include climits" "SHELL:-include cstring")

o3sdk_test_suite(acid_voice_tests SOURCES ${test_sources} DEPENDS unit_acid_voice unit_distortion acid_voice_open303_ref)
# acid_voice_chain_test.cpp writes a WAV of a short 303 line to $O3_303_WAV_DIR when that variable is set (and links unit_distortion for it).
