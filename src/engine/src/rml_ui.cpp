// rml_ui.cpp -- see rml_ui.h. RmlUi's render and system interfaces implemented on bgfx.
#include "rml_ui.h"
#include "embedded_shaders.h"

#include <RmlUi/Debugger.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

// A private stb_image copy for UI images (the core game code owns the public one).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace toms::next {
namespace {

bgfx::VertexLayout& rmlLayout() {
    // Must match Rml::Vertex: Vector2f position, ColourbPremultiplied colour, Vector2f tex_coord.
    static bgfx::VertexLayout layout;
    static bool ready = false;
    if (!ready) {
        layout.begin()
            .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        ready = true;
    }
    return layout;
}

struct Geometry {
    bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  ibh = BGFX_INVALID_HANDLE;
};

class BgfxRenderInterface final : public Rml::RenderInterface {
public:
    float designW = 1024, designH = 768;
    float vpX = 0, vpY = 0, vpW = 1024, vpH = 768;   // letterbox rect in device pixels
    uint16_t view = 2;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params = BGFX_INVALID_HANDLE;   // u_rmlParams, see vs_rml.sc
    bgfx::TextureHandle white = BGFX_INVALID_HANDLE;

    bool create() {
        static_assert(sizeof(Rml::Vertex) == 20, "Rml::Vertex layout changed; update rmlLayout()");
        program = createEmbeddedProgram(ShaderProgram::RmlUi);
        sampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
        params = bgfx::createUniform("u_rmlParams", bgfx::UniformType::Vec4);
        const uint32_t px = 0xffffffff;
        white = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&px, 4));
        return bgfx::isValid(program);
    }
    void destroy() {
        if (bgfx::isValid(program)) bgfx::destroy(program);
        if (bgfx::isValid(sampler)) bgfx::destroy(sampler);
        if (bgfx::isValid(params)) bgfx::destroy(params);
        if (bgfx::isValid(white)) bgfx::destroy(white);
        program = BGFX_INVALID_HANDLE; sampler = BGFX_INVALID_HANDLE; params = BGFX_INVALID_HANDLE;
        white = BGFX_INVALID_HANDLE;
    }

    // ---- geometry ----
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
        auto* g = new Geometry();
        g->vbh = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), (uint32_t)(vertices.size() * sizeof(Rml::Vertex))), rmlLayout());
        g->ibh = bgfx::createIndexBuffer(bgfx::copy(indices.data(), (uint32_t)(indices.size() * sizeof(int))), BGFX_BUFFER_INDEX32);
        return reinterpret_cast<Rml::CompiledGeometryHandle>(g);
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override {
        auto* g = reinterpret_cast<Geometry*>(handle);
        if (bgfx::isValid(g->vbh)) bgfx::destroy(g->vbh);
        if (bgfx::isValid(g->ibh)) bgfx::destroy(g->ibh);
        delete g;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
        auto* g = reinterpret_cast<Geometry*>(handle);
        float model[16], move[16];
        bx::mtxTranslate(move, translation.x, translation.y, 0.0f);
        if (hasTransform) bx::mtxMul(model, move, transform);   // translate first, then the element transform
        else std::copy(move, move + 16, model);
        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, g->vbh);
        bgfx::setIndexBuffer(g->ibh);
        bgfx::TextureHandle tex = texture ? bgfx::TextureHandle{(uint16_t)(texture - 1)} : white;
        bgfx::setTexture(0, sampler, tex);
        // The desktop backbuffer is sRGB (bgfx_host.cpp) and RCSS colours are sRGB, so the shader
        // linearizes them; the web backbuffer is not sRGB, so they pass through.
#ifdef __EMSCRIPTEN__
        const float p[4] = {0, 0, 0, 0};
#else
        const float p[4] = {1, 0, 0, 0};
#endif
        bgfx::setUniform(params, p);
        if (scissorOn) {
            // RmlUi gives the region in UI pixels; bgfx wants backbuffer pixels.
            const float sx = vpW / designW, sy = vpH / designH;
            const float x0 = vpX + scissor.Left() * sx, y0 = vpY + scissor.Top() * sy;
            const float w = scissor.Width() * sx, h = scissor.Height() * sy;
            bgfx::setScissor((uint16_t)std::max(0.0f, std::floor(x0)), (uint16_t)std::max(0.0f, std::floor(y0)),
                             (uint16_t)std::max(0.0f, std::ceil(w)), (uint16_t)std::max(0.0f, std::ceil(h)));
        }
        // Premultiplied alpha (RmlUi's colours and generated textures are premultiplied).
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
        bgfx::submit(view, program);
    }

    // ---- textures ----
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
        int w = 0, h = 0, n = 0;
        stbi_uc* px = stbi_load(source.c_str(), &w, &h, &n, 4);
        if (!px) {
            Rml::Log::Message(Rml::Log::LT_WARNING, "image not found: %s", source.c_str());
            return 0;
        }
        for (int i = 0; i < w * h; ++i) {   // RmlUi expects premultiplied alpha
            stbi_uc* p = px + i * 4;
            p[0] = (stbi_uc)(p[0] * p[3] / 255); p[1] = (stbi_uc)(p[1] * p[3] / 255); p[2] = (stbi_uc)(p[2] * p[3] / 255);
        }
        dimensions = {w, h};
        // Images are the game's pixel art: point sampling keeps them crisp when scaled up. sRGB like
        // the game's own atlases (bgfx_renderer.cpp), so icons look the same in both UIs.
        uint64_t flags = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
#ifndef __EMSCRIPTEN__
        flags |= BGFX_TEXTURE_SRGB;
#endif
        bgfx::TextureHandle t = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1, bgfx::TextureFormat::RGBA8,
                                                      flags,
                                                      bgfx::copy(px, (uint32_t)(w * h * 4)));
        stbi_image_free(px);
        return bgfx::isValid(t) ? (Rml::TextureHandle)t.idx + 1 : 0;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dim) override {
        // Font glyph atlases and other generated images: smooth sampling.
        bgfx::TextureHandle t = bgfx::createTexture2D((uint16_t)dim.x, (uint16_t)dim.y, false, 1, bgfx::TextureFormat::RGBA8,
                                                      BGFX_SAMPLER_UVW_CLAMP, bgfx::copy(source.data(), (uint32_t)source.size()));
        return bgfx::isValid(t) ? (Rml::TextureHandle)t.idx + 1 : 0;
    }
    void ReleaseTexture(Rml::TextureHandle texture) override {
        if (texture) bgfx::destroy(bgfx::TextureHandle{(uint16_t)(texture - 1)});
    }

    // ---- scissor / transform (clip masks, layers and filters are not implemented) ----
    void EnableScissorRegion(bool enable) override { scissorOn = enable; }
    void SetScissorRegion(Rml::Rectanglei region) override { scissor = region; }
    void SetTransform(const Rml::Matrix4f* t) override {
        hasTransform = t != nullptr;
        if (t) std::copy(t->data(), t->data() + 16, transform);
    }

private:
    bool scissorOn = false;
    Rml::Rectanglei scissor;
    bool hasTransform = false;
    float transform[16] = {};
};

class SystemInterface final : public Rml::SystemInterface {
public:
    double now = 0;
    double GetElapsedTime() override { return now; }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        const char* tag = type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ? "error"
                        : type == Rml::Log::LT_WARNING ? "warning" : "info";
        std::fprintf(stderr, "[rmlui %s] %s\n", tag, message.c_str());
        return true;
    }
};

}  // namespace

struct RmlUi::Impl {
    BgfxRenderInterface render;
    SystemInterface system;
    Rml::Context* context = nullptr;
    bool debugger = false;
};

RmlUi::RmlUi() = default;
RmlUi::~RmlUi() { shutdown(); }

bool RmlUi::init(int designW, int designH, const std::vector<std::string>& fontFiles, std::string& error) {
    shutdown();
    impl_ = std::make_unique<Impl>();
    impl_->render.designW = (float)designW;
    impl_->render.designH = (float)designH;
    if (!impl_->render.create()) { error = "RmlUi: shader program missing for this renderer"; impl_.reset(); return false; }
    Rml::SetRenderInterface(&impl_->render);
    Rml::SetSystemInterface(&impl_->system);
    if (!Rml::Initialise()) { error = "RmlUi: Rml::Initialise failed"; impl_->render.destroy(); impl_.reset(); return false; }

    int loaded = 0;
    for (size_t i = 0; i < fontFiles.size(); ++i) {
        const bool primary = loaded == 0;
        const std::string family = primary ? "toms" : "toms-fallback-" + std::to_string(i);
        if (Rml::LoadFontFace(fontFiles[i], family, Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto, !primary, 0)) {
            std::fprintf(stderr, "[rmlui] font %s: %s\n", primary ? "family 'toms'" : "fallback", fontFiles[i].c_str());
            ++loaded;
        }
    }
    if (loaded == 0) {
        error = "RmlUi: no font could be loaded (tried " + std::to_string(fontFiles.size()) + " files)";
        Rml::Shutdown(); impl_->render.destroy(); impl_.reset();
        return false;
    }
    impl_->context = Rml::CreateContext("game", Rml::Vector2i(designW, designH));
    if (!impl_->context) { error = "RmlUi: CreateContext failed"; shutdown(); return false; }
    Rml::Debugger::Initialise(impl_->context);
    return true;
}

void RmlUi::shutdown() {
    if (!impl_) return;
    if (impl_->context) {
        Rml::Debugger::Shutdown();
        Rml::RemoveContext(impl_->context->GetName());
        impl_->context = nullptr;
    }
    Rml::Shutdown();            // releases RmlUi's textures and geometry through the render interface
    impl_->render.destroy();
    impl_.reset();
}

bool RmlUi::ready() const { return impl_ && impl_->context; }
Rml::Context* RmlUi::context() { return impl_ ? impl_->context : nullptr; }

void RmlUi::setViewport(float x, float y, float w, float h) {
    if (!impl_) return;
    impl_->render.vpX = x; impl_->render.vpY = y; impl_->render.vpW = w; impl_->render.vpH = h;
}

void RmlUi::update(double timeSeconds) {
    if (!ready()) return;
    impl_->system.now = timeSeconds;
    impl_->context->Update();
}

void RmlUi::render(uint16_t viewId) {
    if (!ready()) return;
    auto& r = impl_->render;
    r.view = viewId;
    bgfx::setViewRect(viewId, (uint16_t)std::lround(r.vpX), (uint16_t)std::lround(r.vpY),
                      (uint16_t)std::lround(r.vpW), (uint16_t)std::lround(r.vpH));
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    float proj[16];
    bx::mtxOrtho(proj, 0.0f, r.designW, r.designH, 0.0f, -1.0f, 1.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(viewId, nullptr, proj);
    bgfx::touch(viewId);
    impl_->context->Render();
}

void RmlUi::toggleDebugger() {
    if (!ready()) return;
    impl_->debugger = !impl_->debugger;
    Rml::Debugger::SetVisible(impl_->debugger);
}
bool RmlUi::debuggerVisible() const { return impl_ && impl_->debugger; }

}  // namespace toms::next
