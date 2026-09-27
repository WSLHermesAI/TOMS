// imgui_web.cpp — see imgui_web.h. Browser-side ImGui backend: OpenGL3(ES3) renderer + an
// Emscripten DOM event bridge (no SDL/GLFW in this build).
#include "imgui_web.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

namespace toms {
namespace imgui_web {
namespace {

bool g_ready = false;
std::string g_selector = "#canvas";
// CSS size of the canvas, refreshed on resize; used to map mouse CSS px -> drawing-buffer px.
double g_cssW = 1024.0, g_cssH = 768.0;
// Drawing-buffer size handed over by beginFrame() each frame (bufW/bufH of the WebGL renderer).
float g_fbW = 1024.0f, g_fbH = 768.0f;

ImGuiKey keyFromDom(const char* key) {
    if (!key || !*key) return ImGuiKey_None;
    const std::string k(key);
    if (k == "ArrowLeft")  return ImGuiKey_LeftArrow;
    if (k == "ArrowRight") return ImGuiKey_RightArrow;
    if (k == "ArrowUp")    return ImGuiKey_UpArrow;
    if (k == "ArrowDown")  return ImGuiKey_DownArrow;
    if (k == "Enter")      return ImGuiKey_Enter;
    if (k == "Escape")     return ImGuiKey_Escape;
    if (k == "Tab")        return ImGuiKey_Tab;
    if (k == "Backspace")  return ImGuiKey_Backspace;
    if (k == "Delete")     return ImGuiKey_Delete;
    if (k == " ")          return ImGuiKey_Space;
    if (k == "Home")       return ImGuiKey_Home;
    if (k == "End")        return ImGuiKey_End;
    if (k == "PageUp")     return ImGuiKey_PageUp;
    if (k == "PageDown")   return ImGuiKey_PageDown;
    if (k == "Control")    return ImGuiKey_LeftCtrl;
    if (k == "Shift")      return ImGuiKey_LeftShift;
    if (k == "Alt")        return ImGuiKey_LeftAlt;
    if (k == "Meta")       return ImGuiKey_LeftSuper;
    if (k.size() == 1) {
        const char c = k[0];
        if (c >= 'a' && c <= 'z') return (ImGuiKey)(ImGuiKey_A + (c - 'a'));
        if (c >= 'A' && c <= 'Z') return (ImGuiKey)(ImGuiKey_A + (c - 'A'));
        if (c >= '0' && c <= '9') return (ImGuiKey)(ImGuiKey_0 + (c - '0'));
    }
    if (k.size() == 2 && k[0] == 'F' && k[1] >= '1' && k[1] <= '9')
        return (ImGuiKey)(ImGuiKey_F1 + (k[1] - '1'));
    return ImGuiKey_None;
}

// CSS px -> drawing-buffer px (the canvas is usually CSS-scaled on a hi-dpi or full-viewport page).
void toBufferCoords(double cssX, double cssY, float& outX, float& outY) {
    const double sx = (g_cssW > 1.0) ? (double)g_fbW / g_cssW : 1.0;
    const double sy = (g_cssH > 1.0) ? (double)g_fbH / g_cssH : 1.0;
    outX = (float)(cssX * sx);
    outY = (float)(cssY * sy);
}

EM_BOOL onMouseDown(int, const EmscriptenMouseEvent* e, void*) {
    mouseButton(e->button, true, e->targetX, e->targetY);
    return g_ready ? EM_TRUE : EM_FALSE;
}
EM_BOOL onMouseUp(int, const EmscriptenMouseEvent* e, void*) {
    mouseButton(e->button, false, e->targetX, e->targetY);
    return g_ready ? EM_TRUE : EM_FALSE;
}
EM_BOOL onMouseMove(int, const EmscriptenMouseEvent* e, void*) {
    mouseMove(e->targetX, e->targetY);
    return EM_FALSE;   // the game's own pointer channel still needs the move
}
EM_BOOL onWheel(int, const EmscriptenWheelEvent* e, void*) {
    // DOM wheel deltas are pixels; ImGui expects "lines" (~1/100 px is the usual convention).
    mouseWheel(-e->deltaX / 100.0, -e->deltaY / 100.0);
    return g_ready && ImGui::GetIO().WantCaptureMouse ? EM_TRUE : EM_FALSE;
}
EM_BOOL onKeyDown(int, const EmscriptenKeyboardEvent* e, void*) {
    keyEvent(e->key, e->code, true, e->ctrlKey, e->shiftKey, e->altKey, e->metaKey);
    return EM_FALSE;   // the game's own keyboard handling (keyCb) runs in its own callback
}
EM_BOOL onKeyUp(int, const EmscriptenKeyboardEvent* e, void*) {
    keyEvent(e->key, e->code, false, e->ctrlKey, e->shiftKey, e->altKey, e->metaKey);
    return EM_FALSE;
}
EM_BOOL onFocus(int eventType, const EmscriptenFocusEvent*, void*) {
    focusEvent(eventType == EMSCRIPTEN_EVENT_FOCUS);
    return EM_FALSE;
}
EM_BOOL onResize(int, const EmscriptenUiEvent*, void*) {
    emscripten_get_element_css_size(g_selector.c_str(), &g_cssW, &g_cssH);
    return EM_FALSE;
}
EM_BOOL onTouch(int eventType, const EmscriptenTouchEvent* e, void*) {
    if (e->numTouches <= 0) return EM_FALSE;
    const EmscriptenTouchPoint& t = e->touches[0];
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    mouseMove(t.targetX, t.targetY);
    if (eventType == EMSCRIPTEN_EVENT_TOUCHSTART) mouseButton(0, true, t.targetX, t.targetY);
    else if (eventType == EMSCRIPTEN_EVENT_TOUCHEND || eventType == EMSCRIPTEN_EVENT_TOUCHCANCEL)
        mouseButton(0, false, t.targetX, t.targetY);
    return (g_ready && io.WantCaptureMouse) ? EM_TRUE : EM_FALSE;
}

} // namespace

bool ready() { return g_ready; }

bool init(const std::string& canvasSelector) {
    if (g_ready) return true;
    g_selector = canvasSelector;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // No imgui.ini on the web build: the browser runtime's filesystem is per-session, so writing a
    // layout file there would be forgotten anyway (and looks like a save bug when it isn't).
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // ES 3.0 shader header: the WebGL2 context is created by the page's bootstrap (#version 300 es
    // is mandatory there), and imgui_impl_opengl3 uses ES3 code paths when built with
    // IMGUI_IMPL_OPENGL_ES3 (set in CMakeLists for this target).
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        fprintf(stderr, "[imgui_web] ImGui_ImplOpenGL3_Init failed\n");
        ImGui::DestroyContext();
        return false;
    }

    emscripten_get_element_css_size(g_selector.c_str(), &g_cssW, &g_cssH);
    emscripten_set_mousedown_callback(g_selector.c_str(), nullptr, 1, onMouseDown);
    emscripten_set_mouseup_callback(g_selector.c_str(), nullptr, 1, onMouseUp);
    emscripten_set_mousemove_callback(g_selector.c_str(), nullptr, 1, onMouseMove);
    emscripten_set_wheel_callback(g_selector.c_str(), nullptr, 1, onWheel);
    emscripten_set_touchstart_callback(g_selector.c_str(), nullptr, 1, onTouch);
    emscripten_set_touchmove_callback(g_selector.c_str(), nullptr, 1, onTouch);
    emscripten_set_touchend_callback(g_selector.c_str(), nullptr, 1, onTouch);
    emscripten_set_touchcancel_callback(g_selector.c_str(), nullptr, 1, onTouch);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, 1, onKeyDown);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, 1, onKeyUp);
    emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, 1, onFocus);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, 1, onFocus);

    g_ready = true;
    fprintf(stderr, "[imgui_web] ImGui ready (OpenGL3/ES3 backend on the WebGL2 context)\n");
    return true;
}

void shutdown() {
    if (!g_ready) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    g_ready = false;
}

void beginFrame(float framebufferW, float framebufferH) {
    if (!g_ready) return;
    g_fbW = framebufferW > 1.0f ? framebufferW : 1.0f;
    g_fbH = framebufferH > 1.0f ? framebufferH : 1.0f;
    if (g_cssW < 1.0 || g_cssH < 1.0)
        emscripten_get_element_css_size(g_selector.c_str(), &g_cssW, &g_cssH);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(g_fbW, g_fbH);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
}

void endFrame() {
    if (!g_ready) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void mouseButton(int button, bool down, double cssX, double cssY) {
    if (!g_ready) return;
    float x, y; toBufferCoords(cssX, cssY, x, y);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    // DOM: 0 = left, 1 = middle, 2 = right.
    const int imguiBtn = (button == 1) ? 2 : (button == 2 ? 1 : 0);
    io.AddMouseButtonEvent(imguiBtn, down);
}

void mouseMove(double cssX, double cssY) {
    if (!g_ready) return;
    float x, y; toBufferCoords(cssX, cssY, x, y);
    ImGui::GetIO().AddMousePosEvent(x, y);
}

void mouseWheel(double dx, double dy) {
    if (!g_ready) return;
    ImGui::GetIO().AddMouseWheelEvent((float)dx, (float)dy);
}

void keyEvent(const char* key, const char* code, bool down, bool ctrl, bool shift, bool alt,
              bool meta) {
    if (!g_ready) return;
    (void)code;
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
    io.AddKeyEvent(ImGuiMod_Shift, shift);
    io.AddKeyEvent(ImGuiMod_Alt, alt);
    io.AddKeyEvent(ImGuiMod_Super, meta);
    const ImGuiKey k = keyFromDom(key);
    if (k != ImGuiKey_None) io.AddKeyEvent(k, down);
    if (down && key && strlen(key) == 1 && (unsigned char)key[0] >= 32)
        io.AddInputCharactersUTF8(key);
}

void textInput(const char* utf8) {
    if (!g_ready || !utf8) return;
    ImGui::GetIO().AddInputCharactersUTF8(utf8);
}

void focusEvent(bool focused) {
    if (!g_ready) return;
    ImGui::GetIO().AddFocusEvent(focused);
}

bool wantsMouse()    { return g_ready && ImGui::GetIO().WantCaptureMouse; }
bool wantsKeyboard() { return g_ready && ImGui::GetIO().WantCaptureKeyboard; }

} // namespace imgui_web
} // namespace toms
#endif // __EMSCRIPTEN__
