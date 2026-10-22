# anim.check_skin_meshes -- `anim_editor --headless check` reports a skinned sprite that has no mesh in its atlas's mesh
# file (it would be drawn as a flat quad; docs/34_MESH_ANIMATION.md), and passes with it. Run with
#   cmake -DANIM_EDITOR=<anim_editor> -DROOT=<TOMS> -DOUT=<scratch folder> -P check_skin_meshes.cmake

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
file(COPY "${ROOT}/assets/media/atlas/game.atlas" "${ROOT}/assets/media/atlas/game.png" DESTINATION "${OUT}")   # no mesh file

function(check expect_code must_say)
    execute_process(COMMAND "${ANIM_EDITOR}" --headless check "${ROOT}/tests/smoke/anim_skin.anim" --atlas "game=${OUT}/game.atlas"
                    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT code EQUAL expect_code)
        message(FATAL_ERROR "exit ${code}, expected ${expect_code}:
${out}${err}")
    endif()
    if(must_say AND NOT out MATCHES "${must_say}")
        message(FATAL_ERROR "the output does not say '${must_say}':
${out}${err}")
    endif()
    if(NOT must_say AND out MATCHES "has no mesh")
        message(FATAL_ERROR "a mesh is reported missing although the mesh file is there:
${out}")
    endif()
endfunction()

# The atlas without its mesh file: the golem is skinned, so it is an error.
check(2 "skinned, but the sprite 'golem' has no mesh")
# With it (game.mesh.json and its game.mesh.bin): no error.
file(GLOB mesh_files "${ROOT}/assets/media/atlas/game.mesh.*")
file(COPY ${mesh_files} DESTINATION "${OUT}")
check(0 "")
message(STATUS "anim.check_skin_meshes: ok")
