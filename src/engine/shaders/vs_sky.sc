$input a_position
$output v_worldPos

// The environment behind the models (gltf_renderer.cpp drawSky): one triangle over the whole screen at the far
// plane; v_worldPos carries the view direction (eye -> far plane), which the fragment shader looks up.
#include <bgfx_shader.sh>

uniform mat4 u_skyMtx;   // clip -> world (the inverse of the frame's projection x view)
uniform vec4 u_camPos;   // eye xyz

void main()
{
    gl_Position = vec4(a_position.xy, 1.0, 1.0);   // depth 1: behind everything (drawn with LEQUAL, no depth write)
    vec4 p = mul(u_skyMtx, vec4(a_position.xy, 0.5, 1.0));
    v_worldPos = p.xyz / p.w - u_camPos.xyz;
}
