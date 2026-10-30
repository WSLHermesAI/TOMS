$input v_texcoord0, v_color0, v_solid

// A layer's picture drawn with its effect (BgfxRenderer::submitComposite, render_iface.h LayerEffect,
// docs/engine/18_NODE_EFFECTS.md). The picture is premultiplied and may hold values above 1 (RGBA16F); the result is
// premultiplied too, drawn with (ONE, 1 - alpha).
#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);
uniform vec4 u_layerMode;     // x: the effect (LayerEffect::Kind), y: opacity
uniform vec4 u_layerParams;   // the effect's params (lengths in design pixels)
uniform vec4 u_layerColor;    // the effect's colour
uniform vec4 u_layerTexel;    // texture units per design pixel: x, y (y negative for a flipped picture)

vec4 at(vec2 uv) { return texture2DLod(s_tex, uv, 0.0); }

void main()
{
    vec2 uv = v_texcoord0;
    vec4 base = at(uv);
    float mode = u_layerMode.x;
    vec4 c = base;
    if (mode > 0.5 && mode < 1.5)          // MotionBlur: smeared along the vector, centred
    {
        vec2 d = u_layerParams.xy * u_layerTexel.xy;
        c = vec4_splat(0.0);
        for (int i = 0; i < 24; i++)
            c += at(uv + d * (float(i) / 23.0 - 0.5));
        c *= 1.0 / 24.0;
    }
    else if (mode > 1.5 && mode < 2.5)     // Glow: brighter (HDR) and blooming past its outline
    {
        float intensity = u_layerParams.x;
        vec2 radius = u_layerParams.y * u_layerTexel.xy;
        float threshold = u_layerParams.z;
        c = vec4(base.rgb * intensity, base.a);
        vec3 bloom = vec3_splat(0.0);
        for (int i = 0; i < 32; i++)       // a golden-angle spiral of taps over the radius
        {
            float a = float(i) * 2.39996;
            float r = sqrt((float(i) + 0.5) / 32.0);
            vec4 s = at(uv + vec2(cos(a), sin(a)) * radius * r);
            bloom += max(s.rgb * intensity - vec3_splat(threshold * s.a), vec3_splat(0.0)) * (1.0 - 0.6 * r);
        }
        c.rgb += bloom * (u_layerParams.w / 32.0) * u_layerColor.rgb;   // light: adds colour, not coverage
    }
    else if (mode > 2.5 && mode < 3.5)     // Flash: towards one colour, the alpha kept
    {
        c.rgb = mix(base.rgb, u_layerColor.rgb * base.a, clamp(u_layerParams.x, 0.0, 1.0));
    }
    else if (mode > 3.5 && mode < 4.5)     // Outline: a band of colour around the alpha
    {
        vec2 w = u_layerParams.x * u_layerTexel.xy;
        float a = 0.0;
        for (int i = 0; i < 16; i++)
        {
            float ang = float(i) * 0.392699;
            a = max(a, at(uv + vec2(cos(ang), sin(ang)) * w).a);
            a = max(a, at(uv + vec2(cos(ang), sin(ang)) * w * 0.5).a);
        }
        float o = clamp(a - base.a, 0.0, 1.0) * u_layerColor.a;
        c = vec4(base.rgb + u_layerColor.rgb * (u_layerParams.y * o), base.a + o);
    }
    gl_FragColor = c * u_layerMode.y;
}
