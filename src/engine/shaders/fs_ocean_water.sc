$input v_worldPos, v_texcoord0

// The water's colour: Fresnel between the sky's reflection and the light from inside the water, the sun on the waves, foam,
// and haze toward the horizon.
#include <bgfx_shader.sh>
#include "ocean_common.sh"

SAMPLER2D(s_slope0, 3);
SAMPLER2D(s_slope1, 4);
SAMPLER2D(s_slope2, 5);

void main()
{
    vec2 base = v_texcoord0.xy;
    float dist = v_texcoord0.z;
    float height = v_texcoord0.w;
    vec4 s0 = texture2D(s_slope0, base * u_oceanInvSize.x);
    vec4 s1 = texture2D(s_slope1, base * u_oceanInvSize.y);
    vec4 s2 = texture2D(s_slope2, base * u_oceanInvSize.z);
    vec2 slope = (s0.xy + s1.xy + s2.xy) * u_oceanGridB.y;
    float foamAmount = clamp(s0.z + s1.z + s2.z, 0.0, 1.0);

    vec3 toEye = u_oceanEye.xyz - v_worldPos;
    vec3 V = normalize(toEye);
    vec3 N = normalize(vec3(-slope.x, 1.0, -slope.y));
    float NdotV = dot(N, V);
    if (NdotV < 0.02) { N = normalize(N + V * (0.02 - NdotV)); NdotV = 0.02; }
    vec3 L = u_oceanSun.xyz;

    // Far away one pixel covers many small waves: the surface acts rougher, and a grazing view does not become a mirror.
    float far = 1.0 - exp(-dist / 900.0);
    float rough = mix(u_oceanDeep.w, 0.42, far);
    float grazing = pow(1.0 - clamp(NdotV, 0.0, 1.0), 5.0);
    float fresnel = 0.02 + 0.98 * grazing * mix(1.0, 0.45, far);
    fresnel = clamp(fresnel * u_oceanFoam.z, 0.0, 1.0);

    vec3 R = reflect(-V, N);
    R.y = abs(R.y);
    R = normalize(R);
    vec3 reflected = oceanSky(R, int(u_oceanDetail.y));

    // Light from inside the water: deep colour, lit by the sky and the sun, and a glow through the waves' thin tops,
    // strongest looking toward the sun.
    vec3 skyLight = oceanClearSky(vec3(0.0, 1.0, 0.0));
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    vec3 inside = u_oceanDeep.xyz * (skyLight * 0.9 + u_oceanSunColor.xyz * NdotL * 0.35);
    float crest = clamp(height * 0.22 + 0.3, 0.0, 1.5);
    vec3 flatToEye = normalize(vec3(V.x, 0.0, V.z) + vec3_splat(0.0001));
    float backLit = pow(clamp(dot(flatToEye, -normalize(vec3(L.x, 0.0, L.z) + vec3_splat(0.0001))), 0.0, 1.0), 3.0);
    inside += u_oceanScatter.xyz * u_oceanScatter.w * crest * crest * (skyLight * 0.5 + u_oceanSunColor.xyz * (0.12 + backLit * 0.7));

    vec3 color = mix(inside, reflected, fresnel);

    // The sun on the waves (GGX).
    vec3 H = normalize(V + L);
    float NdotH = clamp(dot(N, H), 0.0, 1.0);
    float a = rough * rough;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float spec = a2 / max(3.14159 * d * d, 0.0001);
    spec *= 1.0 - 0.95 * clamp((u_oceanClouds.x - 0.6) / 0.3, 0.0, 1.0);   // no glitter under a closed sky
    color += u_oceanSunColor.xyz * spec * NdotL * (0.02 + 0.98 * pow(1.0 - clamp(dot(V, H), 0.0, 1.0), 5.0)) * 1.2;

    // Foam: the simulation says where, noise breaks it into patches and streaks.
    if (foamAmount > 0.0) {
        float detail = oceanFbm(base * 0.45 + vec2(u_oceanSun.w * 0.05, 0.0), int(u_oceanDetail.z));
        float foam = clamp((foamAmount * 1.25 - (1.0 - detail) * 0.75) * 2.2, 0.0, 1.0);
        vec3 foamLight = skyLight * 0.75 + u_oceanSunColor.xyz * (0.25 + NdotL * 0.55);
        color = mix(color, foamLight * u_oceanFoam.x, foam * (1.0 - far * 0.6));
    }

    // Haze: the far water fades into the sky just above the horizon, so the line between them is soft.
    vec3 horizonDir = normalize(vec3(-V.x, 0.015, -V.z));
    float haze = 1.0 - exp(-dist / u_oceanFoam.y);
    // All sky within a few pixels of the horizon: by the view's angle, since the last grid cells stretch too far to trust dist.
    haze = max(haze * haze, clamp(1.0 - V.y / 0.008, 0.0, 1.0));
    color = mix(color, oceanClearSky(horizonDir), haze);

    gl_FragColor = vec4(oceanTone(color), 1.0);
}
