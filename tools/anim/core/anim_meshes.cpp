// anim_meshes.cpp -- see anim_meshes.h.
#include "anim_meshes.h"
#include "anim_check.h"

#include "atlas_image.h"   // atlas_core: the page PNGs for the headless command

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>

namespace animed {

namespace fs = std::filesystem;

namespace {
std::string f_ReadAll(const fs::path& l_oP, bool* l_pOk = nullptr) {
    std::ifstream l_oIn(l_oP, std::ios::binary);
    if (l_pOk) *l_pOk = (bool)l_oIn;
    return std::string((std::istreambuf_iterator<char>(l_oIn)), std::istreambuf_iterator<char>());
}
}  // namespace

std::string f_MeshBinPath(const std::string& l_strMeshPath) {
    const fs::path l_oP = fs::u8path(l_strMeshPath);
    return (l_oP.parent_path() / fs::u8path(l_oP.stem().u8string() + ".bin")).u8string();   // game.mesh.json -> game.mesh.bin
}

void f_AddAnimMeshes(const toms::anim::AnimFile& l_oFile, const toms::anim::AtlasSet& l_oAtlases, const PagePixels& l_fnPixels,
                   std::vector<toms::SpriteMeshFile>& l_vPerEntry, MeshReport* l_pReport,
                   std::vector<std::pair<int, std::string>>* l_pNeeded) {
    if (l_vPerEntry.size() < l_oAtlases.m_vEntries.size()) l_vPerEntry.resize(l_oAtlases.m_vEntries.size());
    MeshReport l_oRep;
    for (const toms::anim::MeshNeed& l_oNeed : toms::anim::f_MeshesNeeded(l_oFile, l_oAtlases)) {
        if (l_pNeeded) l_pNeeded->push_back({l_oNeed.m_iEntry, l_oNeed.m_strKey});
        toms::SpriteMeshFile& l_oInto = l_vPerEntry[(size_t)l_oNeed.m_iEntry];
        if (const toms::SpriteMesh* l_pWas = l_oInto.f_Find(l_oNeed.m_strKey); l_pWas && l_pWas->m_strSource == "hand") {
            l_oRep.m_iKept++;
            continue;
        }
        const toms::AtlasRegion* l_pR = l_oNeed.m_pRegion;
        if (l_pR->m_bRotated || l_pR->m_bChild) {
            l_oRep.m_vSkipped.push_back(l_oNeed.m_strKey + ": " + (l_pR->m_bRotated ? "a rotated region" : "a child sprite (no pixels of its own)"));
            continue;
        }
        const uint8_t* l_pRgba = nullptr;
        int l_iW = 0, l_iH = 0, l_iStride = 0;
        if (!l_fnPixels || !l_fnPixels(l_oNeed.m_iEntry, l_pR->m_iPage, l_pRgba, l_iW, l_iH, l_iStride) || !l_pRgba || l_pR->m_iX < 0 || l_pR->m_iY < 0 || l_pR->m_iX + l_pR->m_iW > l_iW ||
            l_pR->m_iY + l_pR->m_iH > l_iH) {
            l_oRep.m_vSkipped.push_back(l_oNeed.m_strKey + ": its atlas page is not loaded");
            continue;
        }
        toms::SpriteMesh l_oM = toms::anim::f_CutMesh(l_oNeed, l_pRgba + ((size_t)l_pR->m_iY * l_iStride + l_pR->m_iX) * 4, l_iStride);
        if (l_oM.m_vTriangles.empty()) {
            l_oRep.m_vSkipped.push_back(l_oNeed.m_strKey + ": no visible pixels");
            continue;
        }
        l_oInto.m_mapMeshes[l_oNeed.m_strKey] = std::move(l_oM);
        l_oRep.m_iMade++;
    }
    if (l_pReport) *l_pReport = std::move(l_oRep);
}

bool f_ReadMeshFile(const std::string& l_strMeshPath, toms::SpriteMeshFile& l_oOut, bool* l_pBinary, std::string* l_pErr) {
    l_oOut = toms::SpriteMeshFile();
    if (l_pBinary) *l_pBinary = false;
    bool l_bOk = false;
    const std::string l_strText = f_ReadAll(fs::u8path(l_strMeshPath), &l_bOk);
    if (!l_bOk) return true;
    if (l_pBinary) *l_pBinary = l_strText.find("\"buffers\"") != std::string::npos;
    const fs::path l_oDir = fs::u8path(l_strMeshPath).parent_path();
    auto l_fnReadBuffer = [&](const std::string& l_strUri, std::string& l_strBytes) {
        bool l_bFound = false;
        l_strBytes = f_ReadAll(l_oDir / fs::u8path(l_strUri), &l_bFound);
        return l_bFound;
    };
    std::string l_strE;
    if (toms::f_ParseSpriteMeshes(l_strText, l_fnReadBuffer, l_oOut, &l_strE)) return true;
    if (l_pErr) *l_pErr = l_strMeshPath + ": " + l_strE;
    return false;
}

bool f_WriteMeshFile(const std::string& l_strMeshPath, const toms::SpriteMeshFile& l_oF, bool l_bBinary, std::string* l_pErr,
                   std::vector<std::string>* l_pWritten) {
    const fs::path l_oJsonPath = fs::u8path(l_strMeshPath), l_oBinPath = fs::u8path(f_MeshBinPath(l_strMeshPath));
    std::error_code l_oEc;
    if (l_oF.m_mapMeshes.empty()) {   // nothing to keep: no file
        for (const fs::path& l_oP : {l_oJsonPath, l_oBinPath})
            if (fs::exists(l_oP, l_oEc)) {
                fs::remove(l_oP, l_oEc);
                if (l_pWritten) l_pWritten->push_back(l_oP.u8string());
            }
        return true;
    }
    std::string l_strBin;
    const std::string l_strJson = toms::f_SpriteMeshesToJson(l_oF, l_bBinary ? l_oBinPath.filename().u8string() : std::string(), l_bBinary ? &l_strBin : nullptr);
    auto l_fnPut = [&](const fs::path& l_oP, const std::string& l_strBytes) {
        bool l_bThere = false;
        if (f_ReadAll(l_oP, &l_bThere) == l_strBytes && l_bThere) return true;   // unchanged: left alone (its timestamp too)
        std::ofstream l_oO(l_oP, std::ios::binary);
        l_oO.write(l_strBytes.data(), (std::streamsize)l_strBytes.size());
        if (!l_oO) {
            if (l_pErr) *l_pErr = "cannot write " + l_oP.u8string();
            return false;
        }
        if (l_pWritten) l_pWritten->push_back(l_oP.u8string());
        return true;
    };
    if (!l_fnPut(l_oJsonPath, l_strJson)) return false;
    if (l_bBinary) return l_fnPut(l_oBinPath, l_strBin);
    if (fs::exists(l_oBinPath, l_oEc)) {   // now text: the old buffer goes
        fs::remove(l_oBinPath, l_oEc);
        if (l_pWritten) l_pWritten->push_back(l_oBinPath.u8string());
    }
    return true;
}

namespace {
// One atlas the headless command works on (shared by every clip file that names it).
struct AtlasWork {
    std::string m_strPath;                  // as given / resolved
    toms::AtlasFile m_oAtlas;
    std::vector<atlas::Image> m_vPages;
    toms::SpriteMeshFile m_oDisk, m_oOut;    // the mesh file as found; what it becomes
    bool m_bBinary = true, m_bHadFile = false;
    std::set<std::string> m_setNeeded;      // keys the clip files need
};
}  // namespace

int f_HeadlessMeshes(const std::vector<std::string>& l_vArgs) {
    // args: "meshes" <file.anim>... [--atlas [<id>=]<path>]... [--bin|--text] [--check] [--prune]
    std::vector<std::string> l_vFiles;
    std::vector<toms::anim::AtlasRef> l_vOverrides, l_vDefaults;
    std::map<std::string, std::string> l_mapMapped;   // absolute atlas path -> the one used instead
    auto l_fnAbsPath = [](const fs::path& l_oP) { return fs::absolute(l_oP).lexically_normal().u8string(); };
    auto l_fnAtlasRef = [](const std::string& l_strA) {
        toms::anim::AtlasRef l_oR{toms::anim::f_DefaultAtlasId(l_strA), l_strA};
        const size_t l_nEq = l_strA.find('=');
        if (l_nEq != std::string::npos && l_nEq > 0 && f_IsAtlasId(l_strA.substr(0, l_nEq))) l_oR = {l_strA.substr(0, l_nEq), l_strA.substr(l_nEq + 1)};
        return l_oR;
    };
    bool l_bWantBin = false, l_bWantText = false, l_bCheck = false, l_bPrune = false;
    for (size_t l_nI = 1; l_nI < l_vArgs.size(); l_nI++) {
        if (l_vArgs[l_nI] == "--bin") l_bWantBin = true;
        else if (l_vArgs[l_nI] == "--text") l_bWantText = true;
        else if (l_vArgs[l_nI] == "--check") l_bCheck = true;
        else if (l_vArgs[l_nI] == "--prune") l_bPrune = true;
        else if (l_vArgs[l_nI] == "--atlas" && l_nI + 1 < l_vArgs.size()) l_vOverrides.push_back(l_fnAtlasRef(l_vArgs[++l_nI]));
        else if (l_vArgs[l_nI] == "--default-atlas" && l_nI + 1 < l_vArgs.size()) l_vDefaults.push_back(l_fnAtlasRef(l_vArgs[++l_nI]));
        else if (l_vArgs[l_nI] == "--map" && l_nI + 1 < l_vArgs.size()) {
            const std::string l_strM = l_vArgs[++l_nI];
            const size_t l_nEq = l_strM.find('=');
            if (l_nEq == std::string::npos || l_nEq == 0 || l_nEq + 1 >= l_strM.size()) {
                std::fprintf(stderr, "--map wants <from.atlas>=<to.atlas>: %s\n", l_strM.c_str());
                return 3;
            }
            l_mapMapped[l_fnAbsPath(fs::u8path(l_strM.substr(0, l_nEq)))] = l_strM.substr(l_nEq + 1);
        } else if (l_vArgs[l_nI].rfind("--", 0) == 0) {
            std::fprintf(stderr, "unknown argument: %s\n", l_vArgs[l_nI].c_str());
            return 3;
        } else {
            l_vFiles.push_back(l_vArgs[l_nI]);
        }
    }
    if (l_vFiles.empty() || (l_bWantBin && l_bWantText)) {
        std::fputs("usage: anim_editor --headless meshes <file.anim>... [--atlas [<id>=]<file.atlas>]... [--default-atlas ...]\n"
                   "       [--map <from.atlas>=<to.atlas>]... [--bin|--text] [--check] [--prune]\n"
                   "  adds the meshes of the clips' skinned and deformed nodes (their sprites on their mesh grids) to the mesh\n"
                   "  file of each atlas they use, <atlas>.mesh.json (docs/engine/16_MESH_ANIMATION.md). --atlas: those atlases instead\n"
                   "  of each file's own; --default-atlas: for a file that names none; --map: an atlas swapped for another (an art\n"
                   "  style's copy). --bin / --text: the file's form (default: as it is; a new one\n"
                   "  binary). --prune: only what these clip files need stays. --check: write nothing, exit 1 when out of date\n",
                   stderr);
        return 3;
    }
    std::map<std::string, std::unique_ptr<AtlasWork>> l_mapWorks;   // by normalized path
    std::vector<std::string> l_vOrder;
    std::string l_strErr;
    for (const std::string& l_strFile : l_vFiles) {
        std::string l_strText;
        toms::anim::AnimFile l_oF;
        if (!f_ReadTextFile(l_strFile, l_strText) || !toms::anim::f_ParseAnim(l_strText, l_oF, &l_strErr)) {
            std::fprintf(stderr, "%s: error: %s\n", l_strFile.c_str(), l_strErr.empty() ? "cannot read the file" : l_strErr.c_str());
            return 2;
        }
        const bool l_bFromArgs = !l_vOverrides.empty() || l_oF.m_vAtlases.empty();   // as given on the command line (cwd-relative)
        const std::vector<toms::anim::AtlasRef>& l_vRefs = !l_vOverrides.empty() ? l_vOverrides : l_oF.m_vAtlases.empty() ? l_vDefaults : l_oF.m_vAtlases;
        toms::anim::AtlasSet l_oSet;
        std::vector<AtlasWork*> l_vEntryWork;
        for (const toms::anim::AtlasRef& l_oRef : l_vRefs) {
            fs::path l_oP = l_bFromArgs || f_IsAbsoluteAtlasPath(l_oRef.m_strPath)
                             ? fs::u8path(l_oRef.m_strPath)
                             : (fs::u8path(l_strFile).parent_path() / fs::u8path(l_oRef.m_strPath)).lexically_normal();
            if (auto l_itM = l_mapMapped.find(l_fnAbsPath(l_oP)); l_itM != l_mapMapped.end()) l_oP = fs::u8path(l_itM->second);
            const std::string l_strKey = l_fnAbsPath(l_oP);
            auto l_itIt = l_mapWorks.find(l_strKey);
            if (l_itIt == l_mapWorks.end()) {
                auto l_upW = std::make_unique<AtlasWork>();
                l_upW->m_strPath = l_oP.u8string();
                std::string l_strAtext;
                if (!f_ReadTextFile(l_upW->m_strPath, l_strAtext) || !toms::f_ParseAtlas(l_strAtext, l_upW->m_oAtlas, &l_strErr)) {
                    std::fprintf(stderr, "%s: error: the atlas %s cannot be read%s%s\n", l_strFile.c_str(), l_upW->m_strPath.c_str(),
                                 l_strErr.empty() ? "" : ": ", l_strErr.c_str());
                    return 2;
                }
                for (const toms::AtlasPage& l_oPg : l_upW->m_oAtlas.m_vPages) {
                    atlas::Image l_oImg;
                    if (!atlas::f_LoadImage((l_oP.parent_path() / fs::u8path(l_oPg.m_strFile)).u8string(), l_oImg, &l_strErr)) {
                        std::fprintf(stderr, "error: the page %s of %s: %s\n", l_oPg.m_strFile.c_str(), l_upW->m_strPath.c_str(), l_strErr.c_str());
                        return 2;
                    }
                    l_upW->m_vPages.push_back(std::move(l_oImg));
                }
                const std::string l_strMeshPath = toms::f_SpriteMeshPath(l_upW->m_strPath);
                l_upW->m_bHadFile = fs::exists(fs::u8path(l_strMeshPath));
                bool l_bBin = false;
                if (!f_ReadMeshFile(l_strMeshPath, l_upW->m_oDisk, &l_bBin, &l_strErr)) std::fprintf(stderr, "warning: %s (made again)\n", l_strErr.c_str());
                l_upW->m_bBinary = l_bWantBin || (!l_bWantText && (l_upW->m_bHadFile ? l_bBin : true));
                l_upW->m_oOut = l_bPrune ? toms::SpriteMeshFile() : l_upW->m_oDisk;
                if (l_bPrune)   // "hand" meshes are kept if still needed: carried over for addAnimMeshes to find
                    for (const auto& [l_strK, l_oM] : l_upW->m_oDisk.m_mapMeshes)
                        if (l_oM.m_strSource == "hand") l_upW->m_oOut.m_mapMeshes[l_strK] = l_oM;
                l_itIt = l_mapWorks.emplace(l_strKey, std::move(l_upW)).first;
                l_vOrder.push_back(l_strKey);
            }
            l_oSet.f_Add(l_itIt->second->m_oAtlas, l_oRef.m_strId);
            l_vEntryWork.push_back(l_itIt->second.get());
        }
        std::vector<toms::SpriteMeshFile> l_vPerEntry;
        for (AtlasWork* l_pW : l_vEntryWork) l_vPerEntry.push_back(l_pW->m_oOut);
        const PagePixels l_fnPixels = [&](int l_iEntry, int l_iPage, const uint8_t*& l_pRgba, int& l_iW, int& l_iH, int& l_iStride) {
            if (l_iEntry < 0 || (size_t)l_iEntry >= l_vEntryWork.size()) return false;
            const auto& l_vPp = l_vEntryWork[(size_t)l_iEntry]->m_vPages;
            if (l_iPage < 0 || (size_t)l_iPage >= l_vPp.size() || !l_vPp[(size_t)l_iPage].f_Valid()) return false;
            l_pRgba = l_vPp[(size_t)l_iPage].m_vPx.data();
            l_iW = l_vPp[(size_t)l_iPage].m_iW;
            l_iH = l_vPp[(size_t)l_iPage].m_iH;
            l_iStride = l_iW;
            return true;
        };
        MeshReport l_oRep;
        std::vector<std::pair<int, std::string>> l_vNeeded;
        f_AddAnimMeshes(l_oF, l_oSet, l_fnPixels, l_vPerEntry, &l_oRep, &l_vNeeded);
        for (const std::string& l_strS : l_oRep.m_vSkipped) std::fprintf(stderr, "%s: warning: no mesh for %s\n", l_strFile.c_str(), l_strS.c_str());
        for (size_t l_nE = 0; l_nE < l_vEntryWork.size(); l_nE++) l_vEntryWork[l_nE]->m_oOut = std::move(l_vPerEntry[l_nE]);
        for (const auto& [l_iE, l_strKey] : l_vNeeded) l_vEntryWork[(size_t)l_iE]->m_setNeeded.insert(l_strKey);
    }
    if (l_bPrune)   // carried-over "hand" meshes no clip needs go too
        for (const std::string& l_strKey : l_vOrder) {
            AtlasWork& l_oW = *l_mapWorks[l_strKey];
            for (auto l_itIt = l_oW.m_oOut.m_mapMeshes.begin(); l_itIt != l_oW.m_oOut.m_mapMeshes.end();)
                l_itIt = l_oW.m_setNeeded.count(l_itIt->first) ? std::next(l_itIt) : l_oW.m_oOut.m_mapMeshes.erase(l_itIt);
        }
    int l_iOutOfDate = 0;
    for (const std::string& l_strKey : l_vOrder) {
        AtlasWork& l_oW = *l_mapWorks[l_strKey];
        const std::string l_strMeshPath = toms::f_SpriteMeshPath(l_oW.m_strPath);
        if (l_bCheck) {
            int l_iStale = 0, l_iExtra = 0;
            for (const std::string& l_strK : l_oW.m_setNeeded) {
                const toms::SpriteMesh* l_pHave = l_oW.m_oDisk.f_Find(l_strK);
                const toms::SpriteMesh* l_pWant = l_oW.m_oOut.f_Find(l_strK);
                if (l_pWant && (!l_pHave || !(*l_pHave == *l_pWant))) l_iStale++;
            }
            if (l_bPrune)
                for (const auto& l_pairKv : l_oW.m_oDisk.m_mapMeshes) l_iExtra += !l_oW.m_setNeeded.count(l_pairKv.first);
            const bool l_bOk = l_iStale == 0 && l_iExtra == 0;
            l_iOutOfDate += !l_bOk;
            if (l_bOk) std::printf("%s: up to date (%zu mesh(es) these clips need)\n", l_strMeshPath.c_str(), l_oW.m_setNeeded.size());
            else
                std::printf("%s: out of date: %d mesh(es) missing or stale%s; run anim_editor --headless meshes, or save the clip in the anim editor\n",
                            l_strMeshPath.c_str(), l_iStale, l_iExtra ? (", " + std::to_string(l_iExtra) + " no listed clip uses").c_str() : "");
            continue;
        }
        std::vector<std::string> l_vWritten;
        if (!f_WriteMeshFile(l_strMeshPath, l_oW.m_oOut, l_oW.m_bBinary, &l_strErr, &l_vWritten)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
        std::printf("%s: %zu mesh(es), %zu needed by these clips, %s%s\n", l_strMeshPath.c_str(), l_oW.m_oOut.m_mapMeshes.size(), l_oW.m_setNeeded.size(),
                    l_oW.m_bBinary ? "binary" : "text", l_vWritten.empty() ? ", unchanged" : "");
    }
    return l_iOutOfDate ? 1 : 0;
}

}  // namespace animed
