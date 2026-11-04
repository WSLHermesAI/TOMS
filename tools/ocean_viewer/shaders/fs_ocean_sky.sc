$input v_worldPos

// The sky behind the sea: Preetham's daylight, clouds, the sun's disc. Below the horizon it stays the horizon's colour.
#include <bgfx_shader.sh>
#include "ocean_common.sh"

void main()
{
    vec3 raw = normalize(v_worldPos);
    // Under the horizon only a gap at the water's edge can show: it must read as water, not as bright sky.
    if (raw.y < -0.02) {
        gl_FragColor = vec4(oceanTone(u_oceanDeep.xyz * oceanClearSky(vec3(0.0, 1.0, 0.0))), 1.0);
        return;
    }
    vec3 dir = normalize(vec3(raw.x, max(raw.y, 0.0), raw.z));
    vec3 color = oceanSky(dir, int(u_oceanDetail.x));
    float cosGamma = dot(dir, u_oceanSun.xyz);
    float disc = clamp((cosGamma - 0.99985) / 0.00005, 0.0, 1.0);
    float glow = pow(max(cosGamma, 0.0), 800.0) * 0.6;
    color += u_oceanSunColor.xyz * (disc * u_oceanSunColor.w + glow);
    gl_FragColor = vec4(oceanTone(color), 1.0);
}
