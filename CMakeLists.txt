# TOMS (Tower of the Sorcerer) on Qt6 + bgfx.
#
#   toms_game    SDL3 + bgfx game (desktop exe, or html/wasm for the web); what ships, contains no Qt
#   the editors  Qt6 tools (ui_editor, battle_editor, stage_event_editor, anim / atlas / particle / font editors,
#                gltf_viewer): desktop only, never shipped
#
# Layout:
#   src/     all source code: engine/ (bgfx), game/ (entry point, session), editor/ (Qt: ui_editor,
#            battle_editor), core/ (the core game code, compiled unmodified against a
#            bgfx IRenderer), third_party/ (vendored headers)
#   assets/  all game content: media/ (sprites, sfx, fonts) and data/ (JSON). The game code reads
#            data as <media>/../data, so the two stay siblings.
cmake_minimum_required(VERSION 3.24)
project(toms LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# All executables land in one folder so F5 in Visual Studio finds the DLLs Qt needs next to them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Where things are. Everything else in the build refers to these.
set(TOMS_SRC_DIR         "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(TOMS_CORE_SRC        "${TOMS_SRC_DIR}/core")            # the core game code: engine/ + game/
set(TOMS_THIRD_PARTY_DIR "${TOMS_SRC_DIR}/third_party")     # json, stb, miniaudio headers
set(TOMS_ASSET_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/assets/media" CACHE PATH "Game media: sprites, sfx, fonts")
set(TOMS_DATA_DIR        "${CMAKE_CURRENT_SOURCE_DIR}/assets/data"  CACHE PATH "Game data (JSON); must be the sibling 'data' of the media folder")

option(TOMS_BUILD_EDITOR    "Build the Qt6 editor (needs Qt 6 built for MSVC 64-bit)" ON)
option(TOMS_POPUP_WARNINGS  "Show a Windows popup when a prerequisite is missing" ON)
option(WEB                  "Build for the browser (Emscripten, bgfx WebGL2); set by the web presets" OFF)
if(WEB)
    set(TOMS_BUILD_EDITOR OFF CACHE BOOL "" FORCE)   # the Qt editor is desktop-only
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(TomsPrerequisites)   # checks everything and records warnings (with download links)
include(TomsDependencies)    # bgfx.cmake, SDL3, Dear ImGui and glm through FetchContent

# Compiler settings shared by every target this project owns (third-party targets keep theirs).
function(toms_target_defaults target)
    if(MSVC)
        # /utf-8: the game sources contain UTF-8 CJK string literals.
        target_compile_options(${target} PRIVATE /utf-8 /W3 /Zc:__cplusplus)
        target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    endif()
    if(EMSCRIPTEN)
        # The core game code catches exceptions (nlohmann::json parse errors); Emscripten disables
        # catching by default, which would turn a bad data file into an abort. The old web build
        # enabled it too (-sNO_DISABLE_EXCEPTION_CATCHING).
        target_compile_options(${target} PRIVATE -fexceptions)
        target_link_options(${target} PRIVATE -fexceptions)
    endif()
endfunction()

# CTest: unit tests, screenshot smoke tests, the web smoke test (tests/CMakeLists.txt, docs/project/08_TESTS.md).
# Declared before any subdirectory: src/editor brings in tools/studio_common, whose tests need it.
option(TOMS_BUILD_TESTS "Build the tests (CTest / Visual Studio Test Explorer)" ON)
if(TOMS_BUILD_TESTS AND NOT WEB AND NOT ANDROID)
    enable_testing()
endif()

add_subdirectory(src/engine)   # toms_bgfx: BgfxRenderer (IRenderer), ImGui renderer, bgfx host, shaders
add_subdirectory(src/game)     # toms_core (src/core on bgfx), toms_game_host, toms_game

if(TOMS_BUILD_EDITOR AND TOMS_HAVE_QT)
    add_subdirectory(src/editor)
endif()

if(TOMS_BUILD_TESTS AND NOT WEB AND NOT ANDROID)
    add_subdirectory(tests)
endif()

# The sprite atlas tool (tools/atlas, docs/engine/04_ATLAS_TOOL.md): atlaspack and atlas_editor (Qt).
# The game's atlas project, art_raw/atlas/game.atlasproj, keeps its images inside the cooked atlas it
# writes to assets/media/atlas (game.atlas + game.png, one more per art style), so there is nothing to
# generate at build time: the editor writes those files when the project is saved, and they are committed.
# art_raw/ holds the raw art (sprite PNGs, the projects); assets/ only what the game loads.
if(NOT WEB AND NOT ANDROID)
    set(ATLAS_BUILD_EDITOR ${TOMS_BUILD_EDITOR} CACHE BOOL "Build the Qt atlas editor (tools/atlas/qt)")
    if(NOT TOMS_HAVE_QT)
        set(ATLAS_BUILD_EDITOR OFF)   # TomsPrerequisites found no usable Qt kit
    endif()
    add_subdirectory(tools/atlas)
    # The node animation tooling (docs/engine/05_ANIMATION.md): toms_anim (the runtime without the game)
    # and, with Qt, anim_editor. Shares tools/studio_common with the atlas editor.
    add_subdirectory(tools/anim)
    # Particle effects (docs/engine/07_PARTICLES.md): toms_fx (the runtime without the game) and, with
    # Qt, particle_editor. Shares tools/studio_common too.
    add_subdirectory(tools/particle)
    # 3D models (docs/engine/08_GLTF.md): gltf_viewer, glTF 2.0 on the engine's renderer (skins, morph
    # targets, EXT_mesh_gpu_instancing, PBR), checked by the smoke.gltf_* tests.
    add_subdirectory(tools/gltf_viewer)
    # Picture fonts (docs/engine/09_FONT_TOOL.md): fontpack and, with Qt, font_editor -- .fnt + .png from
    # one picture per character; the runtime is src/core/engine/bitmap_font.cpp.
    add_subdirectory(tools/font)
    # The stage and event editors as a Windows app (docs/gameplay/02_STAGE_EDITOR.md section 6): stage_core (the
    # rules, no Qt, unit.stage_core_test) and, with Qt, stage_event_editor on the game's renderer.
    add_subdirectory(tools/stage_event_editor)
    if(TOMS_BUILD_TESTS)
        # The project opens from its cooked atlas alone, and the art styles' atlases match what it builds.
        add_test(NAME atlas.assets_up_to_date
                 COMMAND atlaspack build "${CMAKE_SOURCE_DIR}/art_raw/atlas/game.atlasproj" --all-variants --check --quiet)
        set_tests_properties(atlas.assets_up_to_date PROPERTIES LABELS "unit;atlas" TIMEOUT 60)
        # The particle sprites (tools/atlas/make_fx_sprites.py, docs/engine/07_PARTICLES.md), the same way.
        add_test(NAME atlas.fx_up_to_date
                 COMMAND atlaspack build "${CMAKE_SOURCE_DIR}/art_raw/atlas/fx.atlasproj" --check --quiet)
        set_tests_properties(atlas.fx_up_to_date PROPERTIES LABELS "unit;atlas" TIMEOUT 60)
    endif()
endif()
# The glTF viewer in the browser (tools\build_gltf_web.cmd): built only when asked for, not with the game.
if(EMSCRIPTEN)
    add_subdirectory(tools/gltf_viewer)
endif()

toms_prerequisites_report()   # prints the summary and shows the popup if anything is missing
