// The ocean viewer's shaders (docs/engine/21_OCEAN_VIEWER.md).
// centroid: with MSAA, a pixel the water's far edge only just touches would otherwise read values from outside its triangle,
// and that edge's triangles are stretched so far that those values are wild (dark specks along the horizon).
centroid vec3 v_worldPos  : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
centroid vec4 v_texcoord0 : TEXCOORD1 = vec4(0.0, 0.0, 0.0, 0.0);   // the water: the point's place before the waves moved it (xy), its distance, its height

vec3 a_position  : POSITION;
