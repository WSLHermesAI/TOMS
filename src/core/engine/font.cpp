// font.cpp — runtime TTF rasterization via stb_truetype.
#include "font.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cmath>
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
// Mirrors FM79979 cDynamicFontTexture::AddNewCharacterToTexture: rasterize the
// glyph with stb's own bitmap renderer and store the ACTUAL rendered w x h plus
// the freetype bearing (left, top). The full bitmap (all h rows) is pasted
// top-left into the atlas cell (no centering, no height cap) so nothing is ever
// clipped. We use an explicit scale = cell/(ascent-descent) (natural size, em-box
// == cell) rather than stbtt_ScaleForPixelHeight, because wqy-zenhei's metrics
// make ScaleForPixelHeight shrink the glyphs far too small.
bool Font::bakeGlyph(uint32_t cp, stbtt_fontinfo& info, const std::vector<uint8_t>&, int idx) {
    int g = stbtt_FindGlyphIndex(&info, (int)cp);
    if (g == 0) return false;              // glyph absent from this font

    // Natural-size scale: em-box == cell.
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    float scale = (ascent > 0) ? ((float)cell_ / (float)(ascent - descent)) : 1.0f;

    int w = 0, h = 0, left = 0, top = 0;
    uint8_t* bmp = stbtt_GetGlyphBitmap(&info, scale, scale, g, &w, &h, &left, &top);
    int cellX = (idx % cols_) * cell_, cellY = (idx / cols_) * cell_;

    if (!bmp || w <= 0 || h <= 0) {        // whitespace glyph: reserve an empty cell
        if (bmp) stbtt_FreeBitmap(bmp, 0);
        glyphBox_[cp] = {0, 0, 0, 0};
        advPx_[cp] = std::max(1, cell_ / 3);   // a space still advances a bit
        glyphM_[cp] = {0, 0, (float)(cell_/3), 0};
        map_[cp] = { (float)cellX / atlasW_, (float)cellY / atlasH_,
                     (float)(cellX + cell_) / atlasW_, (float)(cellY + cell_) / atlasH_ };
        cellIdx_[cp] = idx;
        return true;
    }
    // Paste the FULL bitmap (all h rows) top-left into the cell. No cap, no
    // centering -> the bottom of round glyphs ('e','o','a') is never clipped.
    int offX = cellX, offY = cellY;
    uint8_t* base = atlas_.data();
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            uint8_t a = bmp[(size_t)yy * w + xx];
            int dx = offX + xx, dy = offY + yy;
            if (dx < 0 || dy < 0 || dx >= (int)atlasW_ || dy >= (int)atlasH_) continue;
            size_t p = ((size_t)dy * atlasW_ + dx) * 4;
            base[p] = 255; base[p+1] = 255; base[p+2] = 255; base[p+3] = a;  // white glyph
        }
    stbtt_FreeBitmap(bmp, 0);
    // TIGHT uv (both axes span the actual inked glyph) so the quad never samples
    // padding or a neighbour under linear filtering.
    glyphBox_[cp] = { 0, 0, w, h };        // top-left placement
    advPx_[cp] = w;
    // freetype bearing: Offset.x = left, Offset.y = -top (exactly like the reference)
    glyphM_[cp] = { (float)left, (float)(-top), (float)w, (float)h };
    map_[cp] = { (float)offX / atlasW_, (float)offY / atlasH_,
                 (float)(offX + w) / atlasW_, (float)(offY + h) / atlasH_ };
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
    return buildFromFiles({ttfPath}, chars, cell, fontPx);
}

bool Font::buildFromFiles(const std::vector<std::string>& ttfPaths,
                          const std::vector<uint32_t>& chars,
                          int cell, int fontPx) {
    cell_ = cell; cols_ = 32;
    (void)fontPx;   // per-glyph scale recomputed in bakeGlyph

    // Load every font up front (order matters: first one that has a given glyph
    // wins). A path that fails to open/parse is skipped, not fatal.
    struct Loaded { std::vector<uint8_t> data; std::unique_ptr<stbtt_fontinfo> info; };
    std::vector<Loaded> fonts;
    for (auto& path : ttfPaths) {
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (data.empty()) continue;
        int off = stbtt_GetFontOffsetForIndex(data.data(), 0);
        if (off < 0) continue;
        auto info = std::make_unique<stbtt_fontinfo>();
        if (!stbtt_InitFont(info.get(), data.data(), off)) continue;
        fonts.push_back({std::move(data), std::move(info)});
    }
    if (fonts.empty()) return false;

    // The first successfully-loaded font stays "primary" (ensure()'s realtime
    // fallback re-bakes from this one first); the rest are pre-registered as
    // fallback fonts so any codepoint requested later that wasn't in `chars`
    // still resolves through the same ordered list ensure() already knows how
    // to search, instead of only ever consulting fallbackDir_.
    primData_ = std::move(fonts[0].data);
    primInfo_ = std::move(fonts[0].info);
    primValid_ = true;
    fallbackScanned_ = true;   // skip the directory scan; we already have the list below
    for (size_t i = 1; i < fonts.size(); i++) {
        std::string key = "#prebaked" + std::to_string(i);
        fallbackFiles_.push_back(key);
        fallbackCache_.emplace(key, std::make_pair(std::move(fonts[i].data), std::move(fonts[i].info)));
    }
    // findFallbackFont() re-parses primInfo_'s bytes via primData_, but the fonts
    // vector's font[0] entries were just moved out of -- rebuild local pointers
    // for the pre-bake loop below from the now-owning members instead of `fonts`.
    std::vector<stbtt_fontinfo*> order;
    order.push_back(primInfo_.get());
    for (auto& key : fallbackFiles_) order.push_back(fallbackCache_[key].second.get());

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
        for (stbtt_fontinfo* fi : order)
            if (bakeGlyph(cp, *fi, primData_, idx)) break;
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

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

// Rasterizes the whole codepoint set in ONE offscreen <canvas>, one JS<->WASM round trip,
// mirroring buildFromFiles()'s cell-grid layout (cols_=32, cell x cell cells) so every other
// Font method (uv/glyphMetrics/glyphPxWidth) works identically regardless of backend.
//
// Per glyph the JS side uses TextMetrics.actualBoundingBox{Left,Right,Ascent,Descent} to
// compute the SAME quantities stbtt_GetGlyphBitmap gives bakeGlyph() (see font.cpp above):
//   leftBearing = actualBoundingBoxLeft   (stb's `left`)
//   topBearing  = actualBoundingBoxAscent (stb's `-top`: bitmap-top distance above baseline)
//   sizeX/sizeY = tight ink width/height  (stb's rasterized bitmap w/h)
// and draws each glyph so its ink's top-left corner lands exactly at the cell's top-left
// pixel (pen = cell origin + (actualBoundingBoxLeft, actualBoundingBoxAscent)) -- the same
// "paste bitmap at cell origin, no centering" placement bakeGlyph uses. That keeps the UV
// rect / quad-size math in Game::drawText()/measureText() byte-for-byte the same as the
// stb_truetype path; only how the pixels got into the atlas differs.
bool Font::buildFromCanvas(const std::vector<uint32_t>& chars, int cell, int fontPx) {
    cell_ = cell; cols_ = 32;
    int n = (int)chars.size();
    if (n == 0) return false;

    int slack = 2048;
    rows_ = (n + slack + cols_ - 1) / cols_;
    atlasW_ = (uint32_t)(cols_ * cell_);
    atlasH_ = (uint32_t)(rows_ * cell_);
    atlas_.assign((size_t)atlasW_ * atlasH_ * 4, 0);
    nextIdx_ = 0;
    map_.clear(); cellIdx_.clear(); glyphM_.clear(); glyphBox_.clear(); advPx_.clear();
    primValid_ = false;   // no stb primary font in this backend -- ensure() degrades to
                           // fallbackDir_ scan only (a graceful no-op if nothing's there)

    // metrics[i*4 + {0,1,2,3}] = leftBearing, topBearing, sizeX, sizeY (design/atlas px)
    std::vector<float> metrics((size_t)n * 4, 0.0f);

    // NOTE: every JS statement below is written as ONE variable per `var` and with no
    // top-level comma outside of a (...) group -- the C PREPROCESSOR (not the JS engine)
    // scans EM_ASM's arguments and only tracks balanced parentheses, not braces; a bare
    // comma like `var a = 1, b = 2;` inside the { } code block gets misread as separating
    // macro arguments and breaks the build. See the original single-`var`-list version's
    // compile errors if you're tempted to reintroduce comma-separated declarations here.
    EM_ASM({
        var cps = $0;
        var count = $1;
        var cell = $2;
        var fontPx = $3;
        var atlasPtr = $4;
        var atlasW = $5;
        var atlasH = $6;
        var metricsPtr = $7;
        var canvas = document.createElement('canvas');
        canvas.width = atlasW; canvas.height = atlasH;
        var ctx = canvas.getContext('2d', { willReadFrequently: true });
        ctx.clearRect(0, 0, atlasW, atlasH);
        ctx.font = fontPx + 'px sans-serif';   // browser's own font-fallback chain
        ctx.fillStyle = '#ffffff';
        ctx.textBaseline = 'alphabetic';
        ctx.textAlign = 'left';
        var cols = atlasW / cell;
        for (var i = 0; i < count; i++) {
            var cp = HEAPU32[(cps >> 2) + i];
            var ch = String.fromCodePoint(cp);
            var cellX = (i % cols) * cell;
            var cellY = Math.floor(i / cols) * cell;
            var m = ctx.measureText(ch);
            var left = m.actualBoundingBoxLeft || 0;
            var right = m.actualBoundingBoxRight || 0;
            var asc = m.actualBoundingBoxAscent || 0;
            var desc = m.actualBoundingBoxDescent || 0;
            var w = left + right;
            var h = asc + desc;
            var mi = (metricsPtr >> 2) + i * 4;
            if (w <= 0.01 || h <= 0.01) {
                // whitespace / zero-ink glyph: advance a third of a cell, draw nothing
                // (matches bakeGlyph's whitespace special case in font.cpp).
                HEAPF32[mi+0] = 0; HEAPF32[mi+1] = 0;
                HEAPF32[mi+2] = cell / 3; HEAPF32[mi+3] = 0;
                continue;
            }
            var penX = cellX + left;
            var penY = cellY + asc;
            ctx.fillText(ch, penX, penY);
            HEAPF32[mi+0] = left; HEAPF32[mi+1] = asc;
            HEAPF32[mi+2] = w;    HEAPF32[mi+3] = h;
        }
        // getImageData is spec'd non-premultiplied -- RGB stays 255,255,255 under a white
        // fillStyle regardless of antialiased alpha, exactly the format bakeGlyph() writes.
        var img = ctx.getImageData(0, 0, atlasW, atlasH).data;
        HEAPU8.set(img, atlasPtr);
    }, chars.data(), n, cell_, fontPx, atlas_.data(), atlasW_, atlasH_, metrics.data());

    for (int i = 0; i < n; i++) {
        uint32_t cp = chars[i];
        if (map_.find(cp) != map_.end()) continue;   // duplicate codepoint in input
        int idx = i;
        int cellX = (idx % cols_) * cell_, cellY = (idx / cols_) * cell_;
        float leftBearing = metrics[i*4+0], topBearing = metrics[i*4+1];
        float w = metrics[i*4+2], h = metrics[i*4+3];
        glyphM_[cp] = { leftBearing, topBearing, w, h };
        if (h <= 0.0f) {
            // whitespace: reserve the cell but no ink -- same bookkeeping as bakeGlyph().
            glyphBox_[cp] = {0, 0, 0, 0};
            advPx_[cp] = std::max(1, cell_ / 3);
            map_[cp] = { (float)cellX / atlasW_, (float)cellY / atlasH_,
                         (float)(cellX + cell_) / atlasW_, (float)(cellY + cell_) / atlasH_ };
        } else {
            int iw = (int)std::ceil(w), ih = (int)std::ceil(h);
            glyphBox_[cp] = { 0, 0, iw, ih };
            advPx_[cp] = iw;
            map_[cp] = { (float)cellX / atlasW_, (float)cellY / atlasH_,
                         (float)(cellX + iw) / atlasW_, (float)(cellY + ih) / atlasH_ };
        }
        cellIdx_[cp] = idx;
        nextIdx_ = idx + 1;
    }
    return true;
}
#endif
