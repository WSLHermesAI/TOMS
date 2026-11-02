// render_mesh_test.cpp -- which triangle meshes IRenderer::drawMesh accepts (validMesh, render_iface.h;
// docs/engine/16_MESH_ANIMATION.md): whole triangles, every index naming a vertex, at most 65536 vertices. The drawing
// itself is checked on the GPU by smoke.mesh (tests/CMakeLists.txt). No GPU here.
#include "render_iface.h"

#include <cstdio>
#include <vector>

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_iFail; } } while (0)

Mesh f_MeshOf(const std::vector<MeshVertex>& l_vV, const std::vector<uint16_t>& l_vI) {
    Mesh l_oM;
    l_oM.m_pVertices = l_vV.data();
    l_oM.m_uVertexCount = (uint32_t)l_vV.size();
    l_oM.m_pIndices = l_vI.data();
    l_oM.m_uIndexCount = (uint32_t)l_vI.size();
    return l_oM;
}
}  // namespace

int main() {
    const std::vector<MeshVertex> l_vQuad = {{0, 0, 0, 0}, {10, 0, 1, 0}, {10, 10, 1, 1}, {0, 10, 0, 1}};
    CHECK(f_ValidMesh(f_MeshOf(l_vQuad, {0, 1, 2, 0, 2, 3})), "two triangles over four vertices");
    CHECK(!f_ValidMesh(f_MeshOf(l_vQuad, {0, 1, 2, 0})), "an index count that is not a multiple of 3");
    CHECK(!f_ValidMesh(f_MeshOf(l_vQuad, {0, 1, 4})), "an index past the last vertex");
    CHECK(!f_ValidMesh(f_MeshOf(l_vQuad, {})), "no indices");
    CHECK(!f_ValidMesh(f_MeshOf({}, {0, 1, 2})), "no vertices");
    Mesh l_oTooBig = f_MeshOf(l_vQuad, {0, 1, 2});
    l_oTooBig.m_uVertexCount = 65537;   // only the count is read before the indices
    CHECK(!f_ValidMesh(l_oTooBig), "more vertices than 16-bit indices reach");

    // The default IRenderer::drawMesh is a no-op: a backend without meshes still builds and runs.
    struct NoMeshes : IRenderer {
        void f_Init(uint32_t, uint32_t) override {}
        void f_LoadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
        void f_LoadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
        void begin() override {}
        void f_DrawSprite(const Quad&) override {}
        void end() override {}
        uint32_t f_Width() const override { return 1; }
        uint32_t f_Height() const override { return 1; }
    } l_oR;
    const std::vector<uint16_t> l_vTri = {0, 1, 2};
    l_oR.f_DrawMesh(f_MeshOf(l_vQuad, l_vTri));

    if (g_iFail) { std::printf("render_mesh_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("render_mesh_test: OK\n");
    return 0;
}
