// main.cpp - Tower of the Sorcerer (Vulkan), windowed interactive build.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "game.h"
#include "renderer.h"   // for dynamic_cast<Renderer*> + VulkanContext
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
    // In debug mode, set working directory to the assets folder
    //fs::path assetsPath = fs::path(assetDir).is_absolute()
    //    ? fs::path(assetDir)
    //    : cwd / assetDir;
    fs::path assetsPath = "../../assets";
    fs::current_path(assetsPath, ec);
    if (ec)
        std::cerr << "[WARN] Failed to set CWD to assets folder: " << ec.message() << '\n';
    else
        std::cout << "[CWD] Changed to assets folder: " << assetsPath.string() << '\n';
    // Adjust paths that were relative to the original CWD
    assetDir = ".";
    (void)cwd;
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
        std::cout << "Loaded stage01. Use arrow keys to move, Enter to interact, I for inventory, Escape to quit.\n";

        // Retrieve the GLFW window from the renderer for key polling
        GLFWwindow* win = nullptr;
        if (auto* r = dynamic_cast<Renderer*>(g.renderer())) win = r->vk.window;

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

            // Keyboard â†’ game actions (one action per key press)
            if (keyPressed(GLFW_KEY_ESCAPE)) break;
            if (keyPressed(GLFW_KEY_UP)    || keyPressed(GLFW_KEY_W)) g.movePlayer(0, -1);
            if (keyPressed(GLFW_KEY_DOWN)  || keyPressed(GLFW_KEY_S)) g.movePlayer(0,  1);
            if (keyPressed(GLFW_KEY_LEFT)  || keyPressed(GLFW_KEY_A)) g.movePlayer(-1, 0);
            if (keyPressed(GLFW_KEY_RIGHT) || keyPressed(GLFW_KEY_D)) g.movePlayer( 1, 0);
            if (keyPressed(GLFW_KEY_ENTER) || keyPressed(GLFW_KEY_SPACE)) {
                if (g.inDialogueFlag()) g.chooseDialogue(g.dialogueSel());
                else g.interact();
            }
            if (keyPressed(GLFW_KEY_I)) g.toggleInventory();
            // Inventory cursor
            if (g.inventoryOpen()) {
                if (keyPressed(GLFW_KEY_UP)    || keyPressed(GLFW_KEY_W)) g.invMoveSel(0, -1);
                if (keyPressed(GLFW_KEY_DOWN)  || keyPressed(GLFW_KEY_S)) g.invMoveSel(0,  1);
                if (keyPressed(GLFW_KEY_LEFT)  || keyPressed(GLFW_KEY_A)) g.invMoveSel(-1, 0);
                if (keyPressed(GLFW_KEY_RIGHT) || keyPressed(GLFW_KEY_D)) g.invMoveSel( 1, 0);
                if (keyPressed(GLFW_KEY_ENTER)) g.invUseSelected();
            }
            // Store keys
            if (g.storeModal()) {
                for (int k = '1'; k <= '9'; k++)
                    if (keyPressed(k)) g.storeKey((char)k);
                if (keyPressed(GLFW_KEY_ENTER)) g.storeKey(13);
                if (keyPressed(GLFW_KEY_ESCAPE)) g.storeKey(27);
            }

            // Acquire next swapchain image then draw
            if (auto* r = dynamic_cast<Renderer*>(g.renderer())) {
                if (!r->vk.acquireNext()) continue;  // skip frame if swapchain not ready
            }
            g.draw();
        }

        // Tear down the Vulkan renderer explicitly before Game destructor runs
        if (auto* r = dynamic_cast<Renderer*>(g.renderer())) r->destroy();

        std::cout << "Game closed.\n";
    } // <-- Game (and all engine Objects) destroyed here


    // After all game resources are destroyed: report any leaked Objects.
    Object::DumpLeaks();
    return 0;
}
