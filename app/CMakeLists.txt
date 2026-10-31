# SPDX-License-Identifier: GPL-3.0-or-later
# The sequencer library `o3_303_seq` (docs/M3-PLAN.md, the seq slice), once it exists.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/seq/CMakeLists.txt)
  add_subdirectory(seq)
endif()

# O3-303: the app (docs/DESIGN-303.md). o3sdk_app() (o3-sdk/cmake/O3sdkTargets.cmake) builds the library `app_o3_303`, the simulator
# `o3_303_sim` on the host and, with -DO3SDK_PLATFORM=mk66, the image `o3_303.elf` / `.hex` / `.ptf` with its ctests.
file(GLOB O3_303_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/skin/*.cpp)

# The obsidian skin's bitmaps (docs/SKIN-PLAN.md sections 3 and 6): tools/gen_skin.py draws them in integers, standard library only, and writes
# o3_303/skin_assets.h and skin_assets.cpp into the build directory. --check fails the build when the output is not the hash committed in
# tools/skin_assets.sha256 (so the MK66 build verifies it too). It runs again only when the script or the hash changes. Nothing generated is in git.
set(O3_303_SKIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(O3_303_SKIN_SCRIPT "${PROJECT_SOURCE_DIR}/tools/gen_skin.py")
set(O3_303_SKIN_SHA "${PROJECT_SOURCE_DIR}/tools/skin_assets.sha256")
add_custom_command(
  OUTPUT "${O3_303_SKIN_DIR}/o3_303/skin_assets.h" "${O3_303_SKIN_DIR}/skin_assets.cpp"
  COMMAND ${O3SDK_PYTHON} -I "${O3_303_SKIN_SCRIPT}" --out "${O3_303_SKIN_DIR}" --check "${O3_303_SKIN_SHA}"
  DEPENDS "${O3_303_SKIN_SCRIPT}" "${O3_303_SKIN_SHA}"
  COMMENT "Generating the obsidian skin's bitmaps (tools/gen_skin.py)"
  VERBATIM)
o3sdk_app(o3_303 SOURCES ${O3_303_APP_SOURCES}
  DEPENDS unit_delay unit_acid_voice unit_distortion unit_freeze unit_growl o3_303_seq
  # The boot logo of the image (o3-sdk docs/FRAMEWORK.md, "The boot logo"): the groovebox drawing, converted at build time; a grey picture, drawn white.
  LOGO ../logo/logo.png)
# <o3_303/pad_map.h>: the pure parts of the app, which the tests include.
target_include_directories(app_o3_303 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# <o3_303/skin_assets.h> and its bytes: generated, so a source of the build directory (the glob above cannot see it).
target_sources(app_o3_303 PRIVATE "${O3_303_SKIN_DIR}/skin_assets.cpp")
target_include_directories(app_o3_303 PUBLIC "${O3_303_SKIN_DIR}")
