# game/ -- the core game code (src/core) compiled against bgfx, plus the SDL3 executable.
set(L        ${TOMS_CORE_SRC})
set(L_ENGINE ${L}/engine)
set(L_GAME   ${L}/game)

# ---- toms_core: the game code in src/core (every non-test source there) ----
# The *_test.cpp files are not built yet (CTest, migration phase 2).
add_library(toms_core STATIC
    ${L_ENGINE}/node.cpp ${L_ENGINE}/job_system.cpp ${L_ENGINE}/condition.cpp ${L_ENGINE}/entity_status.cpp
    ${L_ENGINE}/encounter.cpp ${L_ENGINE}/power_bar.cpp
    ${L_ENGINE}/anim_clip.cpp ${L_ENGINE}/anim_player.cpp ${L_ENGINE}/particle_fx.cpp ${L_ENGINE}/gltf_model.cpp ${L_ENGINE}/stb_image_impl.cpp
    ${L_GAME}/core/game.cpp ${L_GAME}/core/game_assets.cpp ${L_GAME}/core/game_ui.cpp
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
    compat/renderer.h)
toms_target_defaults(toms_core)
# compat/renderer.h: `Renderer` = the bgfx renderer (see the header).
target_include_directories(toms_core BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/compat)
target_include_directories(toms_core PUBLIC
    ${L_GAME} ${L_GAME}/core ${L_GAME}/systems ${L_GAME}/save ${L_GAME}/ui ${L_GAME}/audio
    ${L_ENGINE}
    ${TOMS_THIRD_PARTY_DIR}/stb ${TOMS_THIRD_PARTY_DIR}/json ${TOMS_THIRD_PARTY_DIR}/miniaudio
    ${TOMS_THIRD_PARTY_DIR}             # <tweeny/easing.h> (anim_clip.cpp)
    ${TOMS_SRC_DIR}/engine/src)          # bgfx_renderer.h (through compat/renderer.h), no bgfx headers
# Only ImGui is linked here; bgfx itself stays out of the core code's compile settings.
target_link_libraries(toms_core PUBLIC toms_imgui glm::glm)
if(NOT EMSCRIPTEN)   # the job system's worker threads (the web gets -pthread from the root CMakeLists)
    find_package(Threads REQUIRED)
    target_link_libraries(toms_core PUBLIC Threads::Threads)
endif()
set_target_properties(toms_core PROPERTIES FOLDER "toms")
# cgltf (gltf_model.cpp): one header, fetched by cmake/TomsDependencies.cmake.
target_include_directories(toms_core PRIVATE ${cgltf_SOURCE_DIR})

# ---- toms_game_host: GameSession, shared by the SDL game and the Qt editor ----
add_library(toms_game_host STATIC src/game_session.h src/game_session.cpp src/game_ui.h src/game_ui.cpp)
toms_target_defaults(toms_game_host)
target_include_directories(toms_game_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(toms_game_host PUBLIC toms_core toms_bgfx)
if(EMSCRIPTEN)
    # The browser build packs assets/ and data/ into toms_game.data, mounted at /assets and /data.
    target_compile_definitions(toms_game_host PRIVATE TOMS_DEFAULT_ASSET_DIR="/assets")
elseif(ANDROID)
    # The APK carries the repo's assets/ as its assets root; vfs.h maps "assets/media/..." to the entry
    # "media/..." (and "assets/media/../data/..." to "data/...").
    target_compile_definitions(toms_game_host PRIVATE TOMS_DEFAULT_ASSET_DIR="assets/media")
else()
    target_compile_definitions(toms_game_host PRIVATE TOMS_DEFAULT_ASSET_DIR="${TOMS_ASSET_DIR}")
endif()
set_target_properties(toms_game_host PROPERTIES FOLDER "toms")

# ---- toms_game: SDL3 + bgfx, what ships (no Qt). Desktop: toms_game.exe. Web: toms_game.html ----
if(ANDROID)
    # SDLActivity loads libmain.so and calls SDL_main (main() renamed by SDL_main.h). SDL3 is static, so
    # its JNI code lives in this library too; keep JNI_OnLoad (it registers SDL's native methods).
    add_library(toms_game SHARED src/main_sdl.cpp)
    set_target_properties(toms_game PROPERTIES OUTPUT_NAME main)
    target_link_options(toms_game PRIVATE "-Wl,--undefined=JNI_OnLoad" "-Wl,-z,max-page-size=16384")
else()
    add_executable(toms_game src/main_sdl.cpp)
endif()
toms_target_defaults(toms_game)
target_link_libraries(toms_game PRIVATE toms_game_host ${TOMS_SDL3_TARGET})
set_target_properties(toms_game PROPERTIES
    FOLDER "toms"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT toms_game)

# Shipping: a normal Windows app (no console window when double-clicked). main() stays the entry
# point (SDL_MAIN_HANDLED), so the linker is told to start there. Errors still show as popups;
# the log goes to toms.log next to the exe.
option(TOMS_WINDOWED_EXE "Build toms_game.exe without a console window (the shipping preset sets this)" OFF)
if(MSVC AND TOMS_WINDOWED_EXE)
    set_target_properties(toms_game PROPERTIES WIN32_EXECUTABLE ON)
    target_link_options(toms_game PRIVATE /ENTRY:mainCRTStartup)
endif()

if(EMSCRIPTEN)
    set(_shell ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    # Output: bin/toms_game.html + .js + .wasm + .data (the preset's .js suffix is overridden here).
    set_target_properties(toms_game PROPERTIES SUFFIX ".html" LINK_DEPENDS "${_shell}")
    target_link_options(toms_game PRIVATE
        "--shell-file=${_shell}"
        "-sMIN_WEBGL_VERSION=2" "-sMAX_WEBGL_VERSION=2"       # bgfx's GLES backend on WebGL2
        "-sALLOW_MEMORY_GROWTH=1"
        "-sFORCE_FILESYSTEM=1" "-lidbfs.js"                    # saves: IDBFS at /save
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','UTF8ToString','stringToNewUTF8']"
        "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
        "-sSTACK_SIZE=1048576"
        # Game content, including the UI documents (assets/media/ui). The fonts are left out:
        # on the web the browser draws the text (src/engine/src/rml_canvas_font.*).
        "--preload-file=${TOMS_ASSET_DIR}@/assets"
        "--preload-file=${TOMS_DATA_DIR}@/data"
        "--exclude-file=${TOMS_ASSET_DIR}/fonts/*")
    # The multithreaded web build (preset web-release-mt-windows, docs/10_THREADS.md): Web Workers
    # started before main(), so the job system never has to wait for one to be created. One spare
    # beyond the job system's 4 for anything else that starts a thread.
    option(TOMS_WEB_THREADS "Web build with WebAssembly threads (set by the -mt presets, together with -pthread)" OFF)
    if(TOMS_WEB_THREADS)
        target_link_options(toms_game PRIVATE "-sPTHREAD_POOL_SIZE=5")
        target_compile_definitions(toms_core PRIVATE TOMS_WEB_THREAD_POOL=4)
    endif()
    # Re-link when content changes, so the .data bundle never goes stale.
    file(GLOB_RECURSE _web_content CONFIGURE_DEPENDS
         "${TOMS_DATA_DIR}/*.json" "${TOMS_ASSET_DIR}/sprites/*" "${TOMS_ASSET_DIR}/sfx/*"
         "${TOMS_ASSET_DIR}/ui/*" "${TOMS_ASSET_DIR}/atlas/*" "${TOMS_ASSET_DIR}/styles/*")
    set_property(TARGET toms_game APPEND PROPERTY LINK_DEPENDS ${_web_content})
endif()

# Android needs the audio and log system libraries: miniaudio's Android backends (OpenSL ES, and AAudio
# when the device has it) need OpenSLES, and log.h's __android_log_print needs log. Guarded by TARGET so a
# renamed target cannot break configure, and inert everywhere else.
if(ANDROID)
    foreach(_toms_android_target toms_game toms_core)
        if(TARGET ${_toms_android_target})
            target_link_libraries(${_toms_android_target} PRIVATE OpenSLES log)
        endif()
    endforeach()
    # bgfx's OpenGL ES backend creates its context with EGL (bgfx.cmake links GLESv2 but not EGL)
    target_link_libraries(toms_game PRIVATE EGL android)
endif()
