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
    uint32_t id = 0;              // Unicode code point
    int x = 0, y = 0, w = 0, h = 0;   // the picture on its page, in pixels (w or h 0: nothing to draw)
    int xoffset = 0, yoffset = 0;     // where the picture goes relative to the pen (y: from the line top)
    int xadvance = 0;                 // pen step after this character
    int page = 0;
};

class BitmapFont {
public:
    enum class Align { Left, Center, Right };

    // One character of laid out text: the glyph and its picture's place on screen.
    struct Placed {
        const BitmapGlyph* glyph = nullptr;
        float x = 0, y = 0, w = 0, h = 0;
    };

    // Parses the text form of a .fnt file. False (and err) when it has no common line or no chars.
    bool parse(std::string_view text, std::string* err = nullptr);

    // The glyph of a code point, or null. Falls back to the other letter case (the old fonts carry
    // "K" "M" or "k" "m", never both), so "1.5K" draws with either.
    const BitmapGlyph* glyph(uint32_t cp) const;
    int kerning(uint32_t first, uint32_t second) const;

    // Lays UTF-8 text out from (x, y) = the top left of the first line; '\n' starts a new line,
    // each line aligned on x by `align`. Characters the font lacks are skipped (missing counts them).
    std::vector<Placed> layout(std::string_view utf8, float x, float y, float scale = 1.0f,
                               Align align = Align::Left, int* missing = nullptr) const;
    // Width of the widest line and the height of all lines, in pixels at this scale.
    void measure(std::string_view utf8, float scale, float& w, float& h) const;
    // layout() as sprite quads on `texture` (the page loaded with IRenderer::loadTexture).
    void appendQuads(std::string_view utf8, float x, float y, float scale, Align align, const float tint[4],
                     uint16_t texture, std::vector<Quad>& out, int* missing = nullptr) const;

    std::string face;
    int size = 0;
    int lineHeight = 0;
    int base = 0;
    int scaleW = 0, scaleH = 0;          // the page size the glyph rects are in
    std::vector<std::string> pages;      // page file names, relative to the .fnt
    std::vector<BitmapGlyph> glyphs;     // in file order

private:
    std::unordered_map<uint32_t, size_t> index_;
    std::unordered_map<uint64_t, int> kern_;
};

// Decodes the next code point of UTF-8 text at i (advancing i); bad bytes decode as U+FFFD.
uint32_t utf8Next(std::string_view s, size_t& i);

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
    bool load(IRenderer& renderer, const std::string& fntPath, std::string* err = nullptr);
    // Frees the page texture. Call once, on one copy, when no copy draws any more.
    void release();

    void setText(std::string_view utf8);
    void setPosition(float x, float y);                    // the anchor: left / centre / right of the top line
    void setAlign(BitmapFont::Align align);
    void setScale(float scale);
    void setColor(float r, float g, float b, float a = 1.0f);   // multiplies the pictures (white = as drawn)
    void setVisible(bool visible);

    void render();

    const std::string& text() const { return text_; }
    bool loaded() const { return renderer_ != nullptr; }
    float width() const;                                   // the text's size at the current scale
    float height() const;
    int missingChars() const;                              // characters of the text the font lacks
    const BitmapFont& font() const { return font_; }

private:
    void relayout() const;

    IRenderer* renderer_ = nullptr;
    uint16_t texture_ = kSpriteAtlasTexture;
    BitmapFont font_;
    std::string text_;
    float x_ = 0, y_ = 0, scale_ = 1;
    float color_[4] = {1, 1, 1, 1};
    BitmapFont::Align align_ = BitmapFont::Align::Left;
    bool visible_ = true;
    mutable bool dirty_ = true;
    mutable std::vector<Quad> quads_;
    mutable float w_ = 0, h_ = 0;
    mutable int missing_ = 0;
};
