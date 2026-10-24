# tools/font -- picture fonts (BMFont .fnt + .png) from single pictures (docs/engine/09_FONT_TOOL.md).
#
#   toms_bitmap_font  the runtime (src/core/engine/bitmap_font.cpp) on its own: what the game and the
#                     tool lay text out with
#   font_tools        building, splitting, the preview renderer (core/); no Qt
#   font_cli          the command line, shared by fontpack and `font_editor --headless`
#   fontpack          the command line tool
#   font_editor       Qt 6 window: a glyph folder -> font, and a test text drawn with it (qt/)
#   font_tool_test    unit + reference tests on tests/font/
#
# Added from the root CMakeLists.txt (desktop builds); the editor needs Qt 6 Widgets.
# stb_image_impl.cpp: FontText::load decodes the page PNG. The game has bitmap_font.cpp in toms_core (which
# has stb too): never link this library and toms_core into one program.
add_library(toms_bitmap_font STATIC
    "${TOMS_CORE_SRC}/engine/bitmap_font.h" "${TOMS_CORE_SRC}/engine/bitmap_font.cpp"
    "${TOMS_CORE_SRC}/engine/stb_image_impl.cpp")
toms_target_defaults(toms_bitmap_font)
target_include_directories(toms_bitmap_font PUBLIC "${TOMS_CORE_SRC}/engine"   # bitmap_font.h, render_iface.h, vfs.h
                                            PRIVATE "${TOMS_THIRD_PARTY_DIR}/stb")
set_target_properties(toms_bitmap_font PROPERTIES FOLDER "tools/font")

add_library(font_tools STATIC core/font_build.h core/font_build.cpp)
toms_target_defaults(font_tools)
target_include_directories(font_tools PUBLIC core)
target_link_libraries(font_tools PUBLIC toms_bitmap_font atlas_core)   # atlas_core: PNG load/save, u8path
set_target_properties(font_tools PROPERTIES FOLDER "tools/font")

add_library(font_cli STATIC cli/font_cli.h cli/font_cli.cpp)
toms_target_defaults(font_cli)
target_include_directories(font_cli PUBLIC cli)
target_link_libraries(font_cli PUBLIC font_tools)
set_target_properties(font_cli PROPERTIES FOLDER "tools/font")

add_executable(fontpack cli/main.cpp)
toms_target_defaults(fontpack)
target_link_libraries(fontpack PRIVATE font_cli atlas_cli)   # atlas_cli: utf8CommandLine
set_target_properties(fontpack PROPERTIES FOLDER "tools/font")

add_executable(font_tool_test tests/font_tool_test.cpp)
toms_target_defaults(font_tool_test)
target_link_libraries(font_tool_test PRIVATE font_tools)
set_target_properties(font_tool_test PROPERTIES FOLDER "tools/font")
if(TOMS_BUILD_TESTS)
    # tests/font: single pictures -> the convert2fnt-web font of the old project byte for byte, the
    # test text drawn as tests/font/preview.png (TOMS_UPDATE_GOLDEN=1 rewrites it), split / names / wrap.
    add_test(NAME font.tool
             COMMAND font_tool_test "${CMAKE_SOURCE_DIR}/tests/font" "${CMAKE_BINARY_DIR}/test-run/font_tool")
    set_tests_properties(font.tool PROPERTIES LABELS "unit;font" TIMEOUT 60)
endif()

if(ATLAS_BUILD_EDITOR AND TARGET Qt6::Widgets AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qt/CMakeLists.txt")
    add_subdirectory(qt)
endif()
