// imgui_bgfx.h -- Dear ImGui drawn with bgfx. Replaces src/engine/imgui_layer.* (GLFW + Vulkan).
//
// Input is fed by the host (SDL3 or Qt) through ImGuiBgfx::Input, so the same class works in the
// game and in the editor viewport. The game's dev windows (F1 debug, F2 styling spike, Tab stage
// select, notifications) are plain ImGui calls in the legacy code and draw through this.
#pragma once
#include <cstdint>

namespace toms::next {

class ImGuiBgfx {
public:
    struct Input {
        float mouseX = 0, mouseY = 0;   // backbuffer pixels
        bool  mouseDown[3] = {false, false, false};
        float wheel = 0;
        bool  hasMouse = true;
    };

    bool init();                        // creates the ImGui context, font texture and program
    void shutdown();
    void newFrame(uint32_t width, uint32_t height, float dtSeconds, const Input& in);
    void render(uint16_t viewId);       // ImGui::Render() + submit to viewId
    bool ready() const { return ready_; }

private:
    bool ready_ = false;
    uint16_t program_ = UINT16_MAX, sampler_ = UINT16_MAX, fontTex_ = UINT16_MAX;
    uint32_t width_ = 0, height_ = 0;
};

}  // namespace toms::next
