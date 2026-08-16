// font_test.cpp — headless verification of the runtime TTF font atlas.
// Builds a Font from the bundled CJK TTF via stb_truetype and checks that
// ASCII + CJK glyphs get valid UV rects in the atlas. No Vulkan needed.
// Exits 0 on success, 1 on any failed check.
#include "font.h"
#include <stb_truetype.h>   // complete stbtt_fontinfo for ~Font (unique_ptr member)
#include <cstdio>
#include <string>
#include <filesystem>
#include <array>

static int s_fail = 0;
#define CHECK(c,msg) do { if(!(c)){ printf("  FAIL: %s\n", msg); ++s_fail; } } while(0)

int main() {
    // Resolve the TTF: bundled asset, then system fallback.
    std::string ttf = "assets/wqy-zenhei.ttc";
    if (!std::filesystem::exists(ttf))
        ttf = "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc";

    // A small char set mixing ASCII + CJK + punctuation.
    std::vector<uint32_t> cps = {
        'A', 'B', '0', '9', ':', '.',
        0x9B31, // 魔 (U+9B31)
        0x6CD5, // 法 (U+6CD5)
        0x584A, // 塔 (U+584A)
        0x6230, // 戰 (U+6230)
        0x95D8, // 鬥 (U+95D8)
        0xFF08, // （ fullwidth paren
        0xFF09, // ）
    };

    // Build into a scoped Font so it is destroyed (and unregistered) before the
    // leak check below.
    {
        Font f("test-font");

        CHECK(f.buildFromFile(ttf, cps, 32, 24), "buildFromFile succeeds from TTF");
        CHECK(f.atlasW() > 0 && f.atlasH() > 0, "atlas has non-zero dimensions");
        CHECK(f.atlas().size() == (size_t)f.atlasW() * f.atlasH() * 4, "atlas pixel buffer sized W*H*4");

        // Every requested codepoint should have a UV rect.
        for (uint32_t cp : cps) {
            const std::array<float,4>* uv = f.uv(cp);
            CHECK(uv != nullptr, "UV present for codepoint");
            if (uv) {
                CHECK((*uv)[0] >= 0.f && (*uv)[0] <= 1.f, "u0 in [0,1]");
                CHECK((*uv)[2] > (*uv)[0], "u1 > u0 (non-empty width)");
                CHECK((*uv)[3] > (*uv)[1], "v1 > v0 (non-empty height)");
            }
        }

        // ASCII 'A' must actually have rasterized pixels in the atlas (not blank).
        const std::array<float,4>* aUv = f.uv('A');
        CHECK(aUv != nullptr, "'A' UV present");
        if (aUv) {
            int cell = f.atlasW() / 32;   // COLS = 32, cells are CELL x CELL
            int cx = (int)((*aUv)[0] * f.atlasW());
            int cy = (int)((*aUv)[1] * f.atlasH());
            bool foundInk = false;
            for (int y = cy; y < cy + cell && !foundInk; y++)
                for (int x = cx; x < cx + cell; x++) {
                    size_t p = ((size_t)y * f.atlasW() + x) * 4 + 3; // alpha channel
                    if (p < f.atlas().size() && f.atlas()[p] > 0) { foundInk = true; break; }
                }
            CHECK(foundInk, "'A' glyph has rasterized (non-zero alpha) pixels");
        }

        // CJK glyph '魔' must also have ink (proves runtime TTF coverage).
        const std::array<float,4>* mUv = f.uv(0x9B31);
        CHECK(mUv != nullptr, "'魔' UV present");
        if (mUv) {
            int cell = f.atlasW() / 32;
            int cx = (int)((*mUv)[0] * f.atlasW());
            int cy = (int)((*mUv)[1] * f.atlasH());
            bool foundInk = false;
            for (int y = cy; y < cy + cell && !foundInk; y++)
                for (int x = cx; x < cx + cell; x++) {
                    size_t p = ((size_t)y * f.atlasW() + x) * 4 + 3;
                    if (p < f.atlas().size() && f.atlas()[p] > 0) { foundInk = true; break; }
                }
            CHECK(foundInk, "'魔' glyph has rasterized (non-zero alpha) pixels");
        }

        CHECK(f.Type() != nullptr, "Font reports a type string");
    }

    // --- realtime fallback: ensure() bakes a glyph not in the initial set ---
    {
        // Build from the CJK font with ONLY 'A' baked, then ask for 'B' and a few
        // others at runtime. 'B' exists in the primary font -> baked on demand.
        Font f2("ensure-test");
        std::vector<uint32_t> seed = { (uint32_t)'A' };
        CHECK(f2.buildFromFile(ttf, seed, 32, 24), "ensure-test builds from TTF");
        CHECK(f2.has('A'), "'A' baked initially");
        CHECK(!f2.has('B'), "'B' not present yet");
        bool okB = f2.ensure('B');
        CHECK(okB, "ensure('B') finds glyph in primary font");
        const std::array<float,4>* bUv = f2.uv('B');
        CHECK(bUv != nullptr, "'B' UV present after ensure()");
        if (bUv) {
            int cell = f2.atlasW() / 32;
            int cx = (int)((*bUv)[0] * f2.atlasW());
            int cy = (int)((*bUv)[1] * f2.atlasH());
            bool ink = false;
            for (int y = cy; y < cy + cell && !ink; y++)
                for (int x = cx; x < cx + cell; x++) {
                    size_t p = ((size_t)y * f2.atlasW() + x) * 4 + 3;
                    if (p < f2.atlas().size() && f2.atlas()[p] > 0) { ink = true; break; }
                }
            CHECK(ink, "'B' glyph has ink after realtime ensure()");
        }
        // A glyph absent from BOTH the primary font and any fallback must fail
        // gracefully (no crash). Build f3 from DejaVuSans (ASCII only, no CJK),
        // disable fallback, and ask for a CJK glyph no system font can supply here.
        Font f3("no-fallback-test");
        std::string dejavu = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
        if (std::filesystem::exists(dejavu)) {
            std::vector<uint32_t> seed3 = { (uint32_t)'A' };
            CHECK(f3.buildFromFile(dejavu, seed3, 32, 24), "no-fallback builds from DejaVu");
            f3.setFallbackDir("/nonexistent-fonts-dir");     // no fallback available
            bool okCJK = f3.ensure(0x9B31);                    // 魔 : not in DejaVu
            CHECK(!okCJK, "ensure() returns false when no font has the glyph");
        }
    }

    // Font is Object-derived: after the scoped Font is destroyed, nothing leaks.
    CHECK(ObjectRegistry::instance().LiveCount() == 0, "no Font leaked");

    if (s_fail == 0) { printf("font_test: ALL PASS\n"); return 0; }
    printf("font_test: %d FAILED\n", s_fail);
    return 1;
}
