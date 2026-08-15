#version 450
layout(location=0) in vec2 aPos;    // 0..1 within quad
layout(location=1) in vec4 aRect;   // dst x,y,w,h in pixels
layout(location=2) in vec4 aUVrc;   // src uv rect u0,v0,u1,v1
layout(location=3) in vec4 aTint;   // rgba
layout(location=4) in float aSolid; // 1.0 => flat color quad (ignore texture)
// push constant: res.xy = screen size; xform = (offX, offY, scaleX, scaleY)
layout(push_constant) uniform PC { vec2 res; vec4 xform; } pc;
layout(location=0) out vec2 vUV;
layout(location=1) out vec4 vTint;
layout(location=2) out flat int vSolid;
void main() {
    float sx = pc.xform.z, sy = pc.xform.w;
    float x = (aRect.x * sx + pc.xform.x) + aPos.x * (aRect.z * sx);
    float y = (aRect.y * sy + pc.xform.y) + aPos.y * (aRect.w * sy);
    vec2 ndc = vec2(x / pc.res.x * 2.0 - 1.0, 1.0 - (y / pc.res.y * 2.0));
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = vec2(aUVrc.x + aPos.x * (aUVrc.z - aUVrc.x),
               aUVrc.y + aPos.y * (aUVrc.w - aUVrc.y));
    vTint = aTint;
    vSolid = int(aSolid + 0.5);
}
