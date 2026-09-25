# game/ -- the legacy TOMS game code compiled against bgfx, plus the SDL3 executable.
set(L        ${TOMS_LEGACY_ROOT}/src)
set(L_ENGINE ${L}/engine)
set(L_GAME   ${L}/game)

# ---- toms_legacy_game: ../src exactly as it is, minus the Vulkan/GLFW/web-only files ----
# Same list as the old desktop target (TOMS/CMakeLists.txt, tower_vulkan) without main.cpp,
# renderer.cpp, imgui_layer.cpp and the ImGui GLFW/Vulkan backends.
add_library(toms_legacy_game STATIC
    ${L_ENGINE}/node.cpp ${L_ENGINE}/font.cpp ${L_ENGINE}/condition.cpp ${L_ENGINE}/entity_status.cpp
    ${L_ENGINE}/encounter.cpp ${L_ENGINE}/power_bar.cpp
    ${L_GAME}/core/game.cpp ${L_GAME}/core/game_assets.cpp ${L_GAME}/core/game_text_draw.cpp
    ${L_GAME}/core/game_scene_draw.cpp ${L_GAME}/core/game_input.cpp ${L_GAME}/core/game_inventory.cpp
    ${L_GAME}/core/game_store.cpp ${L_GAME}/core/game_combat.cpp ${L_GAME}/core/game_story.cpp
    ${L_GAME}/core/game_title_glue.cpp
    ${L_GAME}/audio/Audio.cpp
    ${L_GAME}/save/save_system.cpp ${L_GAME}/save/save_slots.cpp ${L_GAME}/save/game_settings.cpp
    ${L_GAME}/ui/localization.cpp ${L_GAME}/ui/title_screen.cpp
    ${L_GAME}/systems/mission_system.cpp ${L_GAME}/systems/equipment_system.cpp
    ${L_GAME}/systems/skill_system.cpp ${L_GAME}/systems/forge_system.cpp ${L_GAME}/systems/hub_system.cpp
    ${L_GAME}/systems/equipment_actives.cpp ${L_GAME}/systems/ending_system.cpp
    ${L_GAME}/systems/cycle_system.cpp ${L_GAME}/systems/camera.cpp ${L_GAME}/systems/roamer.cpp
    ${L_GAME}/systems/run_state.cpp ${L_GAME}/systems/floor_table.cpp
    compat/renderer.h compat/vk_util.h)
toms_target_defaults(toms_legacy_game)
# compat/ MUST come first: its renderer.h / vk_util.h shadow the Vulkan ones in src/engine.
target_include_directories(toms_legacy_game BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/compat)
target_include_directories(toms_legacy_game PUBLIC
    ${L_GAME} ${L_GAME}/core ${L_GAME}/systems ${L_GAME}/save ${L_GAME}/ui ${L_GAME}/audio
    ${L_ENGINE}
    ${TOMS_LEGACY_ROOT}/external/stb ${TOMS_LEGACY_ROOT}/external/json ${TOMS_LEGACY_ROOT}/external/miniaudio
    ${CMAKE_SOURCE_DIR}/engine/src)          # bgfx_renderer.h (through compat/renderer.h), no bgfx headers
# Only ImGui is linked here; bgfx itself stays out of the legacy code's compile settings.
target_link_libraries(toms_legacy_game PUBLIC toms_imgui glm::glm)
set_target_properties(toms_legacy_game PROPERTIES FOLDER "toms")

# ---- toms_game_host: GameSession, shared by the SDL game and the Qt editor ----
add_library(toms_game_host STATIC src/game_session.h src/game_session.cpp)
toms_target_defaults(toms_game_host)
target_include_directories(toms_game_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(toms_game_host PUBLIC toms_legacy_game toms_bgfx)
target_compile_definitions(toms_game_host PRIVATE TOMS_DEFAULT_ASSET_DIR="${TOMS_LEGACY_ROOT}/assets")
set_target_properties(toms_game_host PROPERTIES FOLDER "toms")

# ---- toms_game.exe: SDL3 + bgfx, what ships (no Qt) ----
add_executable(toms_game src/main_sdl.cpp)
toms_target_defaults(toms_game)
target_link_libraries(toms_game PRIVATE toms_game_host ${TOMS_SDL3_TARGET})
set_target_properties(toms_game PROPERTIES
    FOLDER "toms"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT toms_game)
