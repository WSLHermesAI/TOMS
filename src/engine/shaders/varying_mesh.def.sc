// The 3D mesh shaders (vs_mesh*.sc, fs_mesh.sc: glTF models, gltf_renderer.cpp) and the debug lines
// (vs_line.sc / fs_line.sc). Kept apart from varying.def.sc, which the 2D sprite shaders use.
vec3 v_worldPos  : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
vec3 v_normal    : NORMAL    = vec3(0.0, 0.0, 1.0);
vec4 v_tangent   : TANGENT   = vec4(1.0, 0.0, 0.0, 1.0);
vec4 v_texcoord0 : TEXCOORD1 = vec4(0.0, 0.0, 0.0, 0.0);   // uv0 in xy, uv1 in zw
vec4 v_color0    : COLOR0    = vec4(1.0, 1.0, 1.0, 1.0);

vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec4 a_tangent   : TANGENT;
vec2 a_texcoord0 : TEXCOORD0;
vec2 a_texcoord1 : TEXCOORD1;
vec4 a_color0    : COLOR0;
vec4 a_indices   : BLENDINDICES;
vec4 a_weight    : BLENDWEIGHT;

// Instanced draws (vs_mesh_inst.sc): the instance's model matrix, one column each.
vec4 i_data0     : TEXCOORD31;
vec4 i_data1     : TEXCOORD30;
vec4 i_data2     : TEXCOORD29;
vec4 i_data3     : TEXCOORD28;
