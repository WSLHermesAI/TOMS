$input a_position
$output v_worldPos

// The sky: one triangle over the screen at the far plane; v_worldPos carries the view direction.
#include <bgfx_shader.sh>

uniform mat4 u_oceanInvVP;
uniform vec4 u_oceanEye;

void main()
{
    gl_Position = vec4(a_position.xy, 1.0, 1.0);
    vec4 p = mul(u_oceanInvVP, vec4(a_position.xy, 0.5, 1.0));
    v_worldPos = p.xyz / p.w - u_oceanEye.xyz;
}
