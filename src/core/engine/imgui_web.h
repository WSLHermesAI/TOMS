// imgui_web.h — Dear ImGui on the BROWSER build (M2: "ImGui wired into all 3 renderer backends").
//
// The desktop build gets its ImGui plumbing from two off-the-shelf backends in imgui_layer.cpp
// (imgui_impl_glfw + imgui_impl_vulkan). The web build has no SDL/GLFW at all -- it drives the
// canvas through raw Emscripten HTML5 callbacks (see emscripten_main.cpp) -- so there is no
// platform backend to reuse and none is vendored for it. This file is that missing backend:
//
//   * ImGui core context + the OpenGL3 backend (ES 3.0 flavour) for the WebGL2 renderer;
//   * a DOM event bridge that feeds mouse / wheel / touch / keyboard into ImGui's event queue;
//   * beginFrame()/endFrame() helpers the web main loop brackets its frame with.
//
// Scope note (deliberate): the browser build enables the same *dev* windows the desktop build has
// -- the F1 debug overlay, the F2 styling spike and the ImGui font-scale setting. The ImGui
// stage-select window and the notification toasts stay desktop-only: their strings come from the
// locale table (CJK) and ImGui's built-in font has no CJK glyphs, so they would draw as tofu boxes
// on web, and the browser already has its own stage-select/toast drawing in the game renderer.
#pragma once

#include <string>

namespace toms {
namespace imgui_web {

// True once init() succeeded; every other call is a no-op otherwise, so the web entry can call
// them unconditionally.
bool ready();

// Create the context, initialise the OpenGL3 (ES3) backend on the CURRENT WebGL2 context and hook
// the DOM events. canvasSelector is the CSS selector the page bootstraps (#canvas). Must be called
// after the renderer exists (the GL context must be current).
bool init(const std::string& canvasSelector);

void shutdown();

// ImGui needs the drawing-buffer size in pixels, not the design resolution: the game maps its fixed
// 1024x768 design space onto the canvas inside its own shader, while ImGui draws real pixels.
void beginFrame(float framebufferW, float framebufferH);

// ImGui::Render() + the backend's RenderDrawData. Call after the game's own renderer->end().
void endFrame();

// ---- input bridge (called from emscripten_main.cpp's callbacks) ----
void mouseButton(int button, bool down, double cssX, double cssY);
void mouseMove(double cssX, double cssY);
void mouseWheel(double dx, double dy);
void keyEvent(const char* key, const char* code, bool down, bool ctrl, bool shift, bool alt,
              bool meta);
void textInput(const char* utf8);
void focusEvent(bool focused);

// True while ImGui owns the mouse / keyboard (a window is open and hovered / an item is active).
// The game's own input routing checks these so a drag on an ImGui slider cannot also move the
// player or fire the battle buttons.
bool wantsMouse();
bool wantsKeyboard();

} // namespace imgui_web
} // namespace toms
