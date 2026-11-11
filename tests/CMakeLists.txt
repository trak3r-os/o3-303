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
# The longest ctests are cut into shares, so that a parallel run's floor is the longest share and not the longest file.
#  * The scenarios test: O3_303_SCENARIO_SHARD=i/n (tests/o3_303_scenarios_test.cpp), six shares; a direct run, with no variable, runs all.
#  * A file of test cases (record_screen_test, project_service_test): O3SDK_TEST_SHARD=i/n (o3-sdk tests/main.cpp), the file's test cases by line mod n. Share 0 keeps the file's own ctest name,
#    the others are <name>_<i>; every share has the labels of the file.
# o3_303_shard_coverage proves, by the lists the shares print, that the shares of every sharded file cover its test cases and the scenarios cover app/scenarios, each once.
set(o3_303_scenario_shards 6)
set(o3_303_sharded record_screen_test:6 project_service_test:8)
set_tests_properties(o3_303_scenarios_test PROPERTIES ENVIRONMENT "O3_303_SCENARIO_SHARD=0/${o3_303_scenario_shards}")
math(EXPR last_scenario_shard "${o3_303_scenario_shards} - 1")
foreach(shard RANGE 1 ${last_scenario_shard})
  add_test(NAME o3_303_scenarios_test_${shard} COMMAND o3_303_tests --source-file=*o3_303_scenarios_test.cpp WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(o3_303_scenarios_test_${shard} PROPERTIES ENVIRONMENT "O3_303_SCENARIO_SHARD=${shard}/${o3_303_scenario_shards}")
  o3sdk_test_labels(o3_303_scenarios_test_${shard} AREA o3_303_tests SLOW)
endforeach()
foreach(spec IN LISTS o3_303_sharded)
  string(REPLACE ":" ";" parts "${spec}")
  list(GET parts 0 test)
  list(GET parts 1 n)
  set_tests_properties(${test} PROPERTIES ENVIRONMENT "O3SDK_TEST_SHARD=0/${n}")
  math(EXPR last "${n} - 1")
  foreach(shard RANGE 1 ${last})
    add_test(NAME ${test}_${shard} COMMAND o3_303_tests --source-file=*${test}.cpp WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(${test}_${shard} PROPERTIES ENVIRONMENT "O3SDK_TEST_SHARD=${shard}/${n}")
    o3sdk_test_labels(${test}_${shard} AREA o3_303_tests)
  endforeach()
endforeach()
# COST (ctest starts the highest first, also in a fresh build directory that has no timing history): every test that took more than about a minute
# in the full run of 2026-11-10 at a load of 16 to 25, divided by about four for an uncontended guess (seconds); a share gets its file's cost over n.
# The scenario shares are about 100 s each, project_service_test's about 50 (its longest case, "a card with nothing on it...", is about 30 on its own).
set(o3_303_cost config_pages_test:43 song_screen_test:37 pages_test:34 library_test:32 file_screen_test:30 modded_screen_test:27
    freeze_app_test:24 instrument_screen_test:23 app_master_test:22 skin_master_record_test:21 perform_screen_test:19 skin_instrument_test:19
    tempo_test:16 skin_file_test:15)
foreach(shard RANGE 0 ${last_scenario_shard})
  set(t o3_303_scenarios_test)
  if(shard GREATER 0)
    set(t o3_303_scenarios_test_${shard})
  endif()
  list(APPEND o3_303_cost ${t}:100)
endforeach()
foreach(spec IN LISTS o3_303_sharded)
  string(REPLACE ":" ";" parts "${spec}")
  list(GET parts 0 test)
  list(GET parts 1 n)
  if(test STREQUAL "project_service_test")
    set(cost 50)
  else()
    set(cost 27)
  endif()
  list(APPEND o3_303_cost ${test}:${cost})
  math(EXPR last "${n} - 1")
  foreach(shard RANGE 1 ${last})
    list(APPEND o3_303_cost ${test}_${shard}:${cost})
  endforeach()
endforeach()
foreach(spec IN LISTS o3_303_cost)
  string(REPLACE ":" ";" parts "${spec}")
  list(GET parts 0 test)
  list(GET parts 1 cost)
  if(TEST ${test})
    set_tests_properties(${test} PROPERTIES COST ${cost})
  endif()
endforeach()
add_test(NAME o3_303_shard_coverage
  COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:o3_303_tests> "-DSHARDS=${o3_303_sharded}" -DSCENARIO_SHARDS=${o3_303_scenario_shards}
          -DSCENARIO_DIR=${PROJECT_SOURCE_DIR}/app/scenarios -P ${CMAKE_CURRENT_SOURCE_DIR}/shard_coverage.cmake
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(o3_303_shard_coverage PROPERTIES TIMEOUT 300)
o3sdk_test_labels(o3_303_shard_coverage AREA o3_303_tests)
# The obsidian skin's bitmaps (docs/SKIN-PLAN.md section 6): two separate runs of tools/gen_skin.py, the copy this build linked and the committed
# hash (tools/skin_assets.sha256) must all agree: the generator is deterministic and the hash is current.
add_test(NAME skin_assets_fresh
  COMMAND ${O3SDK_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/gen_skin.py --fresh ${PROJECT_BINARY_DIR}/app/generated --check ${PROJECT_SOURCE_DIR}/tools/skin_assets.sha256)
set_tests_properties(skin_assets_fresh PROPERTIES TIMEOUT 120)
o3sdk_test_labels(skin_assets_fresh AREA tools)
# The player's manual (docs/MANUAL.md): every picture it shows is in docs/manual/img and has a row in tools/manual_images.py, and every row names a
# scenario that exists and a snap it really takes. The pictures' bytes are not compared (tools/manual_images.py --sim makes them again).
add_test(NAME manual_images
  COMMAND ${O3SDK_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/manual_images.py --check)
set_tests_properties(manual_images PROPERTIES TIMEOUT 60)
o3sdk_test_labels(manual_images AREA tools)
# The MIDI test tool of the unit (tools/midi_check.py, docs/hw/2026-11-01-midi.md) against its fake unit: parsers, signal analysis, the clock sender's timing on this
# machine, the refusal, the restore after a failure mid-phase, and the whole tool on a pty. No hardware. It needs a pty and the o3-sdk tools; without them it
# returns 77 and ctest reports it skipped. RUN_SERIAL: it measures the clock sender's timing in real time, so it passes alone and fails when other tests share the CPU.
add_test(NAME midi_check_selftest
  COMMAND ${O3SDK_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/midi_check.py --self-test)
set_tests_properties(midi_check_selftest PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE LABELS "tools;slow;timing" COST 75 SKIP_RETURN_CODE 77 ENVIRONMENT "O3SDK_DIR=${O3SDK_DIR}")
