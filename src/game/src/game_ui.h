// game_ui.h -- the game's whole UI: RmlUi documents bound to Game (docs/08_RMLUI.md).
//
// Every screen (title, HUD, pad, battle, dialogue, inventory, store, menus, stage select, ending,
// toasts) is an .rml/.rcss pair in assets/media/ui. They all bind one data model, "game", which is
// a toms::UiState (src/core/game/ui/ui_state.h) that Game::buildUiState() refills every frame, and
// every button calls act('<name>', arg) -> Game::uiEvent(name, arg). This class owns the model, the
// documents and which of them are showing; it has no game rules of its own.
#pragma once
#include "ui_state.h"

#include <string>
#include <utility>
#include <vector>

class Game;
namespace Rml { class Context; class ElementDocument; }

namespace toms::next {

class GameUi {
public:
    // Creates the data model and loads every document (hidden). uiDir = assets/media/ui.
    bool init(Rml::Context* ctx, Game* game, const std::string& uiDir, std::string& error);
    void shutdown();

    // Refreshes the model from the game and shows the documents the game state calls for.
    // Call once per frame before RmlUi::update().
    void sync();
    void reload();   // re-read every .rml/.rcss from disk (F5)
    // The font family for every document (RmlUi::languageFont): overrides the RCSS "toms".
    void setFontFamily(const std::string& family);

private:
    bool loadDocuments(std::string& error);
    // Hands RmlUi the game's sprite sheet ("_atlas.rcss", made from the atlas the map uses).
    void publishSprites();

    struct Doc { std::string file; Rml::ElementDocument* doc = nullptr; bool shown = false; };
    Rml::Context* ctx_ = nullptr;
    Game* game_ = nullptr;
    std::string dir_;
    std::vector<Doc> docs_;
    toms::UiState state_;
    bool model_ = false;
    std::string family_ = "toms";
    int spriteRevision_ = -1;   // Game::spriteAtlasRevision() the documents were loaded with
};

}  // namespace toms::next
