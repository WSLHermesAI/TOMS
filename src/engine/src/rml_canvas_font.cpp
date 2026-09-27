// rml_canvas_font.cpp -- see rml_canvas_font.h. Emscripten only.
#include "rml_canvas_font.h"

#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/StringUtilities.h>   // Rml::StringView
#include <RmlUi/Core/TextShapingContext.h>

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

// One shared scratch canvas. Setting canvas.width also resets the 2D context, so every call sets
// the font state it needs.
EM_JS(void, toms_text_metrics, (const char* font, const char* lang, float size, float* out), {
    if (!Module.__tomsText) {
        var c = document.createElement('canvas');
        c.width = 8; c.height = 8;
        Module.__tomsText = { canvas: c, ctx: c.getContext('2d', { willReadFrequently: true }) };
    }
    var t = Module.__tomsText;
    var ctx = t.ctx;
    t.canvas.lang = UTF8ToString(lang);
    if ('lang' in ctx) ctx.lang = UTF8ToString(lang);
    ctx.font = UTF8ToString(font);
    var m = ctx.measureText('Mg漢');
    var asc = m.fontBoundingBoxAscent;
    var desc = m.fontBoundingBoxDescent;
    if (!(asc > 0)) asc = Math.max(m.actualBoundingBoxAscent || 0, size * 0.88);
    if (!(desc > 0)) desc = Math.max(m.actualBoundingBoxDescent || 0, size * 0.24);
    var x = ctx.measureText('x').actualBoundingBoxAscent || size * 0.5;
    HEAPF32[(out >> 2) + 0] = asc;
    HEAPF32[(out >> 2) + 1] = desc;
    HEAPF32[(out >> 2) + 2] = x;
    HEAPF32[(out >> 2) + 3] = ctx.measureText('…').width > 0 ? 1 : 0;
});

EM_JS(double, toms_text_width, (const char* font, const char* lang, const char* text, float letterSpacing), {
    var t = Module.__tomsText;
    var ctx = t.ctx;
    if ('lang' in ctx) ctx.lang = UTF8ToString(lang);
    ctx.font = UTF8ToString(font);
    if ('letterSpacing' in ctx) ctx.letterSpacing = letterSpacing + 'px';
    return ctx.measureText(UTF8ToString(text)).width;
});

// Draws `text` white on transparent into a w x h RGBA buffer at `out`; (x, baseline) in UI px,
// scaled by `scale` to the texture's pixels.
EM_JS(void, toms_text_render, (const char* font, const char* lang, const char* text, float letterSpacing,
                               int rtl, float scale, float x, float baseline, int w, int h, unsigned char* out), {
    var t = Module.__tomsText;
    var c = t.canvas;
    c.width = w; c.height = h;     // also clears it and resets the context
    var ctx = t.ctx;
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
    if ('lang' in ctx) ctx.lang = UTF8ToString(lang);
    ctx.font = UTF8ToString(font);
    if ('letterSpacing' in ctx) ctx.letterSpacing = letterSpacing + 'px';
    ctx.direction = rtl ? 'rtl' : 'ltr';
    ctx.textAlign = 'left';        // x is the left edge in both directions
    ctx.textBaseline = 'alphabetic';
    ctx.fillStyle = '#ffffff';
    ctx.fillText(UTF8ToString(text), x, baseline);
    HEAPU8.set(ctx.getImageData(0, 0, w, h).data, out);
});

constexpr size_t kMaxTextureBytes = 48u * 1024 * 1024;   // then drop the cache and re-render on demand

}  // namespace

namespace toms::next {

void CanvasFontEngine::setLanguage(const std::string& langTag, const std::string& cssFonts) {
    if (langTag == lang_ && cssFonts == cssFonts_) return;
    lang_ = langTag;
    cssFonts_ = cssFonts;
    for (Face& f : faces_) updateFace(f);   // handles stay valid; their fonts and metrics change
    clearCaches();
}

void CanvasFontEngine::setPixelScale(float scale) {
    // Quarter steps, at least 1: a phone at 2.4x draws at 2.5x; a window drag doesn't re-render text
    // every frame.
    const float s = std::max(1.0f, std::round(scale * 4.0f) / 4.0f);
    if (s == scale_) return;
    scale_ = s;
    clearCaches();
}

void CanvasFontEngine::clearCaches() {
    texts_.clear();
    widths_.clear();
    textureBytes_ = 0;
    ++version_;   // RmlUi regenerates every string's geometry
}

void CanvasFontEngine::updateFace(Face& f) {
    const std::string stack = (f.family == "rmlui-debugger-font") ? std::string("sans-serif")
                            : (cssFonts_.empty() ? std::string("sans-serif") : cssFonts_);
    f.css = std::string(f.italic ? "italic " : "") + std::to_string(f.weight) + " " + std::to_string(f.size) + "px " + stack;
    float m[4] = {0, 0, 0, 0};
    toms_text_metrics(f.css.c_str(), lang_.c_str(), (float)f.size, m);
    f.metrics.size = f.size;
    f.metrics.ascent = m[0];
    f.metrics.descent = m[1];
    f.metrics.line_spacing = m[0] + m[1];
    f.metrics.x_height = m[2];
    f.metrics.underline_position = std::max(1.0f, f.size * 0.1f);
    f.metrics.underline_thickness = std::max(1.0f, f.size / 16.0f);
    f.metrics.has_ellipsis = m[3] > 0.5f;
}

Rml::FontFaceHandle CanvasFontEngine::GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
                                                        Rml::Style::FontWeight weight, int size) {
    const bool italic = style == Rml::Style::FontStyle::Italic;
    const int w = weight == Rml::Style::FontWeight::Auto ? 400 : (int)weight;
    for (size_t i = 0; i < faces_.size(); i++) {
        const Face& f = faces_[i];
        if (f.family == family && f.italic == italic && f.weight == w && f.size == size) return (Rml::FontFaceHandle)(i + 1);
    }
    Face f;
    f.family = family;
    f.italic = italic;
    f.weight = w;
    f.size = std::max(1, size);
    updateFace(f);
    faces_.push_back(f);
    return (Rml::FontFaceHandle)faces_.size();
}

const Rml::FontMetrics& CanvasFontEngine::GetFontMetrics(Rml::FontFaceHandle handle) {
    static const Rml::FontMetrics none{};
    if (handle == 0 || handle > faces_.size()) return none;
    return faces_[handle - 1].metrics;
}

float CanvasFontEngine::measure(const Face& f, const std::string& s, float letterSpacing) {
    std::string key = f.css;
    key += '\x1f';
    key += std::to_string(letterSpacing);
    key += '\x1f';
    key += s;
    auto it = widths_.find(key);
    if (it != widths_.end()) return it->second;
    const float w = (float)toms_text_width(f.css.c_str(), lang_.c_str(), s.c_str(), letterSpacing);
    widths_.emplace(std::move(key), w);
    return w;
}

int CanvasFontEngine::GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext& ctx,
                                     Rml::Character) {
    if (handle == 0 || handle > faces_.size() || string.begin() == string.end()) return 0;
    return (int)std::lround(measure(faces_[handle - 1], std::string(string.begin(), string.end()), ctx.letter_spacing));
}

int CanvasFontEngine::GenerateString(Rml::RenderManager& render_manager, Rml::FontFaceHandle handle, Rml::FontEffectsHandle,
                                     Rml::StringView string, Rml::Vector2f position, Rml::ColourbPremultiplied colour,
                                     float, const Rml::TextShapingContext& ctx, Rml::TexturedMeshList& mesh_list) {
    if (handle == 0 || handle > faces_.size() || string.begin() == string.end()) return 0;
    const Face& f = faces_[handle - 1];
    const std::string text(string.begin(), string.end());
    const bool rtl = ctx.text_direction == Rml::Style::Direction::Rtl;
    const float pad = std::ceil(f.size * 0.3f);   // room for overhangs, marks above/below the line

    std::string key = std::to_string(handle);
    key += rtl ? "\x1fR\x1f" : "\x1fL\x1f";
    key += std::to_string(ctx.letter_spacing);
    key += '\x1f';
    key += text;
    auto it = texts_.find(key);
    if (it == texts_.end()) {
        Text t;
        t.width = measure(f, text, ctx.letter_spacing);
        t.boxW = t.width + 2 * pad;
        t.boxH = f.metrics.ascent + f.metrics.descent + 2 * pad;
        const int w = std::max(1, (int)std::ceil(t.boxW * scale_));
        const int h = std::max(1, (int)std::ceil(t.boxH * scale_));
        t.boxW = w / scale_;   // the quad covers exactly the texture's pixels
        t.boxH = h / scale_;
        if (textureBytes_ + (size_t)w * h * 4 > kMaxTextureBytes) clearCaches();
        textureBytes_ += (size_t)w * h * 4;
        // Rendered when RmlUi first needs the texture (and again if the GPU texture is ever lost),
        // so no pixel copy is kept on the CPU side.
        const std::string css = f.css, lang = lang_;
        const float scale = scale_, letterSpacing = ctx.letter_spacing, baseline = pad + f.metrics.ascent;
        t.texture = Rml::CallbackTextureSource([=](const Rml::CallbackTextureInterface& ti) -> bool {
            std::vector<Rml::byte> px((size_t)w * h * 4);
            toms_text_render(css.c_str(), lang.c_str(), text.c_str(), letterSpacing, rtl ? 1 : 0, scale, pad, baseline,
                             w, h, px.data());
            for (size_t i = 0; i < px.size(); i += 4) px[i] = px[i + 1] = px[i + 2] = px[i + 3];   // white -> premultiplied
            return ti.GenerateTexture(px, Rml::Vector2i(w, h));
        });
        it = texts_.emplace(std::move(key), std::move(t)).first;
    }
    const Text& t = it->second;
    Rml::TexturedMesh tm;
    Rml::MeshUtilities::GenerateQuad(tm.mesh, Rml::Vector2f(position.x - pad, position.y - f.metrics.ascent - pad),
                                     Rml::Vector2f(t.boxW, t.boxH), colour, Rml::Vector2f(0, 0), Rml::Vector2f(1, 1));
    tm.texture = t.texture.GetTexture(render_manager);
    mesh_list.push_back(std::move(tm));
    return (int)std::lround(t.width);
}

}  // namespace toms::next
