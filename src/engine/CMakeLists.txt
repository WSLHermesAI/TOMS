# engine/ -- toms_bgfx: everything that talks to bgfx. Both the game and the editor link it.

# ---- shaders: .sc -> shaderc -> headers embedded in the library ----
set(TOMS_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
if(EMSCRIPTEN)
    set(TOMS_SHADER_PROFILES 300_es)                # the browser only runs WebGL2 (ESSL 3.00)
elseif(ANDROID)
    # GLES3 for the OpenGL ES path, and SPIR-V for Vulkan: "auto" prefers Vulkan on a device that
    # has it and falls back to GLES. Without spirv here, a Vulkan device would pick Vulkan and then have
    # no shaders to draw with. (No GLES2 "100_es": this bgfx's shaderc no longer has that profile, and
    # embedded_shaders.cpp only uses essl/ = 300_es.)
    set(TOMS_SHADER_PROFILES 300_es spirv)
elseif(WIN32)
    set(TOMS_SHADER_PROFILES s_5_0 spirv 430 300_es)   # D3D11/12, Vulkan, OpenGL, GLES/WebGL2
else()
    # Linux desktop: no D3D, so no s_5_0. shaderc's HLSL support goes through D3D4Linux, which is only
    # present in a Windows shaderc build -- asking for it on Linux fails with
    # 'HLSL compiler support through D3D4Linux is not compiled in'. Vulkan + GL + GLES remain.
    set(TOMS_SHADER_PROFILES spirv 430 300_es)
endif()
bgfx_compile_shaders(TYPE VERTEX
    SHADERS      ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_sprite.sc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_imgui.sc
                 ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_rml.sc
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
    shaders/vs_imgui.sc  shaders/fs_imgui.sc
    shaders/vs_rml.sc)
toms_target_defaults(toms_bgfx)
target_include_directories(toms_bgfx
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${TOMS_CORE_SRC}/engine                # render_iface.h (the IRenderer contract)
    PRIVATE ${TOMS_SHADER_OUT}
            ${TOMS_THIRD_PARTY_DIR}/stb)
target_link_libraries(toms_bgfx PUBLIC toms_imgui PRIVATE bgfx bx)
# bx requires C++20. Only this target sees bgfx headers, so the core game code stays C++17.
set_target_properties(toms_bgfx PROPERTIES FOLDER "toms" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# RmlUi: the game's whole UI (docs/08_RMLUI.md). PUBLIC: the host builds its data model with
# RmlUi's API.
# The D3D/HLSL shader set only exists on Windows (s_5_0 needs shaderc's D3D4Linux support), and
# embedded_shaders.cpp includes those generated headers. One define, set from the same condition that
# chooses the profiles, so the C++ and the profile list cannot drift apart.
if(WIN32)
    target_compile_definitions(toms_bgfx PRIVATE TOMS_SHADER_HAS_DXBC=1)
endif()
target_sources(toms_bgfx PRIVATE src/rml_ui.h src/rml_ui.cpp)
if(EMSCRIPTEN)   # the browser draws the UI text (no font files on the web)
    target_sources(toms_bgfx PRIVATE src/rml_canvas_font.h src/rml_canvas_font.cpp)
endif()
target_link_libraries(toms_bgfx PUBLIC RmlUi::RmlUi)
source_group("shaders" FILES shaders/varying.def.sc shaders/vs_sprite.sc shaders/fs_sprite.sc
                             shaders/vs_imgui.sc shaders/fs_imgui.sc shaders/vs_rml.sc)
