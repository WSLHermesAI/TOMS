// rml_canvas_font.h -- RmlUi font engine for the browser build: the browser draws the text.
//
// The web build ships no font file (docs/08_RMLUI.md, "Fonts"). Instead of FreeType, this engine
// asks an HTML canvas: widths come from measureText(), and every distinct string is drawn WHOLE
// with fillText() into its own small texture. Drawing whole strings (not glyph by glyph) is what
// lets the browser do the hard parts for every script -- Arabic/Persian joining, right-to-left
// runs, Indic/Thai shaping, CJK and emoji fallback -- with whatever fonts the device has.
//
// Fonts come from a CSS font list per language (text.json "web_font"), set with setLanguage().
// Strings are drawn at the screen's real pixel density (setPixelScale), so text stays sharp when
// the 1024x768 UI is scaled up on a phone. Emscripten only.
#pragma once
#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/FontMetrics.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace toms::next {

class CanvasFontEngine final : public Rml::FontEngineInterface {
public:
    // cssFonts: a CSS font-family list ("'PingFang TC', 'Microsoft JhengHei', sans-serif"); empty =
    // the browser's default. langTag: BCP 47 ("zh-TW"), picks the right CJK glyph variants.
    void setLanguage(const std::string& langTag, const std::string& cssFonts);
    // Backbuffer pixels per UI pixel (the letterbox scale). Rounded, so window resizes don't
    // re-render every string on every frame.
    void setPixelScale(float scale);

    // ---- Rml::FontEngineInterface ----
    bool LoadFontFace(const Rml::String&, int, bool, Rml::Style::FontWeight) override { return true; }
    bool LoadFontFace(const Rml::String&, int, const Rml::String&, Rml::Style::FontStyle, Rml::Style::FontWeight, bool) override { return true; }
    bool LoadFontFace(Rml::Span<const Rml::byte>, int, const Rml::String&, Rml::Style::FontStyle, Rml::Style::FontWeight, bool) override { return true; }
    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
                                          Rml::Style::FontWeight weight, int size) override;
    Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle, const Rml::FontEffectList&) override { return 0; }
    const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override;
    int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext& ctx,
                       Rml::Character prior_character) override;
    int GenerateString(Rml::RenderManager& render_manager, Rml::FontFaceHandle face, Rml::FontEffectsHandle effects,
                       Rml::StringView string, Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity,
                       const Rml::TextShapingContext& ctx, Rml::TexturedMeshList& mesh_list) override;
    int GetVersion(Rml::FontFaceHandle) override { return version_; }
    void ReleaseFontResources() override { clearCaches(); }

private:
    struct Face {
        std::string family;          // as asked by the RCSS ("toms", "rmlui-debugger-font")
        bool italic = false;
        int weight = 400, size = 16;
        std::string css;             // canvas ctx.font value
        Rml::FontMetrics metrics{};
    };
    struct Text {                    // one rendered string
        Rml::CallbackTextureSource texture;
        float width = 0;             // advance, UI px
        float boxW = 0, boxH = 0;    // texture size, UI px
    };
    void updateFace(Face& f);        // css + metrics for the current language
    void clearCaches();              // drop every rendered string; RmlUi regenerates (version++)
    float measure(const Face& f, const std::string& s, float letterSpacing);

    std::vector<Face> faces_;        // FontFaceHandle = index + 1
    std::unordered_map<std::string, Text> texts_;
    std::unordered_map<std::string, float> widths_;
    std::string lang_ = "zh-TW", cssFonts_;
    float scale_ = 1.0f;
    int version_ = 1;
    size_t textureBytes_ = 0;        // GPU memory of the rendered strings (capped, then re-rendered)
};

}  // namespace toms::next
