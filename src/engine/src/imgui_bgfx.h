// imgui_bgfx.h -- Dear ImGui drawn with bgfx (replaced the old GLFW + Vulkan ImGui layer).
//
// Input is fed by the host (SDL3 or Qt) through ImGuiBgfx::Input, so the same class works in the
// game and in the editor viewport. The game's dev windows (F1 debug, F2 styling spike, Tab stage
// select, notifications) are plain ImGui calls in the core code and draw through this.
#pragma once
#include <cstdint>

namespace toms::next {

class ImGuiBgfx {
public:
    struct Input {
        float m_fMouseX = 0, m_fMouseY = 0;   // backbuffer pixels
        bool  m_arrMouseDown[3] = {false, false, false};
        float m_fWheel = 0;
        bool  m_bHasMouse = true;
    };

    bool f_Init();                        // creates the ImGui context, font texture and program
    void f_Shutdown();
    void f_NewFrame(uint32_t l_uWidth, uint32_t l_uHeight, float l_fDtSeconds, const Input& l_oIn);
    void f_Render(uint16_t l_u16ViewId);       // ImGui::Render() + submit to viewId
    bool f_Ready() const { return m_bReady; }

private:
    bool m_bReady = false;
    uint16_t m_u16Program = UINT16_MAX, m_u16Sampler = UINT16_MAX, m_u16FontTex = UINT16_MAX;
    uint32_t m_uWidth = 0, m_uHeight = 0;
};

}  // namespace toms::next
