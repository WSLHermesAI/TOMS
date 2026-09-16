cmake_minimum_required(VERSION 3.16)
project(tower_vulkan CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Paths to vendored third-party headers (self-contained, no system deps)
set(STB_DIR ${CMAKE_SOURCE_DIR}/external/stb)
set(JSON_DIR ${CMAKE_SOURCE_DIR}/external/json)

# Source tree split:
#   src/game/    — gameplay (main, Game, Stage, Audio), itself split by category (see below)
#   src/engine/  — node scene-graph + render + core infra (fonts, textures, shaders, tests)
set(GAME_DIR   ${CMAKE_SOURCE_DIR}/src/game)
set(ENGINE_DIR ${CMAKE_SOURCE_DIR}/src/engine)

# src/game/ is organized into category subfolders (2026-09-14 reorg) so a reader can find code by
# what it does rather than scanning one 59-file flat directory:
#   core/    — the Game class itself + its translation-unit split (game.h + game_*.cpp), main.cpp,
#              stage.h -- everything that IS the game loop, not a reusable subsystem of it.
#   systems/ — self-contained gameplay subsystems (equipment/skill/forge/mission, run/story state,
#              floor table, footprints+roamer AI, camera) -- each pure content+logic with its own
#              test, no Game dependency.
#   save/    — persistence: save_system (meta/run schema), save_slots, game_settings.
#   ui/      — title screen, the design-size layout primitive, the 6-language string table.
#   audio/   — Audio.h/.cpp (miniaudio wrapper).
# Every #include "foo.h" inside src/game/ stays a bare filename (unchanged by this reorg) -- name
# collisions were never possible in the old flat layout, so making every one of these directories
# part of the include search path (below) preserves that same resolution exactly; only the file
# LIST below (which .cpp feeds which target) needed each path updated to its new subfolder.
set(GAME_CORE    ${GAME_DIR}/core)
set(GAME_SYSTEMS ${GAME_DIR}/systems)
set(GAME_SAVE    ${GAME_DIR}/save)
set(GAME_UI      ${GAME_DIR}/ui)
set(GAME_AUDIO   ${GAME_DIR}/audio)
set(GAME_INCLUDE_DIRS ${GAME_DIR} ${GAME_CORE} ${GAME_SYSTEMS} ${GAME_SAVE} ${GAME_UI} ${GAME_AUDIO})

# Dear ImGui (Milestone 2 — see docs/architecture/IMPLEMENTATION_ROADMAP.md). Pinned to v1.90.9, the last
# release before the Vulkan backend's init API changed shape (RenderPass moved from a second
# ImGui_ImplVulkan_Init() argument into the InitInfo struct around v1.91).
# Declared here, NOT inside `if(NOT WEB)`: M2 is "ImGui wired into all three renderer backends",
# so the browser target needs the same ${imgui_SOURCE_DIR}. Only the backends differ per platform
# — imgui_impl_glfw + imgui_impl_vulkan on desktop, imgui_impl_opengl3 (ES3) on WebGL2 (see
# src/engine/imgui_web.h). GLFW itself stays desktop-only (fetched below): it has no business
# under WASM.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.9
)
FetchContent_MakeAvailable(imgui)

# ============================================================================
# Desktop target (Windows / Linux) — Vulkan renderer.
# ============================================================================
option(WEB "Build the Emscripten browser target" OFF)
if(NOT WEB)
    # GLFW is only needed for the windowed desktop (Vulkan) build. Fetching it
    # here (not at top level) keeps the Emscripten (WEB) configure from pulling
    # in GLFW's Wayland/X11 detection, which has no business under WASM.
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
    )
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)

    # ---- Vulkan ----
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND OR NOT Vulkan_LIBRARIES)
        if(DEFINED ENV{VULKAN_SDK})
            set(_VK_ROOT "$ENV{VULKAN_SDK}")
            if(EXISTS "${_VK_ROOT}/x86_64")
                set(_VK_ROOT "${_VK_ROOT}/x86_64")
            endif()
            find_path(Vulkan_INCLUDE_DIR NAMES vulkan/vulkan.h HINTS "${_VK_ROOT}/include" "${_VK_ROOT}/Include")
            find_library(Vulkan_LIBRARY NAMES vulkan vulkan-1
                HINTS "${_VK_ROOT}/lib" "${_VK_ROOT}/Lib" "${_VK_ROOT}/bin"
                      "${_VK_ROOT}/lib/VulkanLoader/lib" "${_VK_ROOT}/Lib/VulkanLoader/lib")
        else()
            find_path(Vulkan_INCLUDE_DIR NAMES vulkan/vulkan.h)
            find_library(Vulkan_LIBRARY NAMES vulkan vulkan-1)
        endif()
        if(NOT Vulkan_LIBRARY)
            message(FATAL_ERROR "Could not find the Vulkan loader library. Set VULKAN_SDK or -DSDK_DIR=.")
        endif()
        set(Vulkan_LIBRARIES ${Vulkan_LIBRARY})
        set(Vulkan_FOUND TRUE)
    endif()

    find_program(GLSLANG_VALIDATOR NAMES glslangValidator
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" "${Vulkan_INCLUDE_DIRS}/../Bin")
    message(STATUS "GLSLANG_VALIDATOR = ${GLSLANG_VALIDATOR}")

    # Precompiled shaders live in assets/shaders/ (plural). If the .spv files are
    # missing but the .vert/.frag sources + glslangValidator are present, build them.
    file(GLOB SPV_FILES "${CMAKE_SOURCE_DIR}/assets/shaders/*.spv")
    if(NOT SPV_FILES)
        message(STATUS "No precompiled .spv in assets/shaders/; will compile from .vert/.frag if glslangValidator is available.")
        if(GLSLANG_VALIDATOR)
            file(GLOB VERT_FILES "${CMAKE_SOURCE_DIR}/assets/shaders/*.vert")
            file(GLOB FRAG_FILES "${CMAKE_SOURCE_DIR}/assets/shaders/*.frag")
            foreach(SRC ${VERT_FILES} ${FRAG_FILES})
                get_filename_component(SPVID ${SRC} NAME_WE)
                add_custom_command(OUTPUT "${CMAKE_SOURCE_DIR}/assets/shaders/${SPVID}.spv"
                    COMMAND ${GLSLANG_VALIDATOR} -V "${SRC}" -o "${CMAKE_SOURCE_DIR}/assets/shaders/${SPVID}.spv"
                    DEPENDS "${SRC}" COMMENT "Compiling ${SPVID} shader")
                list(APPEND SHADER_SPVS "${CMAKE_SOURCE_DIR}/assets/shaders/${SPVID}.spv")
            endforeach()
            add_custom_target(toms_shaders DEPENDS ${SHADER_SPVS})
            add_dependencies(tower_vulkan toms_shaders)
        else()
            message(WARNING "No precompiled .spv shaders found in assets/shaders/ and glslangValidator unavailable. "
                            "The Vulkan renderer needs sprite.vert.spv / sprite.frag.spv next to the executable.")
        endif()
    endif()

    add_executable(tower_vulkan
        ${GAME_CORE}/main.cpp ${GAME_CORE}/game.cpp ${ENGINE_DIR}/renderer.cpp
        ${ENGINE_DIR}/node.cpp ${ENGINE_DIR}/font.cpp
        ${ENGINE_DIR}/render_iface.h ${GAME_AUDIO}/Audio.cpp ${ENGINE_DIR}/vk_util.h
        ${GAME_SAVE}/save_system.cpp ${ENGINE_DIR}/condition.cpp ${ENGINE_DIR}/entity_status.cpp
        ${ENGINE_DIR}/encounter.cpp ${GAME_SYSTEMS}/mission_system.cpp
        ${ENGINE_DIR}/power_bar.cpp ${GAME_SYSTEMS}/equipment_system.cpp ${GAME_SYSTEMS}/skill_system.cpp
        ${GAME_SYSTEMS}/forge_system.cpp
        ${GAME_SYSTEMS}/hub_system.cpp
        ${GAME_SYSTEMS}/active_system.cpp
        ${GAME_SYSTEMS}/ending_system.cpp
        ${GAME_SYSTEMS}/cycle_system.cpp
        ${ENGINE_DIR}/imgui_layer.h ${ENGINE_DIR}/imgui_layer.cpp
        # Title phase (New Game / Continue / Settings): save slots, preferences, string table.
        ${GAME_SAVE}/save_slots.cpp ${GAME_SAVE}/game_settings.cpp
        ${GAME_UI}/localization.cpp ${GAME_UI}/title_screen.cpp
        # Refactor: the maze camera is its own translation unit + class (was ~120 lines inside
        # game.cpp/game.h mixed with unrelated logic) -- see src/game/camera.h, camera_test.cpp.
        ${GAME_SYSTEMS}/camera.cpp
        # S1: 佔格 footprints (footprint.h) + the floor-wanderer AI (roamer.h/.cpp) -- own files,
        # like camera.cpp above.
        ${GAME_SYSTEMS}/roamer.cpp
        # S3: per-run story state (choices/counters/side stories/flags/shards) -- its own class, see
        # run_state.h; persisted as RunSaveData schemaVersion 3.
        ${GAME_SYSTEMS}/run_state.cpp
        ${GAME_SYSTEMS}/floor_table.cpp
        # Refactor (2026-09-13): game.cpp was a single 3,100-line translation unit; it is now split
        # by responsibility into these units (game.h still declares everything; Game is unchanged
        # as a class, and shared file-local helpers moved to game_helpers.h).
        ${GAME_CORE}/game_assets.cpp ${GAME_CORE}/game_text_draw.cpp
        ${GAME_CORE}/game_scene_draw.cpp ${GAME_CORE}/game_input.cpp
        ${GAME_CORE}/game_inventory.cpp ${GAME_CORE}/game_store.cpp
        ${GAME_CORE}/game_combat.cpp ${GAME_CORE}/game_story.cpp
        ${GAME_CORE}/game_title_glue.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)

    target_include_directories(tower_vulkan PRIVATE
        ${GAME_INCLUDE_DIRS} ${ENGINE_DIR}
        ${Vulkan_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIR}
        ${STB_DIR} ${JSON_DIR} ${CMAKE_SOURCE_DIR}/external/miniaudio
        $ENV{GLM_DIR} $ENV{HOME}/opt/glm
        ${glfw_SOURCE_DIR}/include
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(tower_vulkan PRIVATE ${Vulkan_LIBRARIES} glfw)
    if(WIN32)
        target_compile_definitions(tower_vulkan PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()

    # Copy runtime resources next to the executable (assets/, data/)
    foreach(RES assets data)
        if(EXISTS "${CMAKE_SOURCE_DIR}/${RES}")
            add_custom_command(TARGET tower_vulkan POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/${RES}" "$<TARGET_FILE_DIR:tower_vulkan>/${RES}"
                COMMENT "Copying ${RES} next to executable")
        endif()
    endforeach()
    # Shaders live under assets/shaders/ (plural). The Vulkan renderer loads them
    # as "sprite.vert.spv" / "sprite.frag.spv" relative to CWD, so copy the .spv
    # files directly next to the executable (not into a subfolder).
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/shaders")
        file(GLOB SHADER_SPV "${CMAKE_SOURCE_DIR}/assets/shaders/*.spv")
        foreach(SPV ${SHADER_SPV})
            add_custom_command(TARGET tower_vulkan POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy "${SPV}" "$<TARGET_FILE_DIR:tower_vulkan>/"
                COMMENT "Copying shader ${SPV} next to executable")
        endforeach()
    endif()

    # Ensure the CJK TTF exists for the runtime font builder. The bundled copy is
    # gitignored (16MB); fall back to the system font if it isn't present.
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/wqy-zenhei.ttc")
        foreach(SYSFONT
                "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"
                "/usr/share/fonts/opentype/wqy/wqy-zenhei.ttc")
            if(EXISTS "${SYSFONT}")
                add_custom_command(TARGET tower_vulkan POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy
                        "${SYSFONT}" "$<TARGET_FILE_DIR:tower_vulkan>/assets/wqy-zenhei.ttc"
                    COMMENT "Copying CJK TTF (${SYSFONT}) next to executable")
                break()
            endif()
        endforeach()
    endif()

    set_target_properties(tower_vulkan PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:tower_vulkan>")

    # Default F5 target in the generated .sln (else VS picks the alphabetically
    # first project — one of the *_test executables — instead of the game).
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT tower_vulkan)
endif()

# ============================================================================
# Emscripten browser target — WebGL2 (default) or WebGPU.
# Build with:  emcmake cmake -S . -B build-web -DWEB=ON [-DWEB_BACKEND=WebGPU]
# Produces web/tower_vulkan_web.html (+ .js/.wasm) with assets preloaded as a
# single packed .data bundle. SFX/Audio uses miniaudio's built-in Web Audio
# backend (same Audio.cpp as desktop; no extra JS glue or linker flags needed).
# ============================================================================
if(WEB)
    if(NOT DEFINED ENV{EMSDK})
        message(FATAL_ERROR "WEB=ON requires the Emscripten environment (source emsdk_env.sh).")
    endif()
    if(NOT DEFINED WEB_BACKEND)
        set(WEB_BACKEND "WebGL")
    endif()
    message(STATUS "TOMS web backend: ${WEB_BACKEND}")

    # WEB_DEBUG=ON embeds DWARF debug info (-g) so Chrome/Edge DevTools (with the
    # "C/C++ DevTools Support (DWARF)" extension) can set breakpoints and inspect
    # locals directly in the .cpp sources. Output goes to web-debug/ (not web/) so
    # it never clobbers the artifacts build_web.sh ships. No optimization flags are
    # set anywhere in this file (emcc defaults to -O0), so -g doesn't change codegen.
    option(WEB_DEBUG "Emit DWARF debug info for browser DevTools source debugging" OFF)

    add_executable(toms_web
        ${ENGINE_DIR}/emscripten_main.cpp
        ${GAME_CORE}/game.cpp
        ${ENGINE_DIR}/renderer_webgl.cpp
        ${ENGINE_DIR}/font.cpp
        ${ENGINE_DIR}/node.cpp
        ${GAME_SAVE}/save_system.cpp ${ENGINE_DIR}/condition.cpp ${ENGINE_DIR}/entity_status.cpp
        ${ENGINE_DIR}/encounter.cpp ${GAME_SYSTEMS}/mission_system.cpp
        ${ENGINE_DIR}/power_bar.cpp ${GAME_SYSTEMS}/equipment_system.cpp ${GAME_SYSTEMS}/skill_system.cpp
        ${GAME_SYSTEMS}/forge_system.cpp
        ${GAME_SYSTEMS}/hub_system.cpp
        ${GAME_SYSTEMS}/active_system.cpp
        ${GAME_SYSTEMS}/ending_system.cpp
        ${GAME_SYSTEMS}/cycle_system.cpp
        ${GAME_AUDIO}/Audio.cpp
        # Title phase (New Game / Continue / Settings): slots + preferences + string table.
        ${GAME_SAVE}/save_slots.cpp ${GAME_SAVE}/game_settings.cpp
        ${GAME_UI}/localization.cpp ${GAME_UI}/title_screen.cpp
        ${GAME_SYSTEMS}/camera.cpp
        # S1: 佔格 footprints (footprint.h) + the floor-wanderer AI (roamer.h/.cpp) -- own files,
        # like camera.cpp above.
        ${GAME_SYSTEMS}/roamer.cpp
        # S3: per-run story state (choices/counters/side stories/flags/shards) -- its own class, see
        # run_state.h; persisted as RunSaveData schemaVersion 3.
        ${GAME_SYSTEMS}/run_state.cpp
        ${GAME_SYSTEMS}/floor_table.cpp
        # Refactor (2026-09-13): game.cpp was a single 3,100-line translation unit; it is now split
        # by responsibility into these units (game.h still declares everything; Game is unchanged
        # as a class, and shared file-local helpers moved to game_helpers.h).
        ${GAME_CORE}/game_assets.cpp ${GAME_CORE}/game_text_draw.cpp
        ${GAME_CORE}/game_scene_draw.cpp ${GAME_CORE}/game_input.cpp
        ${GAME_CORE}/game_inventory.cpp ${GAME_CORE}/game_store.cpp
        ${GAME_CORE}/game_combat.cpp ${GAME_CORE}/game_story.cpp
        ${GAME_CORE}/game_title_glue.cpp
        # M2: Dear ImGui on the browser build. The desktop plumbing (imgui_layer.cpp) is
        # GLFW+Vulkan and does not exist here, so the web build gets the ImGui core plus the
        # OpenGL3 backend in its ES3 flavour (the WebGL2 context) and its own event bridge
        # (src/engine/imgui_web.cpp) -- see imgui_web.h for the scope note.
        ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${ENGINE_DIR}/imgui_web.cpp)
    if(WEB_BACKEND STREQUAL "WebGPU")
        target_sources(toms_web PRIVATE ${ENGINE_DIR}/renderer_webgpu.cpp)
        target_compile_definitions(toms_web PRIVATE WEBGPU)
        target_compile_options(toms_web PRIVATE --use-port=emdawnwebgpu)
    endif()

    target_include_directories(toms_web PRIVATE
        ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${STB_DIR} ${JSON_DIR} ${CMAKE_SOURCE_DIR}/external/miniaudio
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
        $ENV{GLM_DIR} $ENV{HOME}/opt/glm)

    if(WEB_DEBUG)
        target_compile_options(toms_web PRIVATE -g)
    endif()

    if(NOT WEB_BACKEND STREQUAL "WebGPU")
        # imgui_impl_opengl3 has no loader here: with IMGUI_IMPL_OPENGL_ES3 it calls the ES3
        # entry points directly, which Emscripten's WebGL2 context provides (no GLAD/GLEW).
        target_compile_definitions(toms_web PRIVATE IMGUI_IMPL_OPENGL_ES3)
        target_sources(toms_web PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    endif()

    # Web renders text via an offscreen Canvas 2D context using the browser's own system
    # fonts (Font::buildFromCanvas, src/engine/font.cpp) instead of stb_truetype + bundled
    # TTFs, so none of assets/fonts/ or the (gitignored, desktop-only) wqy-zenhei.ttc need
    # to ship in the .data bundle -- exclude them explicitly since --preload-file below
    # otherwise wildcards the whole assets/ directory.
    set(WEB_FONT_EXCLUDES "--exclude-file ${CMAKE_SOURCE_DIR}/assets/fonts/* \
--exclude-file ${CMAKE_SOURCE_DIR}/assets/wqy-zenhei.ttc")
    if(WEB_BACKEND STREQUAL "WebGPU")
        set(WEB_LINK "--use-port=emdawnwebgpu -sFETCH=1 -sALLOW_MEMORY_GROWTH=1 \
-lidbfs.js \
-sNO_DISABLE_EXCEPTION_CATCHING=1 \
-sEXPORTED_FUNCTIONS=['_main','_downloadFile','_jsRefreshSlots'] \
-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS'] \
--preload-file ${CMAKE_SOURCE_DIR}/assets@assets \
--preload-file ${CMAKE_SOURCE_DIR}/data@data \
${WEB_FONT_EXCLUDES}")
    else()
        set(WEB_LINK "-sUSE_WEBGL2=1 -sFETCH=1 -sALLOW_MEMORY_GROWTH=1 \
-lidbfs.js \
-sNO_DISABLE_EXCEPTION_CATCHING=1 \
-sEXPORTED_FUNCTIONS=['_main','_downloadFile','_jsRefreshSlots'] \
-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS'] \
--preload-file ${CMAKE_SOURCE_DIR}/assets@assets \
--preload-file ${CMAKE_SOURCE_DIR}/data@data \
${WEB_FONT_EXCLUDES} \
-sGL_ASSERTIONS=0")
    endif()

    if(WEB_DEBUG)
        set(WEB_LINK "${WEB_LINK} -g")
    endif()

    set_target_properties(toms_web PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "${WEB_LINK}")
    if(WEB_DEBUG)
        set_target_properties(toms_web PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/web-debug)
    else()
        set_target_properties(toms_web PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/web)
    endif()
endif()

# ---------------------------------------------------------------------------
# Headless unit tests for the Node scene-graph + Object base + Texture (Vulkan).
# Run: ./node_test  ./object_test  ./log_test  ./texture_test
# ---------------------------------------------------------------------------
add_executable(node_test ${ENGINE_DIR}/node_test.cpp ${ENGINE_DIR}/node.cpp ${ENGINE_DIR}/node.h)
target_include_directories(node_test PRIVATE ${ENGINE_DIR} $ENV{GLM_DIR} $ENV{HOME}/opt/glm)

add_executable(object_test ${ENGINE_DIR}/object_test.cpp ${ENGINE_DIR}/node.cpp ${ENGINE_DIR}/node.h ${ENGINE_DIR}/scene.h ${ENGINE_DIR}/object.h)
target_include_directories(object_test PRIVATE ${ENGINE_DIR} $ENV{GLM_DIR} $ENV{HOME}/opt/glm)

add_executable(log_test ${ENGINE_DIR}/log_test.cpp ${ENGINE_DIR}/log.h)
target_include_directories(log_test PRIVATE ${ENGINE_DIR})

# texture_test needs a real Vulkan device (lavapipe) + stb_image + the renderer.
# Vulkan headers are unavailable under the Emscripten (WEB) build, so skip it there.
if(NOT WEB)
add_executable(texture_test ${ENGINE_DIR}/texture_test.cpp ${ENGINE_DIR}/texture.cpp ${ENGINE_DIR}/renderer.cpp
    ${ENGINE_DIR}/node.cpp ${ENGINE_DIR}/vk_util.h ${ENGINE_DIR}/render_iface.h)
target_include_directories(texture_test PRIVATE
    ${GAME_INCLUDE_DIRS} ${ENGINE_DIR}
    ${Vulkan_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIR}
    ${STB_DIR} ${JSON_DIR} ${CMAKE_SOURCE_DIR}/external/miniaudio
    ${glfw_SOURCE_DIR}/include               # renderer.cpp includes GLFW/glfw3.h
    $ENV{GLM_DIR} $ENV{HOME}/opt/glm)
target_link_libraries(texture_test PRIVATE ${Vulkan_LIBRARIES} glfw)
endif()

# run_state_test: S3 -- per-run story state, the four new condition leaves, and the
# schemaVersion 3 save round trip including the documented pre-v3 migration.
add_executable(run_state_test ${GAME_SYSTEMS}/run_state_test.cpp ${GAME_SYSTEMS}/run_state.cpp
        ${GAME_SYSTEMS}/floor_table.cpp
    ${GAME_SAVE}/save_system.cpp ${ENGINE_DIR}/condition.cpp)
target_include_directories(run_state_test PRIVATE
    ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${STB_DIR} ${JSON_DIR} $ENV{GLM_DIR} $ENV{HOME}/opt/glm)

# floor_table_test: S3.5 (a)/(b) -- the 70-floor tower as ordered data. Pure logic (chain ordering,
# prev/next, dangling links, cycles) plus a validator over the shipped data/story/floors set: F01..F70
# in chain order, ten acts of seven, one boss floor per act naming a map that exists, and every grid's
# own exit agreeing with the table's nextFloor.
add_executable(floor_table_test ${GAME_SYSTEMS}/floor_table_test.cpp ${GAME_SYSTEMS}/floor_table.cpp)

# ui_layout_test: S3.6 ("C") -- the design-size/scale-aware layout primitive the pad, dialogue and
# battle screens are being moved onto. Pure arithmetic + a measurer callback, so it runs headless.
add_executable(ui_layout_test ${GAME_UI}/ui_layout_test.cpp)

# node_transform_test: the scene-graph transform chain (parent-composed world transform + its
# inverse), which is what a node-based UI scale depends on. Header-only, runs headless.
add_executable(node_transform_test ${ENGINE_DIR}/node_transform_test.cpp ${ENGINE_DIR}/node.cpp)
target_include_directories(node_transform_test PRIVATE ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${JSON_DIR} $ENV{GLM_DIR} $ENV{HOME}/opt/glm)
target_include_directories(ui_layout_test PRIVATE ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${JSON_DIR})
target_include_directories(floor_table_test PRIVATE
    ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${STB_DIR} ${JSON_DIR} $ENV{GLM_DIR} $ENV{HOME}/opt/glm)

# footprint_test: S1 (佔格 footprints + roamers). Pure logic + a validator over data/stages/*.json
# and data/footprints.json -- no Vulkan/renderer dependency, so it runs everywhere, and it is the
# gate a future floor file has to pass (legal sizes, no entity on a wall, no overlap, stairs still
# reachable with the bigger blockers).
add_executable(footprint_test ${GAME_SYSTEMS}/footprint_test.cpp ${GAME_SYSTEMS}/roamer.cpp
    ${GAME_UI}/localization.cpp)
target_include_directories(footprint_test PRIVATE
    ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${STB_DIR} ${JSON_DIR} $ENV{GLM_DIR} $ENV{HOME}/opt/glm)

# font_test: build the runtime TTF atlas (stb_truetype) and verify glyph UVs.
# No Vulkan device needed — pure CPU atlas rasterization + Object base.
add_executable(font_test ${ENGINE_DIR}/font_test.cpp ${ENGINE_DIR}/font.cpp ${ENGINE_DIR}/object.h)
target_include_directories(font_test PRIVATE
    ${ENGINE_DIR} ${STB_DIR} ${JSON_DIR} $ENV{GLM_DIR} $ENV{HOME}/opt/glm)

# ---------------------------------------------------------------------------
# Milestone 1 scaffolding tests (see docs/architecture/IMPLEMENTATION_ROADMAP.md): Game State Machine,
# Event Bus, Meta/Run save schema. All three are pure-STL / header-plus-json, no
# Vulkan/GLFW dependency, so they build the same way on every platform.
# Run: ./state_machine_test  ./event_bus_test  ./save_test
# ---------------------------------------------------------------------------
add_executable(state_machine_test ${ENGINE_DIR}/state_machine_test.cpp ${ENGINE_DIR}/game_state.h)
target_include_directories(state_machine_test PRIVATE ${ENGINE_DIR})

add_executable(event_bus_test ${ENGINE_DIR}/event_bus_test.cpp ${ENGINE_DIR}/event_bus.h)
target_include_directories(event_bus_test PRIVATE ${ENGINE_DIR})

add_executable(save_test ${GAME_SAVE}/save_test.cpp ${GAME_SAVE}/save_system.cpp ${GAME_SAVE}/save_system.h)
target_include_directories(save_test PRIVATE ${GAME_INCLUDE_DIRS} ${JSON_DIR})

# ---------------------------------------------------------------------------
# Milestone 3 scaffolding tests (see docs/architecture/IMPLEMENTATION_ROADMAP.md): Condition/Flag
# Evaluator, Entity Status System, Story Controller. All pure-STL / header-plus-json, no
# Vulkan/GLFW dependency.
# Run: ./condition_eval_test  ./entity_status_test  ./story_controller_test
# ---------------------------------------------------------------------------
add_executable(condition_eval_test ${ENGINE_DIR}/condition_eval_test.cpp ${ENGINE_DIR}/condition.cpp ${ENGINE_DIR}/condition.h)
target_include_directories(condition_eval_test PRIVATE ${ENGINE_DIR} ${JSON_DIR})

add_executable(entity_status_test ${ENGINE_DIR}/entity_status_test.cpp ${ENGINE_DIR}/entity_status.cpp ${ENGINE_DIR}/entity_status.h)
target_include_directories(entity_status_test PRIVATE ${ENGINE_DIR})

add_executable(story_controller_test ${GAME_SYSTEMS}/story_controller_test.cpp ${GAME_SAVE}/save_system.cpp ${GAME_SYSTEMS}/story_controller.h)
target_include_directories(story_controller_test PRIVATE ${GAME_INCLUDE_DIRS} ${JSON_DIR})

# ---------------------------------------------------------------------------
# Milestone 4 scaffolding tests (see docs/architecture/IMPLEMENTATION_ROADMAP.md): Encounter Resolution,
# Mission System. Pure-STL / header-plus-json, no Vulkan/GLFW dependency.
# Run: ./encounter_resolve_test  ./mission_test
# ---------------------------------------------------------------------------
add_executable(encounter_resolve_test ${ENGINE_DIR}/encounter_resolve_test.cpp ${ENGINE_DIR}/encounter.cpp ${ENGINE_DIR}/encounter.h)
target_include_directories(encounter_resolve_test PRIVATE ${ENGINE_DIR})

add_executable(mission_test ${GAME_SYSTEMS}/mission_test.cpp ${GAME_SYSTEMS}/mission_system.cpp ${GAME_SYSTEMS}/mission_system.h)
target_include_directories(mission_test PRIVATE ${GAME_INCLUDE_DIRS} ${JSON_DIR})

# ---------------------------------------------------------------------------
# Milestone 6 scaffolding tests (see docs/architecture/IMPLEMENTATION_ROADMAP.md): Power Bar minigame math,
# Equipment System. Pure-STL / header-plus-json, no Vulkan/GLFW dependency.
# Run: ./power_bar_test  ./equipment_test
# ---------------------------------------------------------------------------
add_executable(camera_test ${GAME_SYSTEMS}/camera_test.cpp ${GAME_SYSTEMS}/camera.cpp ${GAME_SYSTEMS}/camera.h)
target_include_directories(camera_test PRIVATE ${GAME_INCLUDE_DIRS})

add_executable(power_bar_test ${ENGINE_DIR}/power_bar_test.cpp ${ENGINE_DIR}/power_bar.cpp ${ENGINE_DIR}/power_bar.h)
target_include_directories(power_bar_test PRIVATE ${ENGINE_DIR})

add_executable(equipment_test ${GAME_SYSTEMS}/equipment_test.cpp ${GAME_SYSTEMS}/equipment_system.cpp ${ENGINE_DIR}/power_bar.cpp)
target_include_directories(equipment_test PRIVATE ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${JSON_DIR})

# S4: the skill tree's pure content/math (skill_system.h/.cpp), same shape/reasoning as the
# Equipment System above. Run: ./skill_system_test
add_executable(skill_system_test ${GAME_SYSTEMS}/skill_system_test.cpp ${GAME_SYSTEMS}/skill_system.cpp ${GAME_SAVE}/save_system.cpp)
target_include_directories(skill_system_test PRIVATE ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${JSON_DIR})

# S5: forging's pure content/math (forge_system.h/.cpp), same shape as the Equipment/Skill
# Systems above. Run: ./forge_system_test
add_executable(forge_system_test ${GAME_SYSTEMS}/forge_system_test.cpp ${GAME_SYSTEMS}/forge_system.cpp ${GAME_SAVE}/save_system.cpp)
target_include_directories(forge_system_test PRIVATE ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${JSON_DIR})

# S6: the village hub's pure content (hub_system.h/.cpp), same shape as the Equipment/Skill/
# Forge Systems above. Run: ./hub_system_test
add_executable(hub_system_test ${GAME_SYSTEMS}/hub_system_test.cpp ${GAME_SYSTEMS}/hub_system.cpp ${GAME_SAVE}/save_system.cpp)
target_include_directories(hub_system_test PRIVATE ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${JSON_DIR})

# S7 (first slice): equipment actives' pure content (active_system.h/.cpp), same shape as the
# Equipment/Skill/Forge/Hub Systems above. Run: ./active_system_test
add_executable(active_system_test ${GAME_SYSTEMS}/active_system_test.cpp ${GAME_SYSTEMS}/active_system.cpp ${GAME_SAVE}/save_system.cpp)
target_include_directories(active_system_test PRIVATE ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${JSON_DIR})

# M7 (first slice): the 15-ending resolver's pure content/math (ending_system.h/.cpp), built
# on top of the shared Condition Evaluator (src/engine/condition.cpp). Run: ./ending_system_test
add_executable(ending_system_test ${GAME_SYSTEMS}/ending_system_test.cpp ${GAME_SYSTEMS}/ending_system.cpp
    ${GAME_SAVE}/save_system.cpp ${ENGINE_DIR}/condition.cpp)
target_include_directories(ending_system_test PRIVATE ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${JSON_DIR})

# M8 (first slice): the rebirth config loader's pure content (cycle_system.h/.cpp), same shape
# as the Skill/Forge/Hub/Active/Ending Systems above. Run: ./cycle_system_test
add_executable(cycle_system_test ${GAME_SYSTEMS}/cycle_system_test.cpp ${GAME_SYSTEMS}/cycle_system.cpp ${GAME_SAVE}/save_system.cpp)
target_include_directories(cycle_system_test PRIVATE ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${JSON_DIR})

# ---------------------------------------------------------------------------
# Title phase test (see docs/design/TITLE_PHASE.md): the New Game / Continue / Settings page state
# machine, save-slot files, persisted settings, and the string table. Pure game logic + file
# I/O -- no Vulkan/GLFW, so it builds and runs anywhere.
# Run: ./title_screen_test
# ---------------------------------------------------------------------------
add_executable(title_screen_test
    ${GAME_UI}/title_screen_test.cpp
    ${GAME_UI}/title_screen.cpp ${GAME_SAVE}/save_slots.cpp
    ${GAME_SAVE}/save_system.cpp ${GAME_SAVE}/game_settings.cpp ${GAME_UI}/localization.cpp)
target_include_directories(title_screen_test PRIVATE ${GAME_INCLUDE_DIRS} ${ENGINE_DIR} ${JSON_DIR})
