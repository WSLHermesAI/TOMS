# TomsDependencies.cmake -- third-party libraries, fetched from GitHub at configure time.
#
# The first configure downloads and builds bgfx (+ bx, bimg, shaderc), SDL3 and Dear ImGui.
# That needs git and internet access, and takes a few minutes. Versions are pinned so every
# machine builds the same thing.
#
# The SOURCES are downloaded once into TOMS_DEPS_DIR and shared by every preset (windows-debug,
# windows-release, web, android ...); only the compiled output stays per preset in
# <build>/_deps. After a library has been downloaded at its pinned tag, <name>.tag next to it
# records that tag, and every later configure -- any preset -- uses the folder as it is
# (FETCHCONTENT_SOURCE_DIR_<NAME>) without running git on it. Changing a GIT_TAG below downloads
# that library again, once.
#
# A lock serializes the downloads: Visual Studio reconfigures its preset on its own whenever a
# CMake file changes, and two configures cloning into the same folder at once would wreck it.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(TOMS_DEPS_DIR "${CMAKE_SOURCE_DIR}/Build/_deps-src" CACHE PATH
    "Downloaded third-party sources, shared by every preset (outside the repo to share across clones)")
file(MAKE_DIRECTORY "${TOMS_DEPS_DIR}")
file(LOCK "${TOMS_DEPS_DIR}/.lock" GUARD FILE TIMEOUT 1800 RESULT_VARIABLE _toms_deps_lock)
if(_toms_deps_lock)
    message(FATAL_ERROR "[toms] could not lock ${TOMS_DEPS_DIR}/.lock: ${_toms_deps_lock}")
endif()

# toms_declare_dep(<name> <git repository> <tag> [more FetchContent_Declare arguments])
function(toms_declare_dep name repo tag)
    set(dir "${TOMS_DEPS_DIR}/${name}")
    string(TOUPPER "${name}" up)
    set(have "")
    if(EXISTS "${TOMS_DEPS_DIR}/${name}.tag")
        file(READ "${TOMS_DEPS_DIR}/${name}.tag" have)
        string(STRIP "${have}" have)
    endif()
    if(have STREQUAL tag AND EXISTS "${dir}")
        # Already downloaded at this tag: use it as it is (no git, no network).
        set(FETCHCONTENT_SOURCE_DIR_${up} "${dir}" PARENT_SCOPE)
    endif()
    FetchContent_Declare(${name}
        GIT_REPOSITORY ${repo}
        GIT_TAG        ${tag}
        GIT_SHALLOW    TRUE
        SOURCE_DIR     "${dir}"
        ${ARGN})
    set_property(GLOBAL PROPERTY TOMS_DEP_TAG_${name} "${tag}")
endfunction()

# After FetchContent_MakeAvailable: remember the tag each library now has on disk.
function(toms_deps_fetched)
    foreach(name IN LISTS ARGN)
        get_property(tag GLOBAL PROPERTY TOMS_DEP_TAG_${name})
        file(WRITE "${TOMS_DEPS_DIR}/${name}.tag" "${tag}\n")
    endforeach()
endfunction()

# ---- bgfx (through bgfx.cmake, which also builds the shaderc tool) ----
set(BGFX_BUILD_EXAMPLES        OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLE_COMMON  OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS           OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL               OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS        OFF CACHE BOOL "" FORCE)
if((WEB OR ANDROID) AND TOMS_HOST_SHADERC)
    # Web/Android build with a host shaderc (cmake/TomsPrerequisites.cmake found one): build no bgfx
    # tools for wasm/Android at all (an Android shaderc could not run here), and point bgfx::shaderc at
    # the host executable. It must exist before bgfx.cmake
    # is processed, because bgfx.cmake only defines bgfx_compile_shaders() if bgfx::shaderc exists.
    add_executable(bgfx::shaderc IMPORTED GLOBAL)
    set_target_properties(bgfx::shaderc PROPERTIES IMPORTED_LOCATION "${TOMS_HOST_SHADERC}")
    set(_toms_bgfx_tools OFF)
else()
    set(_toms_bgfx_tools ON)   # desktop, or web without a host shaderc (shaderc.js under node)
endif()
set(BGFX_BUILD_TOOLS           ${_toms_bgfx_tools} CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER    ${_toms_bgfx_tools} CACHE BOOL "" FORCE)   # shaderc: compiles shaders/*.sc
set(BGFX_BUILD_TOOLS_BIN2C     OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY  OFF CACHE BOOL "" FORCE)   # geometryc: turn on for 3D models
set(BGFX_BUILD_TOOLS_TEXTURE   OFF CACHE BOOL "" FORCE)   # texturec: turn on for KTX/DDS textures
toms_declare_dep(bgfx https://github.com/bkaradzic/bgfx.cmake.git v1.161.9510-579 GIT_PROGRESS   TRUE)

# ---- SDL3 (window, input and the main loop of the game executable) ----
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
toms_declare_dep(SDL3 https://github.com/libsdl-org/SDL.git release-3.4.8 GIT_PROGRESS   TRUE)

# ---- Dear ImGui: the same version the old build pins (the game's debug windows use its API) ----
toms_declare_dep(imgui https://github.com/ocornut/imgui.git v1.90.9)

# ---- glm: node.h needs it. The old build found it through the GLM_DIR environment variable
# (a hand-installed copy); fetching it removes that hidden prerequisite. Header-only.
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
toms_declare_dep(glm https://github.com/g-truc/glm.git 1.0.1)

FetchContent_MakeAvailable(bgfx SDL3 imgui glm)
toms_deps_fetched(bgfx SDL3 imgui glm)

# bgfx fix (Android): an app that comes back from the background gets a NEW surface (ANativeWindow).
# bgfx::reset() passes it on (SwapChain::nwh), but GlContext::resize() rebuilt the EGL surface on the
# window it was created with, so every frame after a resume failed with EGL_BAD_SURFACE. Take the new
# window when there is one. Applied to the fetched source at configure time; checks first, so it is
# written once and a re-configure leaves the file (and the build) alone.
set(_toms_egl "${bgfx_SOURCE_DIR}/bgfx/src/glcontext_egl.cpp")
if(EXISTS "${_toms_egl}")
    file(READ "${_toms_egl}" _toms_egl_src)
    if(NOT _toms_egl_src MATCHES "TOMS: surface recreated on resume")
        set(_toms_egl_old "#\tif BX_PLATFORM_ANDROID\n\t\tif (m_ownsContext\n")
        set(_toms_egl_new "#\tif BX_PLATFORM_ANDROID\n\t\tif (NULL != _swapChain.nwh) { m_nwh = (EGLNativeWindowType)_swapChain.nwh; }   // TOMS: surface recreated on resume\n\t\tif (m_ownsContext\n")
        string(FIND "${_toms_egl_src}" "${_toms_egl_old}" _toms_egl_at)
        if(_toms_egl_at EQUAL -1)
            message(WARNING "[toms] bgfx changed: the Android resume fix in cmake/TomsDependencies.cmake no longer applies (${_toms_egl})")
        else()
            string(REPLACE "${_toms_egl_old}" "${_toms_egl_new}" _toms_egl_src "${_toms_egl_src}")
            file(WRITE "${_toms_egl}" "${_toms_egl_src}")
            message(STATUS "[toms] bgfx: applied the Android resume fix to glcontext_egl.cpp")
        endif()
    endif()
endif()

# ---- RmlUi (the game UI, HTML/CSS-like), docs/08_RMLUI.md ----
# Desktop: FreeType draws the text from font files (assets/media/fonts/NotoSansCJKtc-TOMS.otf by
# default, tools/make_ui_font.py; per-language fonts from text.json). Web: no FreeType and no font
# file -- the browser draws the text (src/engine/src/rml_canvas_font.*).
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)   # RmlUi defaults to shared; we link statically
if(EMSCRIPTEN)
    set(RMLUI_FONT_ENGINE  "none" CACHE STRING "" FORCE)
else()
    foreach(_ft ZLIB BZIP2 PNG HARFBUZZ BROTLI)
        set(FT_DISABLE_${_ft} ON CACHE BOOL "" FORCE) # plain TrueType/OpenType, no extra deps
    endforeach()
    toms_declare_dep(freetype https://github.com/freetype/freetype.git VER-2-14-3)
    FetchContent_MakeAvailable(freetype)
    toms_deps_fetched(freetype)
    # RmlUi only needs the target Freetype::Freetype to exist (its find_package is not REQUIRED).
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    set(RMLUI_FONT_ENGINE  "freetype" CACHE STRING "" FORCE)
endif()

set(RMLUI_SAMPLES              OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING              OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS         OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN           OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN        OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS  OFF CACHE BOOL "" FORCE)
toms_declare_dep(rmlui https://github.com/mikke89/RmlUi.git 6.3)
FetchContent_MakeAvailable(rmlui)
toms_deps_fetched(rmlui)
file(LOCK "${TOMS_DEPS_DIR}/.lock" RELEASE)   # downloads done: other configures may go on
foreach(_t freetype rmlui rmlui_core rmlui_debugger)
    if(TARGET ${_t})
        set_target_properties(${_t} PROPERTIES FOLDER "third_party/rmlui")
    endif()
endforeach()

# ---- shaderc is a BUILD TOOL when it is not imported (the "no host shaderc" fallback) ----------------
# In that case bgfx.cmake builds shaderc for the TARGET (wasm) and CMake runs it through node to compile
# shaders. Two flags are required for that to work, and they must NOT be global:
#   -sNODERAWFS=1        without it the tool runs in Emscripten's virtual FS and cannot open the real
#                        source files it is asked to compile ("Unable to open file .../vs_sprite.sc").
#                        It conflicts with the game's --preload-file, so it belongs on this target only.
#   -sALLOW_MEMORY_GROWTH=1  glslang/tint need more than a fixed heap; without growth a real shader
#                        crashes with "RuntimeError: memory access out of bounds".
# A host (desktop) shaderc -- what the Windows flow builds and imports -- needs neither.
if(EMSCRIPTEN AND TARGET shaderc)
    target_link_options(shaderc PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
    message(STATUS "[toms] shaderc: wasm build tool gets -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1")
endif()

# bgfx.cmake always defines bimg_encode (texture encoders, used only by the texturec tool) and so
# `cmake --build` compiles it. Its etcpak sources use x86-only intrinsics that do not exist in
# wasm, so the web build failed there (2026-09-25, WSL). Nothing we ship encodes textures at
# runtime: keep it out of the default build. A target that really needs it still pulls it in.
if(TARGET bimg_encode)
    set_target_properties(bimg_encode PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

if(TARGET SDL3::SDL3-static)
    set(TOMS_SDL3_TARGET SDL3::SDL3-static)
else()
    set(TOMS_SDL3_TARGET SDL3::SDL3)
endif()

# Keep the Visual Studio solution explorer tidy.
foreach(_t bgfx bx bimg bimg_decode bimg_encode shaderc fcpp glslang glsl-optimizer spirv-opt spirv-cross)
    if(TARGET ${_t})
        set_target_properties(${_t} PROPERTIES FOLDER "third_party/bgfx")
    endif()
endforeach()
