# SPDX-License-Identifier: GPL-3.0-or-later
# Host tests of the ACID unit (doctest, o3sdk_test_suite): one ctest per *_test.cpp. The oracle test builds the pinned Replicant 303
# files (third_party/replicant303/DSP, JUCE-free ones) on the host and renders the same note sequences through the original and the port.
file(GLOB test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)

# The original, as it is (it is not ours: no strict warnings, doubles allowed). Host only.
set(REPLICANT_DSP "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/replicant303/DSP")
add_library(acid_voice_replicant_ref STATIC ${REPLICANT_DSP}/Oscillator303.cpp ${REPLICANT_DSP}/DiodeLadderFilter.cpp)
target_include_directories(acid_voice_replicant_ref PUBLIC ${REPLICANT_DSP})

o3sdk_test_suite(acid_voice_tests SOURCES ${test_sources} DEPENDS unit_acid_voice unit_distortion acid_voice_replicant_ref)
# acid_voice_chain_test.cpp writes a WAV of a short 303 line to $O3_303_WAV_DIR when that variable is set (and links unit_distortion for it).
