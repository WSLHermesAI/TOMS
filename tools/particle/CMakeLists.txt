# tools/particle -- particle effect tooling (docs/17_PARTICLES.md).
#
#   toms_fx           the runtime (src/core/engine/particle_fx.cpp) on its own, without the game:
#                     what the editor simulates and draws effects with, so it matches the game
#   particle_editor   Qt 6 editor for .particle files (qt/); `particle_editor --headless check x.particle`
#
# Added from the root CMakeLists.txt (desktop builds); the editor needs Qt 6 Widgets.
add_library(toms_fx STATIC
    "${TOMS_CORE_SRC}/engine/particle_fx.h" "${TOMS_CORE_SRC}/engine/particle_fx.cpp")
toms_target_defaults(toms_fx)
target_link_libraries(toms_fx PUBLIC toms_anim)   # Ease, AtlasRef, AtlasSet; include dirs
set_target_properties(toms_fx PROPERTIES FOLDER "tools/particle")

if(ATLAS_BUILD_EDITOR AND TARGET Qt6::Widgets AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qt/CMakeLists.txt")
    add_subdirectory(qt)
endif()
