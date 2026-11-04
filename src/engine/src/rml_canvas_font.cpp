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
// An EM_JS parameter's C++ name is also its JavaScript name: renaming one means renaming it in the body.
EM_JS(void, toms_text_metrics, (const char* l_pszFont, const char* l_pszLang, float l_fSize, float* l_pOut), {
    if (!Module.__tomsText) {
        var c = document.createElement('canvas');
        c.width = 8; c.height = 8;
        Module.__tomsText = { canvas: c, ctx: c.getContext('2d', { willReadFrequently: true }) };
    }
    var t = Module.__tomsText;
    var ctx = t.ctx;
    t.canvas.lang = UTF8ToString(l_pszLang);
    if ('lang' in ctx) ctx.lang = UTF8ToString(l_pszLang);
    ctx.font = UTF8ToString(l_pszFont);
    var m = ctx.measureText('Mg漢');
    var asc = m.fontBoundingBoxAscent;
    var desc = m.fontBoundingBoxDescent;
    if (!(asc > 0)) asc = Math.max(m.actualBoundingBoxAscent || 0, l_fSize * 0.88);
    if (!(desc > 0)) desc = Math.max(m.actualBoundingBoxDescent || 0, l_fSize * 0.24);
    var x = ctx.measureText('x').actualBoundingBoxAscent || l_fSize * 0.5;
    HEAPF32[(l_pOut >> 2) + 0] = asc;
    HEAPF32[(l_pOut >> 2) + 1] = desc;
    HEAPF32[(l_pOut >> 2) + 2] = x;
    HEAPF32[(l_pOut >> 2) + 3] = ctx.measureText('…').width > 0 ? 1 : 0;
});

EM_JS(double, toms_text_width, (const char* l_pszFont, const char* l_pszLang, const char* l_pszText, float l_fLetterSpacing), {
    var t = Module.__tomsText;
    var ctx = t.ctx;
    if ('lang' in ctx) ctx.lang = UTF8ToString(l_pszLang);
    ctx.font = UTF8ToString(l_pszFont);
    if ('letterSpacing' in ctx) ctx.letterSpacing = l_fLetterSpacing + 'px';
    return ctx.measureText(UTF8ToString(l_pszText)).width;
});

// Draws `text` white on transparent into a w x h RGBA buffer at `out`; (x, baseline) in UI px,
// scaled by `scale` to the texture's pixels.
EM_JS(void, toms_text_render, (const char* l_pszFont, const char* l_pszLang, const char* l_pszText, float l_fLetterSpacing,
                               int l_iRtl, float l_fScale, float l_fX, float l_fBaseline, int l_iW, int l_iH, unsigned char* l_pOut), {
    var t = Module.__tomsText;
    var c = t.canvas;
    c.width = l_iW; c.height = l_iH;     // also clears it and resets the context
    var ctx = t.ctx;
    ctx.setTransform(l_fScale, 0, 0, l_fScale, 0, 0);
    if ('lang' in ctx) ctx.lang = UTF8ToString(l_pszLang);
    ctx.font = UTF8ToString(l_pszFont);
    if ('letterSpacing' in ctx) ctx.letterSpacing = l_fLetterSpacing + 'px';
    ctx.direction = l_iRtl ? 'rtl' : 'ltr';
    ctx.textAlign = 'left';        // x is the left edge in both directions
    ctx.textBaseline = 'alphabetic';
    ctx.fillStyle = '#ffffff';
    ctx.fillText(UTF8ToString(l_pszText), l_fX, l_fBaseline);
    HEAPU8.set(ctx.getImageData(0, 0, l_iW, l_iH).data, l_pOut);
});

constexpr size_t g_nMaxTextureBytes = 48u * 1024 * 1024;   // then drop the cache and re-render on demand

}  // namespace

namespace toms::next {

void CanvasFontEngine::f_SetLanguage(const std::string& l_strLangTag, const std::string& l_strCssFonts) {
    if (l_strLangTag == m_strLang && l_strCssFonts == m_strCssFonts) return;
    m_strLang = l_strLangTag;
    m_strCssFonts = l_strCssFonts;
    for (Face& l_oF : m_vFaces) f_UpdateFace(l_oF);   // handles stay valid; their fonts and metrics change
    f_ClearCaches();
}

void CanvasFontEngine::f_SetPixelScale(float l_fScale) {
    // Quarter steps, at least 1: a phone at 2.4x draws at 2.5x; a window drag doesn't re-render text
    // every frame.
    const float l_fS = std::max(1.0f, std::round(l_fScale * 4.0f) / 4.0f);
    if (l_fS == m_fScale) return;
    m_fScale = l_fS;
    f_ClearCaches();
}

void CanvasFontEngine::f_ClearCaches() {
    m_mapTexts.clear();
    m_mapWidths.clear();
    m_nTextureBytes = 0;
    ++m_iVersion;   // RmlUi regenerates every string's geometry
}

void CanvasFontEngine::f_UpdateFace(Face& l_oF) {
    const std::string l_strStack = (l_oF.m_strFamily == "rmlui-debugger-font") ? std::string("sans-serif")
                            : (m_strCssFonts.empty() ? std::string("sans-serif") : m_strCssFonts);
    l_oF.m_strCss = std::string(l_oF.m_bItalic ? "italic " : "") + std::to_string(l_oF.m_iWeight) + " " + std::to_string(l_oF.m_iSize) + "px " + l_strStack;
    float l_arrM[4] = {0, 0, 0, 0};
    toms_text_metrics(l_oF.m_strCss.c_str(), m_strLang.c_str(), (float)l_oF.m_iSize, l_arrM);
    l_oF.m_oMetrics.size = l_oF.m_iSize;
    l_oF.m_oMetrics.ascent = l_arrM[0];
    l_oF.m_oMetrics.descent = l_arrM[1];
    l_oF.m_oMetrics.line_spacing = l_arrM[0] + l_arrM[1];
    l_oF.m_oMetrics.x_height = l_arrM[2];
    l_oF.m_oMetrics.underline_position = std::max(1.0f, l_oF.m_iSize * 0.1f);
    l_oF.m_oMetrics.underline_thickness = std::max(1.0f, l_oF.m_iSize / 16.0f);
    l_oF.m_oMetrics.has_ellipsis = l_arrM[3] > 0.5f;
}

Rml::FontFaceHandle CanvasFontEngine::GetFontFaceHandle(const Rml::String& l_strFamily, Rml::Style::FontStyle l_eStyle,
                                                        Rml::Style::FontWeight l_eWeight, int l_iSize) {
    const bool l_bItalic = l_eStyle == Rml::Style::FontStyle::Italic;
    const int l_iW = l_eWeight == Rml::Style::FontWeight::Auto ? 400 : (int)l_eWeight;
    for (size_t l_nI = 0; l_nI < m_vFaces.size(); l_nI++) {
        const Face& l_oF = m_vFaces[l_nI];
        if (l_oF.m_strFamily == l_strFamily && l_oF.m_bItalic == l_bItalic && l_oF.m_iWeight == l_iW && l_oF.m_iSize == l_iSize) return (Rml::FontFaceHandle)(l_nI + 1);
    }
    Face l_oF;
    l_oF.m_strFamily = l_strFamily;
    l_oF.m_bItalic = l_bItalic;
    l_oF.m_iWeight = l_iW;
    l_oF.m_iSize = std::max(1, l_iSize);
    f_UpdateFace(l_oF);
    m_vFaces.push_back(l_oF);
    return (Rml::FontFaceHandle)m_vFaces.size();
}

const Rml::FontMetrics& CanvasFontEngine::GetFontMetrics(Rml::FontFaceHandle l_u64Handle) {
    static const Rml::FontMetrics l_oNone{};
    if (l_u64Handle == 0 || l_u64Handle > m_vFaces.size()) return l_oNone;
    return m_vFaces[l_u64Handle - 1].m_oMetrics;
}

float CanvasFontEngine::f_Measure(const Face& l_oF, const std::string& l_strS, float l_fLetterSpacing) {
    std::string l_strKey = l_oF.m_strCss;
    l_strKey += '\x1f';
    l_strKey += std::to_string(l_fLetterSpacing);
    l_strKey += '\x1f';
    l_strKey += l_strS;
    auto l_itIt = m_mapWidths.find(l_strKey);
    if (l_itIt != m_mapWidths.end()) return l_itIt->second;
    const float l_fW = (float)toms_text_width(l_oF.m_strCss.c_str(), m_strLang.c_str(), l_strS.c_str(), l_fLetterSpacing);
    m_mapWidths.emplace(std::move(l_strKey), l_fW);
    return l_fW;
}

int CanvasFontEngine::GetStringWidth(Rml::FontFaceHandle l_u64Handle, Rml::StringView l_oString, const Rml::TextShapingContext& l_oCtx,
                                     Rml::Character) {
    if (l_u64Handle == 0 || l_u64Handle > m_vFaces.size() || l_oString.begin() == l_oString.end()) return 0;
    return (int)std::lround(f_Measure(m_vFaces[l_u64Handle - 1], std::string(l_oString.begin(), l_oString.end()), l_oCtx.letter_spacing));
}

int CanvasFontEngine::GenerateString(Rml::RenderManager& l_oRenderManager, Rml::FontFaceHandle l_u64Handle, Rml::FontEffectsHandle,
                                     Rml::StringView l_oString, Rml::Vector2f l_oPosition, Rml::ColourbPremultiplied l_oColour,
                                     float, const Rml::TextShapingContext& l_oCtx, Rml::TexturedMeshList& l_vMeshList) {
    if (l_u64Handle == 0 || l_u64Handle > m_vFaces.size() || l_oString.begin() == l_oString.end()) return 0;
    const Face& l_oF = m_vFaces[l_u64Handle - 1];
    const std::string l_strText(l_oString.begin(), l_oString.end());
    const bool l_bRtl = l_oCtx.text_direction == Rml::Style::Direction::Rtl;
    const float l_fPad = std::ceil(l_oF.m_iSize * 0.3f);   // room for overhangs, marks above/below the line

    std::string l_strKey = std::to_string(l_u64Handle);
    l_strKey += l_bRtl ? "\x1fR\x1f" : "\x1fL\x1f";
    l_strKey += std::to_string(l_oCtx.letter_spacing);
    l_strKey += '\x1f';
    l_strKey += l_strText;
    auto l_itIt = m_mapTexts.find(l_strKey);
    if (l_itIt == m_mapTexts.end()) {
        Text l_oT;
        l_oT.m_fWidth = f_Measure(l_oF, l_strText, l_oCtx.letter_spacing);
        l_oT.m_fBoxW = l_oT.m_fWidth + 2 * l_fPad;
        l_oT.m_fBoxH = l_oF.m_oMetrics.ascent + l_oF.m_oMetrics.descent + 2 * l_fPad;
        const int l_iW = std::max(1, (int)std::ceil(l_oT.m_fBoxW * m_fScale));
        const int l_iH = std::max(1, (int)std::ceil(l_oT.m_fBoxH * m_fScale));
        l_oT.m_fBoxW = l_iW / m_fScale;   // the quad covers exactly the texture's pixels
        l_oT.m_fBoxH = l_iH / m_fScale;
        if (m_nTextureBytes + (size_t)l_iW * l_iH * 4 > g_nMaxTextureBytes) f_ClearCaches();
        m_nTextureBytes += (size_t)l_iW * l_iH * 4;
        // Rendered when RmlUi first needs the texture (and again if the GPU texture is ever lost),
        // so no pixel copy is kept on the CPU side.
        const std::string l_strCss = l_oF.m_strCss, l_strLang = m_strLang;
        const float l_fScale = m_fScale, l_fLetterSpacing = l_oCtx.letter_spacing, l_fBaseline = l_fPad + l_oF.m_oMetrics.ascent;
        l_oT.m_oTexture = Rml::CallbackTextureSource([=](const Rml::CallbackTextureInterface& l_oTi) -> bool {
            std::vector<Rml::byte> l_vPx((size_t)l_iW * l_iH * 4);
            toms_text_render(l_strCss.c_str(), l_strLang.c_str(), l_strText.c_str(), l_fLetterSpacing, l_bRtl ? 1 : 0, l_fScale, l_fPad, l_fBaseline,
                             l_iW, l_iH, l_vPx.data());
            for (size_t l_nI = 0; l_nI < l_vPx.size(); l_nI += 4) l_vPx[l_nI] = l_vPx[l_nI + 1] = l_vPx[l_nI + 2] = l_vPx[l_nI + 3];   // white -> premultiplied
            return l_oTi.GenerateTexture(l_vPx, Rml::Vector2i(l_iW, l_iH));
        });
        l_itIt = m_mapTexts.emplace(std::move(l_strKey), std::move(l_oT)).first;
    }
    const Text& l_oT = l_itIt->second;
    Rml::TexturedMesh l_oTm;
    Rml::MeshUtilities::GenerateQuad(l_oTm.mesh, Rml::Vector2f(l_oPosition.x - l_fPad, l_oPosition.y - l_oF.m_oMetrics.ascent - l_fPad),
                                     Rml::Vector2f(l_oT.m_fBoxW, l_oT.m_fBoxH), l_oColour, Rml::Vector2f(0, 0), Rml::Vector2f(1, 1));
    l_oTm.texture = l_oT.m_oTexture.GetTexture(l_oRenderManager);
    l_vMeshList.push_back(std::move(l_oTm));
    return (int)std::lround(l_oT.m_fWidth);
}

}  // namespace toms::next
