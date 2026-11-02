// rml_ui.cpp -- see rml_ui.h. RmlUi's render and system interfaces implemented on bgfx.
#include "rml_ui.h"
#include "color_space.h"   // TOMS_GAMMA_SPACE: web and Android blend in gamma space
#include "embedded_shaders.h"

#include "../../core/engine/vfs.h"   // vfsReadAll: APK entries on Android, stdio elsewhere
#include "shared_textures.h"         // the sprite atlas the map renderer lends to the UI
#include <RmlUi/Debugger.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>

#ifdef __EMSCRIPTEN__
#include "rml_canvas_font.h"
#endif

// A private stb_image copy for UI images (the core game code owns the public one).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace toms::next {
namespace {

bgfx::VertexLayout& f_RmlLayout() {
    // Must match Rml::Vertex: Vector2f position, ColourbPremultiplied colour, Vector2f tex_coord.
    static bgfx::VertexLayout l_oLayout;
    static bool l_bReady = false;
    if (!l_bReady) {
        l_oLayout.begin()
            .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        l_bReady = true;
    }
    return l_oLayout;
}

struct Geometry {
    bgfx::VertexBufferHandle m_oVbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_oIbh = BGFX_INVALID_HANDLE;
};

class BgfxRenderInterface final : public Rml::RenderInterface {
public:
    float m_fDesignW = 1024, m_fDesignH = 768;
    float m_fVpX = 0, m_fVpY = 0, m_fVpW = 1024, m_fVpH = 768;   // letterbox rect in device pixels
    uint16_t m_u16View = 2;
    bgfx::ProgramHandle m_oProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_oSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_oParams = BGFX_INVALID_HANDLE;   // u_rmlParams, see vs_rml.sc
    bgfx::TextureHandle m_oWhite = BGFX_INVALID_HANDLE;

    bool f_Create() {
        static_assert(sizeof(Rml::Vertex) == 20, "Rml::Vertex layout changed; update rmlLayout()");
        m_oProgram = f_CreateEmbeddedProgram(ShaderProgram::RmlUi);
        m_oSampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
        m_oParams = bgfx::createUniform("u_rmlParams", bgfx::UniformType::Vec4);
        const uint32_t l_uPx = 0xffffffff;
        m_oWhite = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&l_uPx, 4));
        return bgfx::isValid(m_oProgram);
    }
    void f_Destroy() {
        if (bgfx::isValid(m_oProgram)) bgfx::destroy(m_oProgram);
        if (bgfx::isValid(m_oSampler)) bgfx::destroy(m_oSampler);
        if (bgfx::isValid(m_oParams)) bgfx::destroy(m_oParams);
        if (bgfx::isValid(m_oWhite)) bgfx::destroy(m_oWhite);
        m_oProgram = BGFX_INVALID_HANDLE; m_oSampler = BGFX_INVALID_HANDLE; m_oParams = BGFX_INVALID_HANDLE;
        m_oWhite = BGFX_INVALID_HANDLE;
    }

    // ---- geometry ----
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> l_oVertices, Rml::Span<const int> l_oIndices) override {
        auto* l_pG = new Geometry();
        l_pG->m_oVbh = bgfx::createVertexBuffer(bgfx::copy(l_oVertices.data(), (uint32_t)(l_oVertices.size() * sizeof(Rml::Vertex))), f_RmlLayout());
        l_pG->m_oIbh = bgfx::createIndexBuffer(bgfx::copy(l_oIndices.data(), (uint32_t)(l_oIndices.size() * sizeof(int))), BGFX_BUFFER_INDEX32);
        return reinterpret_cast<Rml::CompiledGeometryHandle>(l_pG);
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle l_u64Handle) override {
        auto* l_pG = reinterpret_cast<Geometry*>(l_u64Handle);
        if (bgfx::isValid(l_pG->m_oVbh)) bgfx::destroy(l_pG->m_oVbh);
        if (bgfx::isValid(l_pG->m_oIbh)) bgfx::destroy(l_pG->m_oIbh);
        delete l_pG;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle l_u64Handle, Rml::Vector2f l_oTranslation, Rml::TextureHandle l_u64Texture) override {
        auto* l_pG = reinterpret_cast<Geometry*>(l_u64Handle);
        float l_arrModel[16], l_arrMove[16];
        bx::mtxTranslate(l_arrMove, l_oTranslation.x, l_oTranslation.y, 0.0f);
        if (m_bHasTransform) bx::mtxMul(l_arrModel, l_arrMove, m_arrTransform);   // translate first, then the element transform
        else std::copy(l_arrMove, l_arrMove + 16, l_arrModel);
        bgfx::setTransform(l_arrModel);
        bgfx::setVertexBuffer(0, l_pG->m_oVbh);
        bgfx::setIndexBuffer(l_pG->m_oIbh);
        bgfx::TextureHandle l_oTex = l_u64Texture ? bgfx::TextureHandle{(uint16_t)(l_u64Texture - 1)} : m_oWhite;
        bgfx::setTexture(0, m_oSampler, l_oTex);
        // The desktop backbuffer is sRGB (bgfx_host.cpp) and RCSS colours are sRGB, so the shader
        // linearizes them; the web's and Android's are not (color_space.h), so they pass through.
#if TOMS_GAMMA_SPACE
        float l_arrP[4] = {0, 0, 0, 0};
#else
        float l_arrP[4] = {1, 0, 0, 0};
#endif
        l_arrP[1] = m_setBorrowed.count(l_u64Texture) ? 1.0f : 0.0f;   // straight alpha: fs_rml premultiplies it
        bgfx::setUniform(m_oParams, l_arrP);
        if (m_bScissorOn) {
            // RmlUi gives the region in UI pixels; bgfx wants backbuffer pixels.
            const float l_fSx = m_fVpW / m_fDesignW, l_fSy = m_fVpH / m_fDesignH;
            const float l_fX0 = m_fVpX + m_oScissor.Left() * l_fSx, l_fY0 = m_fVpY + m_oScissor.Top() * l_fSy;
            const float l_fW = m_oScissor.Width() * l_fSx, l_fH = m_oScissor.Height() * l_fSy;
            bgfx::setScissor((uint16_t)std::max(0.0f, std::floor(l_fX0)), (uint16_t)std::max(0.0f, std::floor(l_fY0)),
                             (uint16_t)std::max(0.0f, std::ceil(l_fW)), (uint16_t)std::max(0.0f, std::ceil(l_fH)));
        }
        // Premultiplied alpha (RmlUi's colours and generated textures are premultiplied).
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
        bgfx::submit(m_u16View, m_oProgram);
    }

    // ---- textures ----
    // Textures borrowed from the map renderer (shared_textures.h): never destroyed here, and drawn
    // with u_rmlParams.y = 1 because their alpha is straight.
    std::set<Rml::TextureHandle> m_setBorrowed;
    int m_iLentVersion = 0;   // lentTexturesVersion() when `borrowed` was last valid

    Rml::TextureHandle LoadTexture(Rml::Vector2i& l_oDimensions, const Rml::String& l_strSource) override {
        if (const SharedTexture* l_pS = f_FindLentTexture(l_strSource)) {   // the sprite atlas: already on the GPU
            l_oDimensions = {l_pS->m_iW, l_pS->m_iH};
            const Rml::TextureHandle l_u64T = (Rml::TextureHandle)l_pS->m_u16Handle + 1;
            if (m_setBorrowed.insert(l_u64T).second)
                Rml::Log::Message(Rml::Log::LT_INFO, "%s: the map's texture, shared (%dx%d)", l_strSource.c_str(), l_pS->m_iW, l_pS->m_iH);
            return l_u64T;
        }
        int l_iW = 0, l_iH = 0, l_iN = 0;
        std::string l_strFile;   // vfs: APK entries on Android
        stbi_uc* l_pPx = toms::f_VfsReadAll(l_strSource, l_strFile)
            ? stbi_load_from_memory((const stbi_uc*)l_strFile.data(), (int)l_strFile.size(), &l_iW, &l_iH, &l_iN, 4) : nullptr;
        if (!l_pPx) {
            Rml::Log::Message(Rml::Log::LT_WARNING, "image not found: %s", l_strSource.c_str());
            return 0;
        }
        for (int l_iI = 0; l_iI < l_iW * l_iH; ++l_iI) {   // RmlUi expects premultiplied alpha
            stbi_uc* l_pP = l_pPx + l_iI * 4;
            l_pP[0] = (stbi_uc)(l_pP[0] * l_pP[3] / 255); l_pP[1] = (stbi_uc)(l_pP[1] * l_pP[3] / 255); l_pP[2] = (stbi_uc)(l_pP[2] * l_pP[3] / 255);
        }
        l_oDimensions = {l_iW, l_iH};
        // Images are the game's pixel art: point sampling keeps them crisp when scaled up. sRGB like
        // the game's own atlases (bgfx_renderer.cpp), so icons look the same in both UIs.
        uint64_t l_u64Flags = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
#if !TOMS_GAMMA_SPACE
        l_u64Flags |= BGFX_TEXTURE_SRGB;
#endif
        bgfx::TextureHandle l_oT = bgfx::createTexture2D((uint16_t)l_iW, (uint16_t)l_iH, false, 1, bgfx::TextureFormat::RGBA8,
                                                      l_u64Flags,
                                                      bgfx::copy(l_pPx, (uint32_t)(l_iW * l_iH * 4)));
        stbi_image_free(l_pPx);
        return bgfx::isValid(l_oT) ? (Rml::TextureHandle)l_oT.idx + 1 : 0;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> l_oSource, Rml::Vector2i l_oDim) override {
        // Font glyph atlases and other generated images: smooth sampling.
        bgfx::TextureHandle l_oT = bgfx::createTexture2D((uint16_t)l_oDim.x, (uint16_t)l_oDim.y, false, 1, bgfx::TextureFormat::RGBA8,
                                                      BGFX_SAMPLER_UVW_CLAMP, bgfx::copy(l_oSource.data(), (uint32_t)l_oSource.size()));
        return bgfx::isValid(l_oT) ? (Rml::TextureHandle)l_oT.idx + 1 : 0;
    }
    void ReleaseTexture(Rml::TextureHandle l_u64Texture) override {
        if (m_setBorrowed.erase(l_u64Texture)) return;   // the map renderer owns it
        if (l_u64Texture) bgfx::destroy(bgfx::TextureHandle{(uint16_t)(l_u64Texture - 1)});
    }

    // ---- scissor / transform (clip masks, layers and filters are not implemented) ----
    void EnableScissorRegion(bool l_bEnable) override { m_bScissorOn = l_bEnable; }
    void SetScissorRegion(Rml::Rectanglei l_oRegion) override { m_oScissor = l_oRegion; }
    void SetTransform(const Rml::Matrix4f* l_pT) override {
        m_bHasTransform = l_pT != nullptr;
        if (l_pT) std::copy(l_pT->data(), l_pT->data() + 16, m_arrTransform);
    }

private:
    bool m_bScissorOn = false;
    Rml::Rectanglei m_oScissor;
    bool m_bHasTransform = false;
    float m_arrTransform[16] = {};
};

// RmlUi::setLogListener: the UI editor's Problems list.
std::function<void(int, const std::string&)>& f_LogListener() {
    static std::function<void(int, const std::string&)> l_fnListener;
    return l_fnListener;
}

// Documents made in memory (RmlUi::setVirtualFile), by file name.
std::map<std::string, std::string>& f_VirtualFiles() {
    static std::map<std::string, std::string> l_mapFiles;
    return l_mapFiles;
}

// RmlUi reads its documents (.rml/.rcss) and its fonts through this interface, on every platform:
// vfsReadAll sees inside the APK on Android (RmlUi's default stdio reader cannot) and is plain C stdio
// elsewhere, and the game's in-memory sprite sheet (virtualFiles) is served from here.
class FileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& l_strPath) override {
        std::string l_strBuf;
        const size_t l_nSlash = l_strPath.find_last_of("/\\");
        auto l_itV = f_VirtualFiles().find(l_nSlash == std::string::npos ? l_strPath : l_strPath.substr(l_nSlash + 1));
        if (l_itV != f_VirtualFiles().end()) l_strBuf = l_itV->second;
        else if (!toms::f_VfsReadAll(l_strPath, l_strBuf)) return 0;   // 0 == failed open, RmlUi's contract
        return (Rml::FileHandle) new Entry{std::move(l_strBuf), 0};
    }
    void Close(Rml::FileHandle l_u64File) override { delete (Entry*)l_u64File; }

    size_t Read(void* l_pBuffer, size_t l_nSize, Rml::FileHandle l_u64File) override {
        Entry* l_pE = (Entry*)l_u64File;
        if (!l_pE || l_pE->m_nPos >= l_pE->m_strData.size()) return 0;
        const size_t l_nN = l_nSize < (l_pE->m_strData.size() - l_pE->m_nPos) ? l_nSize : (l_pE->m_strData.size() - l_pE->m_nPos);
        std::memcpy(l_pBuffer, l_pE->m_strData.data() + l_pE->m_nPos, l_nN);
        l_pE->m_nPos += l_nN;
        return l_nN;
    }
    bool Seek(Rml::FileHandle l_u64File, long l_iOffset, int l_iOrigin) override {
        Entry* l_pE = (Entry*)l_u64File;
        if (!l_pE) return false;
        const long l_iBase = l_iOrigin == SEEK_SET ? 0L : l_iOrigin == SEEK_CUR ? (long)l_pE->m_nPos : (long)l_pE->m_strData.size();
        const long l_iTarget = l_iBase + l_iOffset;
        if (l_iTarget < 0 || (size_t)l_iTarget > l_pE->m_strData.size()) return false;
        l_pE->m_nPos = (size_t)l_iTarget;
        return true;
    }
    size_t Tell(Rml::FileHandle l_u64File) override { Entry* l_pE = (Entry*)l_u64File; return l_pE ? l_pE->m_nPos : 0; }

private:
    struct Entry { std::string m_strData; size_t m_nPos = 0; };
};

class SystemInterface final : public Rml::SystemInterface {
public:
    double m_dNow = 0;
    double GetElapsedTime() override { return m_dNow; }
    bool LogMessage(Rml::Log::Type l_eType, const Rml::String& l_strMessage) override {
        const char* l_pszTag = l_eType == Rml::Log::LT_ERROR || l_eType == Rml::Log::LT_ASSERT ? "error"
                        : l_eType == Rml::Log::LT_WARNING ? "warning" : "info";
        std::fprintf(stderr, "[rmlui %s] %s\n", l_pszTag, l_strMessage.c_str());
        if (f_LogListener())
            f_LogListener()(l_eType == Rml::Log::LT_ERROR || l_eType == Rml::Log::LT_ASSERT ? 2 : l_eType == Rml::Log::LT_WARNING ? 1 : 0, l_strMessage);
        return true;
    }
};

}  // namespace

struct RmlUi::Impl {
    BgfxRenderInterface m_oRender;
    SystemInterface m_oSystem;
    FileInterface m_oFile;
    Rml::Context* m_pContext = nullptr;
    bool m_bDebugger = false;
#ifdef __EMSCRIPTEN__
    CanvasFontEngine m_oCanvasFont;         // web: the browser draws the text (rml_canvas_font.h)
#else
    std::set<std::string> m_setLoadedFamilies;   // desktop: language fonts already registered
    std::vector<Rml::byte> m_vSystemFont;      // the system fallback font's bytes: RmlUi reads them until Rml::Shutdown
#endif
};

#ifndef __EMSCRIPTEN__
namespace {
// A system CJK font as the LAST fallback face, after the bundled one: a character the bundled subset lacks
// (new text, and tools/make_ui_font.py not run again) is drawn from it instead of as an empty box. RmlUi tries the
// fallback faces in the order they were loaded, so the bundled font always wins and screenshots do not change.
// Only one file is loaded (they are 10-25 MB, read into memory). TOMS_SYSTEM_FONT=<path>[#face] picks another file,
// TOMS_SYSTEM_FONT=none turns it off. Never shipped: it is read from the player's own system.
struct SystemFontFile { std::string m_strPath; int m_iFace; };

std::vector<SystemFontFile> f_SystemFontCandidates() {
    std::vector<SystemFontFile> l_vOut;
    if (const char* l_pszEnv = std::getenv("TOMS_SYSTEM_FONT"); l_pszEnv && *l_pszEnv) {
        std::string l_strS = l_pszEnv;
        if (l_strS == "none") return l_vOut;
        int l_iFace = 0;
        if (const size_t l_nHash = l_strS.rfind('#'); l_nHash != std::string::npos) { l_iFace = std::atoi(l_strS.c_str() + l_nHash + 1); l_strS.resize(l_nHash); }
        l_vOut.push_back({l_strS, l_iFace});
        return l_vOut;
    }
#if defined(_WIN32)
    const char* l_pszWin = std::getenv("WINDIR");
    const std::string l_strFonts = std::string(l_pszWin && *l_pszWin ? l_pszWin : "C:\\Windows") + "\\Fonts\\";
    // Microsoft JhengHei (Traditional Chinese, Windows 7+), then YaHei (Simplified), MingLiU, SimSun
    for (const char* l_pszF : {"msjh.ttc", "msyh.ttc", "mingliu.ttc", "simsun.ttc"}) l_vOut.push_back({l_strFonts + l_pszF, 0});
#elif defined(__ANDROID__)
    // Android 7+: Noto Sans CJK, one collection; face 3 is Traditional Chinese (0 JP, 1 KR, 2 SC, 3 TC, 4 HK).
    l_vOut.push_back({"/system/fonts/NotoSansCJK-Regular.ttc", 3});
    l_vOut.push_back({"/system/fonts/NotoSansCJKtc-Regular.otf", 0});
    l_vOut.push_back({"/system/fonts/DroidSansFallbackFull.ttf", 0});   // older Android
    l_vOut.push_back({"/system/fonts/DroidSansFallback.ttf", 0});
#else
    out.push_back({"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 3});
    out.push_back({"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 3});
#endif
    return l_vOut;
}

// Loads the first candidate that exists and that RmlUi accepts. Returns false (and keeps nothing) when none does.
bool f_LoadSystemFallbackFont(std::vector<Rml::byte>& l_vKeep) {
    for (const SystemFontFile& l_oF : f_SystemFontCandidates()) {
        std::FILE* l_pFp = std::fopen(l_oF.m_strPath.c_str(), "rb");   // an absolute system path: not through the APK file interface
        if (!l_pFp) continue;
        std::vector<Rml::byte> l_vBytes;
        std::fseek(l_pFp, 0, SEEK_END);
        const long l_iSize = std::ftell(l_pFp);
        std::fseek(l_pFp, 0, SEEK_SET);
        if (l_iSize > 0) {
            l_vBytes.resize((size_t)l_iSize);
            if (std::fread(l_vBytes.data(), 1, l_vBytes.size(), l_pFp) != l_vBytes.size()) l_vBytes.clear();
        }
        std::fclose(l_pFp);
        if (l_vBytes.empty()) continue;
        l_vKeep = std::move(l_vBytes);
        if (Rml::LoadFontFace(Rml::Span<const Rml::byte>(l_vKeep.data(), l_vKeep.size()), "toms-system", Rml::Style::FontStyle::Normal,
                              Rml::Style::FontWeight::Auto, true, l_oF.m_iFace)) {
            std::fprintf(stderr, "[rmlui] system fallback font: %s (face %d, %.1f MB)\n", l_oF.m_strPath.c_str(), l_oF.m_iFace, l_vKeep.size() / 1048576.0);
            return true;
        }
        std::fprintf(stderr, "[rmlui] system font could not be loaded: %s\n", l_oF.m_strPath.c_str());
        l_vKeep.clear();
    }
    std::fprintf(stderr, "[rmlui] no system fallback font (characters the bundled font lacks show as boxes)\n");
    return false;
}
}  // namespace
#endif

RmlUi::RmlUi() = default;
RmlUi::~RmlUi() { f_Shutdown(); }

bool RmlUi::f_Init(int l_iDesignW, int l_iDesignH, const std::string& l_strDefaultFont, std::string& l_strError) {
    f_Shutdown();
    m_upImpl = std::make_unique<Impl>();
    m_upImpl->m_oRender.m_fDesignW = (float)l_iDesignW;
    m_upImpl->m_oRender.m_fDesignH = (float)l_iDesignH;
    if (!m_upImpl->m_oRender.f_Create()) { l_strError = "RmlUi: shader program missing for this renderer"; m_upImpl.reset(); return false; }
    Rml::SetRenderInterface(&m_upImpl->m_oRender);
    Rml::SetSystemInterface(&m_upImpl->m_oSystem);
    Rml::SetFileInterface(&m_upImpl->m_oFile);   // APK entries on Android, the in-memory sprite sheet everywhere
#ifdef __EMSCRIPTEN__
    (void)l_strDefaultFont;                                  // no font files on the web
    Rml::SetFontEngineInterface(&m_upImpl->canvasFont);
#endif
    if (!Rml::Initialise()) { l_strError = "RmlUi: Rml::Initialise failed"; m_upImpl->m_oRender.f_Destroy(); m_upImpl.reset(); return false; }
#ifndef __EMSCRIPTEN__
    // The bundled font is family "toms" AND the fallback face: characters a language's own font
    // (languageFont) lacks are drawn from it.
    if (!Rml::LoadFontFace(l_strDefaultFont, "toms", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto, true, 0)) {
        l_strError = "RmlUi: the UI font could not be loaded: " + l_strDefaultFont;
        Rml::Shutdown(); m_upImpl->m_oRender.f_Destroy(); m_upImpl.reset();
        return false;
    }
    std::fprintf(stderr, "[rmlui] font family 'toms' (and fallback): %s\n", l_strDefaultFont.c_str());
    f_LoadSystemFallbackFont(m_upImpl->m_vSystemFont);   // after the bundled face: only for characters it lacks
#endif
    m_upImpl->m_pContext = Rml::CreateContext("game", Rml::Vector2i(l_iDesignW, l_iDesignH));
    if (!m_upImpl->m_pContext) { l_strError = "RmlUi: CreateContext failed"; f_Shutdown(); return false; }
    Rml::Debugger::Initialise(m_upImpl->m_pContext);
    m_upImpl->m_oRender.m_iLentVersion = f_LentTexturesVersion();
    return true;
}

void RmlUi::f_Shutdown() {
    if (!m_upImpl) return;
    if (m_upImpl->m_pContext) {
        Rml::Debugger::Shutdown();
        Rml::RemoveContext(m_upImpl->m_pContext->GetName());
        m_upImpl->m_pContext = nullptr;
    }
    Rml::Shutdown();            // releases RmlUi's textures and geometry through the render interface
    m_upImpl->m_oRender.f_Destroy();
    m_upImpl.reset();
    std::map<std::string, std::string>().swap(f_VirtualFiles());   // the in-memory documents go with RmlUi
}

bool RmlUi::f_Ready() const { return m_upImpl && m_upImpl->m_pContext; }
Rml::Context* RmlUi::f_Context() { return m_upImpl ? m_upImpl->m_pContext : nullptr; }

std::string RmlUi::f_LanguageFont(const std::string& l_strCode, const std::string& l_strFontFile, const std::string& l_strWebFonts) {
    if (!m_upImpl) return "toms";
#ifdef __EMSCRIPTEN__
    (void)l_strFontFile;
    std::string l_strTag = l_strCode;                                   // zh_TW -> zh-TW (BCP 47)
    std::replace(l_strTag.begin(), l_strTag.end(), '_', '-');
    m_upImpl->canvasFont.f_SetLanguage(l_strTag, l_strWebFonts);
    return "toms";
#else
    (void)l_strWebFonts;
    if (l_strFontFile.empty()) return "toms";
    const std::string l_strFamily = "toms-" + l_strCode;
    if (m_upImpl->m_setLoadedFamilies.count(l_strFamily)) return l_strFamily;
    if (!toms::f_VfsExists(l_strFontFile)) {
        std::fprintf(stderr, "[rmlui] font for '%s' not found, using the default: %s\n", l_strCode.c_str(), l_strFontFile.c_str());
        return "toms";
    }
    if (!Rml::LoadFontFace(l_strFontFile, l_strFamily, Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto, false, 0)) {
        std::fprintf(stderr, "[rmlui] font for '%s' could not be loaded, using the default: %s\n", l_strCode.c_str(), l_strFontFile.c_str());
        return "toms";
    }
    std::fprintf(stderr, "[rmlui] font family '%s': %s\n", l_strFamily.c_str(), l_strFontFile.c_str());
    m_upImpl->m_setLoadedFamilies.insert(l_strFamily);
    return l_strFamily;
#endif
}

void RmlUi::f_SetViewport(float l_fX, float l_fY, float l_fW, float l_fH) {
    if (!m_upImpl) return;
    m_upImpl->m_oRender.m_fVpX = l_fX; m_upImpl->m_oRender.m_fVpY = l_fY; m_upImpl->m_oRender.m_fVpW = l_fW; m_upImpl->m_oRender.m_fVpH = l_fH;
#ifdef __EMSCRIPTEN__
    if (m_upImpl->render.designW > 0) m_upImpl->canvasFont.f_SetPixelScale(l_fW / m_upImpl->render.designW);
#endif
}

void RmlUi::f_SetDesignSize(int l_iDesignW, int l_iDesignH) {
    if (!m_upImpl || l_iDesignW <= 0 || l_iDesignH <= 0) return;
    if ((float)l_iDesignW == m_upImpl->m_oRender.m_fDesignW && (float)l_iDesignH == m_upImpl->m_oRender.m_fDesignH) return;
    m_upImpl->m_oRender.m_fDesignW = (float)l_iDesignW;
    m_upImpl->m_oRender.m_fDesignH = (float)l_iDesignH;
    if (m_upImpl->m_pContext) m_upImpl->m_pContext->SetDimensions(Rml::Vector2i(l_iDesignW, l_iDesignH));
}

void RmlUi::f_Update(double l_dTimeSeconds) {
    if (!f_Ready()) return;
    // The map renderer lent another texture (art style switch): drop every texture RmlUi holds so
    // nothing draws with the old one; they reload, from the new loan, when next needed.
    if (m_upImpl->m_oRender.m_iLentVersion != f_LentTexturesVersion()) {
        Rml::ReleaseTextures();
        m_upImpl->m_oRender.m_setBorrowed.clear();
        m_upImpl->m_oRender.m_iLentVersion = f_LentTexturesVersion();
    }
    m_upImpl->m_oSystem.m_dNow = l_dTimeSeconds;
    m_upImpl->m_pContext->Update();
}

void RmlUi::f_Render(uint16_t l_u16ViewId) {
    if (!f_Ready()) return;
    auto& l_oR = m_upImpl->m_oRender;
    l_oR.m_u16View = l_u16ViewId;
    bgfx::setViewRect(l_u16ViewId, (uint16_t)std::lround(l_oR.m_fVpX), (uint16_t)std::lround(l_oR.m_fVpY),
                      (uint16_t)std::lround(l_oR.m_fVpW), (uint16_t)std::lround(l_oR.m_fVpH));
    bgfx::setViewMode(l_u16ViewId, bgfx::ViewMode::Sequential);
    float l_arrProj[16];
    bx::mtxOrtho(l_arrProj, 0.0f, l_oR.m_fDesignW, l_oR.m_fDesignH, 0.0f, -1.0f, 1.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(l_u16ViewId, nullptr, l_arrProj);
    bgfx::touch(l_u16ViewId);
    m_upImpl->m_pContext->Render();
}

void RmlUi::f_SetVirtualFile(const std::string& l_strFileName, const std::string& l_strText) { f_VirtualFiles()[l_strFileName] = l_strText; }
void RmlUi::f_ClearVirtualFile(const std::string& l_strFileName) { f_VirtualFiles().erase(l_strFileName); }
void RmlUi::f_SetLogListener(std::function<void(int, const std::string&)> l_fnListener) { f_LogListener() = std::move(l_fnListener); }

void RmlUi::f_ToggleDebugger() {
    if (!f_Ready()) return;
    m_upImpl->m_bDebugger = !m_upImpl->m_bDebugger;
    Rml::Debugger::SetVisible(m_upImpl->m_bDebugger);
}
bool RmlUi::f_DebuggerVisible() const { return m_upImpl && m_upImpl->m_bDebugger; }

}  // namespace toms::next
