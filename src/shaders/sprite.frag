#version 450
layout(binding=0) uniform sampler2D tex;
layout(location=0) in vec2 vUV;
layout(location=1) in vec4 vTint;
layout(location=0) out vec4 outColor;
void main() {
    vec4 c = texture(tex, vUV);
    if (c.a < 0.05) discard;
    outColor = vec4(c.rgb * vTint.rgb, c.a * vTint.a);
}
