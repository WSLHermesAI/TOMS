// atlas_web.cpp -- the embind API of the web atlas tool (atlas_core compiled to WebAssembly).
//
// The editor works on EMBEDDED projects only (atlas_store.h): the Project held here owns its
// images in memory (Project::images, Variant::images). The browser writes raw file bytes (an opened
// project with its packed atlas, PNGs to add, reference folders) into Emscripten's in-memory file
// system (MEMFS) and calls these functions; images are decoded here (stb) exactly as the native
// tool does, so a web build gives the same atlas as atlaspack. Saving runs saveProjectAll() into a
// MEMFS folder that the page then zips. Undo/redo: snapshot() keeps a copy of the Project (cheap:
// images are shared pointers) under an id, restore(id) brings it back. Under Node the real disk is
// mounted with NODEFS and cliMain() runs the atlaspack command line (atlaspack.mjs).
//
// JS:  const M = await createAtlasModule();  const A = new M.AtlasWeb();
//      A.newProject('atlas', '/work/atlas.atlasproj');  A.setImageBytes('hero', pngBytes, '');
//      const r = JSON.parse(A.build(''));  const rgba = A.getPageRGBA(0);
#include "atlas_build.h"
#include "atlas_cli.h"
#include "atlas_export.h"
#include "atlas_store.h"

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <string>
#include <vector>

using namespace atlas;
using emscripten::val;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

val f_BytesToJs(const uint8_t* l_pData, size_t l_nSize) {
    // A copy: the view into the wasm heap dies when memory grows.
    return val(emscripten::typed_memory_view(l_nSize, l_pData)).call<val>("slice");
}

json f_RectJson(const IRect& l_oR) { return json::array({l_oR.m_iX, l_oR.m_iY, l_oR.m_iW, l_oR.m_iH}); }

const char* f_LevelName(Diagnostic::Level l_eL) {
    return l_eL == Diagnostic::Error ? "error" : l_eL == Diagnostic::Warning ? "warning" : "info";
}

json f_DiagJson(const std::vector<Diagnostic>& l_vDs) {
    json l_jA = json::array();
    for (const Diagnostic& l_oD : l_vDs) l_jA.push_back({{"level", f_LevelName(l_oD.m_eLevel)}, {"sprite", l_oD.m_strSprite}, {"message", l_oD.m_strMessage}});
    return l_jA;
}

json f_RegionJson(const Region& l_oR) {
    json l_jJ{{"name", l_oR.m_strName}, {"page", l_oR.m_iPage}, {"frame", f_RectJson(l_oR.m_oFrame)},
           {"offsetX", l_oR.m_iOffsetX}, {"offsetY", l_oR.m_iOffsetY}, {"origW", l_oR.m_iOrigW}, {"origH", l_oR.m_iOrigH},
           {"pivot", {l_oR.m_arrPivot[0], l_oR.m_arrPivot[1]}}, {"hasSplit", l_oR.m_bHasSplit},
           {"split", {l_oR.m_arrSplit[0], l_oR.m_arrSplit[1], l_oR.m_arrSplit[2], l_oR.m_arrSplit[3]}}, {"tags", l_oR.m_vTags},
           {"child", l_oR.m_bChild}, {"parent", l_oR.m_strParent}, {"local", f_RectJson(l_oR.m_oLocal)}, {"baked", l_oR.m_bBaked},
           {"aliasOf", l_oR.m_strAliasOf}, {"sourceFile", l_oR.m_strSourceFile}, {"trimmed", l_oR.f_Trimmed()}};
    return l_jJ;
}

std::string f_ErrJson(const std::string& l_strE) { return json{{"error", l_strE}}.dump(); }

int f_CountPngs(const ImageSource& l_oSrc, const std::string& l_strDir, bool l_bRecursive) {
    return (int)l_oSrc.m_fnListPngs(l_strDir, l_bRecursive).size();
}

class AtlasWeb {
public:
    AtlasWeb() {
        m_oSrc = f_CachedImageSource(m_spCache);
        f_NewProject("atlas", "/work/atlas.atlasproj");
    }

    // ---- files (MEMFS, or NODEFS under node) ----
    bool f_WriteFile(const std::string& l_strPath, const val& l_oBytes) {
        const std::vector<uint8_t> l_vData = emscripten::convertJSArrayToNumberVector<uint8_t>(l_oBytes);
        std::error_code l_oEc;
        fs::create_directories(f_U8path(l_strPath).parent_path(), l_oEc);
        std::ofstream l_oF(f_U8path(l_strPath), std::ios::binary | std::ios::trunc);
        if (!l_oF) return false;
        l_oF.write((const char*)l_vData.data(), (std::streamsize)l_vData.size());
        f_InvalidateImages();
        return (bool)l_oF;
    }
    val f_ReadFile(const std::string& l_strPath) const {
        std::ifstream l_oF(f_U8path(l_strPath), std::ios::binary);
        if (!l_oF) return val::null();
        std::vector<uint8_t> l_vData((std::istreambuf_iterator<char>(l_oF)), std::istreambuf_iterator<char>());
        return f_BytesToJs(l_vData.data(), l_vData.size());
    }
    bool f_RemoveFile(const std::string& l_strPath) {
        std::error_code l_oEc;
        const auto l_u64N = fs::remove_all(f_U8path(l_strPath), l_oEc);
        f_InvalidateImages();
        return !l_oEc && l_u64N > 0;
    }
    bool f_Exists(const std::string& l_strPath) const {
        std::error_code l_oEc;
        return fs::exists(f_U8path(l_strPath), l_oEc);
    }
    // Every regular file below dir (recursive), '/' paths, sorted. JSON array.
    std::string f_ListFiles(const std::string& l_strDir) const {
        std::vector<std::string> l_vOut;
        std::error_code l_oEc;
        if (fs::is_directory(f_U8path(l_strDir), l_oEc))
            for (fs::recursive_directory_iterator l_oIt(f_U8path(l_strDir), l_oEc), l_oEnd; !l_oEc && l_oIt != l_oEnd; l_oIt.increment(l_oEc))
                if (l_oIt->is_regular_file(l_oEc)) l_vOut.push_back(f_U8str(l_oIt->path()));
        std::sort(l_vOut.begin(), l_vOut.end());
        return json(l_vOut).dump();
    }

    // ---- project ----
    // A new, empty embedded project (its packed atlas goes next to the project file).
    void f_NewProject(const std::string& l_strName, const std::string& l_strPath) {
        Project l_oP;
        l_oP.m_strName = l_strName.empty() ? "atlas" : l_strName;
        l_oP.m_bEmbedded = true;
        l_oP.m_oOutput.m_strDir = ".";
        l_oP.m_strFilePath = f_NormalizePath(l_strPath);
        m_oProject = std::move(l_oP);
    }

    // The project's JSON as edited by the page. The images stay what they are here (the JSON does
    // not hold them): base images are kept, a variant keeps its art by id (or by position when the
    // id was just renamed). Returns "" or the error. path: where the project "lives".
    std::string f_SetProject(const std::string& l_strText, const std::string& l_strPath) {
        Project l_oP;
        std::string l_strErr;
        if (!f_ProjectFromJson(l_strText, l_oP, &l_strErr)) return l_strErr.empty() ? "invalid project" : l_strErr;
        l_oP.m_strFilePath = f_NormalizePath(l_strPath);
        l_oP.m_bEmbedded = true;   // the web editor works on embedded projects only
        l_oP.m_mapImages = m_oProject.m_mapImages;
        for (size_t l_nI = 0; l_nI < l_oP.m_vVariants.size(); l_nI++) {
            Variant& l_oV = l_oP.m_vVariants[l_nI];
            const Variant* l_pOld = m_oProject.f_FindVariant(l_oV.m_strId);
            if (!l_pOld && l_nI < m_oProject.m_vVariants.size() && !l_oP.f_FindVariant(m_oProject.m_vVariants[l_nI].m_strId)) l_pOld = &m_oProject.m_vVariants[l_nI];
            l_oV.m_mapImages = l_pOld ? l_pOld->m_mapImages : std::map<std::string, ImagePtr>();
        }
        m_oProject = std::move(l_oP);
        return std::string();
    }
    std::string f_GetProject() const { return f_ProjectToJson(m_oProject); }
    std::string f_ProjectPath() const { return m_oProject.m_strFilePath; }
    std::string f_ResolvePath(const std::string& l_strRel) const { return m_oProject.f_Resolve(l_strRel); }
    std::string f_Relativize(const std::string& l_strPath) const { return m_oProject.f_Relativize(l_strPath); }

    // What opening `path` needs, before opening it: JSON {error} or {embedded, name, store,
    // storeExists, variants:[{id, store, dir, needed, exists}], sources:[{path, dir, pngs}],
    // overrides:[{id, path, dir, pngs}]}.
    std::string f_Inspect(const std::string& l_strPath) {
        Project l_oP;
        std::string l_strErr;
        if (!f_LoadProject(l_strPath, l_oP, &l_strErr)) return f_ErrJson(l_strErr.empty() ? "cannot read " + l_strPath : l_strErr);
        std::error_code l_oEc;
        json l_jR{{"embedded", l_oP.m_bEmbedded}, {"name", l_oP.m_strName}};
        json l_jVars = json::array(), l_jSources = json::array(), l_jOverrides = json::array();
        if (l_oP.m_bEmbedded) {
            l_jR["store"] = f_StorePath(l_oP);
            l_jR["storeExists"] = fs::is_regular_file(f_U8path(f_StorePath(l_oP)), l_oEc);
            for (const Variant& l_oV : l_oP.m_vVariants) {
                const std::string l_strS = f_StorePath(l_oP, &l_oV);
                l_jVars.push_back({{"id", l_oV.m_strId}, {"store", l_strS}, {"dir", f_ParentDir(l_strS)}, {"needed", !l_oV.m_mapImages.empty()},
                                {"exists", fs::is_regular_file(f_U8path(l_strS), l_oEc)}});
            }
        } else {
            for (const SourceFolder& l_oF : l_oP.m_vSources)
                l_jSources.push_back({{"path", l_oF.m_strPath}, {"dir", l_oP.f_Resolve(l_oF.m_strPath)}, {"pngs", f_CountPngs(m_oSrc, l_oP.f_Resolve(l_oF.m_strPath), l_oF.m_bRecursive)}});
            for (const Variant& l_oV : l_oP.m_vVariants)
                if (!l_oV.m_strOverrideDir.empty())
                    l_jOverrides.push_back({{"id", l_oV.m_strId}, {"path", l_oV.m_strOverrideDir}, {"dir", l_oP.f_Resolve(l_oV.m_strOverrideDir)},
                                         {"pngs", f_CountPngs(m_oSrc, l_oP.f_Resolve(l_oV.m_strOverrideDir), true)}});
        }
        l_jR["variants"] = l_jVars;
        l_jR["sources"] = l_jSources;
        l_jR["overrides"] = l_jOverrides;
        return l_jR.dump();
    }

    // Opens a project from MEMFS: an embedded one gets its images cut out of the packed atlas; a
    // folder project is converted (embedSources, its folders must be in MEMFS). dropMissingVariants:
    // a variant whose packed atlas is missing loses its art instead of failing the open.
    // JSON {error} or {project, converted, images, dropped:[ids], diagnostics}.
    std::string f_OpenProject(const std::string& l_strPath, bool l_bDropMissingVariants) {
        Project l_oP;
        std::string l_strErr;
        json l_jDropped = json::array();
        std::vector<Diagnostic> l_vDiags;
        int l_iConverted = -1;
        try {
            if (!f_LoadProject(l_strPath, l_oP, &l_strErr)) return f_ErrJson(l_strErr.empty() ? "cannot read " + l_strPath : l_strErr);
            if (l_oP.m_bEmbedded) {
                std::error_code l_oEc;
                if (l_bDropMissingVariants)
                    for (Variant& l_oV : l_oP.m_vVariants)
                        if (!l_oV.m_mapImages.empty() && !fs::is_regular_file(f_U8path(f_StorePath(l_oP, &l_oV)), l_oEc)) {
                            l_oV.m_mapImages.clear();
                            l_jDropped.push_back(l_oV.m_strId);
                        }
                if (!f_LoadEmbeddedImages(l_oP, &l_strErr)) return f_ErrJson(l_strPath + ": " + l_strErr);
            } else {
                l_iConverted = f_EmbedSources(l_oP, m_oSrc, &l_vDiags);
            }
        } catch (const std::exception& l_oE) {
            return f_ErrJson(l_oE.what());
        }
        m_oProject = std::move(l_oP);
        return json{{"project", f_ProjectToJson(m_oProject)}, {"converted", l_iConverted}, {"images", (int)m_oProject.m_mapImages.size()},
                    {"dropped", l_jDropped}, {"diagnostics", f_DiagJson(l_vDiags)}}.dump();
    }

    // ---- images ----
    // Adds or replaces an image from PNG bytes (variant: that variant's replacement art). "" or the error.
    std::string f_SetImageBytes(const std::string& l_strName, const val& l_oBytes, const std::string& l_strVariant) {
        const std::vector<uint8_t> l_vData = emscripten::convertJSArrayToNumberVector<uint8_t>(l_oBytes);
        Image l_oImg;
        std::string l_strErr;
        if (!f_LoadImageFromMemory(l_vData.data(), l_vData.size(), l_oImg, &l_strErr)) return l_strName + ": " + (l_strErr.empty() ? "not a PNG" : l_strErr);
        if (!f_SetImage(m_oProject, l_strName, l_oImg, l_strVariant, &l_strErr)) return l_strErr.empty() ? "cannot set " + l_strName : l_strErr;
        return std::string();
    }
    std::string f_SetImageFile(const std::string& l_strName, const std::string& l_strPath, const std::string& l_strVariant) {
        Image l_oImg;
        std::string l_strErr;
        if (!f_LoadImage(l_strPath, l_oImg, &l_strErr)) return l_strErr.empty() ? "cannot read " + l_strPath : l_strErr;
        if (!f_SetImage(m_oProject, l_strName, l_oImg, l_strVariant, &l_strErr)) return l_strErr.empty() ? "cannot set " + l_strName : l_strErr;
        return std::string();
    }
    bool f_HasImage(const std::string& l_strName, const std::string& l_strVariant) const {
        if (l_strVariant.empty()) return m_oProject.m_mapImages.count(l_strName) > 0;
        const Variant* l_pV = m_oProject.f_FindVariant(l_strVariant);
        return l_pV && l_pV->m_mapImages.count(l_strName) > 0;
    }
    std::string f_ImageNames(const std::string& l_strVariant) const {
        std::vector<std::string> l_vOut;
        const Variant* l_pV = l_strVariant.empty() ? nullptr : m_oProject.f_FindVariant(l_strVariant);
        for (const auto& l_pairKv : l_pV ? l_pV->m_mapImages : m_oProject.m_mapImages) l_vOut.push_back(l_pairKv.first);
        return json(l_vOut).dump();
    }
    std::string f_ImageNameFor(const std::string& l_strFile, const std::string& l_strFolder, const std::string& l_strPrefix) const {
        return atlas::f_ImageNameFor(l_strFile, l_strFolder, l_strPrefix);
    }

    // Renames an image or child sprite everywhere: the images (base and every variant), its entry,
    // children's parents and animation frames. "" or the error.
    std::string f_RenameSprite(const std::string& l_strFrom, const std::string& l_strTo) {
        if (l_strTo.empty()) return "a sprite needs a name";
        if (l_strTo == l_strFrom) return std::string();
        if (m_oProject.m_mapImages.count(l_strTo) || m_oProject.f_FindSprite(l_strTo)) return "'" + l_strTo + "' already exists";
        bool l_bFound = false;
        auto l_fnMove = [&](std::map<std::string, ImagePtr>& l_mapM) {
            auto l_itIt = l_mapM.find(l_strFrom);
            if (l_itIt == l_mapM.end()) return false;
            ImagePtr l_spImg = l_itIt->second;
            l_mapM.erase(l_itIt);
            l_mapM[l_strTo] = l_spImg;
            return true;
        };
        l_bFound |= l_fnMove(m_oProject.m_mapImages);
        for (Variant& l_oV : m_oProject.m_vVariants) l_fnMove(l_oV.m_mapImages);
        for (SpriteDef& l_oD : m_oProject.m_vSprites) {
            if (l_oD.m_strName == l_strFrom) { l_oD.m_strName = l_strTo; l_bFound = true; }
            if (l_oD.m_strParent == l_strFrom) l_oD.m_strParent = l_strTo;
        }
        for (Animation& l_oA : m_oProject.m_vAnimations)
            for (AnimFrame& l_oF : l_oA.m_vFrames) if (l_oF.m_strSprite == l_strFrom) l_oF.m_strSprite = l_strTo;
        return l_bFound ? std::string() : "no sprite '" + l_strFrom + "'";
    }

    // Deletes sprites (JSON array of names) with their children: images, variant art, entries,
    // animation frames. Returns how many sprites went.
    int f_DeleteSprites(const std::string& l_strNamesJson) {
        std::set<std::string> l_setVictims;
        for (const json& l_jN : json::parse(l_strNamesJson, nullptr, false)) if (l_jN.is_string()) l_setVictims.insert(l_jN.get<std::string>());
        for (bool l_bMore = true; l_bMore;) {   // children of children
            l_bMore = false;
            for (const SpriteDef& l_oD : m_oProject.m_vSprites)
                if (l_oD.f_IsChild() && l_setVictims.count(l_oD.m_strParent) && l_setVictims.insert(l_oD.m_strName).second) l_bMore = true;
        }
        int l_iN = 0;
        for (const std::string& l_strS : l_setVictims) {
            bool l_bAny = m_oProject.m_mapImages.erase(l_strS) > 0;
            for (Variant& l_oV : m_oProject.m_vVariants) l_oV.m_mapImages.erase(l_strS);
            l_bAny |= m_oProject.f_FindSprite(l_strS) != nullptr;
            l_iN += l_bAny;
        }
        m_oProject.m_vSprites.erase(std::remove_if(m_oProject.m_vSprites.begin(), m_oProject.m_vSprites.end(),
                                              [&](const SpriteDef& l_oD) { return l_setVictims.count(l_oD.m_strName) > 0; }),
                               m_oProject.m_vSprites.end());
        for (Animation& l_oA : m_oProject.m_vAnimations)
            l_oA.m_vFrames.erase(std::remove_if(l_oA.m_vFrames.begin(), l_oA.m_vFrames.end(), [&](const AnimFrame& l_oF) { return l_setVictims.count(l_oF.m_strSprite) > 0; }),
                           l_oA.m_vFrames.end());
        return l_iN;
    }

    // Reference folders (project, or one variant's) compared with the images held: JSON
    // [{name, file, status: "new"|"changed"|"same"}].
    std::string f_ScanReferences(const std::string& l_strVariant) const {
        json l_jA = json::array();
        static const char* l_arrSt[] = {"new", "changed", "same"};
        for (const ReferenceImage& l_oR : atlas::f_ScanReferences(m_oProject, m_oSrc, l_strVariant))
            l_jA.push_back({{"name", l_oR.m_strName}, {"file", l_oR.m_strFile}, {"status", l_arrSt[l_oR.m_eStatus]}});
        return l_jA.dump();
    }

    // ---- undo snapshots ----
    int f_Snapshot() {
        const int l_iId = m_iNextSnap++;
        m_mapSnaps[l_iId] = m_oProject;
        return l_iId;
    }
    bool f_Restore(int l_iId) {
        auto l_itIt = m_mapSnaps.find(l_iId);
        if (l_itIt == m_mapSnaps.end()) return false;
        m_oProject = l_itIt->second;
        return true;
    }
    // Forgets every snapshot not in idsJson (the ids the page's undo/redo stacks still use).
    int f_KeepSnapshots(const std::string& l_strIdsJson) {
        std::set<int> l_setKeep;
        for (const json& l_jN : json::parse(l_strIdsJson, nullptr, false)) if (l_jN.is_number_integer()) l_setKeep.insert(l_jN.get<int>());
        for (auto l_itIt = m_mapSnaps.begin(); l_itIt != m_mapSnaps.end();) l_itIt = l_setKeep.count(l_itIt->first) ? std::next(l_itIt) : m_mapSnaps.erase(l_itIt);
        return (int)m_mapSnaps.size();
    }

    // ---- save / extract ----
    // The folders a save at `path` writes the packed atlases to (base first, then each variant), JSON.
    std::string f_OutputDirs(const std::string& l_strPath) const {
        Project l_oQ = m_oProject;
        l_oQ.m_strFilePath = f_NormalizePath(l_strPath);
        json l_jA = json::array();
        l_jA.push_back(l_oQ.f_Resolve(l_oQ.m_oOutput.m_strDir.empty() ? "." : l_oQ.m_oOutput.m_strDir));
        for (const Variant& l_oV : l_oQ.m_vVariants) l_jA.push_back(l_oQ.f_Resolve(l_oV.m_oOutput.m_strDir.empty() ? "." : l_oV.m_oOutput.m_strDir));
        return l_jA.dump();
    }
    // saveProjectAll() of a copy placed at `path` (relative paths are kept as they are, so the
    // saved project matches the folder layout around it). JSON {error} or {written, diagnostics}.
    std::string f_SaveAll(const std::string& l_strPath, const std::string& l_strAlsoCsv) const {
        Project l_oQ = m_oProject;
        l_oQ.m_strFilePath = f_NormalizePath(l_strPath);
        SaveResult l_oRes;
        std::string l_strErr;
        try {
            if (!f_SaveProjectAll(l_strPath, l_oQ, f_SplitCsv(l_strAlsoCsv), &l_oRes, &l_strErr)) return f_ErrJson(l_strErr.empty() ? "save failed" : l_strErr);
        } catch (const std::exception& l_oE) {
            return f_ErrJson(l_oE.what());
        }
        return json{{"written", l_oRes.m_vWritten}, {"diagnostics", f_DiagJson(l_oRes.m_vDiagnostics)}}.dump();
    }
    std::string f_Extract(const std::string& l_strDir, const std::string& l_strVariant, bool l_bChildren) const {
        std::vector<std::string> l_vWritten;
        std::string l_strErr;
        try {
            if (!f_ExtractImages(m_oProject, l_strDir, l_strVariant, l_bChildren, &l_vWritten, &l_strErr)) return f_ErrJson(l_strErr.empty() ? "extract failed" : l_strErr);
        } catch (const std::exception& l_oE) {
            return f_ErrJson(l_oE.what());
        }
        return json{{"written", l_vWritten}}.dump();
    }

    // The sprites as the build sees them, JSON.
    std::string f_Resolve() const {
        std::vector<Diagnostic> l_vDiags;
        const std::vector<ResolvedSprite> l_vRs = f_ResolveSprites(m_oProject, m_oSrc, &l_vDiags);
        json l_jA = json::array();
        for (const ResolvedSprite& l_oR : l_vRs)
            l_jA.push_back({{"name", l_oR.m_oDef.m_strName}, {"file", l_oR.m_strFile}, {"scanned", l_oR.m_bScanned}, {"child", l_oR.m_oDef.f_IsChild()},
                         {"parent", l_oR.m_oDef.m_strParent}, {"exclude", l_oR.m_oDef.m_bExclude}, {"pinned", l_oR.m_oDef.m_bPinned}});
        return json{{"sprites", l_jA}, {"diagnostics", f_DiagJson(l_vDiags)}}.dump();
    }

    // ---- build ----
    std::string f_Build(const std::string& l_strVariant) {
        BuildOptions l_oO;
        l_oO.m_strVariant = l_strVariant;
        l_oO.m_bComposite = true;
        try {
            m_oResult = atlas::f_Build(m_oProject, m_oSrc, l_oO);
        } catch (const std::exception& l_oE) {
            m_oResult = BuildResult();
            m_oResult.m_vDiagnostics.push_back({Diagnostic::Error, "", std::string("build failed: ") + l_oE.what()});
        }
        return f_ResultJson(m_oResult);
    }
    int f_PageCount() const { return (int)m_oResult.m_vPages.size(); }
    val f_GetPageRGBA(int l_iI) const {
        if (l_iI < 0 || l_iI >= (int)m_oResult.m_vPages.size()) return val::null();
        const Image& l_oImg = m_oResult.m_vPages[l_iI].m_oImage;
        return f_BytesToJs(l_oImg.m_vPx.data(), l_oImg.m_vPx.size());
    }
    val f_GetPagePng(int l_iI) const {
        if (l_iI < 0 || l_iI >= (int)m_oResult.m_vPages.size()) return val::null();
        const std::vector<uint8_t> l_vPng = f_EncodePng(m_oResult.m_vPages[l_iI].m_oImage);
        return f_BytesToJs(l_vPng.data(), l_vPng.size());
    }
    // One region at its original size, as a game draws it: {w, h, data(RGBA)} or null.
    val f_GetRegionRGBA(const std::string& l_strName) const {
        const Region* l_pR = m_oResult.f_Find(l_strName);
        if (!l_pR) return val::null();
        const Image l_oImg = f_RenderRegion(m_oResult, *l_pR);
        val l_oO = val::object();
        l_oO.set("w", l_oImg.m_iW);
        l_oO.set("h", l_oImg.m_iH);
        l_oO.set("data", f_BytesToJs(l_oImg.m_vPx.data(), l_oImg.m_vPx.size()));
        return l_oO;
    }
    // The same as a PNG file (Save image as), or null.
    val f_GetRegionPng(const std::string& l_strName) const {
        const Region* l_pR = m_oResult.f_Find(l_strName);
        if (!l_pR) return val::null();
        const std::vector<uint8_t> l_vPng = f_EncodePng(f_RenderRegion(m_oResult, *l_pR));
        return f_BytesToJs(l_vPng.data(), l_vPng.size());
    }

    // ---- export / import ----
    // formatsCsv: "atlas,tp-json"; empty = the project's (or variant's) formats.
    // Returns an array of {name, data:Uint8Array}; on error {error}.
    val f_ExportFiles(const std::string& l_strFormatsCsv) const {
        std::vector<std::string> l_vFormats = f_SplitCsv(l_strFormatsCsv);
        if (l_vFormats.empty()) l_vFormats = m_oProject.m_oOutput.m_vFormats;
        std::vector<ExportFile> l_vFiles;
        std::string l_strErr;
        if (m_oResult.m_vPages.empty() && m_oResult.m_vRegions.empty()) l_strErr = "nothing built";
        else if (!m_oResult.f_Ok()) l_strErr = std::to_string(m_oResult.f_ErrorCount()) + " error(s): fix the problems first";
        else if (!atlas::f_ExportFiles(m_oResult, l_vFormats, l_vFiles, &l_strErr) && l_strErr.empty()) l_strErr = "export failed";
        if (!l_strErr.empty()) {
            val l_oO = val::object();
            l_oO.set("error", l_strErr);
            return l_oO;
        }
        val l_oA = val::array();
        for (const ExportFile& l_oF : l_vFiles) {
            val l_oO = val::object();
            l_oO.set("name", l_oF.m_strName);
            l_oO.set("data", f_BytesToJs(l_oF.m_vData.data(), l_oF.m_vData.size()));
            l_oA.call<void>("push", l_oO);
        }
        return l_oA;
    }
    // Reads an atlas (.pi/.atlas/.json; its page PNGs next to it), cuts the sprites out into memory
    // and makes that the current (embedded) project at projectPath. JSON {error} or
    // {project, diagnostics, sprites, children}.
    std::string f_ImportAtlas(const std::string& l_strPath, const std::string& l_strProjectPath) {
        BuildResult l_oIn;
        std::string l_strErr;
        json l_jR;
        try {
            if (!f_ImportAtlasFile(l_strPath, l_oIn, &l_strErr)) return f_ErrJson(l_strErr.empty() ? "cannot read " + l_strPath : l_strErr);
            Project l_oP;
            if (!f_ImportToProject(l_oIn, l_strProjectPath, "", l_oP, &l_strErr)) return f_ErrJson(l_strErr.empty() ? "import failed" : l_strErr);
            std::string l_strExt = f_U8str(f_U8path(l_strPath).extension());
            for (char& l_cC : l_strExt) l_cC = (char)tolower((unsigned char)l_cC);
            l_oP.m_oOutput.m_vFormats = {l_strExt == ".pi" ? "pi" : l_strExt == ".json" ? "tp-json" : "atlas"};
            l_oP.m_oOutput.m_strDir = ".";
            l_oP.m_bEmbedded = true;
            m_oProject = l_oP;
            int l_iChildren = 0;
            for (const Region& l_oRg : l_oIn.m_vRegions) l_iChildren += l_oRg.m_bChild;
            l_jR = {{"project", f_ProjectToJson(m_oProject)}, {"diagnostics", f_DiagJson(l_oIn.m_vDiagnostics)},
                 {"sprites", (int)l_oIn.m_vRegions.size()}, {"children", l_iChildren}};
        } catch (const std::exception& l_oE) {
            return f_ErrJson(l_oE.what());
        }
        return l_jR.dump();
    }

    std::string f_Formats() const { return json(f_ExportFormats()).dump(); }

private:
    void f_InvalidateImages() {
        m_spCache.reset();
        m_oSrc = f_CachedImageSource(m_spCache);
    }

    static std::vector<std::string> f_SplitCsv(const std::string& l_strS) {
        std::vector<std::string> l_vV;
        std::string l_strCur;
        for (char l_cC : l_strS + ",") {
            if (l_cC == ',') { if (!l_strCur.empty()) l_vV.push_back(l_strCur); l_strCur.clear(); }
            else if (l_cC != ' ') l_strCur += l_cC;
        }
        return l_vV;
    }

    static std::string f_ResultJson(const BuildResult& l_oB) {
        json l_jPages = json::array();
        long long l_i64PageArea = 0, l_i64Used = 0;
        for (const Page& l_oP : l_oB.m_vPages) {
            l_jPages.push_back({{"file", l_oP.m_strFile}, {"w", l_oP.m_oImage.m_iW}, {"h", l_oP.m_oImage.m_iH}});
            l_i64PageArea += (long long)l_oP.m_oImage.m_iW * l_oP.m_oImage.m_iH;
        }
        json l_jRegions = json::array();
        std::set<std::tuple<int, int, int, int, int>> l_setPlaced;   // dedupe/children share pixels
        int l_iImages = 0, l_iChildren = 0, l_iAliases = 0;
        for (const Region& l_oR : l_oB.m_vRegions) {
            l_jRegions.push_back(f_RegionJson(l_oR));
            if (l_oR.m_bChild && !l_oR.m_bBaked) l_iChildren++;
            else if (!l_oR.m_strAliasOf.empty()) l_iAliases++;
            else l_iImages++;
            if ((!l_oR.m_bChild || l_oR.m_bBaked) && l_setPlaced.insert({l_oR.m_iPage, l_oR.m_oFrame.m_iX, l_oR.m_oFrame.m_iY, l_oR.m_oFrame.m_iW, l_oR.m_oFrame.m_iH}).second)
                l_i64Used += (long long)l_oR.m_oFrame.m_iW * l_oR.m_oFrame.m_iH;
        }
        json l_jStats{{"pages", (int)l_oB.m_vPages.size()}, {"regions", (int)l_oB.m_vRegions.size()}, {"images", l_iImages},
                   {"children", l_iChildren}, {"aliases", l_iAliases}, {"errors", l_oB.f_ErrorCount()},
                   {"warnings", l_oB.f_WarningCount()}, {"pageArea", l_i64PageArea}, {"usedArea", l_i64Used},
                   {"occupancy", l_i64PageArea ? (double)l_i64Used / (double)l_i64PageArea : 0.0}};
        json l_jAnims = json::array();
        for (const Animation& l_oA : l_oB.m_vAnimations) {
            json l_jFr = json::array();
            for (const AnimFrame& l_oF : l_oA.m_vFrames) l_jFr.push_back({{"sprite", l_oF.m_strSprite}, {"time", l_oF.m_fTime}});
            l_jAnims.push_back({{"name", l_oA.m_strName}, {"loop", l_oA.m_bLoop}, {"frames", l_jFr}});
        }
        return json{{"name", l_oB.m_strName}, {"ok", l_oB.f_Ok()}, {"pages", l_jPages}, {"regions", l_jRegions},
                    {"diagnostics", f_DiagJson(l_oB.m_vDiagnostics)}, {"animations", l_jAnims}, {"stats", l_jStats}}.dump();
    }

    Project m_oProject;
    std::shared_ptr<void> m_spCache;
    ImageSource m_oSrc;
    BuildResult m_oResult;
    std::map<int, Project> m_mapSnaps;
    int m_iNextSnap = 1;
};

// atlaspack's command line. args: a JS array of strings. Returns the exit code.
int f_CliMain(const val& l_oArgs) {
    std::vector<std::string> l_vV;
    const int l_iN = l_oArgs["length"].as<int>();
    for (int l_iI = 0; l_iI < l_iN; l_iI++) l_vV.push_back(l_oArgs[l_iI].as<std::string>());
    int l_iCode = 2;
    try {
        l_iCode = f_AtlasCliMain(l_vV);
    } catch (const std::exception& l_oE) {
        std::fprintf(stderr, "error: %s\n", l_oE.what());
    }
    std::fflush(stdout);
    std::fflush(stderr);
    return l_iCode;
}

}  // namespace

EMSCRIPTEN_BINDINGS(atlas_web) {
    emscripten::class_<AtlasWeb>("AtlasWeb")
        .constructor<>()
        .function("writeFile", &AtlasWeb::f_WriteFile)
        .function("readFile", &AtlasWeb::f_ReadFile)
        .function("removeFile", &AtlasWeb::f_RemoveFile)
        .function("exists", &AtlasWeb::f_Exists)
        .function("listFiles", &AtlasWeb::f_ListFiles)
        .function("newProject", &AtlasWeb::f_NewProject)
        .function("setProject", &AtlasWeb::f_SetProject)
        .function("getProject", &AtlasWeb::f_GetProject)
        .function("projectPath", &AtlasWeb::f_ProjectPath)
        .function("resolvePath", &AtlasWeb::f_ResolvePath)
        .function("relativize", &AtlasWeb::f_Relativize)
        .function("inspect", &AtlasWeb::f_Inspect)
        .function("openProject", &AtlasWeb::f_OpenProject)
        .function("setImageBytes", &AtlasWeb::f_SetImageBytes)
        .function("setImageFile", &AtlasWeb::f_SetImageFile)
        .function("hasImage", &AtlasWeb::f_HasImage)
        .function("imageNames", &AtlasWeb::f_ImageNames)
        .function("imageNameFor", &AtlasWeb::f_ImageNameFor)
        .function("renameSprite", &AtlasWeb::f_RenameSprite)
        .function("deleteSprites", &AtlasWeb::f_DeleteSprites)
        .function("scanReferences", &AtlasWeb::f_ScanReferences)
        .function("snapshot", &AtlasWeb::f_Snapshot)
        .function("restore", &AtlasWeb::f_Restore)
        .function("keepSnapshots", &AtlasWeb::f_KeepSnapshots)
        .function("outputDirs", &AtlasWeb::f_OutputDirs)
        .function("saveAll", &AtlasWeb::f_SaveAll)
        .function("extract", &AtlasWeb::f_Extract)
        .function("resolve", &AtlasWeb::f_Resolve)
        .function("build", &AtlasWeb::f_Build)
        .function("pageCount", &AtlasWeb::f_PageCount)
        .function("getPageRGBA", &AtlasWeb::f_GetPageRGBA)
        .function("getPagePng", &AtlasWeb::f_GetPagePng)
        .function("getRegionRGBA", &AtlasWeb::f_GetRegionRGBA)
        .function("getRegionPng", &AtlasWeb::f_GetRegionPng)
        .function("exportFiles", &AtlasWeb::f_ExportFiles)
        .function("importAtlas", &AtlasWeb::f_ImportAtlas)
        .function("formats", &AtlasWeb::f_Formats);
    emscripten::function("cliMain", &f_CliMain);
}
