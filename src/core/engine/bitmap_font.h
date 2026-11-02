// bitmap_font.h -- picture fonts: BMFont text .fnt files (one PNG page of hand-drawn glyphs).
//
// The score / counter fonts of the old slot projects (tests/font/connect_score.fnt) are this format, written
// by BMFont ("Arial" header) or convert2fnt-web. tools/font (fontpack, font_editor) makes them from
// one picture per character; docs/engine/09_FONT_TOOL.md.
//
// The font is only data and layout: the caller loads the page PNG (IRenderer::loadTexture) and
// draws the quads appendQuads() makes. The font tool's preview lays text out the same way.
#pragma once
#include "render_iface.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct BitmapGlyph {
    uint32_t m_uId = 0;              // Unicode code point
    int m_iX = 0, m_iY = 0, m_iW = 0, m_iH = 0;   // the picture on its page, in pixels (w or h 0: nothing to draw)
    int m_iXoffset = 0, m_iYoffset = 0;     // where the picture goes relative to the pen (y: from the line top)
    int m_iXadvance = 0;                 // pen step after this character
    int m_iPage = 0;
};

class BitmapFont {
public:
    enum class Align { Left, Center, Right };

    // One character of laid out text: the glyph and its picture's place on screen.
    struct Placed {
        const BitmapGlyph* m_pGlyph = nullptr;
        float m_fX = 0, m_fY = 0, m_fW = 0, m_fH = 0;
    };

    // Parses the text form of a .fnt file. False (and err) when it has no common line or no chars.
    bool f_Parse(std::string_view l_svText, std::string* l_pErr = nullptr);

    // The glyph of a code point, or null. Falls back to the other letter case (the old fonts carry
    // "K" "M" or "k" "m", never both), so "1.5K" draws with either.
    const BitmapGlyph* f_Glyph(uint32_t l_uCp) const;
    int f_Kerning(uint32_t l_uFirst, uint32_t l_uSecond) const;

    // Lays UTF-8 text out from (x, y) = the top left of the first line; '\n' starts a new line,
    // each line aligned on x by `align`. Characters the font lacks are skipped (missing counts them).
    std::vector<Placed> f_Layout(std::string_view l_svUtf8, float l_fX, float l_fY, float l_fScale = 1.0f,
                               Align l_eAlign = Align::Left, int* l_pMissing = nullptr) const;
    // Width of the widest line and the height of all lines, in pixels at this scale.
    void f_Measure(std::string_view l_svUtf8, float l_fScale, float& l_fW, float& l_fH) const;
    // layout() as sprite quads on `texture` (the page loaded with IRenderer::loadTexture).
    void f_AppendQuads(std::string_view l_svUtf8, float l_fX, float l_fY, float l_fScale, Align l_eAlign, const float l_arrTint[4],
                     uint16_t l_u16Texture, std::vector<Quad>& l_vOut, int* l_pMissing = nullptr) const;

    std::string m_strFace;
    int m_iSize = 0;
    int m_iLineHeight = 0;
    int m_iBase = 0;
    int m_iScaleW = 0, m_iScaleH = 0;          // the page size the glyph rects are in
    std::vector<std::string> m_vPages;      // page file names, relative to the .fnt
    std::vector<BitmapGlyph> m_vGlyphs;     // in file order

private:
    std::unordered_map<uint32_t, size_t> m_mapIndex;
    std::unordered_map<uint64_t, int> m_mapKern;
};

// Decodes the next code point of UTF-8 text at i (advancing i); bad bytes decode as U+FFFD.
uint32_t f_Utf8Next(std::string_view l_svS, size_t& l_nI);

// One piece of text drawn with a picture font: the easy way to use BitmapFont in the game.
//
//   FontText score;
//   score.load(renderer, "assets/media/fonts/connect_score.fnt");   // once (init)
//   score.setText("12,345");                                          // in update / an event
//   score.setPosition(900, 40);
//   score.setAlign(BitmapFont::Align::Right);
//   score.render();                                                   // in render, between begin() and end()
//
// The quads are laid out again only when something changed, so render() every frame is cheap.
// A copy of a loaded FontText shares its font and page texture: load once, copy for each label.
class FontText {
public:
    // Reads the .fnt and its page PNG (next to it) and uploads the page. False (and err) on failure;
    // render() then draws nothing.
    bool f_Load(IRenderer& l_oRenderer, const std::string& l_strFntPath, std::string* l_pErr = nullptr);
    // Frees the page texture. Call once, on one copy, when no copy draws any more.
    void f_Release();

    void f_SetText(std::string_view l_svUtf8);
    void f_SetPosition(float l_fX, float l_fY);                    // the anchor: left / centre / right of the top line
    void f_SetAlign(BitmapFont::Align l_eAlign);
    void f_SetScale(float l_fScale);
    void f_SetColor(float l_fR, float l_fG, float l_fB, float l_fA = 1.0f);   // multiplies the pictures (white = as drawn)
    void f_SetVisible(bool l_bVisible);

    void f_Render();

    const std::string& f_Text() const { return m_strText; }
    bool f_Loaded() const { return m_pRenderer != nullptr; }
    float f_Width() const;                                   // the text's size at the current scale
    float f_Height() const;
    int f_MissingChars() const;                              // characters of the text the font lacks
    const BitmapFont& f_Font() const { return m_oFont; }

private:
    void f_Relayout() const;

    IRenderer* m_pRenderer = nullptr;
    uint16_t m_u16Texture = g_u16SpriteAtlasTexture;
    BitmapFont m_oFont;
    std::string m_strText;
    float m_fX = 0, m_fY = 0, m_fScale = 1;
    float m_arrColor[4] = {1, 1, 1, 1};
    BitmapFont::Align m_eAlign = BitmapFont::Align::Left;
    bool m_bVisible = true;
    mutable bool m_bDirty = true;
    mutable std::vector<Quad> m_vQuads;
    mutable float m_fW = 0, m_fH = 0;
    mutable int m_iMissing = 0;
};
