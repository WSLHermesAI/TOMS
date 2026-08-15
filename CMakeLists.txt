cmake_minimum_required(VERSION 3.16)
project(tower_vulkan CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Paths to vendored third-party headers (self-contained, no system deps)
set(STB_DIR ${CMAKE_SOURCE_DIR}/external/stb)
set(JSON_DIR ${CMAKE_SOURCE_DIR}/external/json)

# ============================================================================
# Desktop target (Windows / Linux) — Vulkan renderer.
# ============================================================================
option(WEB "Build the Emscripten browser target" OFF)
if(NOT WEB)
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

    file(GLOB SPV_FILES "${CMAKE_SOURCE_DIR}/src/shaders/*.spv")
    if(NOT SPV_FILES)
        message(WARNING "No precompiled .spv shaders found in src/shaders/. "
                        "The Vulkan renderer needs them; run the shaders tool or place .spv files there.")
    endif()

    add_executable(tower_vulkan
        src/main.cpp src/game.cpp src/renderer.cpp src/node.cpp src/render_iface.h src/Audio.cpp src/vk_util.h)

    target_include_directories(tower_vulkan PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIR}
        ${STB_DIR} ${JSON_DIR} ${CMAKE_SOURCE_DIR}/external/miniaudio
        $ENV{GLM_DIR} $ENV{HOME}/opt/glm)
    target_link_libraries(tower_vulkan PRIVATE ${Vulkan_LIBRARIES})
    if(WIN32)
        target_compile_definitions(tower_vs PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()

    # Copy runtime resources next to the executable (assets/, data/, shaders/)
    foreach(RES assets data src/shaders)
        if(EXISTS "${CMAKE_SOURCE_DIR}/${RES}")
            add_custom_command(TARGET tower_vulkan POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/${RES}" "$<TARGET_FILE_DIR:tower_vulkan>/${RES}"
                COMMENT "Copying ${RES} next to executable")
        endif()
    endforeach()

    set_target_properties(tower_vulkan PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
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
        src/emscripten_main.cpp
        src/game.cpp
        src/renderer_webgl.cpp
        src/node.cpp)
    if(WEB_BACKEND STREQUAL "WebGPU")
        target_sources(tower_vulkan_web PRIVATE src/renderer_webgpu.cpp)
        target_compile_definitions(tower_vulkan_web PRIVATE WEBGPU)
        target_compile_options(tower_vulkan_web PRIVATE --use-port=emdawnwebgpu)
    endif()

    target_include_directories(tower_vulkan_web PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${STB_DIR} ${JSON_DIR} $ENV{GLM_DIR} $ENV{HOME}/opt/glm)

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
-sGL_ASSERTIONS=1")
    endif()

    set_target_properties(tower_vulkan_web PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "${WEB_LINK}")
    set_target_properties(tower_vulkan_web PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/web)
endif()

# ---------------------------------------------------------------------------
# Headless unit tests for the Node scene-graph + Object base (no rendering).
# Run: ./node_test  and  ./object_test
# ---------------------------------------------------------------------------
add_executable(node_test src/node_test.cpp src/node.cpp src/node.h)
target_include_directories(node_test PRIVATE ${CMAKE_SOURCE_DIR}/src $ENV{GLM_DIR} $ENV{HOME}/opt/glm)

add_executable(object_test src/object_test.cpp src/node.cpp src/node.h src/scene.h src/object.h)
target_include_directories(object_test PRIVATE ${CMAKE_SOURCE_DIR}/src $ENV{GLM_DIR} $ENV{HOME}/opt/glm)

add_executable(log_test src/log_test.cpp src/log.h)
target_include_directories(log_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

