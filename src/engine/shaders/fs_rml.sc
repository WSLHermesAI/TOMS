$input v_texcoord0, v_color0

// RmlUi geometry: texture * colour, premultiplied alpha (rml_ui.cpp's blend state).
// u_rmlParams.y = 1: the texture has STRAIGHT alpha -- the game's sprite atlas, which the UI
// borrows from the map renderer instead of loading it a second time (shared_textures.h) -- so it
// is premultiplied here. RmlUi's own textures are premultiplied already (y = 0).
#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);
uniform vec4 u_rmlParams;

void main()
{
    vec4 t = texture2D(s_tex, v_texcoord0);
    if (u_rmlParams.y > 0.5)
        t.rgb *= t.a;
    gl_FragColor = t * v_color0;
}
