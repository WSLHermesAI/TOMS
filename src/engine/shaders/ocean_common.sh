// ocean_common.sh -- what the sky and the water share: the Preetham daylight model, clouds, the tone curve.
uniform mat4 u_oceanInvVP;     // clip -> world
uniform vec4 u_oceanEye;       // eye xyz, exposure
uniform vec4 u_oceanGrid;      // the grid's half width (NDC), bottom (NDC), top (NDC), its farthest distance (m)
uniform vec4 u_oceanGridB;     // a row's angle / the eye's height, wave scale, 1 = encode gamma here (no sRGB screen: the web), -
uniform vec4 u_oceanDetail;    // noise octaves: the sky's clouds, the clouds in the water (0 = clear sky), the foam
uniform vec4 u_oceanInvSize;   // 1 / each cascade's size (m)
uniform vec4 u_oceanTexels;    // each cascade's texels per metre
uniform vec4 u_oceanSun;       // the way to the sun xyz, time (s)
uniform vec4 u_oceanSunColor;  // the sun's light rgb, its disc's strength
uniform vec4 u_oceanPerezY;    // A B C D of the luminance
uniform vec4 u_oceanPerezX;    // A B C D of x
uniform vec4 u_oceanPerezZ;    // A B C D of y
uniform vec4 u_oceanPerezE;    // E of Y, x, y; sky strength
uniform vec4 u_oceanZenith;    // Y, x, y at the zenith, already divided by F(0, sun angle)
uniform vec4 u_oceanClouds;    // cover 0..1, scale, speed, how dark their undersides are
uniform vec4 u_oceanDeep;      // deep water rgb, roughness
uniform vec4 u_oceanScatter;   // light scattered in the waves rgb, its strength
uniform vec4 u_oceanFoam;      // foam's brightness, fog distance (m), reflection strength, -

float oceanFract(float x) { return x - floor(x); }

float oceanHash(vec2 p)
{
    // No sin(x) * 43758: it magnifies rounding, so two corners of a noise cell that should match could differ by compiler,
    // which showed as hard-edged shards in the clouds on Vulkan and Direct3D 12 (a hash of Dave Hoskins's "Hash without Sine").
    float a = oceanFract(p.x * 0.1031);
    float b = oceanFract(p.y * 0.1031);
    float c = oceanFract(p.x * 0.1030);
    float d = a * (b + 33.33) + b * (c + 33.33) + c * (a + 33.33);
    return oceanFract((a + d + b + d) * (c + d));
}

float oceanNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = oceanHash(i);
    float b = oceanHash(i + vec2(1.0, 0.0));
    float c = oceanHash(i + vec2(0.0, 1.0));
    float d = oceanHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float oceanFbm(vec2 p, int octaves)
{
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < octaves; i++) {
        sum += amp * oceanNoise(p);
        p = vec2(p.x * 1.6 - p.y * 1.2, p.x * 1.2 + p.y * 1.6) + vec2(3.1, 1.7);
        amp *= 0.5;
    }
    return sum;
}

vec3 oceanXyYToRgb(vec3 xyY)
{
    float Y = max(xyY.z, 0.0);
    float X = xyY.x / max(xyY.y, 0.001) * Y;
    float Z = (1.0 - xyY.x - xyY.y) / max(xyY.y, 0.001) * Y;
    return max(vec3(3.2406 * X - 1.5372 * Y - 0.4986 * Z,
                    -0.9689 * X + 1.8758 * Y + 0.0415 * Z,
                    0.0557 * X - 0.2040 * Y + 1.0570 * Z), vec3_splat(0.0));
}

float oceanPerez(vec4 c, float e, float cosTheta, float gamma, float cosGamma)
{
    return (1.0 + c.x * exp(c.y / cosTheta)) * (1.0 + c.z * exp(c.w * gamma) + e * cosGamma * cosGamma);
}

// The clear sky along a direction (up or level).
vec3 oceanClearSky(vec3 dir)
{
    float cosTheta = max(dir.y, 0.02);
    float cosGamma = clamp(dot(dir, u_oceanSun.xyz), -1.0, 1.0);
    float gamma = acos(cosGamma);
    vec3 xyY = vec3(u_oceanZenith.y * oceanPerez(u_oceanPerezX, u_oceanPerezE.y, cosTheta, gamma, cosGamma),
                    u_oceanZenith.z * oceanPerez(u_oceanPerezZ, u_oceanPerezE.z, cosTheta, gamma, cosGamma),
                    u_oceanZenith.x * oceanPerez(u_oceanPerezY, u_oceanPerezE.x, cosTheta, gamma, cosGamma));
    vec3 sky = oceanXyYToRgb(xyY) * u_oceanPerezE.w;
    // Preetham's model has little red in a low sun: warm the low sky and cool the high one as the sun goes down.
    float low = clamp(1.0 - u_oceanSun.y / 0.32, 0.0, 1.0);
    vec3 tint = mix(vec3(1.45, 0.6, 0.38), vec3(0.34, 0.42, 1.15), clamp(dir.y * 3.0, 0.0, 1.0));
    sky = mix(sky, sky * tint, low);
    // A closed sky has no colour of its own: grey, right down to the horizon.
    float overcast = clamp((u_oceanClouds.x - 0.6) / 0.3, 0.0, 1.0);
    return mix(sky, vec3_splat(dot(sky, vec3(0.2126, 0.7152, 0.0722))) * vec3(0.95, 1.0, 1.06), overcast);
}

// The sky with its clouds. octaves 0: no clouds.
vec3 oceanSky(vec3 dir, int octaves)
{
    vec3 sky = oceanClearSky(dir);
    if (octaves <= 0 || u_oceanClouds.x <= 0.0) return sky;
    float up = max(dir.y, 0.0);
    // A flat cloud layer overhead: the +0.28 keeps it from smearing into streaks toward the horizon.
    vec2 p = dir.xz / (up + 0.28) * u_oceanClouds.y + vec2(u_oceanSun.w * u_oceanClouds.z, u_oceanSun.w * u_oceanClouds.z * 0.4);
    p += vec2(oceanNoise(p * 0.7 + vec2(5.2, 1.3)), oceanNoise(p * 0.7 + vec2(1.7, 9.2))) * 0.9;   // billows, not plain noise
    float n = oceanFbm(p, octaves);
    float low = 1.0 - u_oceanClouds.x;
    float mask = clamp((n - low * 0.8) / 0.13, 0.0, 1.0);
    mask = mask * mask * (3.0 - 2.0 * mask);
    // Lit from the sun's side: a thicker cloud toward the sun means this part is in shade.
    float lit = 0.72;
    if (octaves >= 3) {   // twice the noise: only where it is worth it
        float toward = oceanFbm(p + u_oceanSun.xz * 0.3, octaves);
        lit = clamp(0.68 + (n - toward) * 4.0, 0.0, 1.0);
    }
    vec3 ambient = oceanClearSky(vec3(0.0, 1.0, 0.0));
    // A closed sky lets little light through: its clouds are grey, not white.
    float thick = 1.0 - 0.75 * clamp((u_oceanClouds.x - 0.55) / 0.4, 0.0, 1.0);
    vec3 cloud = mix(ambient * (1.0 - u_oceanClouds.w) + vec3_splat(0.015), (u_oceanSunColor.xyz * 0.42 + ambient * 0.6) * thick, lit);
    float overcast = clamp((u_oceanClouds.x - 0.6) / 0.3, 0.0, 1.0);
    cloud = mix(cloud, vec3_splat(dot(cloud, vec3(0.2126, 0.7152, 0.0722))), overcast);
    float horizon = clamp(up / 0.06, 0.0, 1.0);
    return mix(sky, cloud, mask * horizon);
}

vec3 oceanTone(vec3 color)
{
    vec3 x = color * u_oceanEye.w;
    float grey = dot(x, vec3(0.2126, 0.7152, 0.0722));
    x = max(mix(vec3_splat(grey), x, 1.25), vec3_splat(0.0));   // ACES washes colours out: put some back first
    vec3 mapped = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);   // ACES
    if (u_oceanGridB.z > 0.5) return pow(mapped, vec3_splat(1.0 / 2.2));
    return mapped;   // the backbuffer is sRGB
}
