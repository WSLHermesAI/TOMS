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

    // Starts RmlUi (bgfx must be running). Desktop: defaultFont is registered as family "toms"
    // (`font-family: toms;` in RCSS) and as the fallback for characters other fonts lack. Web:
    // ignored -- text is drawn by the browser (rml_canvas_font.h), no font file is needed.
    bool init(int designW, int designH, const std::string& defaultFont, std::string& error);
    // The font family to use for a language (docs/08_RMLUI.md, "Fonts"). Desktop: registers
    // fontFile as "toms-<code>" the first time and returns that, or "toms" when fontFile is empty
    // or missing. Web: switches the browser font list to webFonts (CSS) and returns "toms".
    std::string languageFont(const std::string& code, const std::string& fontFile, const std::string& webFonts);
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
