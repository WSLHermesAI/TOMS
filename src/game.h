// game.h — game logic: player, movement, auto combat, talking, stage flow, draw.
#pragma once
#include <vector>
#include <string>
#include <map>
#include "renderer.h"
#include "stage.h"
#include "Audio.h"

struct Player {
    int hp, maxhp, atk, def, gold, exp, lv;
    int key_yellow = 0, key_blue = 0, key_red = 0;
    int x = 1, y = 1;
};

struct EnemyInst { std::string id; std::string name; int hp, atk, def, exp, gold; int x, y; bool boss=false; };

struct DialogueNode { std::string text; std::vector<std::pair<std::string,std::string>> choices; };

struct CombatState {
    EnemyInst enemy;
    int playerHP, enemyHP;
    int round = 0;
    bool active = false;
    int ticks = 0;            // ms accumulator
    std::string log;          // last exchange text
    bool won = false;
};

class Game {
public:
    bool loadAssets(const std::string& assetDir);
    void loadStage(const std::string& id);
    void update(int dtMs);                 // advances combat timer etc.
    void draw();                          // render current frame
    void saveFrame(const std::string& path);
    // input (scripted for headless)
    void movePlayer(int dx, int dy);
    void interact();                       // talk to NPC / trigger dialogue on current cell
    void chooseDialogue(int idx);         // pick a dialogue choice
    void startCombat(const EnemyInst& e);
    Player& player() { return pl; }
    CombatState& combat() { return cs; }
    Stage& stage() { return st; }
    const nlohmann::json& enemyTemplate(const std::string& id) const { return enemyTpl.at(id); }
private:
    void drawText(const std::string& s, float x, float y, float size, const float tint[4]);
    void drawBar(float x, float y, float w, float h, float frac, const float col[4]);
    Quad spriteQuad(float x, float y, float w, float h, int layer, const float tint[4]);
    void spriteUV(int layer, float uv[4]) const;
    int spriteLayer(const std::string& id) const; // index into sprite grid
    void applyItem(const std::string& id);
    void resolveCombatRound();
    void startDialogue(const std::string& npc);
    void enterNode(const std::string& node);

    Renderer ren;
    Player pl;
    Stage st;
    CombatState cs;
    // sprite layer registry (order matches loadAssets)
    std::vector<std::string> spriteIds;
    std::map<std::string,int> idToLayer;
    int spriteGridCols = 9;
    // font atlas map: codepoint -> uv rect
    std::map<uint32_t, std::array<float,4>> fontMap;
    int fontCols = 32, fontCell = 32, fontW = 0, fontH = 0;
    // dialogue state
    bool inDialogue = false;
    std::string dlgNpc;
    std::string dlgNode = "root";
    nlohmann::json dlgData;
    std::vector<std::pair<std::string,std::string>> dlgChoices; // label,next
    // enemy templates
    std::map<std::string, nlohmann::json> enemyTpl;
    // current stage id
    std::string curStage;
    std::string dataDir;
    int totalStages = 10;   // highest stage index (derived from data/stages at loadStage)
    Audio audio;           // SFX subsystem (no-op when no audio device)
};
