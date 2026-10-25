# SPDX-License-Identifier: GPL-3.0-or-later
#
# o3-303: an improved TB-303 as an app on the o3sdk framework (docs/DESIGN-303.md). The framework is the sibling repository
# ../o3-sdk, used as it is (add_subdirectory); it is never edited from here (CLAUDE.md).
#
#   host:  cmake -S . -B build-host && cmake --build build-host -j4 && ctest --test-dir build-host --output-on-failure
#          build-host/app/o3_303_sim --scenario app/scenarios/boot.txt --out /tmp/o3_303
#   mk66:  cmake -S . -B build-mk66 -DO3SDK_PLATFORM=mk66 -DARM_GNU_ROOT=<arm-gnu-toolchain dir>
#          cmake --build build-mk66 -j4       (build-mk66/app/o3_303.elf, .hex, .ptf)
#
# Another place for the framework: -DO3SDK_DIR=<path to the o3sdk checkout>.
cmake_minimum_required(VERSION 3.24)

set(O3SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../o3-sdk" CACHE PATH "The o3sdk framework (a checkout of the o3sdk repository)")
if(NOT EXISTS "${O3SDK_DIR}/cmake/O3sdkPlatform.cmake")
  message(FATAL_ERROR "O3SDK_DIR='${O3SDK_DIR}' is not an o3sdk checkout (cmake/O3sdkPlatform.cmake not found): clone o3sdk next to "
                      "this directory or pass -DO3SDK_DIR=<path>")
endif()

# The platform (host or mk66), the toolchain and the compile settings every o3sdk target shares: before project().
include("${O3SDK_DIR}/cmake/O3sdkPlatform.cmake")

project(o3_303 VERSION 0.0.1 LANGUAGES C CXX)

enable_testing()

# The kit, its units (synth, delay), the platform and, on the host, the simulator. o3sdk's own apps and tests are not built here
# (O3SDK_BUILD_APPS and O3SDK_BUILD_TESTS default to OFF when o3sdk is not the top-level project).
add_subdirectory(${O3SDK_DIR} o3sdk)

add_subdirectory(units)
add_subdirectory(app)
if(NOT O3SDK_CROSS)
  add_subdirectory(tests)
endif()
