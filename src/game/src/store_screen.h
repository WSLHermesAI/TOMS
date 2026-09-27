// store_screen.h -- the item store drawn with RmlUi (assets/media/ui/store.rml + store.rcss).
//
// A test of RmlUi as the game's UI layer (docs/08_RMLUI.md). The screen owns the "store" data
// model: every frame it copies the store state out of Game (title, gold, tabs, the cards of the
// current tab, the toast), and its events call Game's store functions (select tab, buy, close).
// Game keeps all the rules, so this UI and the old hand-drawn one (Game::drawStoreUI) buy and
// price items identically; F4 switches between them.
#pragma once
#include <string>

class Game;
namespace Rml { class Context; class ElementDocument; }

namespace toms::next {

class StoreScreen {
public:
    // Creates the data model and loads the document (hidden). uiDir = assets/media/ui.
    bool init(Rml::Context* ctx, Game* game, const std::string& uiDir, std::string& error);
    void shutdown();

    // Shows/hides the document with the store and refreshes the bound values. Call once per frame
    // before RmlUi::update().
    void sync();
    bool visible() const { return visible_; }
    void reload();   // re-read store.rml / store.rcss from disk (F5)

private:
    struct Model;
    Model* model_ = nullptr;
    Rml::Context* ctx_ = nullptr;
    Rml::ElementDocument* doc_ = nullptr;
    Game* game_ = nullptr;
    std::string path_;
    bool visible_ = false;
};

}  // namespace toms::next
