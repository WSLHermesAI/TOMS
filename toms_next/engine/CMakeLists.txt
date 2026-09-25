# engine/ -- toms_bgfx: everything that talks to bgfx. Both the game and the editor link it.

# ---- shaders: .sc -> shaderc -> headers embedded in the library ----
set(TOMS_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(TOMS_SHADER_PROFILES s_5_0 spirv 430 300_es)   # D3D11/12, Vulkan, OpenGL, GLES/WebGL2
bgfx_compile_shaders(TYPE VERTEX
    SHADERS      ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_sprite.sc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_imgui.sc
    VARYING_DEF  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc
    OUTPUT_DIR   ${TOMS_SHADER_OUT}
    OUT_FILES_VAR TOMS_VS_FILES
    INCLUDE_DIRS ${bgfx_SOURCE_DIR}/bgfx/src
    PROFILES     ${TOMS_SHADER_PROFILES}
    AS_HEADERS)
bgfx_compile_shaders(TYPE FRAGMENT
    SHADERS      ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_sprite.sc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_imgui.sc
    VARYING_DEF  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc
    OUTPUT_DIR   ${TOMS_SHADER_OUT}
    OUT_FILES_VAR TOMS_FS_FILES
    INCLUDE_DIRS ${bgfx_SOURCE_DIR}/bgfx/src
    PROFILES     ${TOMS_SHADER_PROFILES}
    AS_HEADERS)

# ---- Dear ImGui core (same v1.90.9 as the old build; no platform backend, we feed input) ----
add_library(toms_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(toms_imgui PUBLIC ${imgui_SOURCE_DIR})
set_target_properties(toms_imgui PROPERTIES FOLDER "third_party")

# ---- toms_bgfx ----
add_library(toms_bgfx STATIC
    src/bgfx_renderer.h   src/bgfx_renderer.cpp
    src/bgfx_host.h       src/bgfx_host.cpp
    src/imgui_bgfx.h      src/imgui_bgfx.cpp
    src/embedded_shaders.h src/embedded_shaders.cpp
    ${TOMS_VS_FILES} ${TOMS_FS_FILES}
    shaders/varying.def.sc
    shaders/vs_sprite.sc shaders/fs_sprite.sc
    shaders/vs_imgui.sc  shaders/fs_imgui.sc)
toms_target_defaults(toms_bgfx)
target_include_directories(toms_bgfx
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${TOMS_LEGACY_ROOT}/src/engine            # render_iface.h (the IRenderer contract)
    PRIVATE ${TOMS_SHADER_OUT}
            ${TOMS_LEGACY_ROOT}/external/stb)
target_link_libraries(toms_bgfx PUBLIC toms_imgui PRIVATE bgfx bx)
# bx requires C++20. Only this target sees bgfx headers, so the legacy game code stays C++17.
set_target_properties(toms_bgfx PROPERTIES FOLDER "toms" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
source_group("shaders" FILES shaders/varying.def.sc shaders/vs_sprite.sc shaders/fs_sprite.sc
                             shaders/vs_imgui.sc shaders/fs_imgui.sc)
