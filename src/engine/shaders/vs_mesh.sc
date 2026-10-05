$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_color0, a_indices, a_weight
$output v_worldPos, v_normal, v_tangent, v_texcoord0, v_color0

// A glTF primitive (gltf_renderer.cpp): skinned on the GPU when u_meshFlags.x is set, placed by
// u_model[0] (identity for a skinned primitive: the joints already give model space).
#include <bgfx_shader.sh>
#include "mesh_common.sh"

void main()
{
    vec4 world;
    vec3 n;
    vec4 t;
    meshTransform(u_model[0], a_position, a_normal, a_tangent, a_indices, a_weight, world, n, t);
    v_worldPos = world.xyz;
    v_normal = n;
    v_tangent = t;
    v_texcoord0 = vec4(a_texcoord0, a_texcoord1);
    v_color0 = a_color0;
    gl_Position = mul(u_viewProj, world);
}
