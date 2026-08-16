// font.cpp — runtime TTF rasterization via stb_truetype.
#include "font.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <filesystem>

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

// Rasterize one glyph from `info` into cell `idx` of the atlas. Returns false if
// the font has no such glyph. Used by both the initial bake and realtime fallback.
// Bakes at scale = cell/(ascent-descent) so the glyph's DESIGN size equals `cell`
// (no shrink) — matching a freetype layout. The atlas cell is only a packing
// container; drawText uses the stored design metrics (bearing + natural size) to
// size/place the quad exactly (FM79979 FreetypeGlypth.cpp RenderFont @ L145).
bool Font::bakeGlyph(uint32_t cp, stbtt_fontinfo& info, const std::vector<uint8_t>&, int idx) {
    int g = stbtt_FindGlyphIndex(&info, (int)cp);
    if (g == 0) return false;              // glyph absent from this font

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&info, g, 1.0f, 1.0f, &x0, &y0, &x1, &y1);
    // design metrics in font units; convert with the same vertical-fit scale the
    // reference uses (scale so the em-box == cell, i.e. natural glyph size).
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    float scale = (ascent > 0) ? ((float)cell_ / (float)(ascent - descent)) : 1.0f;

    int gw = (int)((x1 - x0) * scale), gh = (int)((y1 - y0) * scale);
    float leftBearing = (float)x0 * scale;   // px from pen to glyph left
    float topBearing  = (float)y0 * scale;   // px from pen(baseline) to glyph top (<=0)
    int cellX = (idx % cols_) * cell_, cellY = (idx / cols_) * cell_;

    if (gw <= 0 || gh <= 0) {              // whitespace glyph: reserve an empty cell
        glyphBox_[cp] = {0, 0, 0, 0};
        advPx_[cp] = std::max(1, cell_ / 3);   // a space still advances a bit
        glyphM_[cp] = {0, 0, (float)(cell_/3), 0};
        map_[cp] = { (float)cellX / atlasW_, (float)cellY / atlasH_,
                     (float)(cellX + cell_) / atlasW_, (float)(cellY + cell_) / atlasH_ };
        cellIdx_[cp] = idx;
        return true;
    }
    if (gw > cell_) gw = cell_;
    if (gh > cell_) gh = cell_;

    std::vector<uint8_t> gb((size_t)gw * gh, 0);
    // rasterize at scale, then copy (stb output is top-down rows)
    stbtt_MakeGlyphBitmap(&info, gb.data(), gw, gh, gw, scale, scale, g);

    int offX = cellX + (cell_ - gw) / 2;   // center within cell for packing only
    int offY = cellY + (cell_ - gh) / 2;
    uint8_t* base = atlas_.data();
    for (int yy = 0; yy < gh; yy++)
        for (int xx = 0; xx < gw; xx++) {
            uint8_t a = gb[(size_t)yy * gw + xx];
            int dx = offX + xx, dy = offY + yy;
            if (dx < 0 || dy < 0 || dx >= (int)atlasW_ || dy >= (int)atlasH_) continue;
            size_t p = ((size_t)dy * atlasW_ + dx) * 4;
            base[p] = 255; base[p+1] = 255; base[p+2] = 255; base[p+3] = a;  // white glyph
        }
    // TIGHT uv (both axes span the actual inked glyph) so the quad never samples
    // padding or a neighbour under linear filtering.
    glyphBox_[cp] = { offX - cellX, offY - cellY, gw, gh };
    advPx_[cp] = gw;
    glyphM_[cp] = { leftBearing, topBearing, (float)gw, (float)gh };
    map_[cp] = { (float)offX / atlasW_, (float)offY / atlasH_,
                 (float)(offX + gw) / atlasW_, (float)(offY + gh) / atlasH_ };
    cellIdx_[cp] = idx;
    return true;
}

// Recompute a glyph's UV from its stored cell-local box. Called after the atlas
// grows (atlasH_ changed) so the v-coordinate stays correct.
void Font::recomputeUV(uint32_t cp) {
    auto it = cellIdx_.find(cp);
    if (it == cellIdx_.end()) return;
    int idx = it->second;
    int cx = (idx % cols_) * cell_, cy = (idx / cols_) * cell_;
    auto b = glyphBox_.find(cp);
    if (b == glyphBox_.end() || (b->second[2] == 0 && b->second[3] == 0)) {
        // whitespace / empty cell
        map_[cp] = { (float)cx / atlasW_, (float)cy / atlasH_,
                     (float)(cx + cell_) / atlasW_, (float)(cy + cell_) / atlasH_ };
    } else {
        int ox = b->second[0], oy = b->second[1], gw = b->second[2];
        map_[cp] = { (float)(cx + ox) / atlasW_, (float)cy / atlasH_,
                     (float)(cx + ox + gw) / atlasW_, (float)(cy + cell_) / atlasH_ };
    }
}

// Add one row of cells to the atlas and recompute every existing glyph's v so the
// UVs stay correct (v is a fraction of atlasH, which just grew). We keep each
// glyph's grid cell index in cellIdx_, so the recompute is exact.
void Font::growAtlasOneRow() {
    rows_ += 1;
    atlasH_ = (uint32_t)(rows_ * cell_);
    atlas_.resize((size_t)atlasW_ * atlasH_ * 4, 0);   // tail is zero-filled (transparent)
    for (auto& kv : cellIdx_) recomputeUV(kv.first);
}

bool Font::buildFromFile(const std::string& ttfPath,
                         const std::vector<uint32_t>& chars,
                         int cell, int fontPx) {
    cell_ = cell; cols_ = 32;
    // load the whole font file into memory (keep a copy for realtime re-bake)
    std::ifstream in(ttfPath, std::ios::binary);
    if (!in) return false;
    primData_.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (primData_.empty()) return false;

    int ttcOffset = stbtt_GetFontOffsetForIndex(primData_.data(), 0);
    if (ttcOffset < 0) return false;
    primInfo_ = std::make_unique<stbtt_fontinfo>();
    if (!stbtt_InitFont(primInfo_.get(), primData_.data(), ttcOffset)) return false;
    primValid_ = true;

    // scale for vertical fit
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(primInfo_.get(), &ascent, &descent, &lineGap);
    float scale = (ascent > 0) ? ((float)(cell_ - 4) / (float)(ascent - descent)) : 1.0f;
    (void)fontPx; (void)scale;   // per-glyph scale recomputed in bakeGlyph

    int n = (int)chars.size();
    if (n == 0) return false;
    // Allocate a FIXED-size atlas with generous slack so realtime ensure() can
    // append new glyphs at fresh cells WITHOUT ever growing/recomputing the atlas
    // mid-frame. Growing the atlas mid-frame rewrites every glyph's V (atlasH_
    // changes) and invalidates the UVs of text quads already submitted THIS frame
    // -> duplicated/offset glyphs. The slack avoids that entirely (see ensure()).
    int slack = 2048;
    rows_ = (n + slack + cols_ - 1) / cols_;
    atlasW_ = (uint32_t)(cols_ * cell_);
    atlasH_ = (uint32_t)(rows_ * cell_);
    atlas_.assign((size_t)atlasW_ * atlasH_ * 4, 0);
    nextIdx_ = 0;
    map_.clear();
    cellIdx_.clear();

    for (uint32_t cp : chars) {
        if (map_.find(cp) != map_.end()) continue;
        int idx = nextIdx_++;
        if (idx >= cols_ * rows_) growAtlasOneRow();
        bakeGlyph(cp, *primInfo_, primData_, idx);
    }
    return true;
}

// Lazily discover + cache fallback fonts; return the first one that contains cp.
std::pair<stbtt_fontinfo*, const std::vector<uint8_t>*> Font::findFallbackFont(uint32_t cp) {
    if (!fallbackScanned_) {
        fallbackScanned_ = true;
        if (std::filesystem::exists(fallbackDir_)) {
            for (auto& p : std::filesystem::recursive_directory_iterator(fallbackDir_)) {
                std::string ext = p.path().extension().string();
                if (ext == ".ttf" || ext == ".ttc" || ext == ".otf")
                    fallbackFiles_.push_back(p.path().string());
            }
        }
    }
    for (auto& path : fallbackFiles_) {
        auto it = fallbackCache_.find(path);
        if (it == fallbackCache_.end()) {
            std::ifstream in(path, std::ios::binary);
            if (!in) continue;
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (data.empty()) continue;
            int off = stbtt_GetFontOffsetForIndex(data.data(), 0);
            auto fi = std::make_unique<stbtt_fontinfo>();
            if (off < 0 || !stbtt_InitFont(fi.get(), data.data(), off)) continue;
            it = fallbackCache_.emplace(path, std::make_pair(std::move(data), std::move(fi))).first;
        }
        stbtt_fontinfo* fi = it->second.second.get();
        if (stbtt_FindGlyphIndex(fi, (int)cp) != 0)
            return { fi, &(it->second.first) };
    }
    return { nullptr, nullptr };
}

// Realtime: ensure a codepoint is in the atlas. Already-baked -> immediate. Else
// try the primary font, then pull the glyph from the first system fallback font
// that has it. Returns true if the glyph is now available.
bool Font::ensure(uint32_t codepoint) {
    if (codepoint == 0) return false;
    if (map_.find(codepoint) != map_.end()) return true;   // already baked

    int idx = nextIdx_++;
    if (idx >= cols_ * rows_) growAtlasOneRow();

    if (primValid_ && bakeGlyph(codepoint, *primInfo_, primData_, idx))
        return true;

    auto fb = findFallbackFont(codepoint);
    if (fb.first && bakeGlyph(codepoint, *fb.first, *fb.second, idx))
        return true;

    // could not find any font with this glyph; drop the reserved index
    nextIdx_--;
    return false;
}
