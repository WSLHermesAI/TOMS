$input a_texcoord2, a_color1, a_texcoord3
$output v_texcoord0, v_color0, v_solid

// Draws the vertices cs_sprite.sc wrote (the compute path of bgfx_renderer.cpp).
#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_viewProj, vec4(a_texcoord2.xy, 0.0, 1.0));
    v_texcoord0 = a_texcoord2.zw;
    v_color0    = a_color1;
    v_solid     = a_texcoord3.x;
}
