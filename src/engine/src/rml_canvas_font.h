// rml_canvas_font.h -- RmlUi font engine for the browser build: the browser draws the text.
//
// The web build ships no font file (docs/gameplay/01_RMLUI.md, "Fonts"). Instead of FreeType, this engine
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
    void f_SetLanguage(const std::string& l_strLangTag, const std::string& l_strCssFonts);
    // Backbuffer pixels per UI pixel (the letterbox scale). Rounded, so window resizes don't
    // re-render every string on every frame.
    void f_SetPixelScale(float l_fScale);

    // ---- Rml::FontEngineInterface ----
    bool LoadFontFace(const Rml::String&, int, bool, Rml::Style::FontWeight) override { return true; }
    bool LoadFontFace(const Rml::String&, int, const Rml::String&, Rml::Style::FontStyle, Rml::Style::FontWeight, bool) override { return true; }
    bool LoadFontFace(Rml::Span<const Rml::byte>, int, const Rml::String&, Rml::Style::FontStyle, Rml::Style::FontWeight, bool) override { return true; }
    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& l_strFamily, Rml::Style::FontStyle l_eStyle,
                                          Rml::Style::FontWeight l_eWeight, int l_iSize) override;
    Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle, const Rml::FontEffectList&) override { return 0; }
    const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle l_u64Handle) override;
    int GetStringWidth(Rml::FontFaceHandle l_u64Handle, Rml::StringView l_oString, const Rml::TextShapingContext& l_oCtx,
                       Rml::Character l_ePriorCharacter) override;
    int GenerateString(Rml::RenderManager& l_oRenderManager, Rml::FontFaceHandle l_u64Face, Rml::FontEffectsHandle l_u64Effects,
                       Rml::StringView l_oString, Rml::Vector2f l_oPosition, Rml::ColourbPremultiplied l_oColour, float l_fOpacity,
                       const Rml::TextShapingContext& l_oCtx, Rml::TexturedMeshList& l_vMeshList) override;
    int GetVersion(Rml::FontFaceHandle) override { return m_iVersion; }
    void ReleaseFontResources() override { f_ClearCaches(); }

private:
    struct Face {
        std::string m_strFamily;          // as asked by the RCSS ("toms", "rmlui-debugger-font")
        bool m_bItalic = false;
        int m_iWeight = 400, m_iSize = 16;
        std::string m_strCss;             // canvas ctx.font value
        Rml::FontMetrics m_oMetrics{};
    };
    struct Text {                    // one rendered string
        Rml::CallbackTextureSource m_oTexture;
        float m_fWidth = 0;             // advance, UI px
        float m_fBoxW = 0, m_fBoxH = 0;    // texture size, UI px
    };
    void f_UpdateFace(Face& l_oF);        // css + metrics for the current language
    void f_ClearCaches();              // drop every rendered string; RmlUi regenerates (version++)
    float f_Measure(const Face& l_oF, const std::string& l_strS, float l_fLetterSpacing);

    std::vector<Face> m_vFaces;        // FontFaceHandle = index + 1
    std::unordered_map<std::string, Text> m_mapTexts;
    std::unordered_map<std::string, float> m_mapWidths;
    std::string m_strLang = "zh-TW", m_strCssFonts;
    float m_fScale = 1.0f;
    int m_iVersion = 1;
    size_t m_nTextureBytes = 0;        // GPU memory of the rendered strings (capped, then re-rendered)
};

}  // namespace toms::next
