// mesh_common.sh -- shared by vs_mesh.sc and vs_mesh_inst.sc: GPU skinning.
//
// u_joints: the skin's joint matrices (world x inverse bind), up to gltf_model.h g_iMaxJoints: 128, or 48 on OpenGL ES. ES 3.0
// guarantees only 256 vertex uniform vectors, and 128 matrices are 512: the program then does not link on a phone or tablet
// and nothing is drawn, with no error on screen (2026-11-04: the glTF title on a Xiaomi Pad).
// u_meshFlags.x > 0.5: the vertex is skinned (a_indices / a_weight); else they are ignored.
#if BGFX_SHADER_LANGUAGE_ESSL
uniform mat4 u_joints[48];
#else
uniform mat4 u_joints[128];
#endif
uniform vec4 u_meshFlags;

mat4 skinMatrix(vec4 indices, vec4 weights)
{
    return weights.x * u_joints[int(indices.x)] + weights.y * u_joints[int(indices.y)]
         + weights.z * u_joints[int(indices.z)] + weights.w * u_joints[int(indices.w)];
}

// Object space -> world (the vertex inputs are passed in: HLSL has them only as main()'s parameters): skinning (when on), then the model matrix.
void meshTransform(mat4 model, vec3 position, vec3 normal, vec4 tangent, vec4 indices, vec4 weights,
                   out vec4 worldPos, out vec3 worldNormal, out vec4 worldTangent)
{
    vec4 p = vec4(position, 1.0);
    vec3 n = normal;
    vec3 t = tangent.xyz;
    if (u_meshFlags.x > 0.5)
    {
        mat4 skin = skinMatrix(indices, weights);
        p = mul(skin, p);
        n = mul(skin, vec4(n, 0.0)).xyz;
        t = mul(skin, vec4(t, 0.0)).xyz;
    }
    worldPos = mul(model, p);
    worldNormal = normalize(mul(model, vec4(n, 0.0)).xyz);
    worldTangent = vec4(normalize(mul(model, vec4(t, 0.0)).xyz + vec3(1e-6, 0.0, 0.0)), tangent.w);
}
