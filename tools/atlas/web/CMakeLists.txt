# tools/atlas/web -- atlas_core as WebAssembly + the browser editor (README.md).
#
#   atlas_web.js / atlas_web.wasm   ES module (createAtlasModule) with an embind API (atlas_web.cpp)
#   site/                           the static editor: index.html, *.js, style.css + the module
#
# Configure the standalone atlas project with Emscripten (tools\build_atlas_web.cmd does this):
#   emcmake cmake -S tools/atlas -B Build/atlas-web -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build Build/atlas-web
# The site lands in <build>/site (Build/atlas-web/site).

add_executable(atlas_web atlas_web.cpp "${CMAKE_CURRENT_SOURCE_DIR}/../cli/atlas_cli.cpp")
atlas_defaults(atlas_web)
target_include_directories(atlas_web PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../cli" "${TOMS_THIRD_PARTY_DIR}/json")
target_link_libraries(atlas_web PRIVATE atlas_core)
set_target_properties(atlas_web PROPERTIES
    OUTPUT_NAME atlas_web
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_options(atlas_web PRIVATE
    -lembind
    -lnodefs.js
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createAtlasModule
    -sALLOW_MEMORY_GROWTH=1
    -sENVIRONMENT=web,worker,node
    -sFORCE_FILESYSTEM=1
    -sEXIT_RUNTIME=0
    -sINVOKE_RUN=0
    -sSTACK_SIZE=1048576
    "-sEXPORTED_RUNTIME_METHODS=['FS','NODEFS']")

# The site: the module + the static UI files, in <build>/site.
set(ATLAS_SITE_DIR "${CMAKE_BINARY_DIR}/site")
set(ATLAS_WEB_STATIC
    index.html style.css app.js wasm.js canvas.js sprites.js props.js anims.js refs.js util.js zip.js atlaspack.mjs)
set(_static_abs)
foreach(f ${ATLAS_WEB_STATIC})
    list(APPEND _static_abs "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
endforeach()
add_custom_command(TARGET atlas_web POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ATLAS_SITE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/atlas_web.js" "${CMAKE_CURRENT_BINARY_DIR}/atlas_web.wasm" "${ATLAS_SITE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_static_abs} "${ATLAS_SITE_DIR}"
    COMMENT "atlas_web: site -> ${ATLAS_SITE_DIR}"
    VERBATIM)
# Editing only the UI files: `cmake --build <dir> --target atlas_web_site` copies them again.
add_custom_target(atlas_web_site
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ATLAS_SITE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_static_abs} "${ATLAS_SITE_DIR}"
    DEPENDS ${_static_abs}
    SOURCES ${_static_abs}
    VERBATIM)
add_dependencies(atlas_web atlas_web_site)
