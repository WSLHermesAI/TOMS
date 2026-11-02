// bitmap_font.cpp -- BMFont text .fnt parsing and text layout (bitmap_font.h).
#include "bitmap_font.h"
#include "vfs.h"

#include <stb_image.h>   // the implementation: stb_image_impl.cpp

#include <algorithm>
#include <cstdlib>

namespace {

// key=value pairs of one line; a value may be quoted (letter=" ", file="a b.png").
struct Fields {
    std::string m_strTag;
    std::vector<std::pair<std::string, std::string>> m_vKv;

    const std::string* f_Find(const char* l_pszKey) const {
        for (const auto& l_pairP : m_vKv)
            if (l_pairP.first == l_pszKey) return &l_pairP.second;
        return nullptr;
    }
    int f_Num(const char* l_pszKey, int l_iDef = 0) const {
        const std::string* l_pV = f_Find(l_pszKey);
        return l_pV ? std::atoi(l_pV->c_str()) : l_iDef;
    }
    std::string f_Str(const char* l_pszKey) const {
        const std::string* l_pV = f_Find(l_pszKey);
        return l_pV ? *l_pV : std::string();
    }
};

Fields f_SplitLine(std::string_view l_svLine) {
    Fields l_oF;
    size_t l_nI = 0;
    auto l_fnSkipSpace = [&] { while (l_nI < l_svLine.size() && (l_svLine[l_nI] == ' ' || l_svLine[l_nI] == '\t')) l_nI++; };
    l_fnSkipSpace();
    while (l_nI < l_svLine.size() && l_svLine[l_nI] != ' ' && l_svLine[l_nI] != '\t') l_oF.m_strTag += l_svLine[l_nI++];
    for (;;) {
        l_fnSkipSpace();
        if (l_nI >= l_svLine.size()) break;
        std::string l_strKey;
        while (l_nI < l_svLine.size() && l_svLine[l_nI] != '=' && l_svLine[l_nI] != ' ' && l_svLine[l_nI] != '\t') l_strKey += l_svLine[l_nI++];
        std::string l_strValue;
        if (l_nI < l_svLine.size() && l_svLine[l_nI] == '=') {
            l_nI++;
            if (l_nI < l_svLine.size() && l_svLine[l_nI] == '"') {
                const size_t l_nEnd = l_svLine.find('"', l_nI + 1);
                const size_t l_nStop = l_nEnd == std::string_view::npos ? l_svLine.size() : l_nEnd;
                l_strValue = std::string(l_svLine.substr(l_nI + 1, l_nStop - l_nI - 1));
                l_nI = l_nStop == l_svLine.size() ? l_nStop : l_nStop + 1;
            } else {
                while (l_nI < l_svLine.size() && l_svLine[l_nI] != ' ' && l_svLine[l_nI] != '\t') l_strValue += l_svLine[l_nI++];
            }
        }
        if (!l_strKey.empty()) l_oF.m_vKv.emplace_back(std::move(l_strKey), std::move(l_strValue));
    }
    return l_oF;
}

uint32_t f_OtherCase(uint32_t l_uCp) {
    if (l_uCp >= 'a' && l_uCp <= 'z') return l_uCp - 32;
    if (l_uCp >= 'A' && l_uCp <= 'Z') return l_uCp + 32;
    return 0;
}

uint64_t f_KernKey(uint32_t l_uA, uint32_t l_uB) { return ((uint64_t)l_uA << 32) | l_uB; }

}  // namespace

uint32_t f_Utf8Next(std::string_view l_svS, size_t& l_nI) {
    const unsigned char l_u8C = (unsigned char)l_svS[l_nI++];
    if (l_u8C < 0x80) return l_u8C;
    int l_iExtra = 0;
    uint32_t l_uCp = 0;
    if ((l_u8C & 0xE0) == 0xC0) { l_iExtra = 1; l_uCp = l_u8C & 0x1F; }
    else if ((l_u8C & 0xF0) == 0xE0) { l_iExtra = 2; l_uCp = l_u8C & 0x0F; }
    else if ((l_u8C & 0xF8) == 0xF0) { l_iExtra = 3; l_uCp = l_u8C & 0x07; }
    else return 0xFFFD;
    for (int l_iK = 0; l_iK < l_iExtra; l_iK++) {
        if (l_nI >= l_svS.size() || ((unsigned char)l_svS[l_nI] & 0xC0) != 0x80) return 0xFFFD;
        l_uCp = (l_uCp << 6) | ((unsigned char)l_svS[l_nI++] & 0x3F);
    }
    return l_uCp;
}

bool BitmapFont::f_Parse(std::string_view l_svText, std::string* l_pErr) {
    *this = BitmapFont();
    bool l_bHaveCommon = false;
    size_t l_nPos = 0;
    while (l_nPos < l_svText.size()) {
        size_t l_nEnd = l_svText.find('\n', l_nPos);
        if (l_nEnd == std::string_view::npos) l_nEnd = l_svText.size();
        std::string_view l_svLine = l_svText.substr(l_nPos, l_nEnd - l_nPos);
        l_nPos = l_nEnd + 1;
        if (!l_svLine.empty() && l_svLine.back() == '\r') l_svLine.remove_suffix(1);
        const Fields l_oF = f_SplitLine(l_svLine);
        if (l_oF.m_strTag == "info") {
            m_strFace = l_oF.f_Str("face");
            m_iSize = std::abs(l_oF.f_Num("size"));
        } else if (l_oF.m_strTag == "common") {
            l_bHaveCommon = true;
            m_iLineHeight = l_oF.f_Num("lineHeight");
            m_iBase = l_oF.f_Num("base");
            m_iScaleW = l_oF.f_Num("scaleW");
            m_iScaleH = l_oF.f_Num("scaleH");
        } else if (l_oF.m_strTag == "page") {
            const int l_iId = l_oF.f_Num("id");
            if (l_iId >= 0 && l_iId < 256) {
                if ((int)m_vPages.size() <= l_iId) m_vPages.resize(l_iId + 1);
                m_vPages[l_iId] = l_oF.f_Str("file");
            }
        } else if (l_oF.m_strTag == "char") {
            BitmapGlyph l_oG;
            l_oG.m_uId = (uint32_t)l_oF.f_Num("id");
            l_oG.m_iX = l_oF.f_Num("x");
            l_oG.m_iY = l_oF.f_Num("y");
            l_oG.m_iW = l_oF.f_Num("width");
            l_oG.m_iH = l_oF.f_Num("height");
            l_oG.m_iXoffset = l_oF.f_Num("xoffset");
            l_oG.m_iYoffset = l_oF.f_Num("yoffset");
            l_oG.m_iXadvance = l_oF.f_Num("xadvance");
            l_oG.m_iPage = l_oF.f_Num("page");
            auto l_itIt = m_mapIndex.find(l_oG.m_uId);
            if (l_itIt != m_mapIndex.end()) m_vGlyphs[l_itIt->second] = l_oG;   // a repeated id: the last one wins
            else { m_mapIndex[l_oG.m_uId] = m_vGlyphs.size(); m_vGlyphs.push_back(l_oG); }
        } else if (l_oF.m_strTag == "kerning") {
            m_mapKern[f_KernKey((uint32_t)l_oF.f_Num("first"), (uint32_t)l_oF.f_Num("second"))] = l_oF.f_Num("amount");
        }
    }
    if (!l_bHaveCommon || m_vGlyphs.empty()) {
        if (l_pErr) *l_pErr = !l_bHaveCommon ? "no 'common' line (not a text BMFont .fnt?)" : "no 'char' lines";
        return false;
    }
    if (m_iLineHeight <= 0)
        for (const BitmapGlyph& l_oG : m_vGlyphs) m_iLineHeight = std::max(m_iLineHeight, l_oG.m_iYoffset + l_oG.m_iH);
    return true;
}

const BitmapGlyph* BitmapFont::f_Glyph(uint32_t l_uCp) const {
    auto l_itIt = m_mapIndex.find(l_uCp);
    if (l_itIt == m_mapIndex.end()) {
        const uint32_t l_uAlt = f_OtherCase(l_uCp);
        if (l_uAlt) l_itIt = m_mapIndex.find(l_uAlt);
    }
    return l_itIt == m_mapIndex.end() ? nullptr : &m_vGlyphs[l_itIt->second];
}

int BitmapFont::f_Kerning(uint32_t l_uFirst, uint32_t l_uSecond) const {
    if (m_mapKern.empty()) return 0;
    auto l_itIt = m_mapKern.find(f_KernKey(l_uFirst, l_uSecond));
    return l_itIt == m_mapKern.end() ? 0 : l_itIt->second;
}

std::vector<BitmapFont::Placed> BitmapFont::f_Layout(std::string_view l_svUtf8, float l_fX, float l_fY, float l_fScale,
                                                   Align l_eAlign, int* l_pMissing) const {
    std::vector<Placed> l_vOut;
    if (l_pMissing) *l_pMissing = 0;
    size_t l_nI = 0;
    float l_fLineY = l_fY;
    while (l_nI <= l_svUtf8.size()) {
        // one line: lay it out from 0, then shift it by the alignment
        const size_t l_nFirst = l_vOut.size();
        float l_fPen = 0;
        uint32_t l_uPrev = 0;
        while (l_nI < l_svUtf8.size()) {
            const uint32_t l_uCp = f_Utf8Next(l_svUtf8, l_nI);
            if (l_uCp == '\n') break;
            if (l_uCp == '\r') continue;
            const BitmapGlyph* l_pG = f_Glyph(l_uCp);
            if (!l_pG) {
                if (l_pMissing) ++*l_pMissing;
                continue;
            }
            if (l_uPrev) l_fPen += f_Kerning(l_uPrev, l_pG->m_uId) * l_fScale;
            if (l_pG->m_iW > 0 && l_pG->m_iH > 0)
                l_vOut.push_back({l_pG, l_fPen + l_pG->m_iXoffset * l_fScale, l_fLineY + l_pG->m_iYoffset * l_fScale, l_pG->m_iW * l_fScale, l_pG->m_iH * l_fScale});
            l_fPen += l_pG->m_iXadvance * l_fScale;
            l_uPrev = l_pG->m_uId;
        }
        const float l_fShift = l_eAlign == Align::Left ? l_fX : l_eAlign == Align::Center ? l_fX - l_fPen * 0.5f : l_fX - l_fPen;
        for (size_t l_nK = l_nFirst; l_nK < l_vOut.size(); l_nK++) l_vOut[l_nK].m_fX += l_fShift;
        l_fLineY += m_iLineHeight * l_fScale;
        if (l_nI >= l_svUtf8.size()) break;
    }
    return l_vOut;
}

void BitmapFont::f_Measure(std::string_view l_svUtf8, float l_fScale, float& l_fW, float& l_fH) const {
    l_fW = 0;
    int l_iLines = 1;
    float l_fPen = 0;
    uint32_t l_uPrev = 0;
    size_t l_nI = 0;
    while (l_nI < l_svUtf8.size()) {
        const uint32_t l_uCp = f_Utf8Next(l_svUtf8, l_nI);
        if (l_uCp == '\n') { l_fW = std::max(l_fW, l_fPen); l_fPen = 0; l_uPrev = 0; l_iLines++; continue; }
        const BitmapGlyph* l_pG = f_Glyph(l_uCp);
        if (!l_pG) continue;
        if (l_uPrev) l_fPen += f_Kerning(l_uPrev, l_pG->m_uId) * l_fScale;
        l_fPen += l_pG->m_iXadvance * l_fScale;
        l_uPrev = l_pG->m_uId;
    }
    l_fW = std::max(l_fW, l_fPen);
    l_fH = (float)l_iLines * m_iLineHeight * l_fScale;
}

void BitmapFont::f_AppendQuads(std::string_view l_svUtf8, float l_fX, float l_fY, float l_fScale, Align l_eAlign, const float l_arrTint[4],
                             uint16_t l_u16Texture, std::vector<Quad>& l_vOut, int* l_pMissing) const {
    if (l_pMissing) *l_pMissing = 0;
    if (m_iScaleW <= 0 || m_iScaleH <= 0) return;
    for (const Placed& l_oP : f_Layout(l_svUtf8, l_fX, l_fY, l_fScale, l_eAlign, l_pMissing)) {
        Quad l_oQ;
        l_oQ.m_arrRect[0] = l_oP.m_fX; l_oQ.m_arrRect[1] = l_oP.m_fY; l_oQ.m_arrRect[2] = l_oP.m_fW; l_oQ.m_arrRect[3] = l_oP.m_fH;
        l_oQ.m_arrUv[0] = (float)l_oP.m_pGlyph->m_iX / m_iScaleW;
        l_oQ.m_arrUv[1] = (float)l_oP.m_pGlyph->m_iY / m_iScaleH;
        l_oQ.m_arrUv[2] = (float)(l_oP.m_pGlyph->m_iX + l_oP.m_pGlyph->m_iW) / m_iScaleW;
        l_oQ.m_arrUv[3] = (float)(l_oP.m_pGlyph->m_iY + l_oP.m_pGlyph->m_iH) / m_iScaleH;
        for (int l_iC = 0; l_iC < 4; l_iC++) l_oQ.m_arrTint[l_iC] = l_arrTint[l_iC];
        l_oQ.m_u16Texture = l_u16Texture;
        l_vOut.push_back(l_oQ);
    }
}

// ---- FontText ----

bool FontText::f_Load(IRenderer& l_oRenderer, const std::string& l_strFntPath, std::string* l_pErr) {
    auto l_fnFail = [&](const std::string& l_strWhy) {
        if (l_pErr) *l_pErr = l_strFntPath + ": " + l_strWhy;
        return false;
    };
    std::string l_strText;
    if (!toms::f_VfsReadAll(l_strFntPath, l_strText)) return l_fnFail("cannot read");
    BitmapFont l_oFont;
    std::string l_strWhy;
    if (!l_oFont.f_Parse(l_strText, &l_strWhy)) return l_fnFail(l_strWhy);
    if (l_oFont.m_vPages.empty() || l_oFont.m_vPages[0].empty()) return l_fnFail("no page file");
    const size_t l_nSlash = l_strFntPath.find_last_of("/\\");
    const std::string l_strPagePath = (l_nSlash == std::string::npos ? std::string() : l_strFntPath.substr(0, l_nSlash + 1)) + l_oFont.m_vPages[0];
    std::string l_strPng;
    if (!toms::f_VfsReadAll(l_strPagePath, l_strPng)) return l_fnFail("cannot read its page " + l_strPagePath);
    int l_iW = 0, l_iH = 0, l_iCh = 0;
    stbi_uc* l_pPx = stbi_load_from_memory((const stbi_uc*)l_strPng.data(), (int)l_strPng.size(), &l_iW, &l_iH, &l_iCh, 4);
    if (!l_pPx) return l_fnFail("cannot decode its page " + l_strPagePath);
    const uint16_t l_u16Tex = l_oRenderer.f_LoadTexture(std::vector<uint8_t>(l_pPx, l_pPx + (size_t)l_iW * l_iH * 4), (uint32_t)l_iW, (uint32_t)l_iH);
    stbi_image_free(l_pPx);
    if (l_oFont.m_iScaleW <= 0 || l_oFont.m_iScaleH <= 0) { l_oFont.m_iScaleW = l_iW; l_oFont.m_iScaleH = l_iH; }
    m_oFont = std::move(l_oFont);
    m_pRenderer = &l_oRenderer;
    m_u16Texture = l_u16Tex;
    m_bDirty = true;
    return true;
}

void FontText::f_Release() {
    if (m_pRenderer && m_u16Texture != g_u16SpriteAtlasTexture) m_pRenderer->f_ReleaseTexture(m_u16Texture);
    m_pRenderer = nullptr;
    m_u16Texture = g_u16SpriteAtlasTexture;
    m_vQuads.clear();
}

void FontText::f_SetText(std::string_view l_svUtf8) {
    if (m_strText == l_svUtf8) return;
    m_strText.assign(l_svUtf8.data(), l_svUtf8.size());
    m_bDirty = true;
}

void FontText::f_SetPosition(float l_fX, float l_fY) {
    if (l_fX == m_fX && l_fY == m_fY) return;
    m_fX = l_fX;
    m_fY = l_fY;
    m_bDirty = true;
}

void FontText::f_SetAlign(BitmapFont::Align l_eAlign) {
    if (l_eAlign == m_eAlign) return;
    m_eAlign = l_eAlign;
    m_bDirty = true;
}

void FontText::f_SetScale(float l_fScale) {
    if (l_fScale == m_fScale) return;
    m_fScale = l_fScale;
    m_bDirty = true;
}

void FontText::f_SetColor(float l_fR, float l_fG, float l_fB, float l_fA) {
    m_arrColor[0] = l_fR; m_arrColor[1] = l_fG; m_arrColor[2] = l_fB; m_arrColor[3] = l_fA;
    m_bDirty = true;
}

void FontText::f_SetVisible(bool l_bVisible) { m_bVisible = l_bVisible; }

void FontText::f_Relayout() const {
    if (!m_bDirty) return;
    m_vQuads.clear();
    m_oFont.f_AppendQuads(m_strText, m_fX, m_fY, m_fScale, m_eAlign, m_arrColor, m_u16Texture, m_vQuads, &m_iMissing);
    m_oFont.f_Measure(m_strText, m_fScale, m_fW, m_fH);
    m_bDirty = false;
}

void FontText::f_Render() {
    if (!m_pRenderer || !m_bVisible || m_strText.empty()) return;
    f_Relayout();
    for (const Quad& l_oQ : m_vQuads) m_pRenderer->f_DrawSprite(l_oQ);
}

float FontText::f_Width() const { f_Relayout(); return m_fW; }
float FontText::f_Height() const { f_Relayout(); return m_fH; }
int FontText::f_MissingChars() const { f_Relayout(); return m_iMissing; }
