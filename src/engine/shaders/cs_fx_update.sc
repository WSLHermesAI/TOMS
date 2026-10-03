// GPU-simulated particles (particle_fx.h, bgfx_renderer.cpp): one thread per slot runs the
// simulation steps since the last draw -- the same step as EffectInstance::step on the CPU -- and
// then writes the particle's quad (the same as EffectInstance::appendQuads) as 4 vertices that
// vs_sprite_cs.sc draws. Nothing is read back: the CPU knows from each particle's life when its
// slot frees.
//   s_state[slot*4 + 0] = pos.x, pos.y, vel.x, vel.y        [2] = spin, radial, tangential, frame0
//   s_state[slot*4 + 1] = age, life, size, rotation         [3] = colour
//   life 0 = an empty slot. A newborn's negative age = the steps it waits (born later in this draw).
#include "bgfx_compute.sh"

BUFFER_RW(s_state, vec4, 0);
BUFFER_WO(s_vertices, vec4, 1);
uniform vec4 u_fxSim;              // x steps, y dt, z capacity
uniform vec4 u_fxForce;            // xy gravity, z drag multiplier per step, w align to velocity
uniform vec4 u_fxOrigin;           // xy radial / tangential origin, z flipbook frames, w fps (0 = over the life)
uniform vec4 u_fxXform[2];         // x' = [0].x x + [0].y y + [0].z, y' = [1].x x + [1].y y + [1].z
uniform vec4 u_fxTint;
uniform vec4 u_fxColorLut[64];     // colour over the life
uniform vec4 u_fxScalarLut[64];    // size, speed, spin over the life
uniform vec4 u_fxFrameUv[16];
uniform vec4 u_fxFrameRect[16];    // for size 1, around the particle

vec4 lutColor(float u)
{
    float f = clamp(u, 0.0, 1.0) * 63.0;
    int i0 = int(floor(f));
    int i1 = min(i0 + 1, 63);
    return mix(u_fxColorLut[i0], u_fxColorLut[i1], f - float(i0));
}

vec4 lutScalar(float u)
{
    float f = clamp(u, 0.0, 1.0) * 63.0;
    int i0 = int(floor(f));
    int i1 = min(i0 + 1, 63);
    return mix(u_fxScalarLut[i0], u_fxScalarLut[i1], f - float(i0));
}

vec2 toScreen(vec2 p)
{
    return vec2(u_fxXform[0].x * p.x + u_fxXform[0].y * p.y + u_fxXform[0].z,
                u_fxXform[1].x * p.x + u_fxXform[1].y * p.y + u_fxXform[1].z);
}

NUM_THREADS(64, 1, 1)
void main()
{
    int i = int(gl_GlobalInvocationID.x);
    if (i >= int(u_fxSim.z)) return;
    vec4 a = s_state[i * 4 + 0];
    vec4 b = s_state[i * 4 + 1];
    vec4 c = s_state[i * 4 + 2];
    vec4 col = s_state[i * 4 + 3];
    bool alive = b.y > 0.0;
    if (alive)
    {
        int steps = int(u_fxSim.x);
        float dt = u_fxSim.y;
        for (int s = 0; s < steps; s++)
        {
            if (b.x < -0.000001) { b.x += dt; continue; }   // not born yet in this draw
            float age = b.x + dt;
            if (age >= b.y) { alive = false; break; }
            float u = age / b.y;
            vec2 v = a.zw + u_fxForce.xy * dt;
            if (c.y != 0.0 || c.z != 0.0)
            {
                vec2 d = a.xy - u_fxOrigin.xy;
                float len = length(d);
                if (len > 0.0001)
                {
                    vec2 n = d / len;
                    v += (n * c.y + vec2(-n.y, n.x) * c.z) * dt;   // (-y, x): clockwise, y down
                }
            }
            v *= u_fxForce.z;
            vec4 sc = lutScalar(u);
            a.zw = v;
            a.xy += v * sc.y * dt;
            b.w += c.x * sc.z * dt;
            b.x = age;
        }
        if (!alive) b.y = 0.0;
        s_state[i * 4 + 0] = a;
        s_state[i * 4 + 1] = b;
    }

    // The quad (degenerate when there is nothing to draw).
    int vo = i * 12;
    vec4 none = vec4(0.0, 0.0, 0.0, 0.0);
    bool show = alive && b.x >= 0.0;
    float u = show ? b.x / b.y : 0.0;
    vec4 tint = col * lutColor(u) * u_fxTint;
    vec4 sc = lutScalar(u);
    float size = b.z * sc.x;
    if (!show || tint.a <= 0.0 || size <= 0.0)
    {
        for (int k = 0; k < 12; k++) s_vertices[vo + k] = none;
        return;
    }
    int count = int(u_fxOrigin.z);
    int f = 0;
    if (count > 1)
    {
        uint step = u_fxOrigin.w > 0.0 ? uint(b.x * u_fxOrigin.w) : uint(min(int(u * float(count)), count - 1));
        f = int((uint(c.w) + step) % uint(count));   // unsigned: HLSL warns (an error here) on int %
    }
    vec4 r = u_fxFrameRect[f] * size;
    vec4 uv = u_fxFrameUv[f];
    float angle = b.w;
    if (u_fxForce.w > 0.5 && (a.z != 0.0 || a.w != 0.0)) angle += degrees(atan2(a.w, a.z));
    float rad = radians(angle);
    float cs = cos(rad);
    float sn = sin(rad);
    vec2 p0 = a.xy + vec2(cs * r.x - sn * r.y, sn * r.x + cs * r.y);
    vec2 p1 = a.xy + vec2(cs * r.z - sn * r.y, sn * r.z + cs * r.y);
    vec2 p2 = a.xy + vec2(cs * r.z - sn * r.w, sn * r.z + cs * r.w);
    vec2 p3 = a.xy + vec2(cs * r.x - sn * r.w, sn * r.x + cs * r.w);
    s_vertices[vo + 0]  = vec4(toScreen(p0), uv.x, uv.y);
    s_vertices[vo + 1]  = tint;
    s_vertices[vo + 2]  = none;
    s_vertices[vo + 3]  = vec4(toScreen(p1), uv.z, uv.y);
    s_vertices[vo + 4]  = tint;
    s_vertices[vo + 5]  = none;
    s_vertices[vo + 6]  = vec4(toScreen(p2), uv.z, uv.w);
    s_vertices[vo + 7]  = tint;
    s_vertices[vo + 8]  = none;
    s_vertices[vo + 9]  = vec4(toScreen(p3), uv.x, uv.w);
    s_vertices[vo + 10] = tint;
    s_vertices[vo + 11] = none;
}
