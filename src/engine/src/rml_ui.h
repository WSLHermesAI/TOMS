// rml_ui.h -- RmlUi (HTML/CSS-like UI) drawn with bgfx. See docs/08_RMLUI.md.
//
// One RmlUi context sized to the game's design resolution (1024x768). The host tells it where the
// letterboxed game image is on screen (setViewport) and renders it into its own bgfx view after the
// game view, so UI documents cover the game exactly like the old immediate-mode screens did.
// Like the other engine headers this one does not include bgfx (bx needs C++20; the game code is
// C++17); RmlUi's own headers are C++17.
#pragma once
#include <RmlUi/Core.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toms::next {

class RmlUi {
public:
    RmlUi();
    ~RmlUi();
    RmlUi(const RmlUi&) = delete;
    RmlUi& operator=(const RmlUi&) = delete;

    // Starts RmlUi (bgfx must be running). fontFiles[0] is registered as family "toms" (use
    // `font-family: toms;` in RCSS); the others are fallbacks for glyphs the first one lacks
    // (.ttc collections load face 0). Missing files are skipped; at least one must load.
    bool init(int designW, int designH, const std::vector<std::string>& fontFiles, std::string& error);
    void shutdown();                     // closes all documents; call before bgfx::shutdown
    bool ready() const;

    Rml::Context* context();

    // Letterboxed game rectangle in backbuffer pixels (BgfxRenderer::computeAspectFitViewport).
    void setViewport(float x, float y, float w, float h);
    void update(double timeSeconds);     // animations, data bindings, layout
    void render(uint16_t viewId);        // submits the UI to this bgfx view

    void toggleDebugger();               // RmlUi's element/style inspector
    bool debuggerVisible() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace toms::next
