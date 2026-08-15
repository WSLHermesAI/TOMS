// font.cpp — runtime TTF rasterization via stb_truetype.
#include "font.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <fstream>
#include <algorithm>
#include <cstring>

// ---- gather codepoints from JSON files (UTF-8) + an extra literal string ----
std::vector<uint32_t> Font::collectFromFiles(const std::vector<std::string>& jsonFiles,
                                             const std::string& extra) {
    std::unordered_map<uint32_t,int> seen;
    auto addCp = [&](uint32_t cp){ if (cp) seen[cp] = 1; };
    auto addStr = [&](const std::string& s){
        size_t i = 0;
        while (i < s.size()) {
            unsigned char c = (unsigned char)s[i]; uint32_t cp; int len;
            if (c < 0x80) { cp = c; len = 1; }
            else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
            else { i++; continue; }
            for (int k = 1; k < len && i + k < s.size(); k++)
                cp = (cp << 6) | ((unsigned char)s[i+k] & 0x3F);
            addCp(cp); i += len;
        }
    };
    for (auto& f : jsonFiles) {
        std::ifstream in(f); if (!in) continue;
        std::string buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        addStr(buf);
    }
    addStr(extra);
    std::vector<uint32_t> out; out.reserve(seen.size());
    for (auto& kv : seen) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

bool Font::buildFromFile(const std::string& ttfPath,
                         const std::vector<uint32_t>& chars,
                         int cell, int fontPx) {
    // load the whole font file into memory
    std::ifstream in(ttfPath, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> ttf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (ttf.empty()) return false;

    stbtt_fontinfo fi;
    // A .ttc is a font collection: offset 0 is the TTCHeader, not a font table.
    // Resolve the first face's real offset; for plain .ttf offset 0 is correct.
    int ttcOffset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (ttcOffset < 0) return false;
    if (!stbtt_InitFont(&fi, ttf.data(), ttcOffset)) return false;

    // scale so the glyph cell matches `cell`; use ascent for vertical fit like PIL
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&fi, &ascent, &descent, &lineGap);
    float scale = stbtt_ScaleForPixelHeight(&fi, (float)fontPx);

    int n = (int)chars.size();
    if (n == 0) return false;
    const int COLS = 32;
    int rows = (n + COLS - 1) / COLS;
    atlasW_ = (uint32_t)(COLS * cell);
    atlasH_ = (uint32_t)(rows * cell);
    atlas_.assign((size_t)atlasW_ * atlasH_ * 4, 0);   // transparent

    uint8_t* base = atlas_.data();
    // temp buffer for one glyph (worst case cell size; +2 for padding)
    std::vector<uint8_t> gb((size_t)(cell + 2) * (cell + 2), 0);

    for (int idx = 0; idx < n; idx++) {
        uint32_t cp = chars[idx];
        int g = stbtt_FindGlyphIndex(&fi, (int)cp);
        if (g == 0) { continue; }   // missing glyph (e.g. control char): skip, no UV

        int ax = 0, lsb = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphHMetrics(&fi, g, &ax, &lsb);
        stbtt_GetGlyphBitmapBox(&fi, g, scale, scale, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;   // stb y is up: glyph height = y1 - y0
        if (gw <= 0 || gh <= 0) {         // whitespace glyph
            int cx = (idx % COLS) * cell, cy = (idx / COLS) * cell;
            map_[cp] = { (float)cx / atlasW_, (float)cy / atlasH_,
                         (float)(cx + cell) / atlasW_, (float)(cy + cell) / atlasH_ };
            continue;
        }
        if (gw > cell + 2) gw = cell + 2;
        if (gh > cell + 2) gh = cell + 2;
        stbtt_MakeGlyphBitmap(&fi, gb.data(), gw, gh, gw, scale, scale, g);

        int cellX = (idx % COLS) * cell;
        int cellY = (idx / COLS) * cell;
        // center the glyph in its CELL (stb output is already top-down rows)
        int offX = cellX + (cell - gw) / 2;
        int offY = cellY + (cell - gh) / 2;
        for (int yy = 0; yy < gh; yy++)
            for (int xx = 0; xx < gw; xx++) {
                uint8_t a = gb[(size_t)yy * gw + xx];
                int dx = offX + xx;
                int dy = offY + yy;
                if (dx < 0 || dy < 0 || dx >= (int)atlasW_ || dy >= (int)atlasH_) continue;
                size_t p = ((size_t)dy * atlasW_ + dx) * 4;
                base[p] = 255; base[p+1] = 255; base[p+2] = 255; base[p+3] = a;  // white glyph, alpha = coverage
            }

        map_[cp] = { (float)cellX / atlasW_, (float)cellY / atlasH_,
                     (float)(cellX + cell) / atlasW_, (float)(cellY + cell) / atlasH_ };
    }
    return true;
}
