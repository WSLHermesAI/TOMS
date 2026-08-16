// texture_test.cpp — headless verification of the modern Texture class.
// Builds a real Vulkan device (lavapipe) via Renderer, then:
//   1) creates a Texture, uploads RGBA pixels, asserts it's valid
//   2) calls UpdatePixels with new (different-size) data, asserts it still valid
//   3) routes a texture through TextureManager and fetches it back
//   4) confirms no Texture/TextureManager leaks at shutdown
// Exits 0 on success, 1 on any failed check.
#include "renderer.h"
#include "texture.h"
#include <cstdio>

static int s_fail = 0;
#define CHECK(c,msg) do { if(!(c)){ printf("  FAIL: %s\n", msg); ++s_fail; } } while(0)

int main() {
    Renderer r;
    r.init(64, 64);                       // real Vulkan device (lavapipe)
    auto refs = r.textureRefs();
    CHECK(refs.device != VK_NULL_HANDLE, "renderer provided valid Vulkan refs");

    // 1) create a texture and upload a 4x4 red checkerboard
    auto tex = std::make_shared<Texture>(refs, "checker");
    std::vector<uint8_t> px(4*4*4, 0);
    for (uint32_t y = 0; y < 4; y++)
        for (uint32_t x = 0; x < 4; x++) {
            uint8_t v = ((x+y)&1) ? 255 : 0;
            size_t i = (y*4 + x)*4; px[i]=v; px[i+1]=0; px[i+2]=0; px[i+3]=255;
        }
    CHECK(tex->LoadFromRGBA(px, 4, 4), "LoadFromRGBA succeeds");
    CHECK(tex->valid(), "texture is valid after load");
    CHECK(tex->width()==4 && tex->height()==4, "texture reports 4x4");
    CHECK(tex->descriptorSet() != VK_NULL_HANDLE, "texture has a descriptor set");

    // 2) UpdatePixels with a different size (8x2) — should re-upload in place
    std::vector<uint8_t> px2(8*2*4, 0);
    for (size_t i = 0; i < px2.size(); i += 4) { px2[i]=0; px2[i+1]=255; px2[i+2]=0; px2[i+3]=255; }
    CHECK(tex->UpdatePixels(px2, 8, 2), "UpdatePixels succeeds");
    CHECK(tex->valid(), "texture still valid after UpdatePixels");
    CHECK(tex->width()==8 && tex->height()==2, "texture reports new 8x2 size");

    // 3) TextureManager: register + fetch
    auto& mgr = TextureManager::instance();
    mgr.Add("sharedTex", tex);
    {
        auto got = mgr.Get("sharedTex");        // scoped so the extra ref dies here
        CHECK(got.get() == tex.get(), "TextureManager returns the same texture");
    }
    // also exercise GetByFullPath (FM79979-style: path is the key, cached)
    {
        auto tp1 = mgr.GetByFullPath("assets/font_atlas.png", refs);
        auto tp2 = mgr.GetByFullPath("assets/font_atlas.png", refs);
        CHECK(tp1 != nullptr, "GetByFullPath loads from disk");
        CHECK(tp1.get() == tp2.get(), "GetByFullPath returns the SAME cached object on repeat");
    }
    CHECK(mgr.size() == 2, "manager holds checker + font_atlas path");

    // 4) release every Texture reference BEFORE tearing down the Vulkan device
    mgr.Remove("sharedTex");
    mgr.Remove("assets/font_atlas.png");
    tex.reset();
    CHECK(mgr.size() == 0, "TextureManager empty after remove");
    CHECK(ObjectRegistry::instance().LiveCount() == 1, "only the (static) TextureManager remains");

    r.vk.destroy();   // releases the Vulkan device + pools (no Texture still alive)

    if (s_fail == 0) { printf("texture_test: ALL PASS\n"); return 0; }
    printf("texture_test: %d FAILED\n", s_fail);
    return 1;
}
