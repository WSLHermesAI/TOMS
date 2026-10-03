// GPU-simulated particles (particle_fx.h, bgfx_renderer.cpp): copies this draw's newborns into
// their slots. A record is 5 vec4: the particle's 4 (see cs_fx_update.sc), then (slot, 0, 0, 0).
#include "bgfx_compute.sh"

BUFFER_RW(s_state, vec4, 0);
BUFFER_RO(s_spawn, vec4, 1);
uniform vec4 u_fxSim;   // w = number of records

NUM_THREADS(64, 1, 1)
void main()
{
    int k = int(gl_GlobalInvocationID.x);
    if (k >= int(u_fxSim.w)) return;
    int slot = int(s_spawn[k * 5 + 4].x);
    s_state[slot * 4 + 0] = s_spawn[k * 5 + 0];
    s_state[slot * 4 + 1] = s_spawn[k * 5 + 1];
    s_state[slot * 4 + 2] = s_spawn[k * 5 + 2];
    s_state[slot * 4 + 3] = s_spawn[k * 5 + 3];
}
