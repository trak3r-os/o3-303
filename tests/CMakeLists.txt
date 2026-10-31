# SPDX-License-Identifier: GPL-3.0-or-later
# o3-303's host tests (doctest): every *_test.cpp in this directory, one executable, one ctest per source file
# (o3sdk_test_suite(), o3-sdk/cmake/O3sdkTargets.cmake). The units' tests (acid_voice and the rest) go here too, against the engine.
file(GLOB test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
# The app end to end (app_*_test.cpp, with app_hands.h): its library defines o3sdk_app(), which only the tests name. The app comes first on the link line: it calls
# hal::cycles() (the skin's draw time), which platform_host defines, and a static library is searched once, for what is undefined by then.
o3sdk_test_suite(o3_303_tests
  SOURCES ${test_sources}
  DEPENDS "$<LINK_LIBRARY:WHOLE_ARCHIVE,app_o3_303>" app sim platform_host)
# The scenarios of app/scenarios are run as they are.
target_compile_definitions(o3_303_tests PRIVATE O3_303_SCENARIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../app/scenarios")
# The obsidian skin's bitmaps (docs/SKIN-PLAN.md section 6): two separate runs of tools/gen_skin.py, the copy this build linked and the committed
# hash (tools/skin_assets.sha256) must all agree: the generator is deterministic and the hash is current.
add_test(NAME skin_assets_fresh
  COMMAND ${O3SDK_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/gen_skin.py --fresh ${PROJECT_BINARY_DIR}/app/generated --check ${PROJECT_SOURCE_DIR}/tools/skin_assets.sha256)
set_tests_properties(skin_assets_fresh PROPERTIES TIMEOUT 120)
