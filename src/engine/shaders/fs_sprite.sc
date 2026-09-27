$input v_texcoord0, v_color0, v_solid

// Same rules as the Vulkan sprite.frag: solid quads output the tint only; textured quads
// discard near-transparent texels and multiply by the tint.
#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);

void main()
{
    if (v_solid > 0.5)
    {
        if (v_color0.a < 0.004) discard;
        gl_FragColor = v_color0;
    }
    else
    {
        vec4 c = texture2D(s_tex, v_texcoord0);
        if (c.a < 0.05) discard;
        gl_FragColor = vec4(c.rgb * v_color0.rgb, c.a * v_color0.a);
    }
}
