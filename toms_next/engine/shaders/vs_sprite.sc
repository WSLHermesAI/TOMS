$input a_position, a_texcoord0, a_color0, a_texcoord1
$output v_texcoord0, v_color0, v_solid

// Sprite/text quads in design-space pixels (1024x768). The view's projection maps them to the
// letterboxed viewport, which replaces the Vulkan shader's push-constant res/xform.
#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_viewProj, vec4(a_position, 0.0, 1.0));
    v_texcoord0 = a_texcoord0;
    v_color0    = a_color0;
    v_solid     = a_texcoord1;
}
