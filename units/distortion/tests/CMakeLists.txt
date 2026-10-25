# SPDX-License-Identifier: GPL-3.0-or-later
# Host tests of the DISTORTION unit (doctest, o3sdk_test_suite): one ctest per *_test.cpp. The oracle test builds the pinned Replicant 303
# DistortionUnit (third_party/replicant303/DSP) on the host and runs the same signals through the original and the port.
file(GLOB test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)

# The original, as it is (it is not ours: no strict warnings, doubles allowed). Host only.
set(REPLICANT_DSP "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/replicant303/DSP")
add_library(distortion_replicant_ref STATIC ${REPLICANT_DSP}/DistortionUnit.cpp)
target_include_directories(distortion_replicant_ref PUBLIC ${REPLICANT_DSP})

o3sdk_test_suite(distortion_tests SOURCES ${test_sources} DEPENDS unit_distortion distortion_replicant_ref)
