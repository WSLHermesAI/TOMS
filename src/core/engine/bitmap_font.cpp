// bitmap_font.cpp -- BMFont text .fnt parsing and text layout (bitmap_font.h).
#include "bitmap_font.h"
#include "vfs.h"

#include <stb_image.h>   // the implementation: stb_image_impl.cpp

#include <algorithm>
#include <cstdlib>

namespace {

// key=value pairs of one line; a value may be quoted (letter=" ", file="a b.png").
struct Fields {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> kv;

    const std::string* find(const char* key) const {
        for (const auto& p : kv)
            if (p.first == key) return &p.second;
        return nullptr;
    }
    int num(const char* key, int def = 0) const {
        const std::string* v = find(key);
        return v ? std::atoi(v->c_str()) : def;
    }
    std::string str(const char* key) const {
        const std::string* v = find(key);
        return v ? *v : std::string();
    }
};

Fields splitLine(std::string_view line) {
    Fields f;
    size_t i = 0;
    auto skipSpace = [&] { while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++; };
    skipSpace();
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') f.tag += line[i++];
    for (;;) {
        skipSpace();
        if (i >= line.size()) break;
        std::string key;
        while (i < line.size() && line[i] != '=' && line[i] != ' ' && line[i] != '\t') key += line[i++];
        std::string value;
        if (i < line.size() && line[i] == '=') {
            i++;
            if (i < line.size() && line[i] == '"') {
                const size_t end = line.find('"', i + 1);
                const size_t stop = end == std::string_view::npos ? line.size() : end;
                value = std::string(line.substr(i + 1, stop - i - 1));
                i = stop == line.size() ? stop : stop + 1;
            } else {
                while (i < line.size() && line[i] != ' ' && line[i] != '\t') value += line[i++];
            }
        }
        if (!key.empty()) f.kv.emplace_back(std::move(key), std::move(value));
    }
    return f;
}

uint32_t otherCase(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    return 0;
}

uint64_t kernKey(uint32_t a, uint32_t b) { return ((uint64_t)a << 32) | b; }

}  // namespace

uint32_t utf8Next(std::string_view s, size_t& i) {
    const unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
    else return 0xFFFD;
    for (int k = 0; k < extra; k++) {
        if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    }
    return cp;
}

bool BitmapFont::parse(std::string_view text, std::string* err) {
    *this = BitmapFont();
    bool haveCommon = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const Fields f = splitLine(line);
        if (f.tag == "info") {
            face = f.str("face");
            size = std::abs(f.num("size"));
        } else if (f.tag == "common") {
            haveCommon = true;
            lineHeight = f.num("lineHeight");
            base = f.num("base");
            scaleW = f.num("scaleW");
            scaleH = f.num("scaleH");
        } else if (f.tag == "page") {
            const int id = f.num("id");
            if (id >= 0 && id < 256) {
                if ((int)pages.size() <= id) pages.resize(id + 1);
                pages[id] = f.str("file");
            }
        } else if (f.tag == "char") {
            BitmapGlyph g;
            g.id = (uint32_t)f.num("id");
            g.x = f.num("x");
            g.y = f.num("y");
            g.w = f.num("width");
            g.h = f.num("height");
            g.xoffset = f.num("xoffset");
            g.yoffset = f.num("yoffset");
            g.xadvance = f.num("xadvance");
            g.page = f.num("page");
            auto it = index_.find(g.id);
            if (it != index_.end()) glyphs[it->second] = g;   // a repeated id: the last one wins
            else { index_[g.id] = glyphs.size(); glyphs.push_back(g); }
        } else if (f.tag == "kerning") {
            kern_[kernKey((uint32_t)f.num("first"), (uint32_t)f.num("second"))] = f.num("amount");
        }
    }
    if (!haveCommon || glyphs.empty()) {
        if (err) *err = !haveCommon ? "no 'common' line (not a text BMFont .fnt?)" : "no 'char' lines";
        return false;
    }
    if (lineHeight <= 0)
        for (const BitmapGlyph& g : glyphs) lineHeight = std::max(lineHeight, g.yoffset + g.h);
    return true;
}

const BitmapGlyph* BitmapFont::glyph(uint32_t cp) const {
    auto it = index_.find(cp);
    if (it == index_.end()) {
        const uint32_t alt = otherCase(cp);
        if (alt) it = index_.find(alt);
    }
    return it == index_.end() ? nullptr : &glyphs[it->second];
}

int BitmapFont::kerning(uint32_t first, uint32_t second) const {
    if (kern_.empty()) return 0;
    auto it = kern_.find(kernKey(first, second));
    return it == kern_.end() ? 0 : it->second;
}

std::vector<BitmapFont::Placed> BitmapFont::layout(std::string_view utf8, float x, float y, float scale,
                                                   Align align, int* missing) const {
    std::vector<Placed> out;
    if (missing) *missing = 0;
    size_t i = 0;
    float lineY = y;
    while (i <= utf8.size()) {
        // one line: lay it out from 0, then shift it by the alignment
        const size_t first = out.size();
        float pen = 0;
        uint32_t prev = 0;
        while (i < utf8.size()) {
            const uint32_t cp = utf8Next(utf8, i);
            if (cp == '\n') break;
            if (cp == '\r') continue;
            const BitmapGlyph* g = glyph(cp);
            if (!g) {
                if (missing) ++*missing;
                continue;
            }
            if (prev) pen += kerning(prev, g->id) * scale;
            if (g->w > 0 && g->h > 0)
                out.push_back({g, pen + g->xoffset * scale, lineY + g->yoffset * scale, g->w * scale, g->h * scale});
            pen += g->xadvance * scale;
            prev = g->id;
        }
        const float shift = align == Align::Left ? x : align == Align::Center ? x - pen * 0.5f : x - pen;
        for (size_t k = first; k < out.size(); k++) out[k].x += shift;
        lineY += lineHeight * scale;
        if (i >= utf8.size()) break;
    }
    return out;
}

void BitmapFont::measure(std::string_view utf8, float scale, float& w, float& h) const {
    w = 0;
    int lines = 1;
    float pen = 0;
    uint32_t prev = 0;
    size_t i = 0;
    while (i < utf8.size()) {
        const uint32_t cp = utf8Next(utf8, i);
        if (cp == '\n') { w = std::max(w, pen); pen = 0; prev = 0; lines++; continue; }
        const BitmapGlyph* g = glyph(cp);
        if (!g) continue;
        if (prev) pen += kerning(prev, g->id) * scale;
        pen += g->xadvance * scale;
        prev = g->id;
    }
    w = std::max(w, pen);
    h = (float)lines * lineHeight * scale;
}

void BitmapFont::appendQuads(std::string_view utf8, float x, float y, float scale, Align align, const float tint[4],
                             uint16_t texture, std::vector<Quad>& out, int* missing) const {
    if (missing) *missing = 0;
    if (scaleW <= 0 || scaleH <= 0) return;
    for (const Placed& p : layout(utf8, x, y, scale, align, missing)) {
        Quad q;
        q.rect[0] = p.x; q.rect[1] = p.y; q.rect[2] = p.w; q.rect[3] = p.h;
        q.uv[0] = (float)p.glyph->x / scaleW;
        q.uv[1] = (float)p.glyph->y / scaleH;
        q.uv[2] = (float)(p.glyph->x + p.glyph->w) / scaleW;
        q.uv[3] = (float)(p.glyph->y + p.glyph->h) / scaleH;
        for (int c = 0; c < 4; c++) q.tint[c] = tint[c];
        q.texture = texture;
        out.push_back(q);
    }
}

// ---- FontText ----

bool FontText::load(IRenderer& renderer, const std::string& fntPath, std::string* err) {
    auto fail = [&](const std::string& why) {
        if (err) *err = fntPath + ": " + why;
        return false;
    };
    std::string text;
    if (!toms::vfsReadAll(fntPath, text)) return fail("cannot read");
    BitmapFont font;
    std::string why;
    if (!font.parse(text, &why)) return fail(why);
    if (font.pages.empty() || font.pages[0].empty()) return fail("no page file");
    const size_t slash = fntPath.find_last_of("/\\");
    const std::string pagePath = (slash == std::string::npos ? std::string() : fntPath.substr(0, slash + 1)) + font.pages[0];
    std::string png;
    if (!toms::vfsReadAll(pagePath, png)) return fail("cannot read its page " + pagePath);
    int w = 0, h = 0, ch = 0;
    stbi_uc* px = stbi_load_from_memory((const stbi_uc*)png.data(), (int)png.size(), &w, &h, &ch, 4);
    if (!px) return fail("cannot decode its page " + pagePath);
    const uint16_t tex = renderer.loadTexture(std::vector<uint8_t>(px, px + (size_t)w * h * 4), (uint32_t)w, (uint32_t)h);
    stbi_image_free(px);
    if (font.scaleW <= 0 || font.scaleH <= 0) { font.scaleW = w; font.scaleH = h; }
    font_ = std::move(font);
    renderer_ = &renderer;
    texture_ = tex;
    dirty_ = true;
    return true;
}

void FontText::release() {
    if (renderer_ && texture_ != kSpriteAtlasTexture) renderer_->releaseTexture(texture_);
    renderer_ = nullptr;
    texture_ = kSpriteAtlasTexture;
    quads_.clear();
}

void FontText::setText(std::string_view utf8) {
    if (text_ == utf8) return;
    text_.assign(utf8.data(), utf8.size());
    dirty_ = true;
}

void FontText::setPosition(float x, float y) {
    if (x == x_ && y == y_) return;
    x_ = x;
    y_ = y;
    dirty_ = true;
}

void FontText::setAlign(BitmapFont::Align align) {
    if (align == align_) return;
    align_ = align;
    dirty_ = true;
}

void FontText::setScale(float scale) {
    if (scale == scale_) return;
    scale_ = scale;
    dirty_ = true;
}

void FontText::setColor(float r, float g, float b, float a) {
    color_[0] = r; color_[1] = g; color_[2] = b; color_[3] = a;
    dirty_ = true;
}

void FontText::setVisible(bool visible) { visible_ = visible; }

void FontText::relayout() const {
    if (!dirty_) return;
    quads_.clear();
    font_.appendQuads(text_, x_, y_, scale_, align_, color_, texture_, quads_, &missing_);
    font_.measure(text_, scale_, w_, h_);
    dirty_ = false;
}

void FontText::render() {
    if (!renderer_ || !visible_ || text_.empty()) return;
    relayout();
    for (const Quad& q : quads_) renderer_->drawSprite(q);
}

float FontText::width() const { relayout(); return w_; }
float FontText::height() const { relayout(); return h_; }
int FontText::missingChars() const { relayout(); return missing_; }
