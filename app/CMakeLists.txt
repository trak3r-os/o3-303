# SPDX-License-Identifier: GPL-3.0-or-later
# The sequencer library `o3_303_seq` (docs/M3-PLAN.md, the seq slice), once it exists.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/seq/CMakeLists.txt)
  add_subdirectory(seq)
endif()

# O3-303: the app (docs/DESIGN-303.md). o3sdk_app() (o3-sdk/cmake/O3sdkTargets.cmake) builds the library `app_o3_303`, the simulator
# `o3_303_sim` on the host and, with -DO3SDK_PLATFORM=mk66, the image `o3_303.elf` / `.hex` / `.ptf` with its ctests.
o3sdk_app(o3_303 SOURCES src/o3_303_app.cpp src/step_model.cpp src/pages.cpp src/step_view.cpp src/main_screen.cpp src/main_screen_perf.cpp src/perform.cpp src/vault_screen.cpp src/rig.cpp
  DEPENDS unit_delay unit_tb303 unit_distortion unit_freeze o3_303_seq)
# <o3_303/pad_map.h>: the pure parts of the app, which the tests include.
target_include_directories(app_o3_303 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
