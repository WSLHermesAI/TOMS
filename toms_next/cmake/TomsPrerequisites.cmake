# TomsPrerequisites.cmake -- configure-time checks with download links.
#
# Every problem is recorded with toms_prereq_problem(). ERRORs stop the configure (after a popup);
# WARNINGs only disable the part that needs the missing thing (e.g. no Qt -> no editor).
# The same checks, plus a few more, are in tools/check_env.ps1, which can be run before
# opening Visual Studio at all.

set_property(GLOBAL PROPERTY TOMS_PREREQ_ERRORS "")
set_property(GLOBAL PROPERTY TOMS_PREREQ_WARNINGS "")
set_property(GLOBAL PROPERTY TOMS_PREREQ_LINKS "")

# toms_prereq_problem(ERROR|WARNING "<what is wrong and what to do>" "<download link or search keyword>")
function(toms_prereq_problem level text link)
    set(entry "${text}\n    -> ${link}")
    if(level STREQUAL "ERROR")
        set_property(GLOBAL APPEND PROPERTY TOMS_PREREQ_ERRORS "${entry}")
    else()
        set_property(GLOBAL APPEND PROPERTY TOMS_PREREQ_WARNINGS "${entry}")
    endif()
    if(link MATCHES "^https?://")
        set_property(GLOBAL APPEND PROPERTY TOMS_PREREQ_LINKS "${link}")
    endif()
endfunction()

# Shows one Windows message box listing every problem, at most once per distinct set of problems
# (the hash is cached), so re-configuring in Visual Studio does not pop it up again and again.
function(_toms_prereq_popup title body)
    if(NOT WIN32 OR NOT TOMS_POPUP_WARNINGS)
        return()
    endif()
    string(SHA1 _hash "${title}${body}")
    if(DEFINED CACHE{TOMS_PREREQ_POPUP_HASH} AND TOMS_PREREQ_POPUP_HASH STREQUAL _hash)
        return()
    endif()
    set(TOMS_PREREQ_POPUP_HASH "${_hash}" CACHE INTERNAL "")
    get_property(_links GLOBAL PROPERTY TOMS_PREREQ_LINKS)
    set(_msg_file  "${CMAKE_BINARY_DIR}/toms_prereq_message.txt")
    set(_link_file "${CMAKE_BINARY_DIR}/toms_prereq_links.txt")
    file(WRITE "${_msg_file}" "${body}")
    string(REPLACE ";" "\n" _links_text "${_links}")
    file(WRITE "${_link_file}" "${_links_text}")
    find_program(_toms_powershell NAMES powershell.exe pwsh.exe)
    if(_toms_powershell)
        execute_process(COMMAND "${_toms_powershell}" -NoProfile -ExecutionPolicy Bypass
                                -File "${CMAKE_CURRENT_LIST_DIR}/../tools/show_message.ps1"
                                -Title "${title}" -MessageFile "${_msg_file}" -LinkFile "${_link_file}"
                        TIMEOUT 600)
    endif()
endfunction()

function(toms_prerequisites_report)
    get_property(_errors   GLOBAL PROPERTY TOMS_PREREQ_ERRORS)
    get_property(_warnings GLOBAL PROPERTY TOMS_PREREQ_WARNINGS)
    set(_body "")
    if(_errors)
        string(APPEND _body "REQUIRED (the build cannot continue):\n")
        foreach(e IN LISTS _errors)
            string(APPEND _body "  * ${e}\n")
        endforeach()
        string(APPEND _body "\n")
    endif()
    if(_warnings)
        string(APPEND _body "OPTIONAL (that part is skipped):\n")
        foreach(w IN LISTS _warnings)
            string(APPEND _body "  * ${w}\n")
        endforeach()
    endif()
    if(_body STREQUAL "")
        message(STATUS "[toms] prerequisites: all OK")
        return()
    endif()
    string(APPEND _body "\nFull install guide: toms_next/docs/02_INSTALL_WINDOWS.md\n"
                        "Check again at any time: toms_next/tools/check_env.cmd")
    if(_errors)
        _toms_prereq_popup("TOMS: required tools are missing" "${_body}")
        message(FATAL_ERROR "[toms] prerequisites missing:\n${_body}")
    else()
        _toms_prereq_popup("TOMS: optional tools are missing" "${_body}")
        message(WARNING "[toms] prerequisites:\n${_body}")
    endif()
endfunction()

# Stops right away if any ERROR has been recorded (used before FetchContent needs git + network).
function(toms_prerequisites_stop_on_error)
    get_property(_errors GLOBAL PROPERTY TOMS_PREREQ_ERRORS)
    if(_errors)
        toms_prerequisites_report()
    endif()
endfunction()

# ============================================================================================
# 1. Platform and compiler
# ============================================================================================
if(WEB)
    # Web build (Emscripten/wasm32): a supported target, not a Windows build on the wrong host. The
    # Windows/Visual Studio checks below are skipped on purpose: wasm32 has 32-bit pointers and the
    # Emscripten toolchain file supplies the compiler. (A STATUS line, not a warning: nothing is wrong.)
    message(STATUS "[toms] web build (Emscripten ${EMSCRIPTEN_VERSION}): desktop compiler checks skipped")
    if(NOT EMSCRIPTEN)
        toms_prereq_problem(ERROR
            "WEB=ON but the Emscripten toolchain is not active. Install emsdk, run emsdk_env, and use a web preset (or tools\\build_web.cmd), which passes the Emscripten toolchain file."
            "https://emscripten.org/docs/getting_started/downloads.html")
    elseif(DEFINED EMSCRIPTEN_VERSION AND EMSCRIPTEN_VERSION VERSION_LESS 3.1.60)
        toms_prereq_problem(WARNING
            "Emscripten ${EMSCRIPTEN_VERSION} is old; SDL3 and bgfx are tested here with 6.0.x. Update with: emsdk install latest && emsdk activate latest."
            "https://emscripten.org/docs/getting_started/downloads.html")
    endif()
elseif(NOT WIN32)
    toms_prereq_problem(WARNING
        "This project is set up and tested for Windows + Visual Studio. Other platforms are not configured yet."
        "docs/02_INSTALL_WINDOWS.md")
endif()
if(WIN32 AND NOT MSVC)
    toms_prereq_problem(ERROR
        "The C++ compiler is not MSVC (found: ${CMAKE_CXX_COMPILER_ID}). Open the folder in Visual Studio, or use a 'x64 Native Tools Command Prompt'. Install the workload 'Desktop development with C++'."
        "https://visualstudio.microsoft.com/downloads/")
endif()
if(NOT WEB AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    toms_prereq_problem(ERROR
        "A 32-bit compiler is active. Select the x64 configuration (the presets in CMakePresets.json do this)."
        "docs/03_VISUAL_STUDIO.md")
endif()
if(NOT WEB AND MSVC AND MSVC_VERSION LESS 1930)
    toms_prereq_problem(ERROR
        "MSVC ${MSVC_VERSION} is too old. Visual Studio 2022 (17.x) or newer is required."
        "https://visualstudio.microsoft.com/downloads/")
endif()

# ============================================================================================
# 2. Git (FetchContent clones bgfx, SDL3 and ImGui on the first configure)
# ============================================================================================
find_package(Git QUIET)
if(NOT GIT_FOUND)
    toms_prereq_problem(ERROR
        "Git was not found. It is needed to download bgfx, SDL3 and Dear ImGui on the first configure."
        "https://git-scm.com/download/win")
endif()

# ============================================================================================
# 3. The original TOMS project (game code, assets, data)
# ============================================================================================
foreach(_need
        "src/game/core/game.h"
        "src/engine/render_iface.h"
        "assets/sprites"
        "assets/fonts"
        "data/stages"
        "external/json/json.hpp"
        "external/stb/stb_image.h"
        "external/miniaudio/miniaudio.h")
    if(NOT EXISTS "${TOMS_LEGACY_ROOT}/${_need}")
        toms_prereq_problem(ERROR
            "Missing '${_need}' under TOMS_LEGACY_ROOT=${TOMS_LEGACY_ROOT}. toms_next must stay inside the TOMS checkout (or set -DTOMS_LEGACY_ROOT=<path>)."
            "git clone https://github.com/WSLHermesAI/TOMS")
    endif()
endforeach()

toms_prerequisites_stop_on_error()

# ============================================================================================
# 4. CJK font (gitignored in the old repo, so a fresh clone does not have it)
# ============================================================================================
# The web build does not need it: it draws glyphs with the browser's own fonts (Canvas 2D).
set(TOMS_CJK_FONT "${TOMS_LEGACY_ROOT}/assets/wqy-zenhei.ttc")
if(NOT WEB AND NOT EXISTS "${TOMS_CJK_FONT}")
    if(WIN32 AND EXISTS "$ENV{WINDIR}/Fonts/msjh.ttc")
        toms_prereq_problem(WARNING
            "assets/wqy-zenhei.ttc (16 MB, gitignored) is missing. The game falls back to Windows' Microsoft JhengHei (msjh.ttc); glyph shapes differ from the shipped look. To match: download WenQuanYi Zen Hei and copy wqy-zenhei.ttc into TOMS/assets/."
            "https://sourceforge.net/projects/wqy/files/wqy-zenhei/")
    else()
        toms_prereq_problem(WARNING
            "No CJK font: assets/wqy-zenhei.ttc is missing and no Windows fallback font was found. The game will show a popup and exit. Download WenQuanYi Zen Hei and copy wqy-zenhei.ttc into TOMS/assets/."
            "https://sourceforge.net/projects/wqy/files/wqy-zenhei/")
    endif()
endif()

# ============================================================================================
# 5. D3D shader compiler DLL (shaderc uses it to build the Direct3D shaders)
# ============================================================================================
if(WIN32 AND NOT EXISTS "$ENV{WINDIR}/System32/d3dcompiler_47.dll")
    toms_prereq_problem(WARNING
        "d3dcompiler_47.dll was not found in System32; shaderc cannot compile Direct3D shaders. Install the Windows SDK (Visual Studio Installer > Individual components > 'Windows 11 SDK')."
        "https://developer.microsoft.com/windows/downloads/windows-sdk/")
endif()

# ============================================================================================
# 6. Qt 6 for the editor (optional: without it only the game is built)
# ============================================================================================
set(TOMS_HAVE_QT OFF)
if(TOMS_BUILD_EDITOR AND NOT WEB)
    # Help find_package: environment variables first, then the default Qt installer locations.
    foreach(_env QT_ROOT_DIR QTDIR Qt6_DIR)
        if(DEFINED ENV{${_env}})
            list(APPEND CMAKE_PREFIX_PATH "$ENV{${_env}}")
        endif()
    endforeach()
    if(WIN32 AND NOT Qt6_DIR)
        file(GLOB _qt_kits LIST_DIRECTORIES true
             "C:/Qt/6.*/msvc20*_64" "D:/Qt/6.*/msvc20*_64" "$ENV{USERPROFILE}/Qt/6.*/msvc20*_64")
        if(_qt_kits)
            list(SORT _qt_kits COMPARE NATURAL ORDER DESCENDING)   # newest Qt first
            list(GET _qt_kits 0 _qt_kit)
            list(APPEND CMAKE_PREFIX_PATH "${_qt_kit}")
            message(STATUS "[toms] auto-detected Qt kit: ${_qt_kit}")
        endif()
    endif()
    find_package(Qt6 6.5 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        if(WIN32 AND NOT Qt6_DIR MATCHES "msvc")
            toms_prereq_problem(WARNING
                "The Qt kit at ${Qt6_DIR} is not an MSVC build (MinGW kits cannot link with MSVC). In the Qt Maintenance Tool, add 'MSVC 2022 64-bit' for Qt 6.8 LTS or newer. The editor is skipped."
                "https://www.qt.io/download-qt-installer-oss")
        else()
            set(TOMS_HAVE_QT ON)
            message(STATUS "[toms] Qt ${Qt6_VERSION} found: ${Qt6_DIR} -> the editor will be built")
        endif()
    else()
        toms_prereq_problem(WARNING
            "Qt 6 (MSVC 2022 64-bit kit, 6.5 or newer; 6.8 LTS recommended) was not found, so toms_editor is skipped. Install it with the Qt Online Installer to C:/Qt (auto-detected), or set the environment variable QTDIR to the kit folder, e.g. C:/Qt/6.8.3/msvc2022_64."
            "https://www.qt.io/download-qt-installer-oss")
    endif()
endif()

# ============================================================================================
# 7. Web only: a shader compiler that runs on THIS machine
# ============================================================================================
# shaderc is a build tool: for a web build it must run on the build machine, not in the browser.
# The desktop build already produced one (out/build/<preset>/bin/shaderc.exe), so the web build
# uses it instead of compiling glslang, tint, spirv-cross and shaderc itself to wasm and running
# them under node (slow, and it runs out of memory at full parallelism).
if(WEB)
    set(TOMS_HOST_SHADERC "" CACHE FILEPATH "shaderc built for the build machine (used by web builds)")
    if(NOT TOMS_HOST_SHADERC)
        file(GLOB _shaderc_candidates
             "${CMAKE_CURRENT_LIST_DIR}/../out/build/*/bin/shaderc.exe"
             "${CMAKE_CURRENT_LIST_DIR}/../out/build/*/bin/shaderc")
        if(_shaderc_candidates)
            list(GET _shaderc_candidates 0 _shaderc)
            get_filename_component(_shaderc "${_shaderc}" ABSOLUTE)
            set(TOMS_HOST_SHADERC "${_shaderc}" CACHE FILEPATH "shaderc built for the build machine (used by web builds)" FORCE)
        endif()
    endif()
    if(TOMS_HOST_SHADERC AND EXISTS "${TOMS_HOST_SHADERC}")
        message(STATUS "[toms] web build uses the host shader compiler: ${TOMS_HOST_SHADERC}")
    else()
        set(TOMS_HOST_SHADERC "" CACHE FILEPATH "" FORCE)
        toms_prereq_problem(WARNING
            "No host shaderc found, so this web build compiles shaderc itself to wasm and runs it with node. That is much slower and needs a lot of RAM (build with few jobs). Faster: build any desktop preset once (tools\build.cmd windows-shipping) or pass -DTOMS_HOST_SHADERC=<path to shaderc>."
            "toms_next/docs/06_BUILD_WEB.md")
    endif()
endif()
