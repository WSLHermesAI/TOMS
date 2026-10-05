$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_color0, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3
$output v_worldPos, v_normal, v_tangent, v_texcoord0, v_color0

// Instanced glTF primitive (EXT_mesh_gpu_instancing): one draw call, the model matrix per instance
// in i_data0..3 (its columns). Skinning as vs_mesh.sc.
#include <bgfx_shader.sh>
#include "mesh_common.sh"

void main()
{
    mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    vec4 world;
    vec3 n;
    vec4 t;
    meshTransform(model, a_position, a_normal, a_tangent, a_indices, a_weight, world, n, t);
    v_worldPos = world.xyz;
    v_normal = n;
    v_tangent = t;
    v_texcoord0 = vec4(a_texcoord0, a_texcoord1);
    v_color0 = a_color0;
    gl_Position = mul(u_viewProj, world);
}
