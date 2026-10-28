# gltf_viewer -- the glTF 2.0 model viewer on the engine's renderer (docs/engine/08_GLTF.md): SDL3 window,
# bgfx (toms_bgfx: GltfRenderer, the bgfx host, ImGui), the loader in toms_core (gltf_model.*).
# Desktop, and the web (tools\build_gltf_web.cmd: WebGL2, single-threaded). The test models it is checked with
# live in tests/gltf/ (make_test_models.py).
add_executable(gltf_viewer main.cpp make_test_models.py fetch_samples.py)
toms_target_defaults(gltf_viewer)
target_link_libraries(gltf_viewer PRIVATE toms_core toms_physics toms_bgfx bgfx bx ${TOMS_SDL3_TARGET})
if(WIN32)
    target_link_libraries(gltf_viewer PRIVATE shell32)   # CommandLineToArgvW: the command line as UTF-8
endif()
# bx (bgfx's headers) needs C++20; only this tool and toms_bgfx see them.
set_target_properties(gltf_viewer PROPERTIES FOLDER "tools" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                      VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
if(WIN32)
    set_target_properties(gltf_viewer PROPERTIES WIN32_EXECUTABLE FALSE)   # console: the log goes there
endif()
# assets/media: resources.json names the default environment picture (env.default, assets/media/env/).
target_compile_definitions(gltf_viewer PRIVATE TOMS_DEFAULT_ASSET_DIR="${TOMS_ASSET_DIR}")
if(EMSCRIPTEN)
    # Output: bin/gltf_viewer.html + .js + .wasm + .data (the test models at /models; the manifest and the environment
    # pictures at /assets). Not part of the game's web build: only `--target gltf_viewer` builds it.
    set(_shell ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    set(_models ${CMAKE_SOURCE_DIR}/tests/gltf)
    file(GLOB _model_files CONFIGURE_DEPENDS "${_models}/*" "${TOMS_ASSET_DIR}/env/*")
    set_target_properties(gltf_viewer PROPERTIES EXCLUDE_FROM_ALL ON SUFFIX ".html"
                          LINK_DEPENDS "${_shell};${_model_files};${TOMS_ASSET_DIR}/resources.json")
    target_link_options(gltf_viewer PRIVATE
        "--shell-file=${_shell}"
        "-sMIN_WEBGL_VERSION=2" "-sMAX_WEBGL_VERSION=2"       # bgfx's GLES backend on WebGL2
        "-sALLOW_MEMORY_GROWTH=1"
        "-sFORCE_FILESYSTEM=1"                                 # web/shell.html writes the opened files
        "-sEXPORTED_RUNTIME_METHODS=['ccall','FS','UTF8ToString','stringToNewUTF8']"
        "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_jsOpenFile']"
        "-sSTACK_SIZE=1048576"
        "--preload-file=${_models}@/models"
        "--preload-file=${TOMS_ASSET_DIR}/resources.json@/assets/resources.json"
        "--preload-file=${TOMS_ASSET_DIR}/env@/assets/env")
endif()
source_group("scripts" FILES make_test_models.py fetch_samples.py)
