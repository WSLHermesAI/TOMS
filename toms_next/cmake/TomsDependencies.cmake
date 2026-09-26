# TomsDependencies.cmake -- third-party libraries, fetched from GitHub at configure time.
#
# The first configure downloads and builds bgfx (+ bx, bimg, shaderc), SDL3 and Dear ImGui.
# That needs git and internet access, and takes a few minutes. Later configures reuse the
# download in <build>/_deps. Versions are pinned so every machine builds the same thing.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---- bgfx (through bgfx.cmake, which also builds the shaderc tool) ----
set(BGFX_BUILD_EXAMPLES        OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLE_COMMON  OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS           OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL               OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS        OFF CACHE BOOL "" FORCE)
if(WEB AND TOMS_HOST_SHADERC)
    # Web build with a host shaderc (cmake/TomsPrerequisites.cmake found one): build no bgfx tools
    # for wasm at all, and point bgfx::shaderc at the host executable. It must exist before bgfx.cmake
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
FetchContent_Declare(bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG        v1.161.9510-579
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

# ---- SDL3 (window, input and the main loop of the game executable) ----
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.8
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

# ---- Dear ImGui: the same version the old build pins (the game's debug windows use its API) ----
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.9
    GIT_SHALLOW    TRUE)

# ---- glm: node.h needs it. The old build found it through the GLM_DIR environment variable
# (a hand-installed copy); fetching it removes that hidden prerequisite. Header-only.
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(bgfx SDL3 imgui glm)

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
