# toms_next -- the Qt6 + bgfx split of TOMS (Tower of the Sorcerer).
#
#   toms_game    SDL3 + bgfx game executable (what ships; contains no Qt)
#   toms_editor  Qt6 editor with an embedded bgfx viewport (desktop only, never shipped)
#
# Phase 1 of the migration (docs/04_MIGRATION_PLAN.md): the existing game code in ../src is
# compiled UNMODIFIED against a bgfx implementation of IRenderer. Nothing in ../ is edited.
cmake_minimum_required(VERSION 3.24)
project(toms_next LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# All executables land in one folder so F5 in Visual Studio finds the DLLs Qt needs next to them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Where the original TOMS project lives. Game code, assets and data are reused from there.
set(TOMS_LEGACY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Root folder of the original TOMS project")
get_filename_component(TOMS_LEGACY_ROOT "${TOMS_LEGACY_ROOT}" ABSOLUTE)

option(TOMS_BUILD_EDITOR    "Build the Qt6 editor (needs Qt 6 built for MSVC 64-bit)" ON)
option(TOMS_POPUP_WARNINGS  "Show a Windows popup when a prerequisite is missing" ON)
option(WEB                  "Build for the browser (Emscripten, bgfx WebGL2); set by the web presets" OFF)
if(WEB)
    set(TOMS_BUILD_EDITOR OFF CACHE BOOL "" FORCE)   # the Qt editor is desktop-only
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(TomsPrerequisites)   # checks everything and records warnings (with download links)
include(TomsDependencies)    # bgfx.cmake, SDL3 and Dear ImGui through FetchContent

# Compiler settings shared by every target this project owns (third-party targets keep theirs).
function(toms_target_defaults target)
    if(MSVC)
        # /utf-8: the game sources contain UTF-8 CJK string literals.
        target_compile_options(${target} PRIVATE /utf-8 /W3 /Zc:__cplusplus)
        target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    endif()
    if(EMSCRIPTEN)
        # The legacy code catches exceptions (nlohmann::json parse errors); Emscripten disables
        # catching by default, which would turn a bad data file into an abort. The old web build
        # enabled it too (-sNO_DISABLE_EXCEPTION_CATCHING).
        target_compile_options(${target} PRIVATE -fexceptions)
        target_link_options(${target} PRIVATE -fexceptions)
    endif()
endfunction()

add_subdirectory(engine)   # toms_bgfx: BgfxRenderer (IRenderer), ImGui renderer, bgfx host, shaders
add_subdirectory(game)     # toms_legacy_game (../src on bgfx), toms_game_host, toms_game.exe

if(TOMS_BUILD_EDITOR AND TOMS_HAVE_QT)
    add_subdirectory(editor)
endif()

toms_prerequisites_report()   # prints the summary and shows the popup if anything is missing
