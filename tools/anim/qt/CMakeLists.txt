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
# studio_bgfx: the viewport draws with the game's own renderer (GameCanvasView).
target_link_libraries(anim_editor PRIVATE Qt6::Widgets studio_common studio_bgfx anim_tools)

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
    # windeployqt copies only the windows platform plugin; --selftest runs with -platform offscreen.
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
        add_custom_command(TARGET anim_editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:anim_editor>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                    "$<TARGET_FILE_DIR:anim_editor>/platforms/")
    endif()
endif()

# `anim_editor --headless check` on the smoke test's clip: it parses and every sprite is in the
# game atlas (no window; runs with the unit tests).
if(DEFINED TOMS_BUILD_TESTS AND TOMS_BUILD_TESTS)
    # The editor itself, offscreen (no window): its UI paths -- selection, Properties and key list edits,
    # viewport drags, undo, save -- on a scratch copy of the smoke test's clip (Main.cpp runSelfTest).
    add_test(NAME anim.editor_selftest
             COMMAND anim_editor -platform offscreen --selftest "${CMAKE_SOURCE_DIR}/tests/smoke/anim_preview.anim"
                     "${CMAKE_BINARY_DIR}/anim_selftest")
    set_tests_properties(anim.editor_selftest PROPERTIES LABELS "unit;anim" TIMEOUT 120)
    add_test(NAME anim.check_preview
             COMMAND anim_editor --headless check "${CMAKE_SOURCE_DIR}/tests/smoke/anim_preview.anim")
    set_tests_properties(anim.check_preview PROPERTIES LABELS "unit;anim" TIMEOUT 30)
    # the skinned clip (docs/34_MESH_ANIMATION.md): its skin bones are nodes of the clip
    add_test(NAME anim.check_skin
             COMMAND anim_editor --headless check "${CMAKE_SOURCE_DIR}/tests/smoke/anim_skin.anim")
    set_tests_properties(anim.check_skin PROPERTIES LABELS "unit;anim" TIMEOUT 30)
    # The hand-writing guide's examples (docs/16_ANIMATION_RECIPES.md) stay valid.
    add_test(NAME anim.check_recipes
             COMMAND anim_editor --headless check "${CMAKE_SOURCE_DIR}/docs/examples/anim_recipes.anim")
    set_tests_properties(anim.check_recipes PROPERTIES LABELS "unit;anim" TIMEOUT 30)
    # the monsters' idle animations the game plays on the map (assets/media/anim/monster_idle.anim)
    add_test(NAME anim.check_monster_idle
             COMMAND anim_editor --headless check "${CMAKE_SOURCE_DIR}/assets/media/anim/monster_idle.anim")
    set_tests_properties(anim.check_monster_idle PROPERTIES LABELS "unit;anim" TIMEOUT 30)
    # the NPCs' idle animations (assets/media/anim/npc_idle.anim, tools/make_idle_clips.py)
    add_test(NAME anim.check_npc_idle
             COMMAND anim_editor --headless check "${CMAKE_SOURCE_DIR}/assets/media/anim/npc_idle.anim")
    set_tests_properties(anim.check_npc_idle PROPERTIES LABELS "unit;anim" TIMEOUT 30)
    # the door-opening clips (assets/media/anim/door_open.anim, Game::openDoor)
    add_test(NAME anim.check_door_open
             COMMAND anim_editor --headless check "${CMAKE_SOURCE_DIR}/assets/media/anim/door_open.anim")
    set_tests_properties(anim.check_door_open PROPERTIES LABELS "unit;anim" TIMEOUT 30)
    # the bow's clips (assets/media/anim/weapon_bow.anim, written by tools/make_bow_anim.py): idle, charge, attack, miss
    add_test(NAME anim.check_bow
             COMMAND anim_editor --headless check "${CMAKE_SOURCE_DIR}/assets/media/anim/weapon_bow.anim")
    set_tests_properties(anim.check_bow PROPERTIES LABELS "unit;anim" TIMEOUT 30)
    # a character shooting it, for every character (archer_bow.anim, tools/make_archer_anim.py)
    add_test(NAME anim.check_archer
             COMMAND anim_editor --headless check "${CMAKE_SOURCE_DIR}/assets/media/anim/archer_bow.anim")
    set_tests_properties(anim.check_archer PROPERTIES LABELS "unit;anim" TIMEOUT 30)
endif()
