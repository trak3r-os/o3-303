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
# The scenarios test is the floor of a full run: three ctests share it (O3_303_SCENARIO_SHARD=i/n, tests/o3_303_scenarios_test.cpp); a direct run, with no variable, runs all.
set_tests_properties(o3_303_scenarios_test PROPERTIES ENVIRONMENT "O3_303_SCENARIO_SHARD=0/3")
foreach(shard 1 2)
  add_test(NAME o3_303_scenarios_test_${shard} COMMAND o3_303_tests --source-file=*o3_303_scenarios_test.cpp WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(o3_303_scenarios_test_${shard} PROPERTIES ENVIRONMENT "O3_303_SCENARIO_SHARD=${shard}/3")
endforeach()
# The obsidian skin's bitmaps (docs/SKIN-PLAN.md section 6): two separate runs of tools/gen_skin.py, the copy this build linked and the committed
# hash (tools/skin_assets.sha256) must all agree: the generator is deterministic and the hash is current.
add_test(NAME skin_assets_fresh
  COMMAND ${O3SDK_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/gen_skin.py --fresh ${PROJECT_BINARY_DIR}/app/generated --check ${PROJECT_SOURCE_DIR}/tools/skin_assets.sha256)
set_tests_properties(skin_assets_fresh PROPERTIES TIMEOUT 120)
# The player's manual (docs/MANUAL.md): every picture it shows is in docs/manual/img and has a row in tools/manual_images.py, and every row names a
# scenario that exists and a snap it really takes. The pictures' bytes are not compared (tools/manual_images.py --sim makes them again).
add_test(NAME manual_images
  COMMAND ${O3SDK_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/manual_images.py --check)
set_tests_properties(manual_images PROPERTIES TIMEOUT 60)
# The MIDI test tool of the unit (tools/midi_check.py, docs/hw/2026-11-01-midi.md) against its fake unit: parsers, signal analysis, the clock sender's timing on this
# machine, the refusal, the restore after a failure mid-phase, and the whole tool on a pty. No hardware. It needs a pty and the o3-sdk tools; without them it
# returns 77 and ctest reports it skipped. RUN_SERIAL: it measures the clock sender's timing in real time, so it passes alone and fails when other tests share the CPU.
add_test(NAME midi_check_selftest
  COMMAND ${O3SDK_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/midi_check.py --self-test)
set_tests_properties(midi_check_selftest PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE LABELS tools SKIP_RETURN_CODE 77 ENVIRONMENT "O3SDK_DIR=${O3SDK_DIR}")
