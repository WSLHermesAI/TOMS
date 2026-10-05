// embedded_shaders.h -- shader programs compiled by bgfx shaderc at build time and embedded in
// the executable, so F5 works with no shader files next to the exe. The right binary (DXBC,
// SPIR-V, GLSL or ESSL) is picked from bgfx::getRendererType() at runtime.
#pragma once
#include <bgfx/bgfx.h>

namespace toms::next {

// The sprite batch's GPU paths: SpriteInstanced (vs_sprite_inst.sc + fs_sprite.sc, one instance per
// quad); SpriteCompute (cs_sprite.sc, a compute program) + SpriteFromCompute (vs_sprite_cs.sc +
// fs_sprite.sc) for the compute path -- those two are invalid where the backend has no compute shaders.
// FxSpawn / FxUpdate (cs_fx_*.sc): GPU-simulated particles (IRenderer::drawGpuParticles), also compute only.
// Mesh / MeshInstanced (vs_mesh*.sc + fs_mesh.sc): glTF models (gltf_renderer.h); Line (vs_line.sc +
// fs_line.sc): debug lines; MeshShadow / MeshShadowInstanced (vs_mesh*.sc + fs_shadow.sc): the shadow
// pass, depth only. Every backend has them.
enum class ShaderProgram { Sprite, ImGui, RmlUi, SpriteInstanced, SpriteCompute, SpriteFromCompute, FxSpawn, FxUpdate,
                           Mesh, MeshInstanced, Line, MeshShadow, MeshShadowInstanced };

// Creates the program for the active renderer. Returns an invalid handle (and logs) if the
// renderer type has no embedded binary (e.g. Metal on Windows builds).
bgfx::ProgramHandle createEmbeddedProgram(ShaderProgram which);

}  // namespace toms::next
