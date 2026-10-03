$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0, v_color0, v_solid

// The GPU path of the sprite batch (bgfx_renderer.cpp): one instance per quad instead of four
// CPU-built vertices. a_position is the corner of a shared unit quad, (0,0) (1,0) (1,1) (0,1);
// the instance carries the quad's corners exactly as the CPU path would write them, so both paths
// draw the same pixels:
//   i_data0 = corner 0 (x, y), corner 1 (x, y)      i_data2 = uv rect (u0, v0, u1, v1); u0 < -0.5 = solid
//   i_data1 = corner 2 (x, y), corner 3 (x, y)      i_data3 = tint (r, g, b, a)
#include <bgfx_shader.sh>

void main()
{
    float u = a_position.x;
    float v = a_position.y;
    // Each corner of the unit quad picks its own corner (no blending between them: exact).
    vec2 top    = i_data0.xy * (1.0 - u) + i_data0.zw * u;   // corners 0 -> 1
    vec2 bottom = i_data1.zw * (1.0 - u) + i_data1.xy * u;   // corners 3 -> 2
    vec2 pos    = top * (1.0 - v) + bottom * v;
    gl_Position = mul(u_viewProj, vec4(pos, 0.0, 1.0));
    bool solid  = i_data2.x < -0.5;
    v_texcoord0 = vec2(mix(i_data2.x, i_data2.z, u), mix(i_data2.y, i_data2.w, v));
    v_color0    = i_data3;
    v_solid     = solid ? 1.0 : 0.0;
}
