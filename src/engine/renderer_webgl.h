// renderer_webgl.h — WebGL2 backend for Emscripten (browser). Implements IRenderer.
// Compiled ONLY under Emscripten. Uses the GL context already created by the web entry.
#pragma once
#include "render_iface.h"
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>

class WebGLRenderer : public IRenderer {
public:
    // Design space the game lays everything out against -- the SAME fixed 1024x768 the Vulkan
    // backend uses (Renderer::kDesignW/kDesignH). W/H hold this, and width()/height() report it.
    static constexpr uint32_t kDesignW = 1024, kDesignH = 768;
    uint32_t W=0, H=0;                 // design space (see above)
    // Actual drawing-buffer size of the <canvas> (queried from the DOM). Used for glViewport.
    // Kept separate from W/H: init()'s w/h arguments are the game's design resolution, which is
    // NOT the canvas size on the browser build.
    uint32_t bufW=0, bufH=0;
    GLuint prog=0, vbo=0, vao=0;
    GLuint spriteTex=0, fontTex=0;
    int spriteCols=9;
    std::vector<Quad> sprites, texts;

    void init(uint32_t w, uint32_t h) override;
    void loadSprites(const std::vector<std::vector<uint8_t>>& layers, uint32_t sw, uint32_t sh) override;
    void loadFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) override;
    void updateFont(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) override;
    void begin() override;
    void drawSprite(const Quad& q) override;
    void drawText(const Quad& q) override;
    void end() override;
    uint32_t width()  const override { return W; }
    uint32_t height() const override { return H; }
    void savePNG(const std::string& path) override { (void)path; }  // no-op on web

private:
    GLuint compile(const char* src, GLenum kind);
    GLuint makeTexture(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, bool flip = true);
    void flush(const std::vector<Quad>& qs, GLuint tex);
    void refreshBufferSize();     // re-read the <canvas> drawing-buffer size into bufW/bufH
};
#endif
