// embedded_shaders.h -- shader programs compiled by bgfx shaderc at build time and embedded in
// the executable, so F5 works with no shader files next to the exe. The right binary (DXBC,
// SPIR-V, GLSL or ESSL) is picked from bgfx::getRendererType() at runtime.
#pragma once
#include <bgfx/bgfx.h>

namespace toms::next {

enum class ShaderProgram { Sprite, ImGui };

// Creates the program for the active renderer. Returns an invalid handle (and logs) if the
// renderer type has no embedded binary (e.g. Metal on Windows builds).
bgfx::ProgramHandle createEmbeddedProgram(ShaderProgram which);

}  // namespace toms::next
