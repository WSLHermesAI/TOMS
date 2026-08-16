// renderer_webgl.cpp — WebGL2 implementation of IRenderer (Emscripten only).
#include "renderer_webgl.h"
#ifdef __EMSCRIPTEN__
#ifndef GL_UNPACK_FLIP_Y_WEBGL
#define GL_UNPACK_FLIP_Y_WEBGL 0x9240
#endif

static const char* VERT = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aRect;
layout(location=2) in vec4 aUV;
layout(location=3) in vec4 aTint;
layout(location=4) in float aSolid;
uniform vec2 uRes;
out vec2 vUV; out vec4 vTint; out float vSolid;
void main() {
  vec2 px = aRect.xy + aPos * aRect.zw;
  vec2 clip = (px / uRes) * 2.0 - 1.0;
  clip.y = -clip.y;
  gl_Position = vec4(clip, 0.0, 1.0);
  vUV = aUV.xy + aPos * (aUV.zw - aUV.xy);
  vTint = aTint;
  vSolid = aSolid;
})";

static const char* FRAG = R"(#version 300 es
precision mediump float;
in vec2 vUV; in vec4 vTint; in float vSolid; out vec4 frag;
uniform sampler2D uTex;
void main() {
  if (vSolid > 0.5) { frag = vTint; return; }   // solid-color quad: ignore texture, use tint
  vec4 c = texture(uTex, vUV);
  if (c.a < 0.01) discard;
  frag = vec4(c.rgb * vTint.rgb, c.a * vTint.a);
})";

GLuint WebGLRenderer::compile(const char* src, GLenum kind) {
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { GLint n; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &n); std::string log(n,' '); glGetShaderInfoLog(s, n, nullptr, log.data()); fprintf(stderr, "[glsl] %s\n", log.c_str()); }
    return s;
}

GLuint WebGLRenderer::makeTexture(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, bool flip) {
    if (rgba.empty() || w == 0 || h == 0) return 0;   // guard: never upload zero-size/empty
    // Clamp to the context's max texture size so a grown font atlas can't request
    // an impossible texture (which would GL-error -> abortOnError -> page abort).
    GLint maxTex = 4096; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    uint32_t cap = (maxTex > 0) ? (uint32_t)maxTex : 4096;
    uint32_t ww = std::min(w, cap), hh = std::min(h, cap);
    GLuint t; glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_FLIP_Y_WEBGL, flip ? GL_TRUE : GL_FALSE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)ww, (GLsizei)hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glPixelStorei(GL_UNPACK_FLIP_Y_WEBGL, GL_FALSE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void WebGLRenderer::init(uint32_t w, uint32_t h) {
    W=w; H=h;
    GLuint vs=compile(VERT, GL_VERTEX_SHADER), fs=compile(FRAG, GL_FRAGMENT_SHADER);
    prog=glCreateProgram(); glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) fprintf(stderr, "[gl] program link failed\n");
    glDeleteShader(vs); glDeleteShader(fs);
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,15*4, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,15*4, (void*)(8));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,15*4, (void*)(24));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,15*4, (void*)(40));
    glEnableVertexAttribArray(4); glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,15*4, (void*)(56));
    glBindVertexArray(0);
}

void WebGLRenderer::loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh) {
    uint32_t AW=0, AH=0; std::vector<uint8_t> atlas; spriteCols=9;
    if (!packAtlas(layers, sw, sh, spriteCols, atlas, AW, AH)) { fprintf(stderr,"[gl] sprite pack fail\n"); return; }
    if (spriteTex) glDeleteTextures(1,&spriteTex);
    // Sprite atlas UVs are top-left origin (packAtlas + Game::spriteUV), same as the
    // Vulkan path. Do NOT flip, otherwise every sprite samples a vertically mirrored
    // cell (wrong/garbled textures — e.g. HUD icons and enemies show the wrong glyph).
    spriteTex = makeTexture(atlas, AW, AH, false);
}

void WebGLRenderer::loadFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
    if (fontTex) glDeleteTextures(1,&fontTex);
    // Font atlas UVs are top-left origin (same as the Vulkan path). Do NOT flip,
    // otherwise glyph cells sample a vertically mirrored row -> wrong characters.
    fontTex = makeTexture(px, w, h, false);
}

// Re-upload the font atlas after a realtime glyph bake (Game::drawText calls this
// when it bakes a previously-unknown codepoint, e.g. the on-canvas gamepad labels
// "^ v < >"). Without this the GPU texture stays stale while font_->atlas() grew,
// so the new glyph samples garbage -> "weird characters". Mirrors Renderer (Vulkan).
void WebGLRenderer::updateFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
    if (px.empty() || w == 0 || h == 0) return;          // nothing to upload -> keep old texture
    GLuint newTex = makeTexture(px, w, h, false);
    if (!newTex) return;                                  // upload failed -> keep old texture (no abort)
    if (fontTex) glDeleteTextures(1, &fontTex);
    fontTex = newTex;
    glGetError();                                         // swallow pending GL error so abortOnError won't fire
}

void WebGLRenderer::begin() { sprites.clear(); texts.clear(); }

void WebGLRenderer::drawSprite(const Quad& q) { sprites.push_back(q); }
void WebGLRenderer::drawText(const Quad& q)   { texts.push_back(q); }

void WebGLRenderer::flush(const std::vector<Quad>& qs, GLuint tex) {
    if (qs.empty()) return;
    std::vector<float> v(qs.size()*6*15);
    for (size_t i=0;i<qs.size();i++) {
        const Quad& q=qs[i];
        float pos[6][2]={{0,0},{1,0},{0,1},{1,0},{1,1},{0,1}};
        for (int t=0;t<6;t++) {
            float* p=v.data()+(i*6+t)*15;
            p[0]=pos[t][0]; p[1]=pos[t][1];
            p[2]=q.rect[0]; p[3]=q.rect[1]; p[4]=q.rect[2]; p[5]=q.rect[3];
            p[6]=q.uv[0]; p[7]=q.uv[1]; p[8]=q.uv[2]; p[9]=q.uv[3];
            p[10]=q.tint[0]; p[11]=q.tint[1]; p[12]=q.tint[2]; p[13]=q.tint[3];
            p[14]=q.solid ? 1.0f : 0.0f;
        }
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)v.size()*4, v.data(), GL_DYNAMIC_DRAW);
    glUseProgram(prog);
    glUniform2f(glGetUniformLocation(prog,"uRes"), (float)W, (float)H);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(prog,"uTex"), 0);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(qs.size()*6));
    glBindVertexArray(0);
}

void WebGLRenderer::end() {
    glViewport(0,0,(GLsizei)W,(GLsizei)H);
    glClearColor(0.06f,0.06f,0.1f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    flush(sprites, spriteTex);
    flush(texts, fontTex);
}
#endif
