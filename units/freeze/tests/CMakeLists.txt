# SPDX-License-Identifier: GPL-3.0-or-later
# Host tests of the FREEZE unit (doctest, o3sdk_test_suite): one ctest per *_test.cpp.
file(GLOB test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
o3sdk_test_suite(freeze_tests SOURCES ${test_sources} DEPENDS unit_freeze)
