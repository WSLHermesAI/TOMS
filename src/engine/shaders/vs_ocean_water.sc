$input a_position
$output v_worldPos, v_texcoord0

// The water: a grid over the screen, each vertex sent along its ray to the flat sea, then moved by the waves. So the
// vertices are spread evenly on screen, right out to the horizon, whatever the camera does.
#include <bgfx_shader.sh>
#include "ocean_common.sh"

SAMPLER2D(s_disp0, 0);
SAMPLER2D(s_disp1, 1);
SAMPLER2D(s_disp2, 2);

float oceanLod(float spacing, float texelsPerMetre)
{
    return max(log2(max(spacing * texelsPerMetre, 1.0)), 0.0);
}

void main()
{
    vec2 ndc = vec2((a_position.x * 2.0 - 1.0) * u_oceanGrid.x, mix(u_oceanGrid.y, u_oceanGrid.z, a_position.y));
    vec4 p = mul(u_oceanInvVP, vec4(ndc, 0.5, 1.0));
    vec3 dir = normalize(p.xyz / p.w - u_oceanEye.xyz);
    dir.y = min(dir.y, -0.00002);
    vec2 offset = dir.xz * (u_oceanEye.y / -dir.y);
    float dist = length(offset);
    float farthest = u_oceanGrid.w;
    if (dist > farthest) { offset *= farthest / dist; dist = farthest; }
    vec2 base = u_oceanEye.xz + offset;

    // How far apart the rows are here: far rows skip waves they could only alias, so use a smaller picture of them.
    float spacing = dist * dist * u_oceanGridB.x;
    vec3 disp = texture2DLod(s_disp0, base * u_oceanInvSize.x, oceanLod(spacing, u_oceanTexels.x)).xyz;
    disp += texture2DLod(s_disp1, base * u_oceanInvSize.y, oceanLod(spacing, u_oceanTexels.y)).xyz;
    disp += texture2DLod(s_disp2, base * u_oceanInvSize.z, oceanLod(spacing, u_oceanTexels.z)).xyz;
    disp *= u_oceanGridB.y;
    disp *= 1.0 - clamp((dist - 3000.0) / 6000.0, 0.0, 1.0);   // far waves would only poke above the horizon as specks

    vec3 world = vec3(base.x + disp.x, disp.y, base.y + disp.z);
    v_worldPos = world;
    v_texcoord0 = vec4(base, dist, disp.y);
    gl_Position = mul(u_viewProj, vec4(world, 1.0));
}
