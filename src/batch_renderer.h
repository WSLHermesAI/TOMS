// batch_renderer.h — quad batching for TOMS (port of FM79979 BatchDataMultiTexture
// *design*, adapted to TOMS's single-atlas reality).
//
// The FM79979 batch groups quads by texture and flushes a new batch when the
// texture (or blend mode) changes, so the renderer issues one draw per texture
// instead of one draw per quad. TOMS already shares a single atlas, so the
// immediate win here is:
//   * an INDEX BUFFER (4 verts + 6 indices per quad instead of 6 verts/quad) ->
//     1/3 fewer vertices,
//   * flushing only when the texture-set / blend mode changes (the reference's
//     "spill to a new batch" rule), so distinct atlases or blend states each get
//     their own draw call -- exactly the grouping the reference does,
//   * writing the packed interleaved vertices directly into the persistent GPU
//     buffer (no per-frame temp reallocation + full copy).
//
// Usage (see renderer.cpp Renderer::end):
//   BatchRenderer br;
//   br.begin();
//   for (sprite) br.add(q, spriteSet, 0);   // 0 = default alpha blend
//   for (text)   br.add(q, fontSet,   0);
//   br.flush();                              // -> br.batches, br.drawCalls
//   // upload br.vbuf (+ br.ibuf) once, then vkCmdDrawIndexed per batch.
#pragma once
#include <vector>
#include <cstdint>
#include "render_iface.h"   // Quad

// Interleaved vertex: aPos(2) aRect(4) aUVrc(4) aTint(4) aSolid(1) = 15 floats
// (matches the Vulkan/WebGL pipeline attributes in renderer.cpp / renderer_webgl.cpp).
static const int BR_FPV = 15;

struct Batch {
    void*     texSet      = nullptr; // texture key (sprite atlas / font atlas / future)
    uint32_t  blend       = 0;       // blend-mode key (0 = srcAlpha/oneMinusSrcAlpha)
    bool      solid       = false;   // true => flat color, sample dummy texture
    uint32_t  indexOffset = 0;       // start index into the shared index buffer
    uint32_t  indexCount  = 0;       // number of indices in this batch
};

class BatchRenderer {
public:
    std::vector<float>     vbuf;   // interleaved vertices (4 per quad)
    std::vector<uint32_t>  ibuf;   // 6 indices per quad
    std::vector<Batch>     batches;
    uint32_t               drawCalls = 0;
    size_t                 quadCount = 0;

    void begin() {
        vbuf.clear(); ibuf.clear(); batches.clear();
        drawCalls = 0; quadCount = 0;
        vbuf.reserve(4096 * BR_FPV);
        ibuf.reserve(4096 * 6);
    }

    // Add one quad. Starts a new batch when texSet, blend, or solid differs from
    // the current batch (mirrors cBatchDataMultiTexture::GetTextureIndexFromCurrentData
    // returning -1 and spilling to a fresh batch).
    void add(const Quad& q, void* texSet, uint32_t blend) {
        if (!batches.empty()) {
            Batch& cur = batches.back();
            if (cur.texSet == texSet && cur.blend == blend && cur.solid == q.solid) {
                appendQuad(q);                 // same batch
                return;
            }
        }
        Batch b; b.texSet = texSet; b.blend = blend; b.solid = q.solid;
        b.indexOffset = (uint32_t)ibuf.size();
        batches.push_back(b);
        appendQuad(q);                          // new batch
    }

    // Finalize: count draws and fix up each batch's indexCount.
    void flush() {
        drawCalls = (uint32_t)batches.size();
        for (size_t i = 0; i < batches.size(); i++) {
            uint32_t next = (i + 1 < batches.size()) ? batches[i+1].indexOffset
                                                     : (uint32_t)ibuf.size();
            batches[i].indexCount = next - batches[i].indexOffset;
        }
    }

private:
    void appendQuad(const Quad& q) {
        // 4 corners: (0,0)(1,0)(1,1)(0,1) -- all share rect/uv/tint/solid
        static const float corners[4][2] = {{0,0},{1,0},{1,1},{0,1}};
        size_t vbase = vbuf.size();
        vbuf.resize(vbase + 4 * BR_FPV);
        for (int c = 0; c < 4; c++) {
            float* vp = vbuf.data() + vbase + c * BR_FPV;
            vp[0]  = corners[c][0]; vp[1]  = corners[c][1];
            vp[2]  = q.rect[0]; vp[3]  = q.rect[1]; vp[4]  = q.rect[2]; vp[5]  = q.rect[3];
            vp[6]  = q.uv[0];   vp[7]  = q.uv[1];   vp[8]  = q.uv[2];   vp[9]  = q.uv[3];
            vp[10] = q.tint[0];  vp[11] = q.tint[1];  vp[12] = q.tint[2];  vp[13] = q.tint[3];
            vp[14] = q.solid ? 1.0f : 0.0f;
        }
        uint32_t base = (uint32_t)(vbase / BR_FPV);
        uint32_t idx[6] = { base, base+1, base+2, base, base+2, base+3 };
        ibuf.insert(ibuf.end(), idx, idx + 6);
        quadCount++;
    }
};
