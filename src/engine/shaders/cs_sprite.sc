// The sprite batch's compute path (bgfx_renderer.cpp), the TOMS form of FM79979's
// cParticleBatchRender: the input buffer holds one quad per 4 vec4 (as m_pParticleInSSO did) and
// the shader writes its 4 vertices into the output buffer (m_pParticlePosOut + m_pColorSSO in one).
// Unlike the old one nothing is read back: the draw uses the output buffer as its vertex stream.
//   in  s_quads[q*4 + 0] = corner 0 (x, y), corner 1 (x, y)    [2] = uv rect; u0 < -0.5 = solid
//       s_quads[q*4 + 1] = corner 2 (x, y), corner 3 (x, y)    [3] = tint
//   out s_vertices[(q*4 + k)*3 + 0..2] = (x, y, u, v), tint, (solid, 0, 0, 0)   for corners k = 0..3
#include "bgfx_compute.sh"

BUFFER_RO(s_quads, vec4, 0);
BUFFER_WO(s_vertices, vec4, 1);
uniform vec4 u_spriteParams;   // x = number of quads

NUM_THREADS(64, 1, 1)
void main()
{
    int q = int(gl_GlobalInvocationID.x);
    if (q >= int(u_spriteParams.x)) return;
    vec4 c01  = s_quads[q * 4 + 0];
    vec4 c23  = s_quads[q * 4 + 1];
    vec4 uv   = s_quads[q * 4 + 2];
    vec4 tint = s_quads[q * 4 + 3];
    vec4 misc = vec4(uv.x < -0.5 ? 1.0 : 0.0, 0.0, 0.0, 0.0);
    int v = q * 4 * 3;
    s_vertices[v + 0]  = vec4(c01.xy, uv.x, uv.y);   // corner 0: u0 v0
    s_vertices[v + 1]  = tint;
    s_vertices[v + 2]  = misc;
    s_vertices[v + 3]  = vec4(c01.zw, uv.z, uv.y);   // corner 1: u1 v0
    s_vertices[v + 4]  = tint;
    s_vertices[v + 5]  = misc;
    s_vertices[v + 6]  = vec4(c23.xy, uv.z, uv.w);   // corner 2: u1 v1
    s_vertices[v + 7]  = tint;
    s_vertices[v + 8]  = misc;
    s_vertices[v + 9]  = vec4(c23.zw, uv.x, uv.w);   // corner 3: u0 v1
    s_vertices[v + 10] = tint;
    s_vertices[v + 11] = misc;
}
