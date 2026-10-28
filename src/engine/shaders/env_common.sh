// env_common.sh -- the environment (image-based lighting, gltf_environment.h) as fs_mesh.sc and fs_sky.sc sample it.
// s_envMap: the prefiltered cubemap (mip = roughness x u_envParams.y); u_envParams: on, the roughest mip,
// the picture's turn about +Y (radians), intensity.

SAMPLERCUBE(s_envMap, 9);        // (6: vs_mesh_animtex's s_animTex; 7, 8: fs_mesh's specular textures)
uniform vec4 u_envParams;

// A world direction in the picture's frame (the picture turned by u_envParams.z about +Y).
vec3 envDir(vec3 d)
{
    float c = cos(u_envParams.z);
    float s = sin(u_envParams.z);
    return vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
}

vec3 envSample(vec3 dir, float lod)
{
    return textureCubeLod(s_envMap, envDir(dir), lod).rgb * u_envParams.w;
}
