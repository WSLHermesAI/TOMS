$input a_position, a_color0, a_texcoord0
$output v_texcoord0, v_color0

// RmlUi geometry. Positions are in UI (design) pixels; u_modelViewProj carries RmlUi's
// per-draw translation and optional element transform (bgfx::setTransform) plus the view's
// orthographic projection. Colours are premultiplied alpha (see rml_ui.cpp's blend state).
// u_rmlParams.x = 1: the backbuffer is sRGB, so the (sRGB) RCSS colours are linearized here and
// a colour written in store.rcss is the colour on screen.
#include <bgfx_shader.sh>

uniform vec4 u_rmlParams;

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 0.0, 1.0));
    v_texcoord0 = a_texcoord0;
    vec4 c = a_color0;
    if (u_rmlParams.x > 0.5 && c.a > 0.0)
        c.rgb = pow(max(c.rgb / c.a, vec3_splat(0.0)), vec3_splat(2.2)) * c.a;
    v_color0 = c;
}
