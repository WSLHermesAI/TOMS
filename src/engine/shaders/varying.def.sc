vec2  v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);
vec4  v_color0    : COLOR0    = vec4(1.0, 1.0, 1.0, 1.0);
float v_solid     : TEXCOORD1 = 0.0;

vec2  a_position  : POSITION;
vec2  a_texcoord0 : TEXCOORD0;
vec4  a_color0    : COLOR0;
float a_texcoord1 : TEXCOORD1;

// Instanced sprite quads (vs_sprite_inst.sc): a_position is the corner (0..1, 0..1) of a shared quad,
// i_data0..3 one quad: its four corners, uv rect and tint.
vec4  i_data0     : TEXCOORD31;   // bgfx's instance attribute slots (examples/05-instancing)
vec4  i_data1     : TEXCOORD30;
vec4  i_data2     : TEXCOORD29;
vec4  i_data3     : TEXCOORD28;

// Vertices written by the compute path (cs_sprite.sc) and drawn by vs_sprite_cs.sc: per vertex
// (x, y, u, v), the tint, and (solid, 0, 0, 0).
vec4  a_texcoord2 : TEXCOORD2;
vec4  a_color1    : COLOR1;
vec4  a_texcoord3 : TEXCOORD3;
