# tools/anim -- node animation tooling (docs/15_ANIMATION.md).
#
#   toms_anim       the runtime (src/core/engine/anim_clip.cpp, anim_player.cpp) on its own, without
#                   the game: what the tools evaluate and draw clips with, so they match the game
#   anim_tools      editor logic without Qt: channel-generic key editing, checks (core/)
#   anim_editor     Qt 6 editor for .anim files (qt/); `anim_editor --headless check x.anim`,
#                   `anim_editor --headless convert-mpdi <folder> [--out <dir>]` (core/mpdi_convert.h)
#
# Added from the root CMakeLists.txt (desktop builds); the editor needs Qt 6 Widgets.
set(ANIM_ENGINE_DIR "${TOMS_CORE_SRC}/engine")

add_library(toms_anim STATIC
    "${ANIM_ENGINE_DIR}/anim_clip.h"   "${ANIM_ENGINE_DIR}/anim_clip.cpp"
    "${ANIM_ENGINE_DIR}/anim_player.h" "${ANIM_ENGINE_DIR}/anim_player.cpp"
    "${ANIM_ENGINE_DIR}/sprite_mesh.h" "${ANIM_ENGINE_DIR}/sprite_mesh.cpp")   # meshes of skinned sprites (docs/34)
toms_target_defaults(toms_anim)
target_include_directories(toms_anim PUBLIC
    "${ANIM_ENGINE_DIR}"                   # anim_clip.h, anim_player.h, atlas_file.h, render_iface.h
    "${TOMS_THIRD_PARTY_DIR}"              # <tweeny/easing.h>
    "${TOMS_THIRD_PARTY_DIR}/json")        # <json.hpp>
target_link_libraries(toms_anim PUBLIC glm::glm)
set_target_properties(toms_anim PROPERTIES FOLDER "tools/anim")

add_library(anim_tools STATIC
    core/anim_keys.h  core/anim_keys.cpp
    core/anim_check.h core/anim_check.cpp
    core/anim_meshes.h core/anim_meshes.cpp      # the atlases' sprite meshes the clips need (docs/34_MESH_ANIMATION.md)
    core/anim_recipes.h core/anim_recipes.cpp    # motion recipes for lattices and bones (docs/16 section 7)
    core/anim_weights.h core/anim_weights.cpp    # weight painting: the editor's Weights mode (docs/34 step 5 part 3)
    core/mpdi_convert.h core/mpdi_convert.cpp)   # the old engine's .mpdi (+ .pi) -> .anim (+ atlas)
toms_target_defaults(anim_tools)
target_include_directories(anim_tools PUBLIC core)
target_link_libraries(anim_tools PUBLIC toms_anim PRIVATE atlas_core)   # atlas_core: the .pi reader, the atlas writer
set_target_properties(anim_tools PROPERTIES FOLDER "tools/anim")

# The .mpdi converter on the hand-made files of tests/mpdi (docs/15_ANIMATION.md, "Converting MPDI").
add_executable(mpdi_convert_test tests/mpdi_convert_test.cpp)
toms_target_defaults(mpdi_convert_test)
target_link_libraries(mpdi_convert_test PRIVATE anim_tools)
set_target_properties(mpdi_convert_test PROPERTIES FOLDER "tools/anim")
if(TOMS_BUILD_TESTS)
    add_test(NAME anim.mpdi_convert
             COMMAND mpdi_convert_test "${CMAKE_SOURCE_DIR}/tests/mpdi" "${CMAKE_BINARY_DIR}/test-run/mpdi_convert")
    set_tests_properties(anim.mpdi_convert PROPERTIES LABELS "unit;anim" TIMEOUT 60)
endif()

# The motion recipes (core/anim_recipes.h; docs/16_ANIMATION_RECIPES.md section 7).
add_executable(anim_recipes_test tests/anim_recipes_test.cpp)
toms_target_defaults(anim_recipes_test)
target_link_libraries(anim_recipes_test PRIVATE anim_tools)
set_target_properties(anim_recipes_test PROPERTIES FOLDER "tools/anim")
if(TOMS_BUILD_TESTS)
    add_test(NAME anim.recipes COMMAND anim_recipes_test)
    set_tests_properties(anim.recipes PROPERTIES LABELS "unit;anim" TIMEOUT 30)
endif()

# Weight painting (core/anim_weights.h; docs/34_MESH_ANIMATION.md step 5 part 3).
add_executable(anim_weights_test tests/anim_weights_test.cpp)
toms_target_defaults(anim_weights_test)
target_link_libraries(anim_weights_test PRIVATE anim_tools)
set_target_properties(anim_weights_test PROPERTIES FOLDER "tools/anim")
if(TOMS_BUILD_TESTS)
    add_test(NAME anim.weights COMMAND anim_weights_test)
    set_tests_properties(anim.weights PROPERTIES LABELS "unit;anim" TIMEOUT 30)
endif()

if(ATLAS_BUILD_EDITOR AND TARGET Qt6::Widgets AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qt/CMakeLists.txt")
    add_subdirectory(qt)
endif()
