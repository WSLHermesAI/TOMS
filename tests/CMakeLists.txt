# tests/ -- everything CTest runs (docs/09_TESTS.md).
#
#   unit   the core game code's own *_test.cpp programs (next to the code they test, in src/core).
#          Pure logic plus checks on the shipped data; no window, no GPU. Seconds.
#   smoke  the real toms_game: a scripted scene, a screenshot, compared with a reference image in
#          tests/golden/. Needs a GPU and opens a window for a second or two.
#   web    tools/web_smoke_test.mjs in headless Chrome: the single- and multithreaded web builds and
#          the packaged page (skipped when a build, Node 22+ or Chrome is missing).
#
#   ctest --preset windows-release            all of them
#   ctest --preset windows-release -L unit    one label only
# Visual Studio: Test > Test Explorer lists every test (CMake presets feed it).

# ---- unit tests -------------------------------------------------------------------------------
# Each test is one .cpp with its own main(); it links the core library and uses what it needs.
# They read data files as "data/...", relative to the working directory -- the old project's
# layout -- so they run in assets/, the parent of assets/data.
set(_core "${TOMS_CORE_SRC}")
set(TOMS_UNIT_TESTS
    engine/anim_clip_test
    engine/condition_eval_test      engine/encounter_resolve_test  engine/entity_status_test
    engine/job_system_test
    engine/event_bus_test           engine/log_test                engine/node_test
    engine/node_transform_test      engine/object_test             engine/power_bar_test
    engine/state_machine_test
    game/core/atlas_sprites_test
    game/save/save_test
    game/systems/camera_test        game/systems/cycle_system_test     game/systems/ending_system_test
    game/systems/equipment_actives_test game/systems/equipment_test    game/systems/floor_table_test
    game/systems/footprint_test     game/systems/forge_system_test     game/systems/hub_system_test
    game/systems/mission_test       game/systems/run_state_test        game/systems/skill_system_test
    game/systems/story_controller_test
    game/ui/title_screen_test)

get_filename_component(_data_parent "${TOMS_DATA_DIR}" DIRECTORY)
foreach(_t IN LISTS TOMS_UNIT_TESTS)
    get_filename_component(_name "${_t}" NAME)
    add_executable(${_name} "${_core}/${_t}.cpp")
    toms_target_defaults(${_name})
    target_link_libraries(${_name} PRIVATE toms_core)
    set_target_properties(${_name} PROPERTIES FOLDER "tests/unit" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests")
    add_test(NAME unit.${_name} COMMAND ${_name})
    set_tests_properties(unit.${_name} PROPERTIES WORKING_DIRECTORY "${_data_parent}" LABELS unit TIMEOUT 60)
endforeach()

# ---- screenshot smoke tests -------------------------------------------------------------------
# The real game, a scripted scene (keys/clicks at given frames), a screenshot, compared with
# tests/golden/<name>.png. --fixed-dt makes every run identical (animations, battle bars, timers);
# real mouse/keyboard input is ignored in that mode. Tolerance = % of pixels allowed to differ
# (more than a small per-channel amount). The reference images come from Direct3D 11, where a run
# matches its reference exactly (0.000%); the other backends are compared with the same images at a
# looser limit (OpenGL differs on ~0.02% of pixels at edges). 0.1% is small enough to catch a changed
# HP bar (0.3% of the screen).
# After an intended visual change: TOMS_UPDATE_GOLDEN=1 ctest ... -L smoke  (then review the PNGs).
add_executable(image_diff tools/image_diff.cpp)
toms_target_defaults(image_diff)
target_include_directories(image_diff PRIVATE "${TOMS_THIRD_PARTY_DIR}/stb")
set_target_properties(image_diff PROPERTIES FOLDER "tests" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests")

set(TOMS_GOLDEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/golden")
function(toms_smoke_test name golden renderer tolerance)
    # The game's arguments travel as one "a|b|c" string; run_smoke.cmake splits it again.
    list(JOIN ARGN "|" _scene)
    set(_args "--renderer=${renderer}|--fixed-dt=16|${_scene}")
    add_test(NAME smoke.${name}
             COMMAND ${CMAKE_COMMAND}
                     -DGAME=$<TARGET_FILE:toms_game> -DDIFF=$<TARGET_FILE:image_diff>
                     -DNAME=${name} -DGOLDEN=${TOMS_GOLDEN_DIR}/${golden}.png
                     -DWORK=${CMAKE_BINARY_DIR}/test-run/${name} -DTOLERANCE=${tolerance}
                     "-DGAME_ARGS=${_args}"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/smoke/run_smoke.cmake)
    # One window at a time: the tests share the GPU and the screen.
    set_tests_properties(smoke.${name} PROPERTIES LABELS smoke RUN_SERIAL TRUE TIMEOUT 180)
endfunction()

#                name        reference   backend tolerance  scene
toms_smoke_test(title        title       d3d11   0.1  --frames=60)
toms_smoke_test(stage        stage       d3d11   0.1  --frames=120 --keys=enter@30)
toms_smoke_test(menu         menu        d3d11   0.1  --frames=110 --keys=enter@30,esc@80)
toms_smoke_test(inventory    inventory   d3d11   0.1  --frames=110 --keys=enter@30,i@80)
toms_smoke_test(dialogue     dialogue    d3d11   0.1  --frames=190 --keys=enter@30,esc@80,down@90,down@95,down@100,enter@110,enter@130)
toms_smoke_test(battle       battle      d3d11   0.1  --frames=420 --keys=enter@30,enter@380 --clicks=788:656@100)
toms_smoke_test(store        store       d3d11   0.1  --stage=stage02 --frames=150 --keys=enter@30,enter@60,enter@90,b@120)
# A node animation (tests/smoke/anim_preview.anim, anim_clip.h) over the map: nested transforms,
# rotation, easing, draw order and additive blending through the real sprite batch.
toms_smoke_test(anim         anim        d3d11   0.1  --frames=110 --keys=enter@30
                "--anim=${CMAKE_CURRENT_SOURCE_DIR}/smoke/anim_preview.anim#orbit")
toms_smoke_test(stage_d3d12  stage       d3d12   0.5  --frames=120 --keys=enter@30)
toms_smoke_test(stage_vulkan stage       vulkan  0.5  --frames=120 --keys=enter@30)
toms_smoke_test(stage_opengl stage       opengl  0.5  --frames=120 --keys=enter@30)

# ---- web smoke tests --------------------------------------------------------------------------
# tools/web_smoke_test.mjs in headless Chrome (title -> new game -> save -> reload). Each test is
# skipped -- not failed -- when its folder, Node 22+ or Chrome/Edge is missing.
#   web.smoke            the single-threaded build (tools\build_web.cmd)
#   web.smoke_mt         the multithreaded build (tools\build_web.cmd mt), served with COOP/COEP
#   web.page_coi         the packaged page (build_web.bat) on a server WITHOUT the headers, like
#                        GitHub Pages: coi-serviceworker must make it isolated -> threaded build
#   web.page_nothreads   the packaged page with ?nothreads: the single-threaded fallback
# docs/10_THREADS.md
find_program(TOMS_NODE node)
set(TOMS_WEB_BUILD_DIR    "${CMAKE_SOURCE_DIR}/Build/web-release-windows/bin"    CACHE PATH "Single-threaded web build tested by the 'web' label")
set(TOMS_WEB_MT_BUILD_DIR "${CMAKE_SOURCE_DIR}/Build/web-release-mt-windows/bin" CACHE PATH "Multithreaded web build tested by the 'web' label")
set(TOMS_WEB_PACKAGE_DIR  "${CMAKE_SOURCE_DIR}/Build/dist/TOMS-web"              CACHE PATH "Packaged web page tested by the 'web' label")
function(toms_web_test name dir)
    add_test(NAME web.${name}
             COMMAND "${TOMS_NODE}" "${CMAKE_SOURCE_DIR}/tools/web_smoke_test.mjs" "${dir}" "${CMAKE_BINARY_DIR}/test-run/web_${name}" ${ARGN})
    set_tests_properties(web.${name} PROPERTIES LABELS web SKIP_RETURN_CODE 77 RUN_SERIAL TRUE TIMEOUT 300)
endfunction()
if(TOMS_NODE)
    toms_web_test(smoke          "${TOMS_WEB_BUILD_DIR}"    --expect-threads=no)
    toms_web_test(smoke_mt       "${TOMS_WEB_MT_BUILD_DIR}" --isolate --expect-threads=yes)
    toms_web_test(page_coi       "${TOMS_WEB_PACKAGE_DIR}"  --expect-threads=yes)
    toms_web_test(page_nothreads "${TOMS_WEB_PACKAGE_DIR}"  --query=nothreads --expect-threads=no)
endif()
