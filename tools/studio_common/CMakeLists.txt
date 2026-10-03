# tools/studio_common -- Qt code shared by the TOMS editors (atlas_editor, anim_editor): the theme,
# the icons drawn in code, the zoom/pan canvas base, a sprite image cache and command line helpers.
# Phase 2 merges the editors into one plugin-based app; this is the code the host and plugins share.
#
# Added (once) by whichever editor needs it first: tools/atlas/qt or tools/anim.
add_library(studio_common STATIC
    Theme.h            Theme.cpp
    Icons.h            Icons.cpp
    CanvasView.h       CanvasView.cpp
    SpriteImageCache.h SpriteImageCache.cpp
    Console.h          Console.cpp)
set_target_properties(studio_common PROPERTIES AUTOMOC ON FOLDER "tools/studio")
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
