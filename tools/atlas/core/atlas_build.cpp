// atlas_build.cpp -- see atlas_build.h.
//
// Steps: resolve sprites (folders + explicit entries) -> load images (variant overrides) ->
// resolve child rects down to their root image -> trim (never cutting into a child) -> dedupe ->
// pack -> composite pages -> regions.
#include "atlas_build.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>

namespace fs = std::filesystem;

namespace atlas {

// ---- result helpers ----------------------------------------------------------------------------

int BuildResult::f_ErrorCount() const {
    int l_iN = 0;
    for (const Diagnostic& l_oD : m_vDiagnostics) l_iN += l_oD.m_eLevel == Diagnostic::Error;
    return l_iN;
}
int BuildResult::f_WarningCount() const {
    int l_iN = 0;
    for (const Diagnostic& l_oD : m_vDiagnostics) l_iN += l_oD.m_eLevel == Diagnostic::Warning;
    return l_iN;
}
bool BuildResult::f_Ok() const { return f_ErrorCount() == 0; }

const Region* BuildResult::f_Find(const std::string& l_strN) const {
    auto l_itIt = std::lower_bound(m_vRegions.begin(), m_vRegions.end(), l_strN,
                               [](const Region& l_oR, const std::string& l_strK) { return l_oR.m_strName < l_strK; });
    return l_itIt != m_vRegions.end() && l_itIt->m_strName == l_strN ? &*l_itIt : nullptr;
}

std::string f_FormatDiagnostic(const Diagnostic& l_oD) {
    const char* l_pszLv = l_oD.m_eLevel == Diagnostic::Error ? "error" : l_oD.m_eLevel == Diagnostic::Warning ? "warning" : "info";
    return std::string(l_pszLv) + ": " + (l_oD.m_strSprite.empty() ? "" : l_oD.m_strSprite + ": ") + l_oD.m_strMessage;
}

// ---- image sources -----------------------------------------------------------------------------

static std::vector<std::string> f_ListPngsFs(const std::string& l_strDir, bool l_bRecursive) {
    std::vector<std::string> l_vOut;
    std::error_code l_oEc;
    const fs::path l_oRoot = f_U8path(l_strDir);
    if (!fs::is_directory(l_oRoot, l_oEc)) return l_vOut;
    auto l_fnTake = [&](const fs::directory_entry& l_oE) {
        if (!l_oE.is_regular_file(l_oEc)) return;
        std::string l_strExt = f_U8str(l_oE.path().extension());
        for (char& l_cC : l_strExt) l_cC = (char)tolower((unsigned char)l_cC);
        if (l_strExt == ".png") l_vOut.push_back(f_NormalizePath(l_strDir) + "/" + f_U8str(l_oE.path().lexically_relative(l_oRoot)));
    };
    if (l_bRecursive) {
        for (fs::recursive_directory_iterator l_oIt(l_oRoot, l_oEc), l_oEnd; !l_oEc && l_oIt != l_oEnd; l_oIt.increment(l_oEc)) l_fnTake(*l_oIt);
    } else {
        for (fs::directory_iterator l_oIt(l_oRoot, l_oEc), l_oEnd; !l_oEc && l_oIt != l_oEnd; l_oIt.increment(l_oEc)) l_fnTake(*l_oIt);
    }
    std::sort(l_vOut.begin(), l_vOut.end());
    return l_vOut;
}

ImageSource f_FileImageSource() {
    ImageSource l_oS;
    l_oS.m_fnLoad = [](const std::string& l_strP, Image& l_oOut, std::string* l_pErr) { return f_LoadImage(l_strP, l_oOut, l_pErr); };
    l_oS.m_fnExists = [](const std::string& l_strP) { std::error_code l_oEc; return fs::is_regular_file(f_U8path(l_strP), l_oEc); };
    l_oS.m_fnListPngs = f_ListPngsFs;
    return l_oS;
}

namespace {
struct CacheEntry { uintmax_t m_u64Size = 0; fs::file_time_type m_oTime; Image m_oImg; };
struct ImageCache { std::unordered_map<std::string, CacheEntry> m_mapMap; };
}  // namespace

ImageSource f_CachedImageSource(std::shared_ptr<void>& l_spHolder) {
    if (!l_spHolder) l_spHolder = std::make_shared<ImageCache>();
    auto l_spCache = std::static_pointer_cast<ImageCache>(l_spHolder);
    ImageSource l_oS = f_FileImageSource();
    l_oS.m_fnLoad = [l_spCache](const std::string& l_strP, Image& l_oOut, std::string* l_pErr) {
        std::error_code l_oEc;
        const uintmax_t l_u64Size = fs::file_size(f_U8path(l_strP), l_oEc);
        const fs::file_time_type l_oT = fs::last_write_time(f_U8path(l_strP), l_oEc);
        auto l_itIt = l_spCache->m_mapMap.find(l_strP);
        if (!l_oEc && l_itIt != l_spCache->m_mapMap.end() && l_itIt->second.m_u64Size == l_u64Size && l_itIt->second.m_oTime == l_oT) {
            l_oOut = l_itIt->second.m_oImg;
            return true;
        }
        if (!f_LoadImage(l_strP, l_oOut, l_pErr)) return false;
        if (!l_oEc) l_spCache->m_mapMap[l_strP] = {l_u64Size, l_oT, l_oOut};
        return true;
    };
    return l_oS;
}

// ---- resolve -----------------------------------------------------------------------------------

std::vector<ResolvedSprite> f_ResolveSprites(const Project& l_oP, const ImageSource& l_oSrc, std::vector<Diagnostic>* l_pDiags) {
    auto l_fnDiag = [&](Diagnostic::Level l_eL, const std::string& l_strS, const std::string& l_strM) {
        if (l_pDiags) l_pDiags->push_back({l_eL, l_strS, l_strM});
    };
    std::map<std::string, ResolvedSprite> l_mapByName;
    if (l_oP.m_bEmbedded) {
        for (const auto& l_pairKv : l_oP.m_mapImages) {
            ResolvedSprite l_oR;
            l_oR.m_oDef.m_strName = l_pairKv.first;
            l_oR.m_spImage = l_pairKv.second;
            l_oR.m_bScanned = true;
            l_mapByName[l_pairKv.first] = l_oR;
        }
        if (!l_oP.m_vSources.empty())
            l_fnDiag(Diagnostic::Warning, "", "an embedded project does not read source folders; import from them instead");
    }
    for (const SourceFolder& l_oF : l_oP.m_bEmbedded ? std::vector<SourceFolder>() : l_oP.m_vSources) {
        const std::string l_strDir = l_oP.f_Resolve(l_oF.m_strPath);
        const std::vector<std::string> l_vFiles = l_oSrc.m_fnListPngs(l_strDir, l_oF.m_bRecursive);
        if (l_vFiles.empty()) l_fnDiag(Diagnostic::Warning, "", "source folder '" + l_oF.m_strPath + "' has no PNG files");
        for (const std::string& l_strFile : l_vFiles) {
            std::string l_strRel = l_strFile.substr(std::min(l_strFile.size(), l_strDir.size()));
            while (!l_strRel.empty() && l_strRel[0] == '/') l_strRel.erase(0, 1);
            if (l_strRel.size() > 4) l_strRel.resize(l_strRel.size() - 4);   // ".png"
            const std::string l_strName = l_oF.m_strPrefix + l_strRel;
            if (l_mapByName.count(l_strName)) { l_fnDiag(Diagnostic::Warning, l_strName, "found in two source folders; the first one is used"); continue; }
            ResolvedSprite l_oR;
            l_oR.m_oDef.m_strName = l_strName;
            l_oR.m_strFile = l_strFile;
            l_oR.m_bScanned = true;
            l_mapByName[l_strName] = l_oR;
        }
    }
    for (const SpriteDef& l_oD : l_oP.m_vSprites) {
        if (l_oD.m_strName.empty()) { l_fnDiag(Diagnostic::Error, "", "a sprite entry has no name"); continue; }
        auto l_itIt = l_mapByName.find(l_oD.m_strName);
        if (l_oP.m_bEmbedded && !l_oD.f_IsChild() && !l_oD.m_strFile.empty()) {
            l_fnDiag(Diagnostic::Warning, l_oD.m_strName, "an embedded project does not read image files; import the image instead");
            continue;
        }
        if (l_oP.m_bEmbedded && l_oD.m_bExclude) l_fnDiag(Diagnostic::Warning, l_oD.m_strName, "'exclude' does nothing in an embedded project; delete the sprite instead");
        if (l_oD.f_IsChild() || !l_oD.m_strFile.empty()) {
            if (l_itIt != l_mapByName.end() && !l_itIt->second.m_bScanned) { l_fnDiag(Diagnostic::Error, l_oD.m_strName, "two sprite entries have this name"); continue; }
            if (l_itIt != l_mapByName.end() && l_oD.f_IsChild()) { l_fnDiag(Diagnostic::Error, l_oD.m_strName, "a child sprite has the same name as an image"); continue; }
            ResolvedSprite l_oR;
            l_oR.m_oDef = l_oD;
            l_oR.m_strFile = l_oD.f_IsChild() ? std::string() : l_oP.f_Resolve(l_oD.m_strFile);
            l_mapByName[l_oD.m_strName] = l_oR;
        } else if (l_itIt != l_mapByName.end()) {
            ResolvedSprite& l_oR = l_itIt->second;   // settings for a scanned sprite
            const std::string l_strKeepName = l_oR.m_oDef.m_strName;
            l_oR.m_oDef = l_oD;
            l_oR.m_oDef.m_strName = l_strKeepName;
        } else {
            l_fnDiag(Diagnostic::Warning, l_oD.m_strName, "sprite entry has no file, no parent and matches no scanned image");
        }
    }
    std::vector<ResolvedSprite> l_vOut;
    for (auto& l_pairKv : l_mapByName)
        if (!l_pairKv.second.m_oDef.m_bExclude || l_oP.m_bEmbedded) l_vOut.push_back(l_pairKv.second);
    return l_vOut;
}

// ---- build -------------------------------------------------------------------------------------

namespace {

struct Entry {                  // one sprite during the build
    ResolvedSprite m_oRs;
    bool m_bChild = false;
    int m_iRoot = -1;              // index of the root image entry (children)
    IRect m_oAbs;                  // children: rect in the root's original pixels (variant scaled)
    IRect m_oLocal;                // children: rect relative to the direct parent (variant scaled)
    Image m_oImg;                  // packed entries: the original image (baked child: its crop)
    IRect m_oTrim;                 // packed entries: kept rect inside img
    bool m_bPacked = false;        // has its own place in the atlas
    int m_iAlias = -1;             // dedupe target entry
    int m_iItem = -1;              // pack item index
    int m_iPage = -1, m_iFx = 0, m_iFy = 0;   // frame position
};

int f_Roundi(double l_dV) { return (int)std::floor(l_dV + 0.5); }

}  // namespace

BuildResult f_Build(const Project& l_oP, const ImageSource& l_oSrc, const BuildOptions& l_oOpt) {
    BuildResult l_oRes;
    l_oRes.m_oSettings = l_oP.m_oSettings;
    const Settings& l_oS = l_oP.m_oSettings;
    auto l_fnDiag = [&](Diagnostic::Level l_eL, const std::string& l_strS, const std::string& l_strM) { l_oRes.m_vDiagnostics.push_back({l_eL, l_strS, l_strM}); };

    const Variant* l_pVariant = nullptr;
    if (!l_oOpt.m_strVariant.empty()) {
        l_pVariant = l_oP.f_FindVariant(l_oOpt.m_strVariant);
        if (!l_pVariant) { l_fnDiag(Diagnostic::Error, "", "unknown variant '" + l_oOpt.m_strVariant + "'"); return l_oRes; }
    }
    l_oRes.m_strName = l_oP.f_OutputName(l_pVariant ? l_pVariant->m_oOutput : l_oP.m_oOutput);
    l_oRes.m_vAnimations = l_oP.m_vAnimations;

    std::vector<ResolvedSprite> l_vResolved = f_ResolveSprites(l_oP, l_oSrc, &l_oRes.m_vDiagnostics);
    std::vector<Entry> l_vE(l_vResolved.size());
    std::unordered_map<std::string, int> l_mapIndex;
    for (size_t l_nI = 0; l_nI < l_vResolved.size(); l_nI++) {
        l_vE[l_nI].m_oRs = l_vResolved[l_nI];
        l_vE[l_nI].m_bChild = l_vResolved[l_nI].m_oDef.f_IsChild();
        l_mapIndex[l_vResolved[l_nI].m_oDef.m_strName] = (int)l_nI;
    }

    // Images (and the variant scale per root: variant size / base size).
    std::vector<double> l_vSx(l_vE.size(), 1.0), l_vSy(l_vE.size(), 1.0);
    for (size_t l_nI = 0; l_nI < l_vE.size(); l_nI++) {
        Entry& l_oE = l_vE[l_nI];
        if (l_oE.m_bChild) continue;
        std::string l_strErr;
        Image l_oBase;
        if (l_oP.m_bEmbedded) {
            if (!l_oE.m_oRs.m_spImage || !l_oE.m_oRs.m_spImage->f_Valid()) { l_fnDiag(Diagnostic::Error, l_oE.m_oRs.m_oDef.m_strName, "has no pixels (the packed atlas did not load)"); continue; }
            l_oBase = *l_oE.m_oRs.m_spImage;
        } else if (!l_oSrc.m_fnLoad(l_oE.m_oRs.m_strFile, l_oBase, &l_strErr)) {
            l_fnDiag(Diagnostic::Error, l_oE.m_oRs.m_oDef.m_strName, l_strErr);
            continue;
        }
        l_oE.m_oImg = std::move(l_oBase);
        if (l_pVariant) {
            auto l_itVi = l_pVariant->m_mapImages.find(l_oE.m_oRs.m_oDef.m_strName);
            const std::string l_strOv = l_pVariant->m_strOverrideDir.empty() ? std::string()
                                 : f_JoinPath(l_oP.f_Resolve(l_pVariant->m_strOverrideDir), l_oE.m_oRs.m_oDef.m_strName + ".png");
            if (l_itVi != l_pVariant->m_mapImages.end()) {
                if (!l_itVi->second || !l_itVi->second->f_Valid()) { l_fnDiag(Diagnostic::Error, l_oE.m_oRs.m_oDef.m_strName, "variant art has no pixels"); continue; }
                l_vSx[l_nI] = (double)l_itVi->second->m_iW / l_oE.m_oImg.m_iW;
                l_vSy[l_nI] = (double)l_itVi->second->m_iH / l_oE.m_oImg.m_iH;
                l_oE.m_oImg = *l_itVi->second;
                l_oE.m_oRs.m_strFile.clear();
            } else if (!l_strOv.empty() && l_oSrc.m_fnExists(l_strOv)) {
                Image l_oVimg;
                if (!l_oSrc.m_fnLoad(l_strOv, l_oVimg, &l_strErr)) { l_fnDiag(Diagnostic::Error, l_oE.m_oRs.m_oDef.m_strName, l_strErr); continue; }
                l_vSx[l_nI] = (double)l_oVimg.m_iW / l_oE.m_oImg.m_iW;
                l_vSy[l_nI] = (double)l_oVimg.m_iH / l_oE.m_oImg.m_iH;
                l_oE.m_oImg = std::move(l_oVimg);
                l_oE.m_oRs.m_strFile = l_strOv;
            }
        }
        l_oE.m_bPacked = true;
    }

    // Children: walk up to the root image, adding up the rects.
    for (size_t l_nI = 0; l_nI < l_vE.size(); l_nI++) {
        if (!l_vE[l_nI].m_bChild) continue;
        const std::string& l_strName = l_vE[l_nI].m_oRs.m_oDef.m_strName;
        IRect l_oAbs = l_vE[l_nI].m_oRs.m_oDef.m_oRect;
        std::set<int> l_setSeen{(int)l_nI};
        int l_iCur = (int)l_nI, l_iRoot = -1;
        bool l_bBad = false;
        if (l_oAbs.m_iW <= 0 || l_oAbs.m_iH <= 0) { l_fnDiag(Diagnostic::Error, l_strName, "child rect must have a positive size"); continue; }
        while (true) {
            const std::string& l_strPar = l_vE[l_iCur].m_oRs.m_oDef.m_strParent;
            auto l_itIt = l_mapIndex.find(l_strPar);
            if (l_itIt == l_mapIndex.end()) { l_fnDiag(Diagnostic::Error, l_strName, "parent '" + l_strPar + "' does not exist"); l_bBad = true; break; }
            const int l_iPi = l_itIt->second;
            if (!l_setSeen.insert(l_iPi).second) { l_fnDiag(Diagnostic::Error, l_strName, "parent chain loops back on itself"); l_bBad = true; break; }
            // The rect of `cur` must lie inside its parent.
            const IRect& l_oCurRect = l_vE[l_iCur].m_oRs.m_oDef.m_oRect;
            int l_iPw, l_iPh;
            if (l_vE[l_iPi].m_bChild) { l_iPw = l_vE[l_iPi].m_oRs.m_oDef.m_oRect.m_iW; l_iPh = l_vE[l_iPi].m_oRs.m_oDef.m_oRect.m_iH; }
            else if (l_vE[l_iPi].m_oImg.f_Valid()) { l_iPw = (int)std::lround(l_vE[l_iPi].m_oImg.m_iW / l_vSx[l_iPi]); l_iPh = (int)std::lround(l_vE[l_iPi].m_oImg.m_iH / l_vSy[l_iPi]); }
            else { l_bBad = true; break; }   // root failed to load: already reported
            if (l_oCurRect.m_iX < 0 || l_oCurRect.m_iY < 0 || l_oCurRect.f_Right() > l_iPw || l_oCurRect.f_Bottom() > l_iPh) {
                l_fnDiag(Diagnostic::Error, l_vE[l_iCur].m_oRs.m_oDef.m_strName, "rect " + std::to_string(l_oCurRect.m_iX) + "," + std::to_string(l_oCurRect.m_iY) + "," +
                     std::to_string(l_oCurRect.m_iW) + "," + std::to_string(l_oCurRect.m_iH) + " lies outside parent '" + l_strPar + "' (" +
                     std::to_string(l_iPw) + "x" + std::to_string(l_iPh) + ")");
                l_bBad = true;
                break;
            }
            if (!l_vE[l_iPi].m_bChild) { l_iRoot = l_iPi; break; }
            l_oAbs.m_iX += l_vE[l_iPi].m_oRs.m_oDef.m_oRect.m_iX;
            l_oAbs.m_iY += l_vE[l_iPi].m_oRs.m_oDef.m_oRect.m_iY;
            l_iCur = l_iPi;
        }
        if (l_bBad || l_iRoot < 0) continue;
        Entry& l_oE = l_vE[l_nI];
        l_oE.m_iRoot = l_iRoot;
        // Variant art may be another size: scale the rect with the root image (edges rounded, so
        // neighbouring children still meet exactly).
        auto l_fnScaleRect = [&](const IRect& l_oR) {
            const int l_iX0 = f_Roundi(l_oR.m_iX * l_vSx[l_iRoot]), l_iY0 = f_Roundi(l_oR.m_iY * l_vSy[l_iRoot]);
            return IRect{l_iX0, l_iY0, f_Roundi(l_oR.f_Right() * l_vSx[l_iRoot]) - l_iX0, f_Roundi(l_oR.f_Bottom() * l_vSy[l_iRoot]) - l_iY0};
        };
        l_oE.m_oAbs = l_fnScaleRect(l_oAbs);
        const IRect l_oParentAbs = l_fnScaleRect({l_oAbs.m_iX - l_oE.m_oRs.m_oDef.m_oRect.m_iX, l_oAbs.m_iY - l_oE.m_oRs.m_oDef.m_oRect.m_iY, 0, 0});
        l_oE.m_oLocal = {l_oE.m_oAbs.m_iX - l_oParentAbs.m_iX, l_oE.m_oAbs.m_iY - l_oParentAbs.m_iY, l_oE.m_oAbs.m_iW, l_oE.m_oAbs.m_iH};
        if (l_oE.m_oRs.m_oDef.m_bBake) {
            l_oE.m_oImg = f_Crop(l_vE[l_iRoot].m_oImg, l_oE.m_oAbs);
            l_oE.m_oTrim = {0, 0, l_oE.m_oImg.m_iW, l_oE.m_oImg.m_iH};
            l_oE.m_bPacked = true;
        }
    }
    // A child whose own parent failed is not resolved: report once so nothing goes missing silently.
    for (Entry& l_oE : l_vE)
        if (l_oE.m_bChild && l_oE.m_iRoot < 0 && !l_oE.m_oRs.m_oDef.m_strParent.empty()) {
            bool l_bReported = false;
            for (const Diagnostic& l_oD : l_oRes.m_vDiagnostics) if (l_oD.m_strSprite == l_oE.m_oRs.m_oDef.m_strName) l_bReported = true;
            if (!l_bReported) l_fnDiag(Diagnostic::Error, l_oE.m_oRs.m_oDef.m_strName, "could not resolve its parent chain");
        }

    // Trim. A root never loses pixels that one of its (non-baked) children uses.
    std::vector<IRect> l_vKeep(l_vE.size());
    for (size_t l_nI = 0; l_nI < l_vE.size(); l_nI++)
        if (l_vE[l_nI].m_bChild && l_vE[l_nI].m_iRoot >= 0 && !l_vE[l_nI].m_oRs.m_oDef.m_bBake) l_vKeep[l_vE[l_nI].m_iRoot] = f_Unite(l_vKeep[l_vE[l_nI].m_iRoot], l_vE[l_nI].m_oAbs);
    for (size_t l_nI = 0; l_nI < l_vE.size(); l_nI++) {
        Entry& l_oE = l_vE[l_nI];
        if (!l_oE.m_bPacked || l_oE.m_bChild) continue;
        if (!l_oE.m_oImg.f_Valid()) { l_oE.m_bPacked = false; continue; }
        const bool l_bTrimOn = l_oE.m_oRs.m_oDef.m_iTrim < 0 ? l_oS.m_bTrim : l_oE.m_oRs.m_oDef.m_iTrim == 1;
        IRect l_oR = l_bTrimOn ? f_OpaqueBounds(l_oE.m_oImg, l_oS.m_iAlphaThreshold) : IRect{0, 0, l_oE.m_oImg.m_iW, l_oE.m_oImg.m_iH};
        l_oR = f_Unite(l_oR, l_vKeep[l_nI]);
        if (l_oR.f_Empty()) { l_oR = {0, 0, 1, 1}; l_fnDiag(Diagnostic::Info, l_oE.m_oRs.m_oDef.m_strName, "image is fully transparent (kept as 1x1)"); }
        l_oE.m_oTrim = l_oR;
    }

    // Dedupe: identical kept pixels share one place. Pinned sprites always keep their own.
    if (l_oS.m_bDedupe) {
        std::unordered_map<uint64_t, std::vector<int>> l_mapByHash;
        for (size_t l_nI = 0; l_nI < l_vE.size(); l_nI++) {
            Entry& l_oE = l_vE[l_nI];
            if (!l_oE.m_bPacked || l_oE.m_oRs.m_oDef.m_bPinned) continue;
            const uint64_t l_u64H = f_HashPixels(l_oE.m_oImg, l_oE.m_oTrim);
            int l_iTarget = -1;
            for (int l_iJ : l_mapByHash[l_u64H])
                if (f_SamePixels(l_vE[l_iJ].m_oImg, l_vE[l_iJ].m_oTrim, l_oE.m_oImg, l_oE.m_oTrim)) { l_iTarget = l_iJ; break; }
            if (l_iTarget >= 0) { l_oE.m_iAlias = l_iTarget; l_oE.m_bPacked = false; }
            else l_mapByHash[l_u64H].push_back((int)l_nI);
        }
    }

    // Pack.
    std::vector<PackItem> l_vItems;
    std::vector<int> l_vItemEntry;
    const int l_iEx = std::max(0, l_oS.m_iExtrude);
    for (size_t l_nI = 0; l_nI < l_vE.size(); l_nI++) {
        Entry& l_oE = l_vE[l_nI];
        if (!l_oE.m_bPacked) continue;
        PackItem l_oIt;
        l_oIt.m_iW = l_oE.m_oTrim.m_iW + 2 * l_iEx;
        l_oIt.m_iH = l_oE.m_oTrim.m_iH + 2 * l_iEx;
        if (l_oE.m_oRs.m_oDef.m_bPinned) {
            l_oIt.m_bFixed = true;
            l_oIt.m_iFixedPage = l_oE.m_oRs.m_oDef.m_iPinPage;
            l_oIt.m_iFixedX = l_oE.m_oRs.m_oDef.m_iPinX - l_iEx;
            l_oIt.m_iFixedY = l_oE.m_oRs.m_oDef.m_iPinY - l_iEx;
        }
        l_oE.m_iItem = (int)l_vItems.size();
        l_vItems.push_back(l_oIt);
        l_vItemEntry.push_back((int)l_nI);
    }
    PackSettings l_oPs;
    l_oPs.m_iMaxWidth = l_oS.m_iMaxWidth; l_oPs.m_iMaxHeight = l_oS.m_iMaxHeight;
    l_oPs.m_iMinWidth = std::min(l_oS.m_iMinWidth, l_oS.m_iMaxWidth); l_oPs.m_iMinHeight = std::min(l_oS.m_iMinHeight, l_oS.m_iMaxHeight);
    l_oPs.m_bPowerOfTwo = l_oS.m_bPowerOfTwo; l_oPs.m_bSquare = l_oS.m_bSquare; l_oPs.m_bFixedSize = l_oS.m_bFixedSize;
    l_oPs.m_iBorder = l_oS.m_iBorder; l_oPs.m_iPadding = l_oS.m_iPadding; l_oPs.m_eHeuristic = l_oS.m_eHeuristic;
    PackResult l_oPr = f_PackPages(l_vItems, l_oPs);
    for (const std::string& l_strM : l_oPr.m_vErrors) l_fnDiag(Diagnostic::Error, "", l_strM);
    for (size_t l_nK = 0; l_nK < l_vItems.size(); l_nK++) {
        Entry& l_oE = l_vE[l_vItemEntry[l_nK]];
        const PackPlacement& l_oPl = l_oPr.m_vPlacements[l_nK];
        if (l_oPl.m_iPage < 0) {
            l_fnDiag(Diagnostic::Error, l_oE.m_oRs.m_oDef.m_strName, "does not fit in a " + std::to_string(l_oS.m_iMaxWidth) + "x" +
                 std::to_string(l_oS.m_iMaxHeight) + " page (" + std::to_string(l_oE.m_oTrim.m_iW) + "x" + std::to_string(l_oE.m_oTrim.m_iH) + ")");
            continue;
        }
        l_oE.m_iPage = l_oPl.m_iPage;
        l_oE.m_iFx = l_oPl.m_iX + l_iEx;
        l_oE.m_iFy = l_oPl.m_iY + l_iEx;
    }

    // Pages.
    for (size_t l_nPg = 0; l_nPg < l_oPr.m_vPageSizes.size(); l_nPg++) {
        Page l_oPage;
        l_oPage.m_strFile = l_oRes.m_strName + (l_nPg == 0 ? std::string() : "_" + std::to_string(l_nPg)) + ".png";
        if (l_oOpt.m_bComposite) l_oPage.m_oImage = Image(l_oPr.m_vPageSizes[l_nPg].m_iW, l_oPr.m_vPageSizes[l_nPg].m_iH);
        else { l_oPage.m_oImage.m_iW = l_oPr.m_vPageSizes[l_nPg].m_iW; l_oPage.m_oImage.m_iH = l_oPr.m_vPageSizes[l_nPg].m_iH; }
        l_oRes.m_vPages.push_back(std::move(l_oPage));
    }
    if (l_oOpt.m_bComposite) {
        for (Entry& l_oE : l_vE) {
            if (!l_oE.m_bPacked || l_oE.m_iPage < 0) continue;
            Image& l_oDst = l_oRes.m_vPages[l_oE.m_iPage].m_oImage;
            f_Blit(l_oDst, l_oE.m_oImg, l_oE.m_oTrim, l_oE.m_iFx, l_oE.m_iFy);
            f_Extrude(l_oDst, {l_oE.m_iFx, l_oE.m_iFy, l_oE.m_oTrim.m_iW, l_oE.m_oTrim.m_iH}, l_iEx);
        }
        if (l_oS.m_bPremultiplyAlpha)
            for (Page& l_oPg : l_oRes.m_vPages) f_Premultiply(l_oPg.m_oImage);
    }

    // Regions.
    for (size_t l_nI = 0; l_nI < l_vE.size(); l_nI++) {
        const Entry& l_oE = l_vE[l_nI];
        const SpriteDef& l_oD = l_oE.m_oRs.m_oDef;
        Region l_oR;
        l_oR.m_strName = l_oD.m_strName;
        l_oR.m_vTags = l_oD.m_vTags;
        l_oR.m_arrPivot[0] = l_oD.m_bHasPivot ? l_oD.m_arrPivot[0] : l_oS.m_arrDefaultPivot[0];
        l_oR.m_arrPivot[1] = l_oD.m_bHasPivot ? l_oD.m_arrPivot[1] : l_oS.m_arrDefaultPivot[1];
        l_oR.m_bHasSplit = l_oD.m_bHasSplit;
        for (int l_iK = 0; l_iK < 4; l_iK++) l_oR.m_arrSplit[l_iK] = l_oD.m_arrSplit[l_iK];
        if (l_oE.m_bChild) {
            if (l_oE.m_iRoot < 0) continue;
            l_oR.m_bChild = true;
            l_oR.m_strParent = l_oD.m_strParent;
            l_oR.m_oLocal = l_oE.m_oLocal;
            l_oR.m_iOrigW = l_oE.m_oAbs.m_iW; l_oR.m_iOrigH = l_oE.m_oAbs.m_iH;
            if (l_oD.m_bBake) {
                if (l_oE.m_iPage < 0) continue;
                l_oR.m_bBaked = true;
                l_oR.m_iPage = l_oE.m_iPage;
                l_oR.m_oFrame = {l_oE.m_iFx, l_oE.m_iFy, l_oE.m_oTrim.m_iW, l_oE.m_oTrim.m_iH};
            } else {
                const Entry& l_oRoot = l_vE[l_oE.m_iRoot];
                const Entry& l_oPlaced = l_oRoot.m_iAlias >= 0 ? l_vE[l_oRoot.m_iAlias] : l_oRoot;
                if (l_oPlaced.m_iPage < 0) continue;
                l_oR.m_iPage = l_oPlaced.m_iPage;
                l_oR.m_oFrame = {l_oPlaced.m_iFx + l_oE.m_oAbs.m_iX - l_oRoot.m_oTrim.m_iX, l_oPlaced.m_iFy + l_oE.m_oAbs.m_iY - l_oRoot.m_oTrim.m_iY, l_oE.m_oAbs.m_iW, l_oE.m_oAbs.m_iH};
            }
        } else {
            if (!l_oE.m_oImg.f_Valid()) continue;
            const Entry& l_oPlaced = l_oE.m_iAlias >= 0 ? l_vE[l_oE.m_iAlias] : l_oE;
            if (l_oPlaced.m_iPage < 0) continue;
            l_oR.m_iPage = l_oPlaced.m_iPage;
            l_oR.m_oFrame = {l_oPlaced.m_iFx, l_oPlaced.m_iFy, l_oE.m_oTrim.m_iW, l_oE.m_oTrim.m_iH};
            l_oR.m_iOffsetX = l_oE.m_oTrim.m_iX; l_oR.m_iOffsetY = l_oE.m_oTrim.m_iY;
            l_oR.m_iOrigW = l_oE.m_oImg.m_iW; l_oR.m_iOrigH = l_oE.m_oImg.m_iH;
            l_oR.m_strSourceFile = l_oE.m_oRs.m_strFile;
            if (l_oE.m_iAlias >= 0) l_oR.m_strAliasOf = l_vE[l_oE.m_iAlias].m_oRs.m_oDef.m_strName;
        }
        if (l_oR.m_bHasSplit && (l_oR.m_arrSplit[0] + l_oR.m_arrSplit[1] > l_oR.m_iOrigW || l_oR.m_arrSplit[2] + l_oR.m_arrSplit[3] > l_oR.m_iOrigH || l_oR.m_arrSplit[0] < 0 ||
                           l_oR.m_arrSplit[1] < 0 || l_oR.m_arrSplit[2] < 0 || l_oR.m_arrSplit[3] < 0))
            l_fnDiag(Diagnostic::Warning, l_oR.m_strName, "9-slice borders are larger than the sprite");
        if (l_oR.m_strName == l_oRes.m_strName)
            l_fnDiag(Diagnostic::Warning, l_oR.m_strName, "has the same name as the atlas (the FM79979 .pi loader renames it)");
        l_oRes.m_vRegions.push_back(std::move(l_oR));
    }
    std::sort(l_oRes.m_vRegions.begin(), l_oRes.m_vRegions.end(), [](const Region& l_oA, const Region& l_oB) { return l_oA.m_strName < l_oB.m_strName; });

    for (const Animation& l_oA : l_oRes.m_vAnimations) {
        if (l_oA.m_strName.empty()) l_fnDiag(Diagnostic::Error, "", "an animation has no name");
        for (const AnimFrame& l_oF : l_oA.m_vFrames)
            if (!l_oRes.f_Find(l_oF.m_strSprite)) l_fnDiag(Diagnostic::Error, l_oA.m_strName, "animation frame '" + l_oF.m_strSprite + "' is not a sprite");
    }
    if (l_oRes.m_vRegions.empty() && l_oRes.f_ErrorCount() == 0) l_fnDiag(Diagnostic::Warning, "", "the atlas has no sprites");
    return l_oRes;
}

}  // namespace atlas
