# tools/anim/qt -- anim_editor: the Qt 6 editor for .anim node animations (docs/15_ANIMATION.md).
#
# Added by tools/anim/CMakeLists.txt when Qt6::Widgets is there. Shares tools/studio_common
# (theme, icons, canvas base) with the atlas editor; AnimEditor is the unit Phase 2's plugin host
# will load, AnimMainWindow the thin host around it today.
if(NOT TARGET studio_common)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../studio_common" "${CMAKE_BINARY_DIR}/tools/studio_common")
endif()

add_executable(anim_editor WIN32
    Main.cpp
    AnimDocument.h        AnimDocument.cpp
    AnimEditor.h          AnimEditor.cpp
    AnimMainWindow.h      AnimMainWindow.cpp
    AnimViewport.h        AnimViewport.cpp
    AnimClipsDock.h       AnimClipsDock.cpp
    AnimNodesDock.h       AnimNodesDock.cpp
    AnimSpritesDock.h     AnimSpritesDock.cpp
    AnimSpriteDrop.h      AnimSpriteDrop.cpp
    AnimPropertiesDock.h  AnimPropertiesDock.cpp
    AnimKeyListDock.h     AnimKeyListDock.cpp
    AnimTransport.h       AnimTransport.cpp
    AnimProblemsDock.h    AnimProblemsDock.cpp
    AnimAtlasesPanel.h    AnimAtlasesPanel.cpp
    BezierDialog.h        BezierDialog.cpp)
set_target_properties(anim_editor PROPERTIES AUTOMOC ON FOLDER "tools/anim"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
toms_target_defaults(anim_editor)
target_link_libraries(anim_editor PRIVATE Qt6::Widgets studio_common anim_tools)

# Copy the Qt DLLs and plugins next to the exe, so double-click and F5 both work.
if(WIN32)
    if(TARGET Qt6::windeployqt)
        set(_anim_windeployqt $<TARGET_FILE:Qt6::windeployqt>)
    else()
        find_program(_anim_windeployqt_exe windeployqt HINTS "${Qt6_DIR}/../../../bin")
        set(_anim_windeployqt "${_anim_windeployqt_exe}")
    endif()
    if(_anim_windeployqt)
        add_custom_command(TARGET anim_editor POST_BUILD
            COMMAND "${_anim_windeployqt}" --no-translations --no-compiler-runtime --no-system-d3d-compiler
                    --no-opengl-sw "$<TARGET_FILE:anim_editor>"
            COMMENT "windeployqt: copying Qt runtime next to anim_editor.exe"
            VERBATIM)
    else()
        message(WARNING "[anim] windeployqt not found: copy the Qt DLLs next to anim_editor.exe or add <Qt>/bin to PATH")
    endif()
endif()

# `anim_editor --headless check` on the smoke test's clip: it parses and every sprite is in the
# game atlas (no window; runs with the unit tests).
if(DEFINED TOMS_BUILD_TESTS AND TOMS_BUILD_TESTS)
    add_test(NAME anim.check_preview
             COMMAND anim_editor --headless check "${CMAKE_SOURCE_DIR}/tests/smoke/anim_preview.anim")
    set_tests_properties(anim.check_preview PROPERTIES LABELS "unit;anim" TIMEOUT 30)
    # The hand-writing guide's examples (docs/16_ANIMATION_RECIPES.md) stay valid.
    add_test(NAME anim.check_recipes
             COMMAND anim_editor --headless check "${CMAKE_SOURCE_DIR}/docs/examples/anim_recipes.anim")
    set_tests_properties(anim.check_recipes PROPERTIES LABELS "unit;anim" TIMEOUT 30)
endif()
