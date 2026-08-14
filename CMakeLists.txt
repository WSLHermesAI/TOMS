cmake_minimum_required(VERSION 3.16)
project(tower_vulkan CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Vulkan ----
# Try find_package first (works on Windows + system-Vulkan Linux).
# If the loader lib isn't found (local/no-system SDK), fall back to manual search.
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND OR NOT Vulkan_LIBRARIES)
    # Manual fallback: local LunarG SDK or any vulkan loader on the system.
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

# ---- vendored third-party headers (self-contained, no system deps) ----
set(STB_DIR "${CMAKE_SOURCE_DIR}/external/stb")
set(JSON_DIR "${CMAKE_SOURCE_DIR}/external/json")

# ---- shaders: use precompiled .spv already in the repo (cross-platform) ----
# Optionally regenerate if glslangValidator is available (set GLSLANG_VALIDATOR or VULKAN_SDK).
file(GLOB SPV_FILES "${CMAKE_SOURCE_DIR}/src/shaders/*.spv")

find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" "${Vulkan_INCLUDE_DIRS}/../Bin")
if(GLSLANG_VALIDATOR)
    foreach(SH ${CMAKE_SOURCE_DIR}/src/shaders/sprite.vert ${CMAKE_SOURCE_DIR}/src/shaders/sprite.frag)
        string(REPLACE ".vert" ".vert.spv" SPV ${SH})
        string(REPLACE ".frag" ".frag.spv" SPV ${SPV})
        add_custom_command(OUTPUT ${SPV}
            COMMAND ${GLSLANG_VALIDATOR} -V ${SH} -o ${SPV}
            DEPENDS ${SH} MAIN_DEPENDENCY ${SH})
    endforeach()
endif()

add_executable(tower_vulkan
    src/main.cpp src/game.cpp src/renderer.cpp src/Audio.cpp ${SPV_FILES})

target_include_directories(tower_vulkan PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/miniaudio
    ${STB_DIR}
    ${JSON_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIR})

target_link_libraries(tower_vulkan PRIVATE ${Vulkan_LIBRARIES})
# pthread / dl only exist on Linux
target_link_libraries(tower_vulkan PRIVATE $<$<PLATFORM_ID:Linux>:pthread;dl>)
# On Linux, bake the loader's directory into the RPATH so it runs without LD_LIBRARY_PATH
if(UNIX AND Vulkan_LIBRARY)
    get_filename_component(_VK_LIBDIR "${Vulkan_LIBRARY}" DIRECTORY)
    set_target_properties(tower_vulkan PROPERTIES
        BUILD_RPATH "${_VK_LIBDIR}"
        INSTALL_RPATH "${_VK_LIBDIR}")
endif()

# Put the executable at the project root (matches asset/data relative paths).
set_target_properties(tower_vulkan PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

# Copy runtime resources next to the executable so it works from any CWD / VS output dir.
# (Visual Studio runs from out/build/<cfg>/, so the copy makes that dir self-contained.)
set(RUN_RES "assets" "data" "src/shaders")
foreach(RES ${RUN_RES})
    if(EXISTS "${CMAKE_SOURCE_DIR}/${RES}")
        add_custom_command(TARGET tower_vulkan POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/${RES}"
                "$<TARGET_FILE_DIR:tower_vulkan>/${RES}"
            COMMENT "Copying ${RES} next to executable")
    endif()
endforeach()
