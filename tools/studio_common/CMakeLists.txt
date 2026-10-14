# tools/studio_common -- Qt code shared by the TOMS editors (atlas_editor, anim_editor): the theme,
# the icons drawn in code, the zoom/pan canvas base, a sprite image cache and command line helpers.
# Phase 2 merges the editors into one plugin-based app; this is the code the host and plugins share.
#
# Added (once) by whichever editor needs it first: tools/atlas/qt or tools/anim.
# The editors' Chinese (Language.h): every i18n/zh_TW/*.json, built in as the resource studio_i18n.
file(GLOB _studio_i18n CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/zh_TW/*.json")
set(_studio_qrc "<RCC>\n  <qresource prefix=\"/i18n/zh_TW\">\n")
foreach(_f IN LISTS _studio_i18n)
    get_filename_component(_n "${_f}" NAME)
    string(APPEND _studio_qrc "    <file alias=\"${_n}\">${_f}</file>\n")
endforeach()
string(APPEND _studio_qrc "  </qresource>\n</RCC>\n")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/studio_i18n.qrc" CONTENT "${_studio_qrc}")

add_library(studio_common STATIC
    Theme.h            Theme.cpp
    Language.h         Language.cpp
    Icons.h            Icons.cpp
    CanvasView.h       CanvasView.cpp
    AutoBackup.h       AutoBackup.cpp
    SpriteImageCache.h SpriteImageCache.cpp
    Console.h          Console.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/studio_i18n.qrc")
set_target_properties(studio_common PROPERTIES AUTOMOC ON AUTORCC ON FOLDER "tools/studio")
if(COMMAND toms_target_defaults)
    toms_target_defaults(studio_common)
elseif(MSVC)
    target_compile_options(studio_common PRIVATE /utf-8 /W3 /Zc:__cplusplus)
    target_compile_definitions(studio_common PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
target_include_directories(studio_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(studio_common PUBLIC Qt6::Widgets)
if(WIN32)
    target_link_libraries(studio_common PRIVATE shell32)   # CommandLineToArgvW
endif()

# The canvas base offscreen (QT_QPA_PLATFORM=offscreen): the coordinate readout follows a simulated mouse.
add_executable(canvas_view_test tests/canvas_view_test.cpp)
set_target_properties(canvas_view_test PROPERTIES FOLDER "tools/studio")
target_link_libraries(canvas_view_test PRIVATE studio_common)
# windeployqt copies only the windows platform plugin; the offscreen tests need qoffscreen next to the exe too.
if(WIN32 AND TARGET Qt6::QOffscreenIntegrationPlugin)
    add_custom_command(TARGET canvas_view_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:canvas_view_test>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                "$<TARGET_FILE_DIR:canvas_view_test>/platforms/")
endif()
if(TOMS_BUILD_TESTS)
    add_test(NAME studio.canvas_coordinates COMMAND canvas_view_test)
    set_tests_properties(studio.canvas_coordinates PROPERTIES LABELS "unit" TIMEOUT 60
                         ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# The English / 中文 switch (Language.h) on a small window, offscreen. TOMS_LANGUAGE=en: it starts in
# English and never saves the choice.
add_executable(language_test tests/language_test.cpp)
set_target_properties(language_test PROPERTIES FOLDER "tools/studio")
target_link_libraries(language_test PRIVATE studio_common)
if(TOMS_BUILD_TESTS)
    add_test(NAME studio.language COMMAND language_test)
    set_tests_properties(studio.language PROPERTIES LABELS "unit" TIMEOUT 60
                         ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TOMS_LANGUAGE=en")
    # Every tr() text of the editors has its Chinese in i18n/zh_TW (i18n/i18n_check.py).
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME studio.i18n_complete
                 COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/i18n/i18n_check.py" --check)
        set_tests_properties(studio.i18n_complete PROPERTIES LABELS "unit" TIMEOUT 60
                             ENVIRONMENT "PYTHONIOENCODING=utf-8")
    endif()
endif()

# studio_bgfx: GameCanvasView -- an editor canvas drawn by the game's own renderer (toms_bgfx's
# BgfxRenderer), so the atlas, anim and particle editors preview exactly what toms_game draws.
if(TARGET toms_bgfx AND NOT TARGET studio_bgfx)
    add_library(studio_bgfx STATIC GameCanvasView.h GameCanvasView.cpp)
    set_target_properties(studio_bgfx PROPERTIES AUTOMOC ON FOLDER "tools/studio")
    if(COMMAND toms_target_defaults)
        toms_target_defaults(studio_bgfx)
    endif()
    target_link_libraries(studio_bgfx PUBLIC studio_common toms_bgfx glm::glm)
endif()
