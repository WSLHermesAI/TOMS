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

bgfx::VertexLayout& f_ImguiLayout() {
    static bgfx::VertexLayout l_oLayout;
    static bool l_bReady = false;
    if (!l_bReady) {
        l_oLayout.begin()
            .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
            .end();
        l_bReady = true;
    }
    return l_oLayout;
}

}  // namespace

bool ImGuiBgfx::f_Init() {
    if (m_bReady) return true;
    IMGUI_CHECKVERSION();
    if (!ImGui::GetCurrentContext()) ImGui::CreateContext();
    ImGuiIO& l_oIo = ImGui::GetIO();
    l_oIo.IniFilename = nullptr;   // do not write imgui.ini next to the exe
    l_oIo.BackendRendererName = "toms_imgui_bgfx";
    l_oIo.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    unsigned char* l_pPixels = nullptr;
    int l_iW = 0, l_iH = 0;
    l_oIo.Fonts->GetTexDataAsRGBA32(&l_pPixels, &l_iW, &l_iH);
    m_u16FontTex = bgfx::createTexture2D((uint16_t)l_iW, (uint16_t)l_iH, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                     bgfx::copy(l_pPixels, (uint32_t)(l_iW * l_iH * 4))).idx;
    l_oIo.Fonts->SetTexID((ImTextureID)(uintptr_t)m_u16FontTex);

    m_u16Program = f_CreateEmbeddedProgram(ShaderProgram::ImGui).idx;
    m_u16Sampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler).idx;
    m_bReady = m_u16Program != UINT16_MAX;
    return m_bReady;
}

void ImGuiBgfx::f_Shutdown() {
    if (m_u16FontTex != UINT16_MAX) bgfx::destroy(bgfx::TextureHandle{m_u16FontTex});
    if (m_u16Program != UINT16_MAX) bgfx::destroy(bgfx::ProgramHandle{m_u16Program});
    if (m_u16Sampler != UINT16_MAX) bgfx::destroy(bgfx::UniformHandle{m_u16Sampler});
    m_u16FontTex = m_u16Program = m_u16Sampler = UINT16_MAX;
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    m_bReady = false;
}

void ImGuiBgfx::f_NewFrame(uint32_t l_uWidth, uint32_t l_uHeight, float l_fDtSeconds, const Input& l_oIn) {
    m_uWidth = l_uWidth;
    m_uHeight = l_uHeight;
    ImGuiIO& l_oIo = ImGui::GetIO();
    l_oIo.DisplaySize = ImVec2((float)l_uWidth, (float)l_uHeight);
    l_oIo.DeltaTime = std::max(l_fDtSeconds, 1.0f / 1000.0f);
    if (l_oIn.m_bHasMouse) l_oIo.AddMousePosEvent(l_oIn.m_fMouseX, l_oIn.m_fMouseY);
    for (int l_iB = 0; l_iB < 3; ++l_iB) l_oIo.AddMouseButtonEvent(l_iB, l_oIn.m_arrMouseDown[l_iB]);
    if (l_oIn.m_fWheel != 0.0f) l_oIo.AddMouseWheelEvent(0.0f, l_oIn.m_fWheel);
    ImGui::NewFrame();
}

void ImGuiBgfx::f_Render(uint16_t l_u16ViewId) {
    ImGui::Render();
    ImDrawData* l_pDd = ImGui::GetDrawData();
    if (!m_bReady || !l_pDd || l_pDd->CmdListsCount == 0) return;

    bgfx::setViewRect(l_u16ViewId, 0, 0, (uint16_t)m_uWidth, (uint16_t)m_uHeight);
    bgfx::setViewMode(l_u16ViewId, bgfx::ViewMode::Sequential);
    float l_arrProj[16];
    bx::mtxOrtho(l_arrProj, l_pDd->DisplayPos.x, l_pDd->DisplayPos.x + l_pDd->DisplaySize.x,
                 l_pDd->DisplayPos.y + l_pDd->DisplaySize.y, l_pDd->DisplayPos.y, 0.0f, 1000.0f, 0.0f,
                 bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(l_u16ViewId, nullptr, l_arrProj);

    const bgfx::VertexLayout& l_oLayout = f_ImguiLayout();
    for (int l_iN = 0; l_iN < l_pDd->CmdListsCount; ++l_iN) {
        const ImDrawList* l_pList = l_pDd->CmdLists[l_iN];
        const uint32_t l_uNumV = (uint32_t)l_pList->VtxBuffer.size();
        const uint32_t l_uNumI = (uint32_t)l_pList->IdxBuffer.size();
        if (bgfx::getAvailTransientVertexBuffer(l_uNumV, l_oLayout) < l_uNumV ||
            bgfx::getAvailTransientIndexBuffer(l_uNumI, sizeof(ImDrawIdx) == 4) < l_uNumI)
            break;   // out of transient memory this frame: skip the rest rather than crash
        bgfx::TransientVertexBuffer l_oTvb;
        bgfx::TransientIndexBuffer l_oTib;
        bgfx::allocTransientVertexBuffer(&l_oTvb, l_uNumV, l_oLayout);
        bgfx::allocTransientIndexBuffer(&l_oTib, l_uNumI, sizeof(ImDrawIdx) == 4);
        std::memcpy(l_oTvb.data, l_pList->VtxBuffer.Data, l_uNumV * sizeof(ImDrawVert));
        std::memcpy(l_oTib.data, l_pList->IdxBuffer.Data, l_uNumI * sizeof(ImDrawIdx));

        for (const ImDrawCmd& l_oCmd : l_pList->CmdBuffer) {
            if (l_oCmd.UserCallback) { l_oCmd.UserCallback(l_pList, &l_oCmd); continue; }
            if (l_oCmd.ElemCount == 0) continue;
            const float l_fX0 = std::max(l_oCmd.ClipRect.x - l_pDd->DisplayPos.x, 0.0f);
            const float l_fY0 = std::max(l_oCmd.ClipRect.y - l_pDd->DisplayPos.y, 0.0f);
            const float l_fX1 = std::min(l_oCmd.ClipRect.z - l_pDd->DisplayPos.x, (float)m_uWidth);
            const float l_fY1 = std::min(l_oCmd.ClipRect.w - l_pDd->DisplayPos.y, (float)m_uHeight);
            if (l_fX1 <= l_fX0 || l_fY1 <= l_fY0) continue;
            bgfx::setScissor((uint16_t)l_fX0, (uint16_t)l_fY0, (uint16_t)(l_fX1 - l_fX0), (uint16_t)(l_fY1 - l_fY0));
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
            bgfx::TextureHandle l_oTex{(uint16_t)(uintptr_t)l_oCmd.GetTexID()};
            bgfx::setTexture(0, bgfx::UniformHandle{m_u16Sampler}, l_oTex);
            bgfx::setVertexBuffer(0, &l_oTvb, l_oCmd.VtxOffset, l_uNumV - l_oCmd.VtxOffset);
            bgfx::setIndexBuffer(&l_oTib, l_oCmd.IdxOffset, l_oCmd.ElemCount);
            bgfx::submit(l_u16ViewId, bgfx::ProgramHandle{m_u16Program});
        }
    }
}

}  // namespace toms::next
