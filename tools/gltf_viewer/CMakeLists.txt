# gltf_viewer -- the glTF 2.0 model viewer on the engine's renderer (docs/18_GLTF.md): SDL3 window,
# bgfx (toms_bgfx: GltfRenderer, the bgfx host, ImGui), the loader in toms_core (gltf_model.*).
# Desktop only. The test models it is checked with live in tests/gltf/ (make_test_models.py).
add_executable(gltf_viewer main.cpp make_test_models.py fetch_samples.py)
toms_target_defaults(gltf_viewer)
target_link_libraries(gltf_viewer PRIVATE toms_core toms_bgfx bgfx bx ${TOMS_SDL3_TARGET})
# bx (bgfx's headers) needs C++20; only this tool and toms_bgfx see them.
set_target_properties(gltf_viewer PROPERTIES FOLDER "tools" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                      VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
if(WIN32)
    set_target_properties(gltf_viewer PROPERTIES WIN32_EXECUTABLE FALSE)   # console: the log goes there
endif()
source_group("scripts" FILES make_test_models.py fetch_samples.py)
