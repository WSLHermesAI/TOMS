// imgui_bgfx.cpp -- see imgui_bgfx.h.
#include "imgui_bgfx.h"
#include "embedded_shaders.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace toms::next {
namespace {

bgfx::VertexLayout& imguiLayout() {
    static bgfx::VertexLayout layout;
    static bool ready = false;
    if (!ready) {
        layout.begin()
            .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
            .end();
        ready = true;
    }
    return layout;
}

}  // namespace

bool ImGuiBgfx::init() {
    if (ready_) return true;
    IMGUI_CHECKVERSION();
    if (!ImGui::GetCurrentContext()) ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // do not write imgui.ini next to the exe
    io.BackendRendererName = "toms_imgui_bgfx";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    fontTex_ = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                     bgfx::copy(pixels, (uint32_t)(w * h * 4))).idx;
    io.Fonts->SetTexID((ImTextureID)(uintptr_t)fontTex_);

    program_ = createEmbeddedProgram(ShaderProgram::ImGui).idx;
    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler).idx;
    ready_ = program_ != UINT16_MAX;
    return ready_;
}

void ImGuiBgfx::shutdown() {
    if (fontTex_ != UINT16_MAX) bgfx::destroy(bgfx::TextureHandle{fontTex_});
    if (program_ != UINT16_MAX) bgfx::destroy(bgfx::ProgramHandle{program_});
    if (sampler_ != UINT16_MAX) bgfx::destroy(bgfx::UniformHandle{sampler_});
    fontTex_ = program_ = sampler_ = UINT16_MAX;
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    ready_ = false;
}

void ImGuiBgfx::newFrame(uint32_t width, uint32_t height, float dtSeconds, const Input& in) {
    width_ = width;
    height_ = height;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = std::max(dtSeconds, 1.0f / 1000.0f);
    if (in.hasMouse) io.AddMousePosEvent(in.mouseX, in.mouseY);
    for (int b = 0; b < 3; ++b) io.AddMouseButtonEvent(b, in.mouseDown[b]);
    if (in.wheel != 0.0f) io.AddMouseWheelEvent(0.0f, in.wheel);
    ImGui::NewFrame();
}

void ImGuiBgfx::render(uint16_t viewId) {
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (!ready_ || !dd || dd->CmdListsCount == 0) return;

    bgfx::setViewRect(viewId, 0, 0, (uint16_t)width_, (uint16_t)height_);
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    float proj[16];
    bx::mtxOrtho(proj, dd->DisplayPos.x, dd->DisplayPos.x + dd->DisplaySize.x,
                 dd->DisplayPos.y + dd->DisplaySize.y, dd->DisplayPos.y, 0.0f, 1000.0f, 0.0f,
                 bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(viewId, nullptr, proj);

    const bgfx::VertexLayout& layout = imguiLayout();
    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList* list = dd->CmdLists[n];
        const uint32_t numV = (uint32_t)list->VtxBuffer.size();
        const uint32_t numI = (uint32_t)list->IdxBuffer.size();
        if (bgfx::getAvailTransientVertexBuffer(numV, layout) < numV ||
            bgfx::getAvailTransientIndexBuffer(numI, sizeof(ImDrawIdx) == 4) < numI)
            break;   // out of transient memory this frame: skip the rest rather than crash
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, numV, layout);
        bgfx::allocTransientIndexBuffer(&tib, numI, sizeof(ImDrawIdx) == 4);
        std::memcpy(tvb.data, list->VtxBuffer.Data, numV * sizeof(ImDrawVert));
        std::memcpy(tib.data, list->IdxBuffer.Data, numI * sizeof(ImDrawIdx));

        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) { cmd.UserCallback(list, &cmd); continue; }
            if (cmd.ElemCount == 0) continue;
            const float x0 = std::max(cmd.ClipRect.x - dd->DisplayPos.x, 0.0f);
            const float y0 = std::max(cmd.ClipRect.y - dd->DisplayPos.y, 0.0f);
            const float x1 = std::min(cmd.ClipRect.z - dd->DisplayPos.x, (float)width_);
            const float y1 = std::min(cmd.ClipRect.w - dd->DisplayPos.y, (float)height_);
            if (x1 <= x0 || y1 <= y0) continue;
            bgfx::setScissor((uint16_t)x0, (uint16_t)y0, (uint16_t)(x1 - x0), (uint16_t)(y1 - y0));
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
            bgfx::TextureHandle tex{(uint16_t)(uintptr_t)cmd.GetTexID()};
            bgfx::setTexture(0, bgfx::UniformHandle{sampler_}, tex);
            bgfx::setVertexBuffer(0, &tvb, cmd.VtxOffset, numV - cmd.VtxOffset);
            bgfx::setIndexBuffer(&tib, cmd.IdxOffset, cmd.ElemCount);
            bgfx::submit(viewId, bgfx::ProgramHandle{program_});
        }
    }
}

}  // namespace toms::next
