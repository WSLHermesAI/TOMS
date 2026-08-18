cmake_minimum_required(VERSION 3.16)
project(tower_vulkan CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Paths to vendored third-party headers (self-contained, no system deps)
set(STB_DIR ${CMAKE_SOURCE_DIR}/external/stb)
set(JSON_DIR ${CMAKE_SOURCE_DIR}/external/json)

# Source tree split:
#   src/game/    — gameplay (main, Game, Stage, Audio)
#   src/engine/  — node scene-graph + render + core infra (fonts, textures, shaders, tests)
set(GAME_DIR   ${CMAKE_SOURCE_DIR}/src/game)
set(ENGINE_DIR ${CMAKE_SOURCE_DIR}/src/engine)

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
        ${GAME_DIR}/main.cpp ${GAME_DIR}/game.cpp ${ENGINE_DIR}/renderer.cpp
        ${ENGINE_DIR}/node.cpp ${ENGINE_DIR}/font.cpp
        ${ENGINE_DIR}/render_iface.h ${GAME_DIR}/Audio.cpp ${ENGINE_DIR}/vk_util.h)

    target_include_directories(tower_vulkan PRIVATE
        ${GAME_DIR} ${ENGINE_DIR}
        ${Vulkan_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIR}
        ${STB_DIR} ${JSON_DIR} ${CMAKE_SOURCE_DIR}/external/miniaudio
        $ENV{GLM_DIR} $ENV{HOME}/opt/glm
        ${glfw_SOURCE_DIR}/include)
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
# single packed .data bundle. SFX/Audio is excluded on purpose.
# ============================================================================
if(WEB)
    if(NOT DEFINED ENV{EMSDK})
        message(FATAL_ERROR "WEB=ON requires the Emscripten environment (source emsdk_env.sh).")
    endif()
    if(NOT DEFINED WEB_BACKEND)
        set(WEB_BACKEND "WebGL")
    endif()
    message(STATUS "TOMS web backend: ${WEB_BACKEND}")

    add_executable(tower_vulkan_web
        ${ENGINE_DIR}/emscripten_main.cpp
        ${GAME_DIR}/game.cpp
        ${ENGINE_DIR}/renderer_webgl.cpp
        ${ENGINE_DIR}/font.cpp
        ${ENGINE_DIR}/node.cpp)
    if(WEB_BACKEND STREQUAL "WebGPU")
        target_sources(tower_vulkan_web PRIVATE ${ENGINE_DIR}/renderer_webgpu.cpp)
        target_compile_definitions(tower_vulkan_web PRIVATE WEBGPU)
        target_compile_options(tower_vulkan_web PRIVATE --use-port=emdawnwebgpu)
    endif()

    target_include_directories(tower_vulkan_web PRIVATE
        ${GAME_DIR} ${ENGINE_DIR} ${STB_DIR} ${JSON_DIR} $ENV{GLM_DIR} $ENV{HOME}/opt/glm)

    if(WEB_BACKEND STREQUAL "WebGPU")
        set(WEB_LINK "--use-port=emdawnwebgpu -sFETCH=1 -sALLOW_MEMORY_GROWTH=1 \
-sEXPORTED_FUNCTIONS=['_main','_downloadFile'] \
-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS'] \
--preload-file ${CMAKE_SOURCE_DIR}/assets@assets \
--preload-file ${CMAKE_SOURCE_DIR}/data@data")
    else()
        set(WEB_LINK "-sUSE_WEBGL2=1 -sFETCH=1 -sALLOW_MEMORY_GROWTH=1 \
-sEXPORTED_FUNCTIONS=['_main','_downloadFile'] \
-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS'] \
--preload-file ${CMAKE_SOURCE_DIR}/assets@assets \
--preload-file ${CMAKE_SOURCE_DIR}/data@data \
-sGL_ASSERTIONS=0")
    endif()

    set_target_properties(tower_vulkan_web PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "${WEB_LINK}")
    set_target_properties(tower_vulkan_web PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/web)
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
    ${GAME_DIR} ${ENGINE_DIR}
    ${Vulkan_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIR}
    ${STB_DIR} ${JSON_DIR} ${CMAKE_SOURCE_DIR}/external/miniaudio
    $ENV{GLM_DIR} $ENV{HOME}/opt/glm)
target_link_libraries(texture_test PRIVATE ${Vulkan_LIBRARIES})
endif()

# font_test: build the runtime TTF atlas (stb_truetype) and verify glyph UVs.
# No Vulkan device needed — pure CPU atlas rasterization + Object base.
add_executable(font_test ${ENGINE_DIR}/font_test.cpp ${ENGINE_DIR}/font.cpp ${ENGINE_DIR}/object.h)
target_include_directories(font_test PRIVATE
    ${ENGINE_DIR} ${STB_DIR} ${JSON_DIR} $ENV{GLM_DIR} $ENV{HOME}/opt/glm)
