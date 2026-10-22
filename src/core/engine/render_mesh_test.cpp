// render_mesh_test.cpp -- which triangle meshes IRenderer::drawMesh accepts (validMesh, render_iface.h;
// docs/34_MESH_ANIMATION.md): whole triangles, every index naming a vertex, at most 65536 vertices. The drawing
// itself is checked on the GPU by smoke.mesh (tests/CMakeLists.txt). No GPU here.
#include "render_iface.h"

#include <cstdio>
#include <vector>

namespace {
int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)

Mesh meshOf(const std::vector<MeshVertex>& v, const std::vector<uint16_t>& i) {
    Mesh m;
    m.vertices = v.data();
    m.vertexCount = (uint32_t)v.size();
    m.indices = i.data();
    m.indexCount = (uint32_t)i.size();
    return m;
}
}  // namespace

int main() {
    const std::vector<MeshVertex> quad = {{0, 0, 0, 0}, {10, 0, 1, 0}, {10, 10, 1, 1}, {0, 10, 0, 1}};
    CHECK(validMesh(meshOf(quad, {0, 1, 2, 0, 2, 3})), "two triangles over four vertices");
    CHECK(!validMesh(meshOf(quad, {0, 1, 2, 0})), "an index count that is not a multiple of 3");
    CHECK(!validMesh(meshOf(quad, {0, 1, 4})), "an index past the last vertex");
    CHECK(!validMesh(meshOf(quad, {})), "no indices");
    CHECK(!validMesh(meshOf({}, {0, 1, 2})), "no vertices");
    Mesh tooBig = meshOf(quad, {0, 1, 2});
    tooBig.vertexCount = 65537;   // only the count is read before the indices
    CHECK(!validMesh(tooBig), "more vertices than 16-bit indices reach");

    // The default IRenderer::drawMesh is a no-op: a backend without meshes still builds and runs.
    struct NoMeshes : IRenderer {
        void init(uint32_t, uint32_t) override {}
        void loadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
        void loadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
        void begin() override {}
        void drawSprite(const Quad&) override {}
        void end() override {}
        uint32_t width() const override { return 1; }
        uint32_t height() const override { return 1; }
    } r;
    const std::vector<uint16_t> tri = {0, 1, 2};
    r.drawMesh(meshOf(quad, tri));

    if (g_fail) { std::printf("render_mesh_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("render_mesh_test: OK\n");
    return 0;
}
