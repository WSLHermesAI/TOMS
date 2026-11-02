// game_ui.h -- the game's whole UI: RmlUi documents bound to Game (docs/gameplay/01_RMLUI.md).
//
// Every screen (title, HUD, pad, battle, dialogue, inventory, store, menus, stage select, ending,
// toasts) is an .rml/.rcss pair in assets/media/ui. They all bind one data model, "game", which is
// a toms::UiState (src/core/game/ui/ui_state.h) that Game::buildUiState() refills every frame, and
// every button calls act('<name>', arg) -> Game::uiEvent(name, arg). This class owns the model, the
// documents and which of them are showing; it has no game rules of its own.
#pragma once
#include "ui_state.h"

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

class Game;
namespace Rml { class Context; class ElementDocument; }

namespace toms::next {

class GameUi {
public:
    // Creates the data model and loads every document (hidden). uiDir = assets/media/ui.
    bool f_Init(Rml::Context* l_pCtx, Game* l_pGame, const std::string& l_strUiDir, std::string& l_strError);
    void f_Shutdown();

    // Refreshes the model from the game and shows the documents the game state calls for.
    // Call once per frame before RmlUi::update().
    void f_Sync();
    void f_Reload();   // re-read every .rml/.rcss from disk (F5)
    // Live UI editing (hot reload): when an .rml or .rcss in the UI folder changed on disk since the last call, reload
    // and return its name ("" = nothing changed; the first call only takes note of the files).
    std::string f_ReloadIfChanged();
    // The font family for every document (RmlUi::languageFont): overrides the RCSS "toms".
    void f_SetFontFamily(const std::string& l_strFamily);
    // A 3D scene is drawn behind the title (title.scene: the title's body turns see-through).
    void f_SetTitleScene(bool l_bOn) { m_bTitleScene = l_bOn; }

private:
    bool f_LoadDocuments(std::string& l_strError);
    // Hands RmlUi the game's sprite sheet ("_atlas.rcss", made from the atlas the map uses).
    void f_PublishSprites();

    struct Doc { std::string m_strFile; Rml::ElementDocument* m_pDoc = nullptr; bool m_bShown = false; };
    Rml::Context* m_pCtx = nullptr;
    Game* m_pGame = nullptr;
    std::string m_strDir;
    std::vector<Doc> m_vDocs;
    toms::UiState m_oState;
    bool m_bTitleScene = false;
    bool m_bModel = false;
    std::string m_strFamily = "toms";
    int m_iSpriteRevision = -1;   // Game::spriteAtlasRevision() the documents were loaded with
    std::map<std::string, std::filesystem::file_time_type> m_mapStamps;   // reloadIfChanged: each UI file's last write
    bool m_bStamped = false;
    std::string m_strPlayerSelKey;  // the player menu's selection last frame (scroll a new one into view)
    int m_iScrollFrames = 0;      // frames left to bring it into view (it is laid out a frame later)
};

}  // namespace toms::next
