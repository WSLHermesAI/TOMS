// main.cpp — Tower of the Sorcerer (Vulkan C++), cross-platform build.
// Loads assets, runs a scripted playthrough, and dumps PNG frames.
// Works headless on both Linux (lavapipe) and Windows (real GPU / SwiftShader).
#include "game.h"
#include "renderer.h"   // for dynamic_cast<Renderer*> batch metric
#include "object.h"     // for Object::DumpLeaks() at shutdown
#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    // asset dir: env override -> argv[1] -> "assets"; argv[2] = scenario mode
    std::string assetDir = "assets";
    std::string mode = "full";
    if (argc > 1) assetDir = argv[1];
    else if (const char* ad = std::getenv("ASSET_DIR")) assetDir = ad;
    if (argc > 2) mode = argv[2];

    // frames output dir (relative to CWD); create portably
    std::string framesDir = "frames";
    std::error_code ec;
    fs::create_directories(framesDir, ec);

    // Everything game-side lives in this block so Game (and all engine objects)
    // are fully destroyed before we dump the object-leak report at the end.
    {
        int frame = 0;
        auto shot = [&](Game& g, const std::string& name) {
            g.draw();
            g.saveFrame(framesDir + "/" + name + ".png");
            frame++;
        };

        Game g;
        // route engine logs to a file as well as stdout (optional; empty path disables)
        toms::Logger::instance().setFile("toms.log");
        toms::Logger::instance().setLevel(toms::LogLevel::Info);
        TOMS_LOG_INFO("TOMS engine start (C++{}, {} build)", __cplusplus/100,
#ifdef __EMSCRIPTEN__
            "web"
#else
            "native"
#endif
        );
        if (!g.loadAssets(assetDir)) { std::cerr << "asset load failed\n"; return 1; }
        if (const char* hm = std::getenv("TOMS_HIDE")) g.hideMask = std::atoi(hm);
        // Diagnostic split: if TOMS_SPLIT_NODE=1..4, emit only that node's quads.
        if (const char* sn = std::getenv("TOMS_SPLIT_NODE")) {
            if (auto* r = dynamic_cast<Renderer*>(g.renderer())) r->setNodeFilter((uint8_t)std::atoi(sn));
        }
        g.loadStage("stage01");

        std::cout << "loaded stage 1\n";
        shot(g, "frame01_stage1");

        // --- Stage 1: walk to villager (NPC) and talk ---
        g.movePlayer(1, 0);
        g.interact();              // talk to villager
        shot(g, "frame02_dialogue_elder");
        g.chooseDialogue(0);       // "我該怎麼做？" -> how
        shot(g, "frame03_dialogue_elder2");
        g.chooseDialogue(0);       // end
        shot(g, "frame04_after_talk");

        // --- Fight a slime: start combat directly (deterministic verification) ---
        {
            EnemyInst en;
            auto& t = g.enemyTemplate("slime");
            en.id = "slime"; en.name = t["name"]; en.hp = t["hp"]; en.atk = t["atk"]; en.def = t["def"];
            en.exp = t["exp"]; en.gold = t["gold"]; en.boss = false; en.x = 0; en.y = 0;
            g.startCombat(en);
        }
        shot(g, "frame05_combat_start");
        for (int r = 0; r < 60; r++) { g.update(700); if (!g.combat().active) break; if (r % 5 == 0) shot(g, "frame06_combat_r" + std::to_string(r)); }
        shot(g, "frame07_combat_done");

        // --- Stage 10 boss: start boss combat directly ---
        g.loadStage("stage10");
        {
            EnemyInst en;
            auto& t = g.enemyTemplate("demonlord_vorkath");
            en.id = "demonlord_vorkath"; en.name = t["name"]; en.hp = t["hp"]; en.atk = t["atk"]; en.def = t["def"];
            en.exp = t["exp"]; en.gold = t["gold"]; en.boss = true; en.x = 0; en.y = 0;
            g.startCombat(en);
        }
        shot(g, "frame08_boss_combat");
        for (int r = 0; r < 200; r++) { g.update(700); if (!g.combat().active) break; if (r % 20 == 0) shot(g, "frame09_boss_r" + std::to_string(r)); }
        // epilogue: defeating the boss routes to stage_11 (resolveCombatRound loads it on boss win).
        // Load it explicitly here to capture the extension stage for verification.
        g.loadStage("stage_11");
        shot(g, "frame10_stage11_epilogue");
        // walk to the princess and trigger the victory dialogue
        for (int i = 0; i < 9; i++) g.movePlayer(1, 0);   // right along row 9 to x=10
        for (int i = 0; i < 4; i++) g.movePlayer(0, -1);  // up to y=5 (princess cell)
        g.interact();
        shot(g, "frame11_princess_victory");

        // --- Inventory / item system demo ---
        g.loadStage("stage01");
        // simulate picking up usable items (normally done by walking onto item: tiles)
        g.player().inv.push_back("potion_red");
        g.player().inv.push_back("exp_up");
        g.player().inv.push_back("gem_atk");
        g.player().inv.push_back("scroll");
        g.toggleInventory();                       // open the 9-grid UI
        shot(g, "frame12_inventory_open");
        // move selection and use the exp_up item (should grant EXP and level up)
        g.invMoveSel(1, 0);                        // select slot 1 (exp_up)
        int lvBefore = g.player().lv;
        g.invUseSelected();                        // use exp_up
        shot(g, "frame13_after_use_exp");
        (void)lvBefore;
        // use a potion (heal)
        g.invMoveSel(-1, -1);                      // back to first slot
        g.invUseSelected();                        // use potion_red
        shot(g, "frame14_after_use_potion");
        g.toggleInventory();                       // close

        // report batch metrics (BatchRenderer) for verification
        if (auto* r = dynamic_cast<Renderer*>(g.renderer()))
            TOMS_LOG_INFO("batch: drawCalls={} quads={}", r->lastDrawCalls, r->lastQuadCount);

        // ---- Scenario modes (argv[2]): drive DIFFERENT player behavior, then exit ----
        // Each path intentionally touches movement / dialogue / combat / inventory in a
        // different way, so we can verify no scenario leaks an Object (e.g. a stray
        // GameObject left alive when the process ends).
        if (mode == "walk") {
            g.loadStage("stage01");
            for (int i = 0; i < 20; i++) { g.movePlayer(1, 0); g.movePlayer(0, 1); g.movePlayer(-1, 0); }
            g.toggleInventory(); g.toggleInventory();   // open+close UI while moving
            TOMS_LOG_INFO("scenario=walk done");
        } else if (mode == "combat") {
            g.loadStage("stage03");
            EnemyInst en;
            auto& t = g.enemyTemplate("slime");
            en.id="slime"; en.name=t["name"]; en.hp=t["hp"]; en.atk=t["atk"]; en.def=t["def"];
            en.exp=t["exp"]; en.gold=t["gold"]; en.x=0; en.y=0;
            g.startCombat(en);
            for (int r=0; r<120; r++) { g.update(700); if (!g.combat().active) break; }
            g.toggleInventory(); g.toggleInventory();
            TOMS_LOG_INFO("scenario=combat done (combat finished={})", !g.combat().active);
        } else if (mode == "dialogue") {
            g.loadStage("stage01");
            g.movePlayer(1,0);
            g.interact();                 // open dialogue
            g.chooseDialogue(0); g.chooseDialogue(0);
            g.interact();                 // re-open (may no-op if not on NPC) -> safe
            g.toggleInventory(); g.invMoveSel(1,0); g.invUseSelected(); g.toggleInventory();
            TOMS_LOG_INFO("scenario=dialogue done");
        } else if (mode == "store") {
            // ---- Store system verification (keyboard + mouse interactivity) ----
            // 1) load stage 3 -> unlock dialog pops (once)
            g.loadStage("stage03");
            g.draw(); g.saveFrame(framesDir + "/store01_unlock_dialog.png");
            // confirm the unlock dialog with a MOUSE CLICK on its confirm button.
            // unlock button center (from drawStoreUnlockDialog): bx=(W-420)/2=302, by=(H-180)/2=294,
            //   btnX=578, btnY=414, w=120,h=40 -> center (638,434)
            g.storeClick(638, 434);
            g.draw(); g.saveFrame(framesDir + "/store02_after_unlock.png");
            // 2) give gold (simulate kills), open the store by MOUSE-clicking the HUD icon
            g.player().gold = 50;
            g.draw();  // sets storeIconRect (top-right icon: x=W-56=968, y=14, s=42 -> center 989,35)
            g.storeClick(989, 35);
            g.draw(); g.saveFrame(framesDir + "/store03_open.png");
            // 3) buy HP potion (card 0) via MOUSE click on its buy button
            //    card rects: cw=300,ch=300,gap=24,n=3 -> ox=38, oy=214; btn center=(188,479)
            g.storeClick(188, 479);
            g.draw(); g.saveFrame(framesDir + "/store04_bought_hp.png");
            // 4) select STR card (keyboard '2') and buy via keyboard Enter
            g.storeKey('2'); g.storeKey(13);
            g.draw(); g.saveFrame(framesDir + "/store05_bought_str.png");
            // 5) not-enough-gold: drain gold, try to buy DEF (card 3) -> toast + shake, no purchase
            g.player().gold = 0;
            g.storeKey('3');  // select DEF card
            g.storeClick(188 + 324, 479);  // DEF card buy button (card index 2 -> +324 x)
            g.draw(); g.saveFrame(framesDir + "/store06_not_enough_gold.png");
            // 6) close via keyboard Esc
            g.storeKey(27);
            g.draw(); g.saveFrame(framesDir + "/store07_closed.png");
            TOMS_LOG_INFO("scenario=store done (unlocked={} storeOpen={})", g.storeUnlocked(), g.storeOpenFlag());
        } else if (mode == "shopshots") {
            // ---- Clean screenshots: HOW IT UNLOCKS + HOW IT LOOKS ----
            // (a) Before unlock: on stage 1 the HUD store icon is present but LOCKED (dimmed)
            g.loadStage("stage01");
            g.draw(); g.saveFrame(framesDir + "/shop_a_icon_locked_stage1.png");
            // (b) Entering stage 3 -> unlock dialog pops (once), with confirm button
            g.loadStage("stage03");
            g.draw(); g.saveFrame(framesDir + "/shop_b_unlock_dialog_stage3.png");
            // (c) Confirm unlock via the 確定 button (mouse click)
            g.storeClick(638, 434);
            g.draw(); g.saveFrame(framesDir + "/shop_c_after_confirm_icon_active.png");
            // (d) Open the store by clicking the now-active HUD icon (gold = 50)
            g.player().gold = 50;
            g.draw();  // sets storeIconRect (top-right icon center ~989,35)
            g.storeClick(989, 35);
            g.draw(); g.saveFrame(framesDir + "/shop_d_store_open.png");
            // (e) Not-enough-gold demo: drain gold, try to buy -> toast + shake
            g.player().gold = 0;
            g.storeKey('3');                 // select DEF card
            g.storeClick(188 + 324, 479);    // click its buy button -> blocked
            g.draw(); g.saveFrame(framesDir + "/shop_e_not_enough_gold.png");
            // (f) Give gold, buy the HP potion to show it applies immediately (price 2->4)
            g.player().gold = 30;
            g.storeKey('1'); g.storeKey(13); // buy HP potion via keyboard
            g.draw(); g.saveFrame(framesDir + "/shop_f_bought_hp.png");
            TOMS_LOG_INFO("scenario=shopshots done (unlocked={})", g.storeUnlocked());
        } else if (mode == "shopshots") {
            // ---- Clean screenshots: HOW IT UNLOCKS + HOW IT LOOKS (for user) ----
            // (a) Before unlock: on stage 1 the HUD store icon is present but LOCKED (dimmed, not clickable)
            g.loadStage("stage01");
            g.draw(); g.saveFrame(framesDir + "/shop_a_icon_locked_stage1.png");
            // (b) Entering stage 3 -> unlock dialog pops (once), with a 確定 confirm button
            g.loadStage("stage03");
            g.draw(); g.saveFrame(framesDir + "/shop_b_unlock_dialog_stage3.png");
            // (c) Confirm unlock via the 確定 button (mouse click) -> icon becomes active
            g.storeClick(638, 434);
            g.draw(); g.saveFrame(framesDir + "/shop_c_after_confirm_icon_active.png");
            // (d) Open the store by clicking the now-active HUD icon (gold = 50)
            g.player().gold = 50;
            g.draw();  // sets storeIconRect (top-right icon center ~989,35)
            g.storeClick(989, 35);
            g.draw(); g.saveFrame(framesDir + "/shop_d_store_open.png");
            // (e) Not-enough-gold demo: drain gold, try to buy -> red toast 金錢不足 + shake
            g.player().gold = 0;
            g.storeKey('3');                  // select DEF card
            g.storeClick(188 + 324, 479);     // click its buy button -> blocked
            g.draw(); g.saveFrame(framesDir + "/shop_e_not_enough_gold.png");
            // (f) Give gold, buy HP potion via keyboard -> applies immediately (price 2 -> 4 next)
            g.player().gold = 30;
            g.storeKey('1'); g.storeKey(13);
            g.draw(); g.saveFrame(framesDir + "/shop_f_bought_hp.png");
            TOMS_LOG_INFO("scenario=shopshots done (unlocked={})", g.storeUnlocked());
        } else if (mode == "text") {
            // ---- Minimal text diagnostic: is the font/UV pipeline correct? ----
            // Step 1: "hello world" centered (ASCII, should always work)
            g.renderer()->begin();
            g.renderer()->setNode(NODE_STAGE);
            {
                const std::string s = "hello world";
                float sz = 48;
                float x = (g.renderer()->width() - g.measureText(s, sz)) / 2.0f;
                float y = g.renderer()->height()/2.0f + sz*0.30f;   // y = baseline
                float t[4] = {1,1,1,1};
                g.drawTextPublic(s, x, y, sz, t);
            }
            g.renderer()->end();
            g.saveFrame(framesDir + "/text_hello.png");
            // Step 2: "你好世界" centered (Traditional Chinese) — the real test
            g.renderer()->begin();
            g.renderer()->setNode(NODE_STAGE);
            {
                const std::string s = "你好世界";
                float sz = 48;
                float x = (g.renderer()->width() - g.measureText(s, sz)) / 2.0f;
                float y = g.renderer()->height()/2.0f + sz*0.30f;   // y = baseline
                float t[4] = {1,1,1,1};
                g.drawTextPublic(s, x, y, sz, t);
            }
            g.renderer()->end();
            g.saveFrame(framesDir + "/text_nihao.png");
            TOMS_LOG_INFO("scenario=text done");
        } else if (mode == "padtest") {
            // Verify the web gamepad input path (handleTouch): single-step taps + dialogue nav.
            // Right=d3(224,604) Up=d0(140,546) Down=d1(140,632) A=d4(914,660) B=d5(824,598) I=d6(908,554)
            g.loadStage("stage01");
            // walk the player into open space so movement isn't wall-blocked
            g.movePlayer(1, 0); g.movePlayer(0, 1); g.movePlayer(0, 1);
            int y0 = g.player().y;
            // simulate a TAP: down + repeat + up. With the fix only phase 0 moves -> exactly 1 step.
            g.handleTouch(140, 632, 0);   // down
            g.handleTouch(140, 632, 1);   // repeat (would be the 2nd step in the old bug)
            g.handleTouch(140, 632, 2);   // up
            int y1 = g.player().y;
            std::cout << "TAP down moved dy=" << (y1 - y0) << " (expect 1 if single-step)\n";

            // Dialogue nav: open dialogue via interact on the villager, then drive gamepad dpad+A.
            g.loadStage("stage01");
            g.movePlayer(1, 0); g.interact();   // open villager dialogue
            std::cout << "inDialogue=" << g.inDialogueFlag() << " choices=" << g.dialogueChoiceCount() << "\n";
            int sel0 = g.dialogueSel();
            g.handleTouch(140, 632, 0);         // dpad down -> next choice
            std::cout << "dlgSel after down: " << sel0 << " -> " << g.dialogueSel() << "\n";
            g.handleTouch(914, 660, 0);         // A -> chooseDialogue(dlgSel)
            std::cout << "inDialogue after A=" << g.inDialogueFlag() << "\n";
            TOMS_LOG_INFO("scenario=padtest done");
            // heavy churn: many stage loads, combats, inventory toggles, item uses
            for (const char* s : {"stage01","stage03","stage05","stage07","stage10"}) {
                g.loadStage(s);
                g.toggleInventory(); g.invMoveSel(1,0); g.invUseSelected(); g.toggleInventory();
                EnemyInst en; auto& t = g.enemyTemplate("slime");
                en.id="slime"; en.name=t["name"]; en.hp=t["hp"]; en.atk=t["atk"]; en.def=t["def"];
                en.exp=t["exp"]; en.gold=t["gold"]; en.x=0; en.y=0;
                g.startCombat(en); for (int r=0;r<80;r++){ g.update(700); if(!g.combat().active) break; }
                g.movePlayer(1,0); g.movePlayer(0,1);
            }
            TOMS_LOG_INFO("scenario=stress done");
        }

        // Tear down the Vulkan renderer explicitly (frees atlases/pools/device) before
        // Game is destroyed so we don't leak GPU memory. Safe if the shared_ptr dtor
        // runs again (VulkanContext::destroy is NULL_HANDLE-guarded).
        if (auto* r = dynamic_cast<Renderer*>(g.renderer())) r->destroy();

        std::cout << "done. frames written: " << frame << "\n";
    } // <-- Game (and all engine Objects) destroyed here

    // After all game resources are destroyed: report any leaked Objects.
    Object::DumpLeaks();
    return 0;
}
