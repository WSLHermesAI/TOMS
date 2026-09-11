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

    // --- multi-font pre-bake: buildFromFiles() covers scripts the primary font lacks ---
    // wqy-zenhei (Han) has neither Hangul nor Japanese Kana; pairing it with Noto
    // Sans KR/JP should bake ALL of them into one atlas in a single call, with no
    // runtime ensure()/re-upload needed (see multi-language support work).
    {
        std::string notoKR = "assets/fonts/NotoSansKR-Regular.ttf";
        std::string notoJP = "assets/fonts/NotoSansJP-Regular.ttf";
        if (std::filesystem::exists(notoKR) && std::filesystem::exists(notoJP)) {
            Font f4("multi-font-test");
            std::vector<uint32_t> cps4 = {
                'A',                // ASCII: from wqy-zenhei (primary, first in list)
                0x9B31,             // 魔 : Han, from wqy-zenhei
                0xAC00,             // 가 : Hangul, NOT in wqy-zenhei -> must come from NotoSansKR
                0x3042,             // あ : Hiragana, NOT in wqy-zenhei -> must come from NotoSansJP
            };
            std::vector<std::string> fontList = { ttf, notoKR, notoJP };
            CHECK(f4.buildFromFiles(fontList, cps4, 32, 24), "buildFromFiles succeeds with 3 fonts");
            auto hasInk = [&](uint32_t cp) {
                const std::array<float,4>* uv = f4.uv(cp);
                if (!uv) return false;
                int cell = f4.atlasW() / 32;
                int cx = (int)((*uv)[0] * f4.atlasW()), cy = (int)((*uv)[1] * f4.atlasH());
                for (int y = cy; y < cy + cell; y++)
                    for (int x = cx; x < cx + cell; x++) {
                        size_t p = ((size_t)y * f4.atlasW() + x) * 4 + 3;
                        if (p < f4.atlas().size() && f4.atlas()[p] > 0) return true;
                    }
                return false;
            };
            CHECK(f4.has(0xAC00), "Hangul '가' baked via NotoSansKR fallback");
            CHECK(hasInk(0xAC00), "Hangul glyph has rasterized ink");
            CHECK(f4.has(0x3042), "Hiragana 'あ' baked via NotoSansJP fallback");
            CHECK(hasInk(0x3042), "Hiragana glyph has rasterized ink");
            CHECK(f4.has('A') && hasInk('A'), "ASCII still baked from primary font");
            CHECK(f4.has(0x9B31) && hasInk(0x9B31), "Han glyph still baked from primary font");
        } else {
            printf("  (skipping buildFromFiles multi-font test: Noto fonts not found)\n");
        }
    }

    // --- real content coverage: every codepoint actually shipped in data/*.json must bake ---
    // Regression guard for multi-language support: data/*.json now carries zh_TW/en/zh_CN/ja/
    // ko/es text (dialogue, stages, items, store, text.json). This bakes the SAME font list
    // Game::loadAssets() uses and fails loudly if any real shipped codepoint (e.g. a Hangul
    // syllable or Hiragana/Katakana kana from a translated string) has no glyph anywhere --
    // exactly the failure mode a missing/wrong font file would cause silently in-game.
    {
        std::string notoKR = "assets/fonts/NotoSansKR-Regular.ttf";
        std::string notoJP = "assets/fonts/NotoSansJP-Regular.ttf";
        std::string dataDir = "data";
        if (std::filesystem::exists(notoKR) && std::filesystem::exists(notoJP) && std::filesystem::exists(dataDir)) {
            std::vector<std::string> jsonFiles;
            for (auto& p : std::filesystem::recursive_directory_iterator(dataDir))
                if (p.path().extension() == ".json") jsonFiles.push_back(p.path().string());
            std::vector<uint32_t> cps = Font::collectFromFiles(jsonFiles);
            CHECK(cps.size() > 500, "collected a substantial codepoint set from real shipped content");

            Font f5("content-coverage-test");
            std::vector<std::string> fontList = { ttf, notoJP, notoKR };
            CHECK(f5.buildFromFiles(fontList, cps, 32, 24), "buildFromFiles succeeds with real shipped content");

            int missing = 0;
            uint32_t firstMissing = 0;
            for (uint32_t cp : cps) {
                if (!f5.has(cp)) { missing++; if (!firstMissing) firstMissing = cp; }
            }
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                "every shipped codepoint has a glyph (missing=%d/%d, first=U+%04X)",
                missing, (int)cps.size(), firstMissing);
            CHECK(missing == 0, msg);

            // Spot-check one real codepoint per non-Han script actually used by the shipped
            // translations, so this test fails specifically (not just "missing > 0") if a
            // script's font was dropped or swapped for the wrong one.
            struct ScriptCheck { uint32_t cp; const char* label; };
            ScriptCheck checks[] = {
                { 0xAC00, "Hangul (Korean)" },        // 가 -- appears in ko translations
                { 0x3042, "Hiragana (Japanese)" },     // あ -- appears in ja translations
                { 0x30A2, "Katakana (Japanese)" },     // ア -- appears in ja translations (loanwords)
                { 0x00F1, "Spanish n-tilde" },          // n with tilde -- appears in es translations
                { 0x00E1, "Spanish a-acute" },          // a with acute -- appears in es translations
            };
            for (auto& c : checks) {
                bool present = f5.has(c.cp);
                char cmsg[96];
                std::snprintf(cmsg, sizeof(cmsg), "%s codepoint U+%04X present in atlas", c.label, c.cp);
                CHECK(present, cmsg);
            }
        } else {
            printf("  (skipping content-coverage test: Noto fonts or data/ dir not found)\n");
        }
    }

    // Font is Object-derived: after the scoped Font is destroyed, nothing leaks.
    CHECK(ObjectRegistry::instance().LiveCount() == 0, "no Font leaked");

    if (s_fail == 0) { printf("font_test: ALL PASS\n"); return 0; }
    printf("font_test: %d FAILED\n", s_fail);
    return 1;
}
