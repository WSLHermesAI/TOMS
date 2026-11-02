// rml_ui.h -- RmlUi (HTML/CSS-like UI) drawn with bgfx. See docs/gameplay/01_RMLUI.md.
//
// One RmlUi context sized to the game's design resolution (1024x768). The host tells it where the
// letterboxed game image is on screen (setViewport) and renders it into its own bgfx view after the
// game view, so UI documents cover the game exactly like the old immediate-mode screens did.
// Like the other engine headers this one does not include bgfx (bx needs C++20; the game code is
// C++17); RmlUi's own headers are C++17.
#pragma once
#include <RmlUi/Core.h>

#include <cstdint>
#include <functional>
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
    bool f_Init(int l_iDesignW, int l_iDesignH, const std::string& l_strDefaultFont, std::string& l_strError);
    // The font family to use for a language (docs/gameplay/01_RMLUI.md, "Fonts"). Desktop: registers
    // fontFile as "toms-<code>" the first time and returns that, or "toms" when fontFile is empty
    // or missing. Web: switches the browser font list to webFonts (CSS) and returns "toms".
    std::string f_LanguageFont(const std::string& l_strCode, const std::string& l_strFontFile, const std::string& l_strWebFonts);
    void f_Shutdown();                     // closes all documents; call before bgfx::shutdown
    bool f_Ready() const;

    Rml::Context* f_Context();

    // Letterboxed game rectangle in backbuffer pixels (BgfxRenderer::computeAspectFitViewport).
    void f_SetViewport(float l_fX, float l_fY, float l_fW, float l_fH);
    // The design canvas changed (canvas.h: the screen turned or was resized). The documents lay out again at the new
    // size, and their @media rules (orientation: portrait, ...) are matched again.
    void f_SetDesignSize(int l_iDesignW, int l_iDesignH);
    void f_Update(double l_dTimeSeconds);     // animations, data bindings, layout
    void f_Render(uint16_t l_u16ViewId);        // submits the UI to this bgfx view

    // A document made in memory: any RmlUi file request whose file name is `fileName` (whatever
    // folder it is linked from) reads `text` instead of a file. The game serves its sprite sheet
    // this way (GameUi, "_atlas.rcss"). Takes effect for documents loaded afterwards.
    static void f_SetVirtualFile(const std::string& l_strFileName, const std::string& l_strText);
    // The file is read from disk again (the UI editor: the text was saved, or its changes dropped).
    static void f_ClearVirtualFile(const std::string& l_strFileName);
    // Every RmlUi log message (level: 0 info, 1 warning, 2 error) also goes here, besides stderr; nullptr = none.
    // The UI editor lists the warnings and errors of the documents it previews.
    static void f_SetLogListener(std::function<void(int l_iLevel, const std::string& l_strMessage)> l_fnListener);

    void f_ToggleDebugger();               // RmlUi's element/style inspector
    bool f_DebuggerVisible() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_upImpl;
};

}  // namespace toms::next
