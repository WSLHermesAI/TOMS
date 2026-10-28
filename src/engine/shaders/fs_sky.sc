$input v_worldPos

// The environment picture as the background (vs_sky.sc): exposure, tone mapping and sRGB as fs_mesh.sc.
#include <bgfx_shader.sh>
#include "env_common.sh"

uniform vec4 u_camPos;       // eye xyz, exposure
uniform vec4 u_skyColor;     // -, 1 = write sRGB
uniform vec4 u_groundColor;  // -, 1 = ACES tone mapping
uniform vec4 u_envSky;       // the mip to show (blur), -, -, -

vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec3 color = envSample(normalize(v_worldPos), u_envSky.x) * u_camPos.w;
    if (u_groundColor.w > 0.5) color = aces(color);
    if (u_skyColor.w > 0.5) color = pow(max(color, vec3_splat(0.0)), vec3_splat(1.0 / 2.2));
    gl_FragColor = vec4(color, 1.0);
}
