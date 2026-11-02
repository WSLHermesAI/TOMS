// atlas_store.cpp -- see atlas_store.h.
#include "atlas_store.h"
#include "atlas_export.h"

#include <algorithm>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace atlas {

std::string f_StorePath(const Project& l_oP, const Variant* l_pV) {
    const Output& l_oO = l_pV ? l_pV->m_oOutput : l_oP.m_oOutput;
    return f_JoinPath(l_oP.f_Resolve(l_oO.m_strDir), l_oP.f_OutputName(l_oO) + ".atlas");
}

// ---- open --------------------------------------------------------------------------------------

namespace {

// Cuts the images out of one packed atlas. `want` = names to take (empty: every image region).
bool f_SliceStore(const Project& l_oP, const std::string& l_strAtlasPath, const std::vector<std::string>& l_vWant,
                std::map<std::string, ImagePtr>& l_mapInto, std::string* l_pErr) {
    BuildResult l_oPacked;
    std::string l_strE;
    if (!f_ImportLibgdx(l_strAtlasPath, l_oPacked, &l_strE)) {
        if (l_pErr) *l_pErr = "cannot read the packed atlas: " + l_strE;
        return false;
    }
    auto l_fnTake = [&](const Region& l_oR) {
        Image l_oImg = f_RenderRegion(l_oPacked, l_oR);
        if (l_oP.m_oSettings.m_bPremultiplyAlpha) f_Unpremultiply(l_oImg);
        l_mapInto[l_oR.m_strName] = std::make_shared<const Image>(std::move(l_oImg));
    };
    if (l_vWant.empty()) {
        for (const Region& l_oR : l_oPacked.m_vRegions)
            if (!l_oR.m_bChild) l_fnTake(l_oR);   // children (baked ones too) come from the project file
        return true;
    }
    for (const std::string& l_strN : l_vWant) {
        const Region* l_pR = l_oPacked.f_Find(l_strN);
        if (!l_pR || l_pR->m_bChild) {
            if (l_pErr) *l_pErr = l_strAtlasPath + " has no image '" + l_strN + "'";
            return false;
        }
        l_fnTake(*l_pR);
    }
    return true;
}

bool f_SameVisible(const Image& l_oA, const Image& l_oB) {
    if (l_oA.m_iW != l_oB.m_iW || l_oA.m_iH != l_oB.m_iH) return false;
    for (size_t l_nI = 0; l_nI + 3 < l_oA.m_vPx.size(); l_nI += 4) {
        if (l_oA.m_vPx[l_nI + 3] == 0 && l_oB.m_vPx[l_nI + 3] == 0) continue;   // invisible: colour does not matter
        if (l_oA.m_vPx[l_nI] != l_oB.m_vPx[l_nI] || l_oA.m_vPx[l_nI + 1] != l_oB.m_vPx[l_nI + 1] || l_oA.m_vPx[l_nI + 2] != l_oB.m_vPx[l_nI + 2] || l_oA.m_vPx[l_nI + 3] != l_oB.m_vPx[l_nI + 3]) return false;
    }
    return true;
}

}  // namespace

bool f_LoadEmbeddedImages(Project& l_oP, std::string* l_pErr) {
    if (!l_oP.m_bEmbedded) return true;
    l_oP.m_mapImages.clear();
    const std::string l_strBase = f_StorePath(l_oP);
    std::error_code l_oEc;
    if (!fs::is_regular_file(f_U8path(l_strBase), l_oEc)) {
        if (l_pErr) *l_pErr = "the packed atlas " + l_strBase + " is missing (it holds this project's images)";
        return false;
    }
    if (!f_SliceStore(l_oP, l_strBase, {}, l_oP.m_mapImages, l_pErr)) return false;
    for (Variant& l_oV : l_oP.m_vVariants) {
        if (l_oV.m_mapImages.empty()) continue;
        std::vector<std::string> l_vNames;
        for (const auto& l_pairKv : l_oV.m_mapImages) l_vNames.push_back(l_pairKv.first);
        if (!f_SliceStore(l_oP, f_StorePath(l_oP, &l_oV), l_vNames, l_oV.m_mapImages, l_pErr)) {
            if (l_pErr) *l_pErr = "variant " + l_oV.m_strId + ": " + *l_pErr;
            return false;
        }
    }
    return true;
}

bool f_OpenProject(const std::string& l_strPath, Project& l_oOut, std::string* l_pErr) {
    Project l_oP;
    if (!f_LoadProject(l_strPath, l_oP, l_pErr)) return false;
    if (!f_LoadEmbeddedImages(l_oP, l_pErr)) {
        if (l_pErr) *l_pErr = l_strPath + ": " + *l_pErr;
        return false;
    }
    l_oOut = std::move(l_oP);
    return true;
}

// ---- save --------------------------------------------------------------------------------------

void f_RebaseProject(Project& l_oP, const std::string& l_strNewPath) {
    // An output of "." means "next to the project": the packed atlas moves along with it.
    std::vector<std::string*> l_vPaths;
    if (l_oP.m_oOutput.m_strDir != "." && !l_oP.m_oOutput.m_strDir.empty()) l_vPaths.push_back(&l_oP.m_oOutput.m_strDir);
    for (SourceFolder& l_oF : l_oP.m_vSources) l_vPaths.push_back(&l_oF.m_strPath);
    for (SourceFolder& l_oF : l_oP.m_vReferences) l_vPaths.push_back(&l_oF.m_strPath);
    for (SpriteDef& l_oD : l_oP.m_vSprites) l_vPaths.push_back(&l_oD.m_strFile);
    for (Variant& l_oV : l_oP.m_vVariants) {
        l_vPaths.push_back(&l_oV.m_oOutput.m_strDir);
        l_vPaths.push_back(&l_oV.m_strOverrideDir);
        l_vPaths.push_back(&l_oV.m_strReference);
    }
    std::vector<std::string> l_vAbs;
    for (std::string* l_pS : l_vPaths) l_vAbs.push_back(l_pS->empty() ? std::string() : l_oP.f_Resolve(*l_pS));
    l_oP.m_strFilePath = f_NormalizePath(l_strNewPath);
    for (size_t l_nI = 0; l_nI < l_vPaths.size(); l_nI++)
        if (!l_vAbs[l_nI].empty()) *l_vPaths[l_nI] = l_oP.f_Relativize(l_vAbs[l_nI]);
}

bool f_SaveProjectAll(const std::string& l_strPath, Project& l_oP, const std::vector<std::string>& l_vAlsoFormats,
                    SaveResult* l_pResult, std::string* l_pErr) {
    Project l_oQ = l_oP;   // nothing changes in p unless everything worked
    if (f_NormalizePath(l_strPath) != l_oQ.m_strFilePath) f_RebaseProject(l_oQ, l_strPath);
    SaveResult l_oRes;
    if (l_oQ.m_bEmbedded) {
        struct Target { std::string m_strDir; std::vector<ExportFile> m_vFiles; };
        std::vector<Target> l_vTargets;
        std::set<std::string> l_setStores;
        std::vector<const Variant*> l_vAll{nullptr};
        for (const Variant& l_oV : l_oQ.m_vVariants) l_vAll.push_back(&l_oV);
        const ImageSource l_oSrc = f_FileImageSource();
        for (const Variant* l_pV : l_vAll) {
            const std::string l_strLabel = l_pV ? "variant " + l_pV->m_strId + ": " : std::string();
            if (!l_setStores.insert(f_StorePath(l_oQ, l_pV)).second) {
                if (l_pErr) *l_pErr = l_strLabel + "writes to the same packed atlas as another variant (" + f_StorePath(l_oQ, l_pV) + ")";
                return false;
            }
            BuildResult l_oB = f_Build(l_oQ, l_oSrc, BuildOptions{l_pV ? l_pV->m_strId : std::string(), true});
            for (Diagnostic l_oD : l_oB.m_vDiagnostics) {
                l_oD.m_strMessage = l_strLabel + l_oD.m_strMessage;
                l_oRes.m_vDiagnostics.push_back(l_oD);
            }
            // Every image must have a place in the packed atlas, or it would be lost.
            std::vector<std::string> l_vNames;
            if (l_pV) for (const auto& l_pairKv : l_pV->m_mapImages) l_vNames.push_back(l_pairKv.first);
            else for (const auto& l_pairKv : l_oQ.m_mapImages) l_vNames.push_back(l_pairKv.first);
            for (const std::string& l_strN : l_vNames) {
                const Region* l_pR = l_oB.f_Find(l_strN);
                if (!l_pR || l_pR->m_bChild) {
                    std::string l_strWhy;
                    for (const Diagnostic& l_oD : l_oB.m_vDiagnostics) if (l_oD.m_strSprite == l_strN) l_strWhy = ": " + l_oD.m_strMessage;
                    if (l_pErr) *l_pErr = l_strLabel + "image '" + l_strN + "' could not be packed, so it would be lost" + l_strWhy;
                    return false;
                }
            }
            const Output& l_oO = l_pV ? l_pV->m_oOutput : l_oQ.m_oOutput;
            std::vector<std::string> l_vFormats = l_oO.m_vFormats;
            for (const std::string& l_strF : l_vAlsoFormats) l_vFormats.push_back(l_strF);
            l_vFormats.push_back("atlas");   // the store
            std::sort(l_vFormats.begin(), l_vFormats.end());
            l_vFormats.erase(std::unique(l_vFormats.begin(), l_vFormats.end()), l_vFormats.end());
            Target l_oT;
            l_oT.m_strDir = l_oQ.f_Resolve(l_oO.m_strDir);
            if (!f_ExportFiles(l_oB, l_vFormats, l_oT.m_vFiles, l_pErr)) return false;
            l_vTargets.push_back(std::move(l_oT));
        }
        for (const Target& l_oT : l_vTargets) {
            std::vector<std::string> l_vChanged;
            if (!f_WriteFiles(l_oT.m_strDir, l_oT.m_vFiles, l_pErr, &l_vChanged)) return false;
            for (const std::string& l_strF : l_vChanged) l_oRes.m_vWritten.push_back(f_JoinPath(l_oT.m_strDir, l_strF));
        }
    }
    if (!f_SaveProject(l_strPath, l_oQ, l_pErr)) return false;
    l_oRes.m_vWritten.push_back(l_oQ.m_strFilePath);
    l_oP = std::move(l_oQ);
    if (l_pResult) *l_pResult = std::move(l_oRes);
    return true;
}

// ---- folders -> embedded -----------------------------------------------------------------------

int f_EmbedSources(Project& l_oP, const ImageSource& l_oSrc, std::vector<Diagnostic>* l_pDiags) {
    if (l_oP.m_bEmbedded) return 0;
    auto l_fnDiag = [&](Diagnostic::Level l_eL, const std::string& l_strS, const std::string& l_strM) {
        if (l_pDiags) l_pDiags->push_back({l_eL, l_strS, l_strM});
    };
    std::map<std::string, ImagePtr> l_mapImages;
    for (const ResolvedSprite& l_oR : f_ResolveSprites(l_oP, l_oSrc, l_pDiags)) {
        if (l_oR.m_oDef.f_IsChild()) continue;
        Image l_oImg;
        std::string l_strErr;
        if (!l_oSrc.m_fnLoad(l_oR.m_strFile, l_oImg, &l_strErr)) { l_fnDiag(Diagnostic::Error, l_oR.m_oDef.m_strName, l_strErr); continue; }
        l_mapImages[l_oR.m_oDef.m_strName] = std::make_shared<const Image>(std::move(l_oImg));
    }
    // Explicit entries keep their settings; files are now inside, excluded sprites are gone.
    std::vector<SpriteDef> l_vDefs;
    for (SpriteDef l_oD : l_oP.m_vSprites) {
        if (l_oD.m_bExclude && !l_oD.f_IsChild()) continue;
        l_oD.m_strFile.clear();
        l_oD.m_bExclude = false;
        l_vDefs.push_back(l_oD);
    }
    l_oP.m_vSprites.swap(l_vDefs);
    for (Variant& l_oV : l_oP.m_vVariants) {
        if (l_oV.m_strOverrideDir.empty()) continue;
        const std::string l_strDir = l_oP.f_Resolve(l_oV.m_strOverrideDir);
        for (const auto& l_pairKv : l_mapImages) {
            const std::string l_strF = f_JoinPath(l_strDir, l_pairKv.first + ".png");
            if (!l_oSrc.m_fnExists(l_strF)) continue;
            Image l_oImg;
            std::string l_strErr;
            if (!l_oSrc.m_fnLoad(l_strF, l_oImg, &l_strErr)) { l_fnDiag(Diagnostic::Error, l_pairKv.first, "variant " + l_oV.m_strId + ": " + l_strErr); continue; }
            l_oV.m_mapImages[l_pairKv.first] = std::make_shared<const Image>(std::move(l_oImg));
        }
        l_oV.m_strReference = l_oV.m_strOverrideDir;
        l_oV.m_strOverrideDir.clear();
    }
    l_oP.m_vReferences.insert(l_oP.m_vReferences.end(), l_oP.m_vSources.begin(), l_oP.m_vSources.end());
    l_oP.m_vSources.clear();
    l_oP.m_mapImages = std::move(l_mapImages);
    l_oP.m_bEmbedded = true;
    return (int)l_oP.m_mapImages.size();
}

// ---- references --------------------------------------------------------------------------------

std::string f_ImageNameFor(const std::string& l_strFile, const std::string& l_strFolder, const std::string& l_strPrefix) {
    std::string l_strRel = f_NormalizePath(l_strFile);
    const std::string l_strDir = f_NormalizePath(l_strFolder);
    if (!l_strDir.empty() && l_strRel.compare(0, l_strDir.size(), l_strDir) == 0) {
        l_strRel = l_strRel.substr(l_strDir.size());
        while (!l_strRel.empty() && l_strRel[0] == '/') l_strRel.erase(0, 1);
    } else {
        l_strRel = f_FileName(l_strRel);
    }
    if (l_strRel.size() > 4) {
        std::string l_strExt = l_strRel.substr(l_strRel.size() - 4);
        for (char& l_cC : l_strExt) l_cC = (char)tolower((unsigned char)l_cC);
        if (l_strExt == ".png") l_strRel.resize(l_strRel.size() - 4);
    }
    return l_strPrefix + l_strRel;
}

std::vector<ReferenceImage> f_ScanReferences(const Project& l_oP, const ImageSource& l_oSrc, const std::string& l_strVariant) {
    const Variant* l_pV = l_strVariant.empty() ? nullptr : l_oP.f_FindVariant(l_strVariant);
    std::vector<SourceFolder> l_vFolders;
    if (l_pV) { if (!l_pV->m_strReference.empty()) l_vFolders.push_back({l_pV->m_strReference, true, ""}); }
    else l_vFolders = l_oP.m_vReferences;
    std::map<std::string, ReferenceImage> l_mapFound;
    for (const SourceFolder& l_oF : l_vFolders) {
        const std::string l_strDir = l_oP.f_Resolve(l_oF.m_strPath);
        for (const std::string& l_strFile : l_oSrc.m_fnListPngs(l_strDir, l_oF.m_bRecursive)) {
            ReferenceImage l_oRi;
            l_oRi.m_strName = f_ImageNameFor(l_strFile, l_strDir, l_oF.m_strPrefix);
            l_oRi.m_strFile = l_strFile;
            if (l_mapFound.count(l_oRi.m_strName)) continue;   // the first folder wins, as in builds
            if (l_pV && !l_oP.m_mapImages.count(l_oRi.m_strName)) continue;   // a variant only replaces existing sprites
            const std::map<std::string, ImagePtr>& l_mapHave = l_pV ? l_pV->m_mapImages : l_oP.m_mapImages;
            auto l_itIt = l_mapHave.find(l_oRi.m_strName);
            if (l_itIt == l_mapHave.end() || !l_itIt->second) l_oRi.m_eStatus = ReferenceImage::New;
            else {
                Image l_oImg;
                l_oRi.m_eStatus = l_oSrc.m_fnLoad(l_strFile, l_oImg, nullptr) && f_SameVisible(l_oImg, *l_itIt->second) ? ReferenceImage::Same : ReferenceImage::Changed;
            }
            l_mapFound[l_oRi.m_strName] = l_oRi;
        }
    }
    std::vector<ReferenceImage> l_vOut;
    for (auto& l_pairKv : l_mapFound) l_vOut.push_back(l_pairKv.second);
    return l_vOut;
}

bool f_SetImage(Project& l_oP, const std::string& l_strName, const Image& l_oImg, const std::string& l_strVariant, std::string* l_pErr) {
    if (l_strName.empty()) { if (l_pErr) *l_pErr = "an image needs a name"; return false; }
    if (!l_oImg.f_Valid()) { if (l_pErr) *l_pErr = l_strName + ": the image is empty"; return false; }
    if (const SpriteDef* l_pD = l_oP.f_FindSprite(l_strName))
        if (l_pD->f_IsChild()) { if (l_pErr) *l_pErr = l_strName + " is a child sprite; it has no image of its own"; return false; }
    if (!l_strVariant.empty()) {
        Variant* l_pV = nullptr;
        for (Variant& l_oX : l_oP.m_vVariants) if (l_oX.m_strId == l_strVariant) l_pV = &l_oX;
        if (!l_pV) { if (l_pErr) *l_pErr = "unknown variant '" + l_strVariant + "'"; return false; }
        if (!l_oP.m_mapImages.count(l_strName)) { if (l_pErr) *l_pErr = "variant art for '" + l_strName + "', which the base art does not have"; return false; }
        l_pV->m_mapImages[l_strName] = std::make_shared<const Image>(l_oImg);
        return true;
    }
    l_oP.m_mapImages[l_strName] = std::make_shared<const Image>(l_oImg);
    return true;
}

// ---- back to files -----------------------------------------------------------------------------

bool f_ExtractImages(const Project& l_oP, const std::string& l_strDir, const std::string& l_strVariant, bool l_bChildren,
                   std::vector<std::string>* l_pWritten, std::string* l_pErr) {
    if (!l_strVariant.empty() && !l_oP.f_FindVariant(l_strVariant)) { if (l_pErr) *l_pErr = "unknown variant '" + l_strVariant + "'"; return false; }
    BuildResult l_oB = f_Build(l_oP, BuildOptions{l_strVariant, true});
    std::error_code l_oEc;
    for (const Region& l_oR : l_oB.m_vRegions) {
        if (l_oR.m_bChild && !l_bChildren) continue;
        const std::string l_strFile = f_JoinPath(l_strDir, l_oR.m_strName + ".png");
        fs::create_directories(f_U8path(l_strFile).parent_path(), l_oEc);
        if (!f_SavePng(l_strFile, f_RenderRegion(l_oB, l_oR), l_pErr)) return false;
        if (l_pWritten) l_pWritten->push_back(l_strFile);
    }
    return true;
}

}  // namespace atlas
