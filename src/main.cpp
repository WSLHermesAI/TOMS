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
        } else if (mode == "stress") {
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

        std::cout << "done. frames written: " << frame << "\n";
    } // <-- Game (and all engine Objects) destroyed here

    // After all game resources are destroyed: report any leaked Objects.
    Object::DumpLeaks();
    return 0;
}
