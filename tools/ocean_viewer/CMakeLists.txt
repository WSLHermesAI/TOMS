# ocean_viewer -- try the ocean and its sky on their own, with every setting on sliders (docs/engine/21_OCEAN_VIEWER.md): SDL3
# window, bgfx. The waves are in toms_core (ocean_sim.*), the drawing and its shaders in toms_bgfx (ocean_renderer.*).
# Desktop, and the web (tools\build_ocean_web.cmd: WebGL2, single-threaded).
add_executable(ocean_viewer main.cpp)
toms_target_defaults(ocean_viewer)
target_link_libraries(ocean_viewer PRIVATE toms_core toms_bgfx bgfx bx ${TOMS_SDL3_TARGET})
# bx (bgfx's headers) needs C++20; only this tool and toms_bgfx see them.
set_target_properties(ocean_viewer PROPERTIES FOLDER "tools" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                      VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
if(WIN32)
    set_target_properties(ocean_viewer PROPERTIES WIN32_EXECUTABLE FALSE)   # console: the log goes there
endif()
# "Save settings" writes here, beside the code, so the numbers that look right can be committed.
target_compile_definitions(ocean_viewer PRIVATE TOMS_OCEAN_SETTINGS="${CMAKE_CURRENT_SOURCE_DIR}/ocean_settings.json")

if(EMSCRIPTEN)
    # Output: bin/ocean_viewer.html + .js + .wasm. It loads no files. Not part of the game's web build: only
    # `--target ocean_viewer` builds it.
    set(_shell ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    set_target_properties(ocean_viewer PROPERTIES EXCLUDE_FROM_ALL ON SUFFIX ".html" LINK_DEPENDS "${_shell}")
    target_link_options(ocean_viewer PRIVATE
        "--shell-file=${_shell}"
        "-sMIN_WEBGL_VERSION=2" "-sMAX_WEBGL_VERSION=2"       # bgfx's GLES backend on WebGL2
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString','stringToNewUTF8']"
        "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
        "-sSTACK_SIZE=1048576")
endif()

if(TOMS_BUILD_TESTS AND NOT EMSCRIPTEN)
    # It starts, compiles its programs on this backend and draws: no reference picture, the look is still being chosen.
    add_test(NAME smoke.ocean_viewer COMMAND ocean_viewer --preset=day --no-ui --size=480x270 --frames=4 --fixed-dt=33
             "--screenshot=${CMAKE_BINARY_DIR}/test-run/ocean_viewer.png")
    set_tests_properties(smoke.ocean_viewer PROPERTIES LABELS "smoke" TIMEOUT 60)
endif()
