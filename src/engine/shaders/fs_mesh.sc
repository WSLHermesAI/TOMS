$input v_worldPos, v_normal, v_tangent, v_texcoord0, v_color0

// glTF metallic-roughness PBR (gltf_renderer.cpp, docs/18_GLTF.md):
//   base colour x vertex colour x texture, metal/rough texture (B = metal, G = rough), normal map
//   (with the file's tangents, else a tangent frame from screen-space derivatives), occlusion (R),
//   emissive; alpha mask (discard) / blend; double-sided back faces lit from behind; unlit.
//   Light: up to 4 directional / point / spot lights (GGX, Smith, Schlick; point and spot fall off
//   with 1/d^2 and their range, spots between their cones), each with an optional shadow map in the
//   shadow atlas (3x3 hardware PCF; a point light has 6 tiles, one per cube face). A hemisphere
//   ambient (sky / ground) with an analytic environment BRDF. Then exposure, ACES tone mapping, sRGB.
//
//   OpenGL ES / WebGL2 (BGFX_SHADER_LANGUAGE_ESSL): no shadow lookups. WebGL on Windows runs through
//   ANGLE -> Direct3D, which cannot link this shader with them (the dynamically indexed shadow matrix
//   array): the link fails with an empty log and the game stopped (2026-10-05). GltfRenderer reports
//   no shadow support on that backend, so nothing asks for them there.
#include <bgfx_shader.sh>

SAMPLER2D(s_baseColor, 0);
SAMPLER2D(s_metalRough, 1);
SAMPLER2D(s_normalMap, 2);
SAMPLER2D(s_occlusion, 3);
SAMPLER2D(s_emissive, 4);
SAMPLER2DSHADOW(s_shadowMap, 5);

uniform vec4 u_baseColor;        // factor (linear)
uniform vec4 u_pbr;              // metallic, roughness, normal scale, occlusion strength
uniform vec4 u_emissiveCutoff;   // emissive rgb (linear), alpha cutoff (< 0: no mask)
uniform vec4 u_texSet;           // base, metal/rough, normal, occlusion: 0 none, 1 uv0, 2 uv1
uniform vec4 u_texSet2;          // emissive (0 / 1 / 2), unlit, has tangents, debug view (0 lit, 1 normal, 2 base, 3 metal/rough, 4 uv, 5 shadow)
uniform vec4 u_uvTransform;      // KHR_texture_transform: offset xy, scale zw
uniform vec4 u_uvRotation;       // rotation (radians), -, -, -
uniform vec4 u_camPos;           // eye xyz, exposure
uniform vec4 u_lightPos[4];      // xyz position, w type: 0 directional, 1 point, 2 spot
uniform vec4 u_lightDirs[4];     // xyz direction the light shines, w range (0 = infinite)
uniform vec4 u_lightColors[4];   // rgb x intensity, w first shadow tile (-1 = no shadow)
uniform vec4 u_lightSpot[4];     // cos(outer cone), 1 / (cos(inner) - cos(outer)), depth bias, normal offset
uniform vec4 u_lightCount;       // lights, ambient intensity, shadow atlas texel (1 / size), shadows on
uniform mat4 u_shadowMtx[24];    // world -> shadow atlas (uv, depth) per tile
uniform vec4 u_shadowRect[24];   // the tile's uv rectangle (u0, v0, u1, v1)
uniform vec4 u_skyColor;         // hemisphere top rgb, 1 = write sRGB
uniform vec4 u_groundColor;      // hemisphere bottom rgb, 1 = ACES tone mapping
uniform vec4 u_matParams;        // double-sided, transmission, -, -

#define PI 3.14159265

// (The varyings are passed in: HLSL has them only as main()'s parameters.)
vec2 uvFor(vec4 uvs, float set)
{
    vec2 uv = set > 1.5 ? uvs.zw : uvs.xy;
    float c = cos(u_uvRotation.x);
    float s = sin(u_uvRotation.x);
    vec2 scaled = uv * u_uvTransform.zw;
    return vec2(c * scaled.x + s * scaled.y, -s * scaled.x + c * scaled.y) + u_uvTransform.xy;
}

vec3 toLinear(vec3 c) { return pow(abs(c), vec3_splat(2.2)); }

float D_GGX(float NoH, float a)
{
    float a2 = a * a;
    float f = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * f * f + 1e-7);
}

float V_SmithGGX(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (gv + gl + 1e-5);
}

vec3 F_Schlick(vec3 f0, float VoH) { return f0 + (vec3_splat(1.0) - f0) * pow(1.0 - VoH, 5.0); }

// Karis' analytic approximation of the split-sum environment BRDF (mobile UE4).
vec3 envBRDF(vec3 f0, float rough, float NoV)
{
    vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

vec3 hemisphere(vec3 dir) { return mix(u_groundColor.rgb, u_skyColor.rgb, dir.y * 0.5 + 0.5); }

// What a glossy surface reflects: a studio-like sky with a bright horizon over a darker floor (what
// makes metal read as metal), blurring into the plain hemisphere as the surface gets rough.
vec3 environment(vec3 dir, float rough)
{
    vec3 horizon = u_skyColor.rgb * 1.6;
    float y = dir.y;
    vec3 sharp = y >= 0.0 ? mix(horizon, u_skyColor.rgb, sqrt(clamp(y, 0.0, 1.0)))
                          : mix(horizon * 0.45, u_groundColor.rgb * 0.6, sqrt(clamp(-y * 4.0, 0.0, 1.0)));
    return mix(sharp, hemisphere(dir), rough);
}

vec3 directLight(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 diffuse, vec3 f0, float a)
{
    vec3 H = normalize(L + V);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoV = clamp(abs(dot(N, V)), 1e-4, 1.0);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);
    vec3 F = F_Schlick(f0, VoH);
    vec3 spec = D_GGX(NoH, a) * V_SmithGGX(NoV, NoL, a) * F;
    vec3 diff = (vec3_splat(1.0) - F) * diffuse / PI;
    return (diff + spec) * radiance * NoL;
}

// 0 = in shadow, 1 = lit; 3x3 taps of hardware 2x2 PCF, kept inside the light's tile.
float shadowAt(vec3 worldPos, int tile, float bias)
{
    vec4 sc = mul(u_shadowMtx[tile], vec4(worldPos, 1.0));
    vec3 p = sc.xyz / sc.w;
    vec4 rect = u_shadowRect[tile];
    if (p.x < rect.x || p.x > rect.z || p.y < rect.y || p.y > rect.w || p.z >= 1.0 || p.z <= 0.0) return 1.0;
    float texel = u_lightCount.z;
    vec2 lo = rect.xy + vec2_splat(texel * 0.5);
    vec2 hi = rect.zw - vec2_splat(texel * 0.5);
    float lit = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 uv = clamp(p.xy + vec2(float(x), float(y)) * texel, lo, hi);
            lit += shadow2D(s_shadowMap, vec3(uv, p.z - bias));
        }
    }
    return lit / 9.0;
}

// One light's light at this point, with its shadow. Called once per light with constant array indices
// (main()), which keeps the program simple for the WebGL path too.
vec3 shadeLight(vec4 lp, vec4 ld, vec4 lc, vec4 ls, vec3 worldPos, vec3 Ng, vec3 N, vec3 V, vec3 diffuse, vec3 f0, float a,
                inout float firstShadow)
{
    vec3 L = -ld.xyz;
    float att = 1.0;
    float dist = 1.0;
    if (lp.w > 0.5)   // point / spot
    {
        vec3 toL = lp.xyz - worldPos;
        dist = max(length(toL), 1e-4);
        L = toL / dist;
        att = 1.0 / (dist * dist);
        if (ld.w > 0.0)   // KHR_lights_punctual's smooth range window
        {
            float r = dist / ld.w;
            float w = clamp(1.0 - r * r * r * r, 0.0, 1.0);
            att *= w * w;
        }
        if (lp.w > 1.5)   // spot cone
        {
            float t = clamp((dot(-L, ld.xyz) - ls.x) * ls.y, 0.0, 1.0);
            att *= t * t;
        }
    }
    if (att <= 0.0) return vec3_splat(0.0);
    float lit = 1.0;
#if !BGFX_SHADER_LANGUAGE_ESSL
    if (lc.w >= 0.0 && u_lightCount.w > 0.5)
    {
        int tile = int(lc.w + 0.5);
        if (lp.w > 0.5 && lp.w < 1.5)   // point: the cube face the point is on
        {
            vec3 d = worldPos - lp.xyz;
            vec3 ad = abs(d);
            int face = 0;
            if (ad.x >= ad.y && ad.x >= ad.z) face = d.x > 0.0 ? 0 : 1;
            else if (ad.y >= ad.z) face = d.y > 0.0 ? 2 : 3;
            else face = d.z > 0.0 ? 4 : 5;
            tile += face;
        }
        // Against acne: a normal offset (more where the light grazes the surface) and, for point / spot
        // lights, a step towards the light -- both about a shadow texel in world units, which for a
        // perspective map grows with the distance (a depth-buffer bias would be far too big far away).
        float texelWorld = ls.w * (lp.w > 0.5 ? dist : 1.0);
        // (x3 more at grazing angles: a low-poly curved surface's real facets would shadow its smooth
        // shading near the terminator)
        vec3 at = worldPos + Ng * texelWorld * (1.0 + 3.0 * (1.0 - clamp(dot(Ng, L), 0.0, 1.0)));
        if (lp.w > 0.5) at += L * texelWorld;
        lit = shadowAt(at, tile, ls.z);
        if (firstShadow > 1.5) firstShadow = lit;
    }
#endif
    return directLight(L, lc.rgb * (att * lit), N, V, diffuse, f0, a);
}

vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    // Base colour.
    vec4 base = u_baseColor * v_color0;
    if (u_texSet.x > 0.5)
    {
        vec4 t = texture2D(s_baseColor, uvFor(v_texcoord0, u_texSet.x));   // an sRGB texture: sampled linear
        base *= t;
    }
    if (u_emissiveCutoff.w >= 0.0 && base.a < u_emissiveCutoff.w) discard;

    if (u_texSet2.y > 0.5)   // KHR_materials_unlit
    {
        vec3 c = base.rgb;
        gl_FragColor = vec4(u_skyColor.w > 0.5 ? pow(abs(c), vec3_splat(1.0 / 2.2)) : c, base.a);
        return;
    }

    float metallic = u_pbr.x;
    float rough = u_pbr.y;
    if (u_texSet.y > 0.5)
    {
        vec4 mr = texture2D(s_metalRough, uvFor(v_texcoord0, u_texSet.y));
        rough *= mr.g;
        metallic *= mr.b;
    }
    rough = clamp(rough, 0.04, 1.0);
    float a = rough * rough;

    // Normal. A double-sided material's back face is lit from its own side: decided from the normal
    // and the view, not gl_FrontFacing -- Direct3D calls clockwise triangles "front" while glTF's
    // front faces are counter-clockwise, so that flag is backwards there.
    vec3 V = normalize(u_camPos.xyz - v_worldPos);
    vec3 N = normalize(v_normal);
    if (u_matParams.x > 0.5 && dot(N, V) < 0.0) N = -N;
    if (u_texSet.z > 0.5)
    {
        vec2 uv = uvFor(v_texcoord0, u_texSet.z);
        vec3 T;
        vec3 B;
        if (u_texSet2.z > 0.5)
        {
            T = normalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
            B = cross(N, T) * v_tangent.w;
        }
        else   // no tangents in the file: a frame from the derivatives of position and uv
        {
            vec3 dp1 = dFdx(v_worldPos);
            vec3 dp2 = dFdy(v_worldPos);
            vec2 duv1 = dFdx(uv);
            vec2 duv2 = dFdy(uv);
            vec3 dp2perp = cross(dp2, N);
            vec3 dp1perp = cross(N, dp1);
            T = dp2perp * duv1.x + dp1perp * duv2.x;
            B = dp2perp * duv1.y + dp1perp * duv2.y;
            float inv = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);
            T *= inv;
            B *= inv;
        }
        vec3 tn = texture2D(s_normalMap, uv).xyz * 2.0 - 1.0;
        tn.xy *= u_pbr.z;
        N = normalize(T * tn.x + B * tn.y + N * tn.z);
    }

    float transmission = u_matParams.y;
    vec3 diffuse = base.rgb * (1.0 - metallic) * (1.0 - transmission);   // glass: light goes through
    vec3 f0 = mix(vec3_splat(0.04), base.rgb, metallic);

    // The lights (up to 4; unrolled, see shadeLight()).
    vec3 color = vec3_splat(0.0);
    float firstShadow = 2.0;   // the first shadowed light's term (debug view 5)
    vec3 Ng = normalize(v_normal);   // the surface (not the normal map) for the shadow offset
    if (u_lightCount.x > 0.5)
        color += shadeLight(u_lightPos[0], u_lightDirs[0], u_lightColors[0], u_lightSpot[0], v_worldPos, Ng, N, V, diffuse, f0, a, firstShadow);
    if (u_lightCount.x > 1.5)
        color += shadeLight(u_lightPos[1], u_lightDirs[1], u_lightColors[1], u_lightSpot[1], v_worldPos, Ng, N, V, diffuse, f0, a, firstShadow);
    if (u_lightCount.x > 2.5)
        color += shadeLight(u_lightPos[2], u_lightDirs[2], u_lightColors[2], u_lightSpot[2], v_worldPos, Ng, N, V, diffuse, f0, a, firstShadow);
    if (u_lightCount.x > 3.5)
        color += shadeLight(u_lightPos[3], u_lightDirs[3], u_lightColors[3], u_lightSpot[3], v_worldPos, Ng, N, V, diffuse, f0, a, firstShadow);

    // Ambient: hemisphere irradiance + a reflection of the same hemisphere.
    float NoV = clamp(abs(dot(N, V)), 1e-4, 1.0);
    vec3 R = reflect(-V, N);
    vec3 ambient = diffuse * hemisphere(N) + envBRDF(f0, rough, NoV) * environment(R, rough);
    float ao = 1.0;
    if (u_texSet.w > 0.5) ao = mix(1.0, texture2D(s_occlusion, uvFor(v_texcoord0, u_texSet.w)).r, u_pbr.w);
    color += ambient * u_lightCount.y * ao;

    vec3 emissive = u_emissiveCutoff.rgb;
    if (u_texSet2.x > 0.5) emissive *= texture2D(s_emissive, uvFor(v_texcoord0, u_texSet2.x)).rgb;
    color += emissive;

    // Debug views.
    if (u_texSet2.w > 0.5 && u_texSet2.w < 1.5) color = toLinear(N * 0.5 + 0.5);
    else if (u_texSet2.w > 1.5 && u_texSet2.w < 2.5) color = base.rgb;
    else if (u_texSet2.w > 2.5 && u_texSet2.w < 3.5) color = toLinear(vec3(0.0, rough, metallic));
    else if (u_texSet2.w > 3.5 && u_texSet2.w < 4.5) color = toLinear(vec3(fract(v_texcoord0.xy), 0.0));
    else if (u_texSet2.w > 4.5) color = firstShadow > 1.5 ? vec3(0.0, 0.0, 0.4) : vec3_splat(firstShadow);   // blue: no shadow map
    else
    {
        color *= u_camPos.w;   // exposure
        if (u_groundColor.w > 0.5) color = aces(color);
    }
    if (u_skyColor.w > 0.5) color = pow(max(color, vec3_splat(0.0)), vec3_splat(1.0 / 2.2));
    float alpha = base.a;
    if (transmission > 0.0)   // premultiplied glass: its reflections, a little of its tint, more cover at grazing angles
    {
        float cover = mix(base.a, clamp(0.12 + pow(1.0 - NoV, 3.0) * 0.8, 0.0, 1.0), transmission);
        color += base.rgb * 0.08 * transmission;
        alpha = cover;
    }
    gl_FragColor = vec4(color, alpha);
}
