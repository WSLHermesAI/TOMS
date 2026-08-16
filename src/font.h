// font.h — runtime TrueType font rasterized to a texture atlas (C++17, no external
// font lib beyond stb_truetype). Replaces the offline PIL step (gen_font_atlas.py):
// the engine now builds the font atlas from a .ttf/.ttc at load time via stb_truetype.
//
// Inherits Object so a Font is shared_ptr-managed, typed, named and leak-tracked
// (same model as Texture). The atlas pixels are uploaded by the Renderer exactly
// like the old font_atlas.png; drawText() keeps using the codepoint->UV map.

#pragma once
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <unordered_map>
#include "object.h"

struct stbtt_fontinfo;   // forward-declared; only font.cpp includes stb_truetype.h

class Font : public Object {
    TOMS_OBJECT(Font)
public:
    Font(const std::string& name = "font") { SetName(name); }
    ~Font() override = default;

    // Build a glyph atlas from a TTF/TTC file. `chars` = every codepoint to bake
    // (ASCII + CJK + HUD punctuation). `px` = glyph cell size in the atlas (e.g. 32),
    // `fontPx` = point size passed to stb_truetype (e.g. 24). Returns false on failure.
    bool buildFromFile(const std::string& ttfPath,
                       const std::vector<uint32_t>& chars,
                       int cell = 32, int fontPx = 24);

    // Atlas pixels (RGBA8, premultiplied white glyphs on transparent) + dimensions.
    const std::vector<uint8_t>& atlas() const { return atlas_; }
    uint32_t atlasW() const { return atlasW_; }
    uint32_t atlasH() const { return atlasH_; }

    // UV rect {u0,v0,u1,v1} for a codepoint, or null if not baked.
    const std::array<float,4>* uv(uint32_t codepoint) const {
        auto it = map_.find(codepoint); return it == map_.end() ? nullptr : &it->second;
    }
    bool has(uint32_t codepoint) const { return map_.find(codepoint) != map_.end(); }

    // Per-glyph tight metrics (atlas pixels). advance()/glyphPxWidth() return the
    // baked glyph width so callers can draw tight quads + kern correctly instead
    // of giving every glyph a full-cell square (which caused "V o r k a t h" gaps).
    int cellSize() const { return cell_; }
    int glyphPxWidth(uint32_t cp) const {
        auto it = advPx_.find(cp); return it == advPx_.end() ? cell_ : it->second;
    }
    // Tight vertical metrics (cell-local atlas px) for baseline-correct placement.
    int glyphPxHeight(uint32_t cp) const {
        auto it = glyphBox_.find(cp); return it == glyphBox_.end() ? cell_ : it->second[3];
    }
    int glyphPxOffY(uint32_t cp) const {
        auto it = glyphBox_.find(cp); return it == glyphBox_.end() ? 0 : it->second[1];
    }

    // Convenience: collect every printable codepoint in a set of JSON text files +
    // an explicit extra string (HUD labels). Used so the baked set matches the
    // shipped content (same as gen_font_atlas.py).
    static std::vector<uint32_t> collectFromFiles(const std::vector<std::string>& jsonFiles,
                                                  const std::string& extra = "");

    // --- realtime system-font fallback ---
    // Ensure a codepoint is present in the atlas. If it was baked already, returns
    // true immediately. Otherwise it tries the primary font, then lazily scans a
    // list of system fallback fonts (see setFallbackDir / default /usr/share/fonts)
    // for the first one that actually contains the glyph, and rasterizes that
    // single glyph into the atlas at runtime (so a missing character can be pulled
    // from the OS font set on the fly). Returns true if the glyph is now present.
    bool ensure(uint32_t codepoint);

    // Directory to scan for fallback fonts (default: /usr/share/fonts). The scan is
    // lazy: files are only opened/parsed when a missing glyph needs them.
    void setFallbackDir(const std::string& dir) { fallbackDir_ = dir; }

private:
    // Bake one glyph from `info` into cell `idx` (growing the atlas if needed).
    // Returns false if the font has no such glyph.
    bool bakeGlyph(uint32_t cp, stbtt_fontinfo& info, const std::vector<uint8_t>& fontData, int idx);
    // Recompute a glyph's UV from its stored cell-local box after the atlas grew.
    void recomputeUV(uint32_t cp);
    // Grow the atlas by one row (keeps existing UVs valid by recomputing v from idx).
    void growAtlasOneRow();
    // Lazily load + cache a fallback font; returns the info for the first file that
    // contains `cp`, or null.
    std::pair<stbtt_fontinfo*, const std::vector<uint8_t>*> findFallbackFont(uint32_t cp);

    std::vector<uint8_t> atlas_;
    uint32_t atlasW_ = 0, atlasH_ = 0;
    int cell_ = 32, cols_ = 32, rows_ = 0, nextIdx_ = 0;
    std::unordered_map<uint32_t, std::array<float,4>> map_;
    std::unordered_map<uint32_t, int> cellIdx_;   // cp -> grid cell index (for grow)
    // tight per-glyph metrics (cell-local atlas px): offX,offY,gw,gh ; plus baked width
    std::unordered_map<uint32_t, std::array<int,4>> glyphBox_;
    std::unordered_map<uint32_t, int> advPx_;
    // primary font bytes + info (so ensure() can re-rasterize from it)
    std::vector<uint8_t> primData_;
    std::unique_ptr<stbtt_fontinfo> primInfo_;
    bool primValid_ = false;
    // lazily-loaded system fallback fonts
    std::string fallbackDir_ = "/usr/share/fonts";
    std::vector<std::string> fallbackFiles_;     // discovered on first need
    bool fallbackScanned_ = false;
    std::unordered_map<std::string, std::pair<std::vector<uint8_t>, std::unique_ptr<stbtt_fontinfo>>> fallbackCache_;
};
