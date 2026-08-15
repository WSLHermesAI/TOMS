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

    // Convenience: collect every printable codepoint in a set of JSON text files +
    // an explicit extra string (HUD labels). Used so the baked set matches the
    // shipped content (same as gen_font_atlas.py).
    static std::vector<uint32_t> collectFromFiles(const std::vector<std::string>& jsonFiles,
                                                  const std::string& extra = "");

private:
    std::vector<uint8_t> atlas_;
    uint32_t atlasW_ = 0, atlasH_ = 0;
    std::unordered_map<uint32_t, std::array<float,4>> map_;
};
