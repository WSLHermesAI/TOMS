# tools/particle/qt -- particle_editor: the Qt 6 editor for .particle effects (docs/17_PARTICLES.md).
#
# Added by tools/particle/CMakeLists.txt when Qt6::Widgets is there. Shares tools/studio_common
# (theme, icons, canvas) with the atlas and anim editors; ParticleEditor is the unit the studio
# app's plugin host will load, ParticleMainWindow the thin host around it today.
if(NOT TARGET studio_common)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../studio_common" "${CMAKE_BINARY_DIR}/tools/studio_common")
endif()

# The presets (Effect > Add Preset) are the recipe effects, built in so the editor works anywhere.
set(_presets_qrc "${CMAKE_CURRENT_BINARY_DIR}/particle_presets.qrc")
file(CONFIGURE OUTPUT "${_presets_qrc}" CONTENT [=[
<RCC><qresource prefix="/presets">
  <file alias="fx_recipes.particle">@CMAKE_SOURCE_DIR@/docs/examples/fx_recipes.particle</file>
</qresource></RCC>
]=] @ONLY)

add_executable(particle_editor WIN32
    Main.cpp
    ParticleDocument.h    ParticleDocument.cpp
    ParticleEditor.h      ParticleEditor.cpp
    ParticleMainWindow.h  ParticleMainWindow.cpp
    ParticlePlayback.h    ParticlePlayback.cpp
    ParticleViewport.h    ParticleViewport.cpp  ParticleViewportGpu.cpp
    EffectsDock.h         EffectsDock.cpp
    InspectorDock.h       InspectorDock.cpp
    ParticleWidgets.h     ParticleWidgets.cpp
    TimelineDock.h        TimelineDock.cpp
    FxSpritesDock.h       FxSpritesDock.cpp
    "${_presets_qrc}")
set_target_properties(particle_editor PROPERTIES AUTOMOC ON AUTORCC ON FOLDER "tools/particle"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
toms_target_defaults(particle_editor)
# toms_bgfx: the preview draws with the game's own renderer (BgfxRenderer, ParticleViewportGpu.cpp).
target_link_libraries(particle_editor PRIVATE Qt6::Widgets studio_common studio_bgfx toms_fx)
# Where a new file finds the fx atlas (it is stored relative to the .particle on save).
target_compile_definitions(particle_editor PRIVATE TOMS_FX_ATLAS="${TOMS_ASSET_DIR}/atlas/fx.atlas")

# Copy the Qt DLLs and plugins next to the exe, so double-click and F5 both work.
if(WIN32)
    if(TARGET Qt6::windeployqt)
        set(_fx_windeployqt $<TARGET_FILE:Qt6::windeployqt>)
    else()
        find_program(_fx_windeployqt_exe windeployqt HINTS "${Qt6_DIR}/../../../bin")
        set(_fx_windeployqt "${_fx_windeployqt_exe}")
    endif()
    if(_fx_windeployqt)
        add_custom_command(TARGET particle_editor POST_BUILD
            COMMAND "${_fx_windeployqt}" --no-translations --no-compiler-runtime --no-system-d3d-compiler
                    --no-opengl-sw "$<TARGET_FILE:particle_editor>"
            COMMENT "windeployqt: copying Qt runtime next to particle_editor.exe"
            VERBATIM)
    endif()
endif()

if(DEFINED TOMS_BUILD_TESTS AND TOMS_BUILD_TESTS)
    # The recipes (docs/examples/fx_recipes.particle) check clean against their atlases.
    add_test(NAME particle.check_recipes
             COMMAND particle_editor --headless check "${CMAKE_SOURCE_DIR}/docs/examples/fx_recipes.particle")
    set_tests_properties(particle.check_recipes PROPERTIES LABELS "unit;particle" TIMEOUT 30)
endif()
