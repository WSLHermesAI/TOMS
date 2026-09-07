// main.cpp - Tower of the Sorcerer (Vulkan), windowed interactive build.
// GLFW_INCLUDE_NONE => don't pull GL/gl.h (no OpenGL dev headers needed on
// Windows/Linux). <vulkan/vulkan.h> is included by renderer.h.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "game.h"
#include "renderer.h"   // for dynamic_cast<Renderer*> + VulkanContext
#include "imgui_layer.h" // Milestone 2: Dear ImGui dev-only debug overlay (Vulkan+GLFW)
#include "object.h"     // for Object::DumpLeaks() at shutdown
#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>
#include <unordered_map>
#include <cstdio>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    // asset dir: env override -> argv[1] -> "assets"; argv[2] = scenario mode
    std::string assetDir = "assets";
    if (argc > 1) assetDir = argv[1];
    else if (const char* ad = std::getenv("ASSET_DIR")) assetDir = ad;

    // Log current working directory
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if (!ec) std::cout << "[CWD] " << cwd.string() << '\n';

#ifndef NDEBUG
#ifdef _WIN32
    //// In debug mode, set working directory to the assets folder
    ////fs::path assetsPath = fs::path(assetDir).is_absolute()
    ////    ? fs::path(assetDir)
    ////    : cwd / assetDir;
    //fs::path assetsPath = "../../assets";
    //fs::current_path(assetsPath, ec);
    //if (ec)
    //    std::cerr << "[WARN] Failed to set CWD to assets folder: " << ec.message() << '\n';
    //else
    //    std::cout << "[CWD] Changed to assets folder: " << assetsPath.string() << '\n';
    //// Adjust paths that were relative to the original CWD
    //assetDir = ".";
    //(void)cwd;
#endif
#endif

    // Everything game-side lives in this block so Game (and all engine objects)
    // are fully destroyed before we dump the object-leak report at the end.
    {
        Game g;
        toms::Logger::instance().setFile("toms.log");
        toms::Logger::instance().setLevel(toms::LogLevel::Info);
#ifdef __EMSCRIPTEN__
        const char* buildKind = "web";
#else
        const char* buildKind = "native";
#endif
        TOMS_LOG_INFO("TOMS engine start (C++{}, {} build)", __cplusplus/100, buildKind);
        if (!g.loadAssets(assetDir)) { std::cerr << "asset load failed\n"; return 1; }
        if (const char* hm = std::getenv("TOMS_HIDE")) g.hideMask = std::atoi(hm);
        if (const char* sn = std::getenv("TOMS_SPLIT_NODE")) {
            if (auto* r = dynamic_cast<Renderer*>(g.renderer())) r->setNodeFilter((uint8_t)std::atoi(sn));
        }
        g.loadStage("stage01");
        std::cout << "Loaded stage01. Use arrow keys to move, Enter to interact, I or the Backpack button for inventory, Escape to quit.\n";

        // Retrieve the GLFW window from the renderer for key polling
        GLFWwindow* win = nullptr;
        if (auto* r = dynamic_cast<Renderer*>(g.renderer())) win = r->vk.window;

        // Milestone 2: Dear ImGui dev-only debug overlay (F1 to toggle). Init failure is
        // logged but non-fatal -- the game just runs without the overlay in that case.
        toms::ImGuiLayer imguiLayer;
        bool showDebugOverlay = false;
        bool showStylingSpike = false;   // M2 styling spike (F2) — see Game::drawStylingSpike()
        if (auto* r = dynamic_cast<Renderer*>(g.renderer())) {
            if (imguiLayer.init(win, r->vk.instance, r->vk.physical, r->vk.device, r->vk.gfxFamily,
                                 r->vk.gfxQueue, r->renderPass, r->vk.swapImageCount)) {
                r->uiOverlayHook = [&imguiLayer](VkCommandBuffer cmd) { imguiLayer.renderDrawData(cmd); };
            }
        }

        // Key repeat state: track which keys were down last frame
        auto keyDown = [&](int key) -> bool {
            return win && glfwGetKey(win, key) == GLFW_PRESS;
        };

        // Per-key debounce: only fire once per press, not every frame
        std::unordered_map<int, bool> keyWas;
        auto keyPressed = [&](int key) -> bool {
            bool now = keyDown(key);
            bool fired = now && !keyWas[key];
            keyWas[key] = now;
            return fired;
        };

        using clock = std::chrono::steady_clock;
        auto lastTime = clock::now();

        // ---- Interactive game loop ----
        while (win && !glfwWindowShouldClose(win)) {
            glfwPollEvents();

            auto now = clock::now();
            int dtMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();
            lastTime = now;
            if (dtMs > 0) g.update(dtMs);

            // Keyboard -> game actions (one action per key press).
            //
            // IMPORTANT: keyPressed(key) is a stateful edge-trigger -- it mutates its own
            // debounce record on every call, not just when it returns true. Calling it more
            // than once per frame for the SAME physical key means every call after the first
            // sees the key as already "consumed" and returns false that frame, even though
            // nothing meaningful happened with it. This silently broke: Enter while the store
            // dialog is open (consumed by the generic interact/dialogue check below before the
            // store block ever saw it), Escape while any modal is open (consumed by the
            // unconditional quit below), and arrow/WASD navigation while the inventory is open
            // (consumed by the movePlayer check below). Fix: query each physical key exactly
            // once per frame into a local, and have every block below reference that local
            // instead of re-calling keyPressed() for the same key.
            bool upPressed    = keyPressed(GLFW_KEY_UP)    || keyPressed(GLFW_KEY_W);
            bool downPressed  = keyPressed(GLFW_KEY_DOWN)  || keyPressed(GLFW_KEY_S);
            bool leftPressed  = keyPressed(GLFW_KEY_LEFT)  || keyPressed(GLFW_KEY_A);
            bool rightPressed = keyPressed(GLFW_KEY_RIGHT) || keyPressed(GLFW_KEY_D);
            bool enterPressed = keyPressed(GLFW_KEY_ENTER);
            bool spacePressed = keyPressed(GLFW_KEY_SPACE);
            bool escPressed   = keyPressed(GLFW_KEY_ESCAPE);

            if (keyPressed(GLFW_KEY_F1)) showDebugOverlay = !showDebugOverlay;
            if (keyPressed(GLFW_KEY_F2)) showStylingSpike = !showStylingSpike;
            // Escape: close whatever modal is open (store, then inventory) before quitting the
            // game outright -- previously this unconditionally quit even with a dialog open.
            if (escPressed) {
                if (g.storeModal()) g.storeKey(27);
                else if (g.inventoryOpen()) g.toggleInventory();
                else if (!g.modalActive()) break;
                // Combat/dialogue have no defined Escape-to-cancel action; leave it a no-op
                // rather than quitting the game out from under an active conversation/fight.
            }
            if (upPressed)    g.movePlayer(0, -1);
            if (downPressed)  g.movePlayer(0,  1);
            if (leftPressed)  g.movePlayer(-1, 0);
            if (rightPressed) g.movePlayer( 1, 0);
            if (enterPressed || spacePressed) {
                if (g.inDialogueFlag()) g.chooseDialogue(g.dialogueSel());
                else g.interact();
            }
            if (keyPressed(GLFW_KEY_I)) g.toggleInventory();
            // Store: B opens the shop (only when no other modal is up)
            if (keyPressed(GLFW_KEY_B) && !g.modalActive()) g.openStore();
            // Inventory cursor
            if (g.inventoryOpen()) {
                if (upPressed)    g.invMoveSel(0, -1);
                if (downPressed)  g.invMoveSel(0,  1);
                if (leftPressed)  g.invMoveSel(-1, 0);
                if (rightPressed) g.invMoveSel( 1, 0);
                if (enterPressed) g.invUseSelected();
            }
            // Store keys (Escape is handled above, uniformly with other modals)
            if (g.storeModal()) {
                for (int k = '1'; k <= '9'; k++)
                    if (keyPressed(k)) g.storeKey((char)k);
                if (enterPressed) g.storeKey(13);
            }

            // Mouse: desktop clicks were never wired to the store/dialogue/inventory click
            // targets at all -- handleTouch()/storeClick() previously only ran from the web/
            // touch input path (see emscripten_main.cpp). Forward a left-click the same way, in
            // the renderer's fixed 1024x768 design-resolution space (matching handleTouch's
            // documented contract) via Renderer::deviceToDesign(), which also accounts for the
            // letterboxed/pillarboxed viewport (see Renderer::computeAspectFitViewport) -- a
            // click landing in a letterbox bar is correctly ignored rather than mismapped.
            static bool mouseWasDown = false;
            if (win && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                if (!mouseWasDown) {
                    double mx, my; glfwGetCursorPos(win, &mx, &my);
                    // glfwGetCursorPos is in window (screen) coordinates, which differ from
                    // framebuffer/device pixels on HiDPI displays -- rescale to device pixels
                    // before feeding the viewport math, which operates in device pixels.
                    int winW, winH, fbW, fbH;
                    glfwGetWindowSize(win, &winW, &winH);
                    glfwGetFramebufferSize(win, &fbW, &fbH);
                    if (winW > 0 && winH > 0 && fbW > 0 && fbH > 0) {
                        double dx = mx * ((double)fbW / winW), dy = my * ((double)fbH / winH);
                        if (auto* r = dynamic_cast<Renderer*>(g.renderer())) {
                            float bx, by;
                            if (r->deviceToDesign(dx, dy, bx, by)) g.handleTouch(bx, by, 0);  // phase 0 = down
                        }
                    }
                }
                mouseWasDown = true;
            } else {
                mouseWasDown = false;
            }

            // Acquire next swapchain image then draw. beginFrame() transparently
            // recreates the swapchain (window was resized, or it's out of date)
            // and returns false to skip the frame when that happens.
            if (auto* r = dynamic_cast<Renderer*>(g.renderer())) {
                if (!r->beginFrame()) continue;
            }
            imguiLayer.newFrame();
            if (showDebugOverlay) g.drawDebugOverlay();
            // setStylingSpikeVisible(false) first: drawStylingSpike() flips it back to true when
            // called, so this is what makes the backdrop (drawn later, inside g.draw()) actually
            // turn off the frame after F2 is toggled off, instead of staying stuck visible.
            g.setStylingSpikeVisible(false);
            if (showStylingSpike) g.drawStylingSpike();
            imguiLayer.endFrame();
            g.draw();
        }

        // Tear down ImGui before the Vulkan device/render pass it depends on go away.
        imguiLayer.shutdown();
        // Tear down the Vulkan renderer explicitly before Game destructor runs
        if (auto* r = dynamic_cast<Renderer*>(g.renderer())) r->destroy();

        std::cout << "Game closed.\n";
    } // <-- Game (and all engine Objects) destroyed here


    // After all game resources are destroyed: report any leaked Objects.
    Object::DumpLeaks();
    return 0;
}
