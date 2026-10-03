# tools/atlas/qt -- atlas_editor: the Qt 6 editor for .atlasproj files (docs/14_ATLAS_TOOL.md).
#
# Added by tools/atlas/CMakeLists.txt once Qt6::Widgets is found. Builds run on a worker thread
# through Qt Concurrent; without it the editor is skipped (atlaspack still covers every build).
if(NOT TARGET Qt6::Concurrent)
    find_package(Qt6 QUIET COMPONENTS Concurrent)
endif()
if(NOT TARGET Qt6::Concurrent)
    message(STATUS "[atlas] Qt 6 Concurrent not found: atlas_editor is skipped")
    return()
endif()

# Theme, Icons, CanvasView (and the console helpers) live in tools/studio_common, shared with the
# anim editor (tools/anim).
if(NOT TARGET studio_common)
    add_subdirectory("${ATLAS_TOMS_ROOT}/tools/studio_common" "${CMAKE_BINARY_DIR}/tools/studio_common")
endif()

add_executable(atlas_editor WIN32
    Main.cpp
    AtlasDocument.h      AtlasDocument.cpp
    Commands.h           Commands.cpp
    AtlasCanvas.h        AtlasCanvas.cpp
    SpriteEditCanvas.h   SpriteEditCanvas.cpp
    CanvasPanel.h        CanvasPanel.cpp
    SpriteTreeModel.h    SpriteTreeModel.cpp
    SpriteTreeDock.h     SpriteTreeDock.cpp
    PropertiesDock.h     PropertiesDock.cpp
    AnimationDock.h      AnimationDock.cpp
    ProblemsDock.h       ProblemsDock.cpp
    GridSliceDialog.h    GridSliceDialog.cpp
    ReferencesDialog.h   ReferencesDialog.cpp
    MainWindow.h         MainWindow.cpp)
set_target_properties(atlas_editor PROPERTIES AUTOMOC ON AUTORCC ON)
atlas_defaults(atlas_editor)
target_link_libraries(atlas_editor PRIVATE Qt6::Widgets Qt6::Concurrent atlas_cli studio_common)
set_target_properties(atlas_editor PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# Copy the Qt DLLs and plugins next to the exe, so double-click and F5 both work.
if(WIN32)
    if(TARGET Qt6::windeployqt)
        set(_atlas_windeployqt $<TARGET_FILE:Qt6::windeployqt>)
    else()
        find_program(_atlas_windeployqt_exe windeployqt HINTS "${Qt6_DIR}/../../../bin")
        set(_atlas_windeployqt "${_atlas_windeployqt_exe}")
    endif()
    if(_atlas_windeployqt)
        add_custom_command(TARGET atlas_editor POST_BUILD
            COMMAND "${_atlas_windeployqt}" --no-translations --no-compiler-runtime --no-system-d3d-compiler
                    --no-opengl-sw "$<TARGET_FILE:atlas_editor>"
            COMMENT "windeployqt: copying Qt runtime next to atlas_editor.exe"
            VERBATIM)
    else()
        message(WARNING "[atlas] windeployqt not found: copy the Qt DLLs next to atlas_editor.exe or add <Qt>/bin to PATH")
    endif()
endif()
