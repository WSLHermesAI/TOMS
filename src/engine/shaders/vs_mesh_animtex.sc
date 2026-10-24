$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_color0, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_worldPos, v_normal, v_tangent, v_texcoord0, v_color0

// Instanced skinning for crowds (gltf_anim_texture.h, docs/engine/08_GLTF.md "Crowds"): one draw call for every
// instance of a model; each instance reads its joints from the shared animation texture.
//   s_animTex: RGBA32F, one row per baked frame, 2 texels per joint -- (rotation quaternion) and
//              (translation xyz, uniform scale) of world(joint) x inverseBind.
//   i_data0..2: the instance's placement, as its first three rows (the fourth is 0, 0, 0, 1).
//   i_data3: clip A -- row of frame, row of the next frame, how far between them.
//   i_data4: clip B -- the same, and in w the weight of B (0: B is not read).
// Rotations are nlerped (signs aligned, so they blend the short way) and translation / scale lerped, so a
// blend between two clips keeps every limb's length. The fragment shader is fs_mesh.sc (or fs_shadow.sc).
#include <bgfx_shader.sh>

SAMPLER2D(s_animTex, 6);

vec4 animTexel(int x, float row)
{
    return texelFetch(s_animTex, ivec2(x, int(row)), 0);
}

// One clip's pose of a joint: nlerp between its two frames.
void clipPose(int joint, vec4 f, out vec4 q, out vec4 ts)
{
    int xq = joint * 2;
    vec4 q0 = animTexel(xq, f.x);
    vec4 q1 = animTexel(xq, f.y);
    q = q0 * (1.0 - f.z) + q1 * (dot(q0, q1) < 0.0 ? -f.z : f.z);
    ts = mix(animTexel(xq + 1, f.x), animTexel(xq + 1, f.y), f.z);
}

mat4 jointMatrix(int joint, vec4 a, vec4 b)
{
    vec4 q;
    vec4 ts;
    clipPose(joint, a, q, ts);
    if (b.w > 0.0)
    {
        vec4 qb;
        vec4 tsb;
        clipPose(joint, b, qb, tsb);
        q = q * (1.0 - b.w) + qb * (dot(q, qb) < 0.0 ? -b.w : b.w);
        ts = mix(ts, tsb, b.w);
    }
    q = normalize(q);
    float x = q.x;
    float y = q.y;
    float z = q.z;
    float w = q.w;
    vec3 c0 = vec3(1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + w * z), 2.0 * (x * z - w * y)) * ts.w;
    vec3 c1 = vec3(2.0 * (x * y - w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + w * x)) * ts.w;
    vec3 c2 = vec3(2.0 * (x * z + w * y), 2.0 * (y * z - w * x), 1.0 - 2.0 * (x * x + y * y)) * ts.w;
    return mtxFromCols(vec4(c0, 0.0), vec4(c1, 0.0), vec4(c2, 0.0), vec4(ts.xyz, 1.0));
}

void main()
{
    mat4 model = mtxFromRows(i_data0, i_data1, i_data2, vec4(0.0, 0.0, 0.0, 1.0));
    // Linear blend skinning over the vertex's joints; a joint with no weight is not read.
    mat4 skin = a_weight.x * jointMatrix(int(a_indices.x), i_data3, i_data4);
    if (a_weight.y > 0.0) skin += a_weight.y * jointMatrix(int(a_indices.y), i_data3, i_data4);
    if (a_weight.z > 0.0) skin += a_weight.z * jointMatrix(int(a_indices.z), i_data3, i_data4);
    if (a_weight.w > 0.0) skin += a_weight.w * jointMatrix(int(a_indices.w), i_data3, i_data4);

    vec4 p = mul(skin, vec4(a_position, 1.0));
    vec3 n = mul(skin, vec4(a_normal, 0.0)).xyz;
    vec3 t = mul(skin, vec4(a_tangent.xyz, 0.0)).xyz;
    vec4 world = mul(model, p);
    v_worldPos = world.xyz;
    v_normal = normalize(mul(model, vec4(n, 0.0)).xyz);
    v_tangent = vec4(normalize(mul(model, vec4(t, 0.0)).xyz + vec3(1e-6, 0.0, 0.0)), a_tangent.w);
    v_texcoord0 = vec4(a_texcoord0, a_texcoord1);
    v_color0 = a_color0;
    gl_Position = mul(u_viewProj, world);
}
