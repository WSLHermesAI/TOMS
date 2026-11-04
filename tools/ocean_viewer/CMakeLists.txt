# ocean_viewer -- try an ocean and a sky on their own, outside the game (docs/engine/21_OCEAN_VIEWER.md): SDL3 window, bgfx
# (toms_bgfx: the host, ImGui), its own shaders. The waves (ocean_sim.*) have no GPU code and a unit test of their own.
# Desktop, and the web (tools\build_ocean_web.cmd: WebGL2, single-threaded).
set(TOMS_OCEAN_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
if(EMSCRIPTEN)
    set(TOMS_OCEAN_PROFILES 300_es)            # the browser only runs WebGL2
elseif(WIN32)
    set(TOMS_OCEAN_PROFILES s_5_0 spirv 430)   # D3D11/12, Vulkan, OpenGL
else()
    set(TOMS_OCEAN_PROFILES spirv 430)
endif()
set(_dir ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
bgfx_compile_shaders(TYPE VERTEX
    SHADERS      ${_dir}/vs_ocean_water.sc ${_dir}/vs_ocean_sky.sc
    VARYING_DEF  ${_dir}/varying.def.sc
    OUTPUT_DIR   ${TOMS_OCEAN_SHADER_OUT}
    OUT_FILES_VAR TOMS_OCEAN_VS_FILES
    INCLUDE_DIRS ${bgfx_SOURCE_DIR}/bgfx/src ${_dir}
    PROFILES     ${TOMS_OCEAN_PROFILES}
    AS_HEADERS)
bgfx_compile_shaders(TYPE FRAGMENT
    SHADERS      ${_dir}/fs_ocean_water.sc ${_dir}/fs_ocean_sky.sc
    VARYING_DEF  ${_dir}/varying.def.sc
    OUTPUT_DIR   ${TOMS_OCEAN_SHADER_OUT}
    OUT_FILES_VAR TOMS_OCEAN_FS_FILES
    INCLUDE_DIRS ${bgfx_SOURCE_DIR}/bgfx/src ${_dir}
    PROFILES     ${TOMS_OCEAN_PROFILES}
    AS_HEADERS)

# bgfx's rule only follows the .sc file itself: the shared file must rebuild them too.
foreach(_out ${TOMS_OCEAN_VS_FILES} ${TOMS_OCEAN_FS_FILES})
    add_custom_command(OUTPUT ${_out} APPEND DEPENDS ${_dir}/ocean_common.sh)
endforeach()

add_executable(ocean_viewer main.cpp ocean_sim.h ocean_sim.cpp ocean_renderer.h ocean_renderer.cpp
    ${TOMS_OCEAN_VS_FILES} ${TOMS_OCEAN_FS_FILES}
    shaders/varying.def.sc shaders/ocean_common.sh
    shaders/vs_ocean_water.sc shaders/fs_ocean_water.sc shaders/vs_ocean_sky.sc shaders/fs_ocean_sky.sc)
toms_target_defaults(ocean_viewer)
target_include_directories(ocean_viewer PRIVATE ${TOMS_OCEAN_SHADER_OUT})
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
    add_executable(ocean_sim_test ocean_sim_test.cpp ocean_sim.h ocean_sim.cpp)
    toms_target_defaults(ocean_sim_test)
    target_link_libraries(ocean_sim_test PRIVATE toms_core)
    set_target_properties(ocean_sim_test PROPERTIES FOLDER "tools")
    add_test(NAME unit.ocean_sim_test COMMAND ocean_sim_test)
    set_tests_properties(unit.ocean_sim_test PROPERTIES LABELS "unit" TIMEOUT 60)
    # It starts, compiles its programs on this backend and draws: no reference picture, the look is still being chosen.
    add_test(NAME smoke.ocean_viewer COMMAND ocean_viewer --preset=day --no-ui --size=480x270 --frames=4 --fixed-dt=33
             "--screenshot=${CMAKE_BINARY_DIR}/test-run/ocean_viewer.png")
    set_tests_properties(smoke.ocean_viewer PROPERTIES LABELS "smoke" TIMEOUT 60)
endif()
