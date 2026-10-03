# tools/anim -- node animation tooling (docs/15_ANIMATION.md).
#
#   toms_anim       the runtime (src/core/engine/anim_clip.cpp, anim_player.cpp) on its own, without
#                   the game: what the tools evaluate and draw clips with, so they match the game
#   anim_tools      editor logic without Qt: channel-generic key editing, checks (core/)
#   anim_editor     Qt 6 editor for .anim files (qt/); `anim_editor --headless check x.anim`
#
# Added from the root CMakeLists.txt (desktop builds); the editor needs Qt 6 Widgets.
set(ANIM_ENGINE_DIR "${TOMS_CORE_SRC}/engine")

add_library(toms_anim STATIC
    "${ANIM_ENGINE_DIR}/anim_clip.h"   "${ANIM_ENGINE_DIR}/anim_clip.cpp"
    "${ANIM_ENGINE_DIR}/anim_player.h" "${ANIM_ENGINE_DIR}/anim_player.cpp")
toms_target_defaults(toms_anim)
target_include_directories(toms_anim PUBLIC
    "${ANIM_ENGINE_DIR}"                   # anim_clip.h, anim_player.h, atlas_file.h, render_iface.h
    "${TOMS_THIRD_PARTY_DIR}"              # <tweeny/easing.h>
    "${TOMS_THIRD_PARTY_DIR}/json")        # <json.hpp>
target_link_libraries(toms_anim PUBLIC glm::glm)
set_target_properties(toms_anim PROPERTIES FOLDER "tools/anim")

add_library(anim_tools STATIC
    core/anim_keys.h  core/anim_keys.cpp
    core/anim_check.h core/anim_check.cpp)
toms_target_defaults(anim_tools)
target_include_directories(anim_tools PUBLIC core)
target_link_libraries(anim_tools PUBLIC toms_anim)
set_target_properties(anim_tools PROPERTIES FOLDER "tools/anim")

if(ATLAS_BUILD_EDITOR AND TARGET Qt6::Widgets AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qt/CMakeLists.txt")
    add_subdirectory(qt)
endif()
