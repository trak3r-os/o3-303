# SPDX-License-Identifier: GPL-3.0-or-later
# o3-303's host tests (doctest): every *_test.cpp in this directory, one executable, one ctest per source file
# (o3sdk_test_suite(), o3-sdk/cmake/O3sdkTargets.cmake). The units' tests (tb303 and the rest) go here too, against the engine.
file(GLOB test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
# The app end to end (o3_303_app_test.cpp): its library defines o3sdk_app(), which only the test names.
o3sdk_test_suite(o3_303_tests
  SOURCES ${test_sources}
  DEPENDS app sim platform_host "$<LINK_LIBRARY:WHOLE_ARCHIVE,app_o3_303>")
# The scenarios of app/scenarios are run as they are.
target_compile_definitions(o3_303_tests PRIVATE O3_303_SCENARIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../app/scenarios")
