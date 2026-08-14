cmake_minimum_required(VERSION 3.16)
project(tower_editor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt6 (also works with Qt5 if you change the find_package call)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
endif()

# vendored nlohmann json (same header the game uses)
set(JSON_DIR "${CMAKE_SOURCE_DIR}/../external/json")

add_executable(tower_editor
    src/main.cpp
    src/mainwindow.cpp
    src/stagescene.cpp
    src/stagemodel.cpp
)

target_include_directories(tower_editor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${JSON_DIR}
)

target_link_libraries(tower_editor PRIVATE
    Qt6::Widgets
)

# On Windows, copy Qt DLLs next to the exe for a portable build (optional).
if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET tower_editor POST_BUILD
        COMMAND Qt6::windeployqt "$<TARGET_FILE:tower_editor>")
endif()

# Default data folder = the game's stages, so "Import" lands on real content.
set_target_properties(tower_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
