# tools/atlas -- the sprite atlas tool (docs/14_ATLAS_TOOL.md).
#
#   atlas_core     C++17 library: project file, packing, exporters/importers. No Qt, no engine.
#   atlaspack      command line front end (headless builds, CI, the asset pipeline)
#   atlas_editor   Qt 6 editor; `atlas_editor --headless ...` runs the same commands as atlaspack
#   atlas_core_test  unit + golden tests
#
# Built as part of TOMS (add_subdirectory from the root CMakeLists.txt) or on its own:
#   cmake -S tools/atlas -B Build/atlas -G Ninja && cmake --build Build/atlas
cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED PROJECT_NAME)
    project(toms_atlas LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    enable_testing()
    set(ATLAS_STANDALONE ON)
endif()

get_filename_component(ATLAS_TOMS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT DEFINED TOMS_THIRD_PARTY_DIR)
    set(TOMS_THIRD_PARTY_DIR "${ATLAS_TOMS_ROOT}/src/third_party")
endif()

function(atlas_defaults target)
    if(COMMAND toms_target_defaults)
        toms_target_defaults(${target})
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W3 /Zc:__cplusplus)
        target_compile_definitions(${target} PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    endif()
    if(EMSCRIPTEN)
        target_compile_options(${target} PRIVATE -fexceptions)
        target_link_options(${target} PRIVATE -fexceptions)
    endif()
    set_target_properties(${target} PROPERTIES FOLDER "tools/atlas")
endfunction()

add_library(atlas_core STATIC
    core/atlas_image.h   core/atlas_image.cpp
    core/atlas_packer.h  core/atlas_packer.cpp
    core/atlas_project.h core/atlas_project.cpp
    core/atlas_build.h   core/atlas_build.cpp
    core/atlas_export.h  core/atlas_export.cpp
    core/atlas_xml.h     core/atlas_xml.cpp
    core/atlas_store.h   core/atlas_store.cpp
    "${ATLAS_TOMS_ROOT}/src/core/engine/atlas_file.h")
atlas_defaults(atlas_core)
target_include_directories(atlas_core
    PUBLIC  core "${ATLAS_TOMS_ROOT}/src/core/engine"
    PRIVATE "${TOMS_THIRD_PARTY_DIR}/stb" "${TOMS_THIRD_PARTY_DIR}/json")

if(NOT EMSCRIPTEN)
    # The command line lives in a library: atlaspack and `atlas_editor --headless` share it.
    add_library(atlas_cli STATIC cli/atlas_cli.h cli/atlas_cli.cpp)
    atlas_defaults(atlas_cli)
    target_include_directories(atlas_cli PUBLIC cli)
    target_link_libraries(atlas_cli PUBLIC atlas_core)

    add_executable(atlaspack cli/main.cpp)
    atlas_defaults(atlaspack)
    target_link_libraries(atlaspack PRIVATE atlas_cli)

    add_executable(atlas_core_test tests/atlas_core_test.cpp)
    atlas_defaults(atlas_core_test)
    target_include_directories(atlas_core_test PRIVATE "${TOMS_THIRD_PARTY_DIR}/json")
    target_link_libraries(atlas_core_test PRIVATE atlas_core)
    target_compile_definitions(atlas_core_test PRIVATE
        ATLAS_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
        ATLAS_TOMS_ROOT="${ATLAS_TOMS_ROOT}")
    if(ATLAS_STANDALONE OR (DEFINED TOMS_BUILD_TESTS AND TOMS_BUILD_TESTS))
        add_test(NAME atlas.core COMMAND atlas_core_test)
        set_tests_properties(atlas.core PROPERTIES LABELS "unit;atlas" TIMEOUT 120)
    endif()
endif()

# The Qt editor: needs Qt 6 Widgets (found by TomsPrerequisites in the TOMS build).
option(ATLAS_BUILD_EDITOR "Build the Qt atlas editor" ON)
if(ATLAS_BUILD_EDITOR AND NOT EMSCRIPTEN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qt/CMakeLists.txt")
    if(NOT TARGET Qt6::Widgets)
        find_package(Qt6 6.5 QUIET COMPONENTS Widgets)
    endif()
    if(TARGET Qt6::Widgets)
        add_subdirectory(qt)
    else()
        message(STATUS "[atlas] Qt 6 not found: atlas_editor is skipped (atlaspack is still built)")
    endif()
endif()

# The web build: atlas_core as WebAssembly for web/ (tools/atlas/web/README.md).
if(EMSCRIPTEN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/web/CMakeLists.txt")
    add_subdirectory(web)
endif()
