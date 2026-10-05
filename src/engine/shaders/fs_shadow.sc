$input v_worldPos, v_normal, v_tangent, v_texcoord0, v_color0

// The shadow pass (gltf_renderer.cpp): depth only, into a tile of the shadow atlas. vs_mesh.sc /
// vs_mesh_inst.sc place the vertices (skinned, instanced) exactly as in the colour pass.
// Its $input must list exactly what the vertex shader outputs: bgfx refuses the program otherwise.
#include <bgfx_shader.sh>

void main()
{
    gl_FragColor = vec4_splat(0.0);
}
