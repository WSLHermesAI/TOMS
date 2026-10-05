# TOMS (Tower of the Sorcerer) on Qt6 + bgfx.
#
#   toms_game    SDL3 + bgfx game (desktop exe, or html/wasm for the web); what ships, contains no Qt
#   toms_editor  Qt6 editor with an embedded bgfx viewport (desktop only, never shipped)
#
# Layout:
#   src/     all source code: engine/ (bgfx), game/ (entry point, session), editor/ (Qt; stage/ is
#            the old stage editor), core/ (the core game code, compiled unmodified against a
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
set(TOMS_STAGE_EDITOR    "${TOMS_SRC_DIR}/editor/stage")    # the old Qt stage editor sources
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

add_subdirectory(src/engine)   # toms_bgfx: BgfxRenderer (IRenderer), ImGui renderer, bgfx host, shaders
add_subdirectory(src/game)     # toms_core (src/core on bgfx), toms_game_host, toms_game

if(TOMS_BUILD_EDITOR AND TOMS_HAVE_QT)
    add_subdirectory(src/editor)
endif()

# CTest: unit tests, screenshot smoke tests, the web smoke test (tests/CMakeLists.txt, docs/09_TESTS.md).
option(TOMS_BUILD_TESTS "Build the tests (CTest / Visual Studio Test Explorer)" ON)
if(TOMS_BUILD_TESTS AND NOT WEB AND NOT ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()

# The sprite atlas tool (tools/atlas, docs/14_ATLAS_TOOL.md): atlaspack and atlas_editor (Qt).
# The game's atlas project, assets/media/atlas/game.atlasproj, keeps its images inside the packed
# atlas next to it (game.atlas + game.png, one more per art style), so there is nothing to generate
# at build time: the editor writes those files when the project is saved, and they are committed.
if(NOT WEB AND NOT ANDROID)
    set(ATLAS_BUILD_EDITOR ${TOMS_BUILD_EDITOR} CACHE BOOL "Build the Qt atlas editor (tools/atlas/qt)")
    if(NOT TOMS_HAVE_QT)
        set(ATLAS_BUILD_EDITOR OFF)   # TomsPrerequisites found no usable Qt kit
    endif()
    add_subdirectory(tools/atlas)
    # The node animation tooling (docs/15_ANIMATION.md): toms_anim (the runtime without the game)
    # and, with Qt, anim_editor. Shares tools/studio_common with the atlas editor.
    add_subdirectory(tools/anim)
    # Particle effects (docs/17_PARTICLES.md): toms_fx (the runtime without the game) and, with
    # Qt, particle_editor. Shares tools/studio_common too.
    add_subdirectory(tools/particle)
    # 3D models (docs/18_GLTF.md): gltf_viewer, glTF 2.0 on the engine's renderer (skins, morph
    # targets, EXT_mesh_gpu_instancing, PBR), checked by the smoke.gltf_* tests.
    add_subdirectory(tools/gltf_viewer)
    if(TOMS_BUILD_TESTS)
        # The project opens from its packed atlas alone, and its other files (plist, rcss, the
        # art styles' atlases) match what it builds.
        add_test(NAME atlas.assets_up_to_date
                 COMMAND atlaspack build "${TOMS_ASSET_DIR}/atlas/game.atlasproj" --all-variants --check --quiet)
        set_tests_properties(atlas.assets_up_to_date PROPERTIES LABELS "unit;atlas" TIMEOUT 60)
        # The particle sprites (tools/atlas/make_fx_sprites.py, docs/17_PARTICLES.md), the same way.
        add_test(NAME atlas.fx_up_to_date
                 COMMAND atlaspack build "${TOMS_ASSET_DIR}/atlas/fx.atlasproj" --check --quiet)
        set_tests_properties(atlas.fx_up_to_date PROPERTIES LABELS "unit;atlas" TIMEOUT 60)
    endif()
endif()

toms_prerequisites_report()   # prints the summary and shows the popup if anything is missing
