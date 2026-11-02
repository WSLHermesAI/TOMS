// anim_check.cpp -- see anim_check.h.
#include "anim_check.h"
#include "mpdi_convert.h"

#include "anim_keys.h"
#include "anim_meshes.h"
#include "anim_recipes.h"
#include "anim_player.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace animed {

namespace fs = std::filesystem;
using toms::anim::AnimFile;
using toms::anim::Clip;

namespace {

void f_Add(std::vector<Problem>& l_vOut, Problem::Level l_eL, int l_iClip, const std::vector<int>& l_vPath, const std::string& l_strWhere,
         const std::string& l_strMsg) {
    Problem l_oP;
    l_oP.m_eLevel = l_eL;
    l_oP.m_iClip = l_iClip;
    l_oP.m_vPath = l_vPath;
    l_oP.m_strWhere = l_strWhere;
    l_oP.m_strMessage = l_strMsg;
    l_vOut.push_back(l_oP);
}

bool f_Finite4(const glm::vec4& l_v4V) { return std::isfinite(l_v4V.x) && std::isfinite(l_v4V.y) && std::isfinite(l_v4V.z) && std::isfinite(l_v4V.w); }

std::string f_Seconds(float l_fT) {
    char l_pszBuf[32];
    std::snprintf(l_pszBuf, sizeof l_pszBuf, "%.3g s", l_fT);
    return l_pszBuf;
}

// What sprite references resolve to: the file's atlases (by id) and the loaded ones in lookup order.
struct SpriteLookup {
    std::vector<const CheckAtlas*> m_vAll;      // every atlas of the file, loaded or not
    std::vector<const CheckAtlas*> m_vLoaded;
    bool m_bComplete = true;      // every atlas loaded
    toms::anim::AtlasSet m_oSet;  // the loaded ones with their meshes, as the game looks sprites up
    bool f_Check() const { return !m_vLoaded.empty(); }
    const CheckAtlas* f_ById(const std::string& l_strId) const {
        for (const CheckAtlas* l_pA : m_vAll)
            if (l_pA->m_strId == l_strId) return l_pA;
        return nullptr;
    }
    // The atlases that have the sprite, in lookup order.
    std::vector<const CheckAtlas*> f_Owners(const std::string& l_strS) const {
        std::vector<const CheckAtlas*> l_vOut;
        for (const CheckAtlas* l_pA : m_vLoaded)
            if (l_pA->m_pAtlas->f_Find(l_strS)) l_vOut.push_back(l_pA);
        return l_vOut;
    }
};

std::string f_IdList(const std::vector<const CheckAtlas*>& l_vV) {
    std::string l_strS;
    for (size_t l_nI = 0; l_nI < l_vV.size(); l_nI++) l_strS += (l_nI ? ", " : "") + l_vV[l_nI]->m_strId;
    return l_strS;
}

void f_CheckSprite(const std::string& l_strRef, int l_iClip, const std::vector<int>& l_vPath, const std::string& l_strWhere,
                 const SpriteLookup& l_oAtlases, std::vector<Problem>& l_vOut) {
    const toms::anim::SpriteRef l_oR = toms::anim::f_ParseSpriteRef(l_strRef);
    if (!l_oR.m_strAtlas.empty()) {   // "id:name": that atlas, no fallback
        if (l_oAtlases.m_vAll.empty()) return;   // "no atlas" is reported once for the file
        const CheckAtlas* l_pA = l_oAtlases.f_ById(l_oR.m_strAtlas);
        if (!l_pA) {
            f_Add(l_vOut, Problem::Error, l_iClip, l_vPath, l_strWhere,
                "sprite '" + l_strRef + "': no atlas has the id '" + l_oR.m_strAtlas + "' (the atlases are: " + f_IdList(l_oAtlases.m_vAll) + ")");
            return;
        }
        if (!l_pA->m_pAtlas) return;   // it did not load: reported for the atlas
        if (!l_pA->m_pAtlas->f_Find(l_oR.m_strName)) {
            std::string l_strMsg = "sprite '" + l_strRef + "': the atlas '" + l_oR.m_strAtlas + "' (" + l_pA->m_strPath + ") has no sprite '" + l_oR.m_strName + "'";
            const std::vector<const CheckAtlas*> l_vOthers = l_oAtlases.f_Owners(l_oR.m_strName);
            if (!l_vOthers.empty()) l_strMsg += " (" + f_IdList(l_vOthers) + " has)";
            f_Add(l_vOut, Problem::Error, l_iClip, l_vPath, l_strWhere, l_strMsg);
        }
        return;
    }
    if (!l_oAtlases.f_Check()) return;
    const std::vector<const CheckAtlas*> l_vOwners = l_oAtlases.f_Owners(l_oR.m_strName);
    if (l_vOwners.empty())
        f_Add(l_vOut, Problem::Error, l_iClip, l_vPath, l_strWhere,
            "sprite '" + l_strRef + (l_oAtlases.m_bComplete ? "' is in no atlas" : "' is in none of the atlases that loaded"));
    else if (l_vOwners.size() > 1)
        f_Add(l_vOut, Problem::Warning, l_iClip, l_vPath, l_strWhere,
            "sprite '" + l_strRef + "' is in several atlases (" + f_IdList(l_vOwners) + ") and the reference does not name one: " +
                l_vOwners.front()->m_strId + "'s is used. Pick it in the Sprites dock, or Key > Qualify Sprite References, to store the atlas");
}

bool f_HasNodeNamed(const Node& l_oN, const std::string& l_strName) {
    if (l_oN.m_strName == l_strName) return true;
    for (const Node& l_oC : l_oN.m_vChildren) if (f_HasNodeNamed(l_oC, l_strName)) return true;
    return false;
}

void f_CheckNode(const Node& l_oN, int l_iClip, const Clip& l_oC, std::vector<int>& l_vPath, const std::string& l_strParentWhere,
               const SpriteLookup& l_oAtlases, std::vector<Problem>& l_vOut) {
    const std::string l_strWhere = l_strParentWhere + "/" + (l_oN.m_strName.empty() ? std::string("(unnamed)") : l_oN.m_strName);
    if (l_oN.m_strName.empty()) f_Add(l_vOut, Problem::Info, l_iClip, l_vPath, l_strWhere, "node has no name");
    {
        std::set<std::string> l_setUsed;
        if (!l_oN.m_strSprite.empty()) l_setUsed.insert(l_oN.m_strSprite);
        for (const auto& l_oK : l_oN.m_vSpriteKeys)
            if (!l_oK.m_oV.empty()) l_setUsed.insert(l_oK.m_oV);
        for (const std::string& l_strS : l_setUsed) f_CheckSprite(l_strS, l_iClip, l_vPath, l_strWhere, l_oAtlases, l_vOut);
    }
    // A skin (docs/engine/16_MESH_ANIMATION.md): its sprites need a mesh to bend; every bone must be a node of the clip (a bone
    // no node has bends nothing).
    if (l_oN.f_Skinned() && l_oAtlases.f_Check()) {
        Node l_oAlone = l_oN;   // this node only: skinnedWithoutMesh walks the children too, and they get their own turn
        l_oAlone.m_vChildren.clear();
        Clip l_oOne;
        l_oOne.m_oRoot = std::move(l_oAlone);
        for (const toms::anim::MissingMesh& l_oM : toms::anim::f_SkinnedWithoutMesh(l_oOne, l_oAtlases.m_oSet)) {
            int l_iEntry = -1;
            const std::string l_strKey = l_oAtlases.m_oSet.f_MeshKeyFor(l_oM.m_strSprite, l_oN, &l_iEntry);
            const CheckAtlas* l_pA = l_iEntry >= 0 && l_iEntry < (int)l_oAtlases.m_vLoaded.size() ? l_oAtlases.m_vLoaded[(size_t)l_iEntry] : nullptr;
            const std::string l_strMeshFile = l_pA ? toms::f_SpriteMeshPath(l_pA->m_strPath) : std::string("<atlas>.mesh.json");
            f_Add(l_vOut, Problem::Error, l_iClip, l_vPath, l_strWhere,
                "skinned, but the sprite '" + l_oM.m_strSprite + "' has no mesh '" + l_strKey + "' in " + l_strMeshFile +
                    (l_pA && !l_pA->m_pMeshes ? " (there is no such file)" : "") +
                    ": it is drawn as a flat quad, not bent. Save the clip in the anim editor, or run anim_editor --headless meshes");
        }
    }
    for (const std::string& l_strBone : l_oN.m_vSkinBones)
        if (!f_HasNodeNamed(l_oC.m_oRoot, l_strBone))
            f_Add(l_vOut, Problem::Warning, l_iClip, l_vPath, l_strWhere, "skin bone '" + l_strBone + "' is no node of this clip: it holds the mesh still");
    for (Channel l_eCh : g_arrAllChannels) {
        if (const int l_iO = f_OverlappingKeys(l_oN, l_eCh))
            f_Add(l_vOut, Problem::Warning, l_iClip, l_vPath, l_strWhere,
                std::to_string(l_iO) + " overlapping " + f_ChannelName(l_eCh) + " key time(s): the later key is never reached");
        for (float l_fT : f_KeyTimes(l_oN, l_eCh)) {
            if (l_fT < 0 || !std::isfinite(l_fT)) {
                f_Add(l_vOut, Problem::Error, l_iClip, l_vPath, l_strWhere, std::string(f_ChannelName(l_eCh)) + " key at a negative or invalid time");
                break;
            }
            if (l_oC.m_fLength > 0 && l_fT > l_oC.m_fLength + g_fTimeEps) {
                f_Add(l_vOut, Problem::Warning, l_iClip, l_vPath, l_strWhere,
                    std::string(f_ChannelName(l_eCh)) + " key at " + f_Seconds(l_fT) + ", after the clip's length (" + f_Seconds(l_oC.m_fLength) + ")");
                break;
            }
        }
    }
    auto l_fnFinite3 = [&](const glm::vec3& l_v3V) { return f_Finite4(glm::vec4(l_v3V, 0.0f)); };
    bool l_bBad = !l_fnFinite3(l_oN.m_v3Pos) || !l_fnFinite3(l_oN.m_v3Rot) || !l_fnFinite3(l_oN.m_v3Scale) || !f_Finite4(l_oN.m_v4Color);
    for (const auto& l_oK : l_oN.m_vPosKeys) l_bBad = l_bBad || !l_fnFinite3(l_oK.m_oV);
    for (const auto& l_oK : l_oN.m_vScaleKeys) l_bBad = l_bBad || !l_fnFinite3(l_oK.m_oV);
    for (const auto& l_oK : l_oN.m_vRotKeys) l_bBad = l_bBad || !l_fnFinite3(l_oK.m_oV);
    for (const auto& l_oK : l_oN.m_vColorKeys) l_bBad = l_bBad || !f_Finite4(l_oK.m_oV);
    if (l_bBad) f_Add(l_vOut, Problem::Error, l_iClip, l_vPath, l_strWhere, "a value is not a finite number");
    if (l_oN.m_bHasPivot && (l_oN.m_v2Pivot.x < -1 || l_oN.m_v2Pivot.x > 2 || l_oN.m_v2Pivot.y < -1 || l_oN.m_v2Pivot.y > 2))
        f_Add(l_vOut, Problem::Info, l_iClip, l_vPath, l_strWhere, "pivot lies far outside the sprite");
    if (l_oN.m_bLoop && !(l_oN.f_LastKeyTime() - l_oN.f_FirstKeyTime() > 1e-4f))
        f_Add(l_vOut, Problem::Warning, l_iClip, l_vPath, l_strWhere, "loops, but it and its children have no keys over time (nothing to repeat)");
    for (size_t l_nI = 0; l_nI < l_oN.m_vChildren.size(); l_nI++) {
        l_vPath.push_back((int)l_nI);
        f_CheckNode(l_oN.m_vChildren[l_nI], l_iClip, l_oC, l_vPath, l_strWhere, l_oAtlases, l_vOut);
        l_vPath.pop_back();
    }
}

}  // namespace

std::vector<Problem> f_CheckAnim(const AnimFile& l_oF, const std::vector<CheckAtlas>& l_vAtlases) {
    std::vector<Problem> l_vOut;
    SpriteLookup l_oLookup;
    if (l_vAtlases.empty())
        f_Add(l_vOut, Problem::Warning, -1, {}, "file", "no atlas: sprites cannot be shown or checked (add one in the Atlases list)");
    for (const CheckAtlas& l_oA : l_vAtlases) {
        l_oLookup.m_vAll.push_back(&l_oA);
        if (l_oA.m_pAtlas) {
            l_oLookup.m_vLoaded.push_back(&l_oA);
            l_oLookup.m_oSet.f_Add(*l_oA.m_pAtlas, l_oA.m_strId);
            l_oLookup.m_oSet.m_vEntries.back().m_pMeshes = l_oA.m_pMeshes;
        }
        else {
            l_oLookup.m_bComplete = false;
            f_Add(l_vOut, Problem::Error, -1, {}, "atlas",
                "the atlas '" + l_oA.m_strId + "' (" + l_oA.m_strPath + ") could not be loaded" + (l_oA.m_strError.empty() ? "" : ": " + l_oA.m_strError));
        }
        if (l_oA.m_bAbsolute)
            f_Add(l_vOut, Problem::Warning, -1, {}, "atlas", "absolute atlas path '" + l_oA.m_strPath + "': it will be stored relative to the .anim on save");
    }
    if (l_oF.m_vClips.empty()) f_Add(l_vOut, Problem::Warning, -1, {}, "file", "the file has no clips");
    std::set<std::string> l_setNames;
    for (size_t l_nI = 0; l_nI < l_oF.m_vClips.size(); l_nI++) {
        const Clip& l_oC = l_oF.m_vClips[l_nI];
        const std::string l_strWhere = l_oC.m_strName.empty() ? std::string("(unnamed clip)") : l_oC.m_strName;
        if (l_oC.m_strName.empty()) f_Add(l_vOut, Problem::Error, (int)l_nI, {}, l_strWhere, "clip has no name");
        else if (!l_setNames.insert(l_oC.m_strName).second) f_Add(l_vOut, Problem::Error, (int)l_nI, {}, l_strWhere, "two clips are called '" + l_oC.m_strName + "'");
        if (l_oC.m_iPlayCount == 0) f_Add(l_vOut, Problem::Warning, (int)l_nI, {}, l_strWhere, "playCount 0: the clip never plays");
        if (l_oC.f_Duration() <= 0) f_Add(l_vOut, Problem::Info, (int)l_nI, {}, l_strWhere, "the clip has no keys and no length (a still pose)");
        std::vector<int> l_vPath;
        f_CheckNode(l_oC.m_oRoot, (int)l_nI, l_oC, l_vPath, l_strWhere, l_oLookup, l_vOut);
    }
    return l_vOut;
}

const char* f_LevelName(Problem::Level l_eL) { return l_eL == Problem::Error ? "error" : l_eL == Problem::Warning ? "warning" : "info"; }

int f_CountLevel(const std::vector<Problem>& l_vP, Problem::Level l_eL) {
    int l_iN = 0;
    for (const Problem& l_oX : l_vP) l_iN += l_oX.m_eLevel == l_eL;
    return l_iN;
}

bool f_ReadTextFile(const std::string& l_strPath, std::string& l_strOut) {
    std::ifstream l_oIn(fs::u8path(l_strPath), std::ios::binary);
    if (!l_oIn) return false;
    std::ostringstream l_oSs;
    l_oSs << l_oIn.rdbuf();
    l_strOut = l_oSs.str();
    return true;
}

bool f_IsAtlasId(const std::string& l_strId) {
    if (l_strId.empty()) return false;
    for (char l_cC : l_strId)
        if (l_cC == ':' || l_cC == '/' || l_cC == '\\' || l_cC == '=' || std::isspace((unsigned char)l_cC)) return false;
    return true;
}

bool f_IsAbsoluteAtlasPath(const std::string& l_strP) {
    if (l_strP.empty()) return false;
    if (l_strP[0] == '/' || l_strP[0] == '\\') return true;
    return l_strP.size() >= 2 && l_strP[1] == ':' && std::isalpha((unsigned char)l_strP[0]);
}

int f_HeadlessMain(const std::vector<std::string>& l_vArgs) {
    if (!l_vArgs.empty() && l_vArgs[0] == "meshes") return f_HeadlessMeshes(l_vArgs);
    if (!l_vArgs.empty() && l_vArgs[0] == "recipe") return f_HeadlessRecipe(l_vArgs);
    if (l_vArgs.size() >= 2 && l_vArgs[0] == "convert-mpdi") {   // the old engine's .mpdi (+ .pi) -> .anim (+ atlas)
        std::string l_strOut;
        for (size_t l_nI = 2; l_nI < l_vArgs.size(); l_nI++) {
            if (l_vArgs[l_nI] == "--out" && l_nI + 1 < l_vArgs.size()) l_strOut = l_vArgs[++l_nI];
            else { std::fprintf(stderr, "unknown argument: %s\n", l_vArgs[l_nI].c_str()); return 3; }
        }
        const MpdiConvertReport l_oR = f_ConvertMpdiFolder(l_vArgs[1], l_strOut);
        for (const std::string& l_strW : l_oR.m_vWarnings) std::fprintf(stderr, "warning: %s\n", l_strW.c_str());
        for (const std::string& l_strE : l_oR.m_vErrors) std::fprintf(stderr, "error: %s\n", l_strE.c_str());
        std::printf("convert-mpdi: %d .mpdi found, %d .anim written (%d clips, %d nodes), %d .pi converted; %zu warning(s), %zu error(s)\n",
                    l_oR.m_iMpdiFiles, l_oR.m_iAnimFiles, l_oR.m_iClips, l_oR.m_iNodes, l_oR.m_iPiFiles, l_oR.m_vWarnings.size(), l_oR.m_vErrors.size());
        return l_oR.m_vErrors.empty() && l_oR.m_iMpdiFiles > 0 ? 0 : 2;
    }
    if (l_vArgs.size() < 2 || l_vArgs[0] != "check") {
        std::fputs("usage: anim_editor --headless check <file.anim> [--atlas [<id>=]<file.atlas>]...\n"
                   "  parses the file, checks sprite references against its atlases and the keys; exit 0 = no errors, 2 = errors\n"
                   "  --atlas (repeatable) replaces the file's atlas list, in lookup order; the id defaults to the file name\n"
                   "       anim_editor --headless meshes <file.anim>... [--atlas ...] [--bin|--text] [--check] [--prune]\n"
                   "  adds the meshes of their skinned and deformed nodes to their atlases' <atlas>.mesh.json (docs/engine/16)\n",
                   stderr);
        return 3;
    }
    const std::string l_strFile = l_vArgs[1];
    std::vector<toms::anim::AtlasRef> l_vOverrides;
    for (size_t l_nI = 2; l_nI < l_vArgs.size(); l_nI++) {
        if (l_vArgs[l_nI] == "--atlas" && l_nI + 1 < l_vArgs.size()) {
            const std::string l_strA = l_vArgs[++l_nI];
            toms::anim::AtlasRef l_oR{toms::anim::f_DefaultAtlasId(l_strA), l_strA};
            const size_t l_nEq = l_strA.find('=');
            if (l_nEq != std::string::npos && l_nEq > 0 && f_IsAtlasId(l_strA.substr(0, l_nEq))) l_oR = {l_strA.substr(0, l_nEq), l_strA.substr(l_nEq + 1)};
            for (const toms::anim::AtlasRef& l_oO : l_vOverrides)
                if (l_oO.m_strId == l_oR.m_strId) {
                    std::fprintf(stderr, "two atlases have the id '%s': name one with --atlas <id>=<path>\n", l_oR.m_strId.c_str());
                    return 3;
                }
            l_vOverrides.push_back(l_oR);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", l_vArgs[l_nI].c_str());
            return 3;
        }
    }
    std::string l_strText, l_strErr;
    if (!f_ReadTextFile(l_strFile, l_strText)) {
        std::fprintf(stderr, "%s: error: cannot read the file\n", l_strFile.c_str());
        return 2;
    }
    AnimFile l_oF;
    if (!toms::anim::f_ParseAnim(l_strText, l_oF, &l_strErr)) {
        std::fprintf(stderr, "%s: error: %s\n", l_strFile.c_str(), l_strErr.c_str());
        return 2;
    }
    // From the command line: as given (relative to the working directory); from the file:
    // relative to the .anim.
    const bool l_bFromArgs = !l_vOverrides.empty();
    const std::vector<toms::anim::AtlasRef>& l_vNames = l_bFromArgs ? l_vOverrides : l_oF.m_vAtlases;
    std::vector<toms::AtlasFile> l_vParsed(l_vNames.size());
    std::vector<toms::SpriteMeshFile> l_vMeshes(l_vNames.size());
    std::vector<CheckAtlas> l_vRefs(l_vNames.size());
    int l_iLoaded = 0;
    for (size_t l_nI = 0; l_nI < l_vNames.size(); l_nI++) {
        CheckAtlas& l_oR = l_vRefs[l_nI];
        l_oR.m_strId = l_vNames[l_nI].m_strId;
        l_oR.m_strPath = l_vNames[l_nI].m_strPath;
        l_oR.m_bAbsolute = !l_bFromArgs && f_IsAbsoluteAtlasPath(l_oR.m_strPath);
        const fs::path l_oP = l_bFromArgs || l_oR.m_bAbsolute ? fs::u8path(l_oR.m_strPath)
                                                  : (fs::u8path(l_strFile).parent_path() / fs::u8path(l_oR.m_strPath)).lexically_normal();
        std::string l_strAtext, l_strAerr;
        if (!f_ReadTextFile(l_oP.u8string(), l_strAtext)) l_oR.m_strError = "cannot read " + l_oP.u8string();
        else if (!toms::f_ParseAtlas(l_strAtext, l_vParsed[l_nI], &l_strAerr)) l_oR.m_strError = l_strAerr;
        else {
            l_oR.m_pAtlas = &l_vParsed[l_nI];
            l_iLoaded++;
            // Its meshes, when it has a mesh file (skinned sprites need them).
            std::string l_strMerr;
            if (fs::exists(fs::u8path(toms::f_SpriteMeshPath(l_oP.u8string())))) {
                if (f_ReadMeshFile(toms::f_SpriteMeshPath(l_oP.u8string()), l_vMeshes[l_nI], nullptr, &l_strMerr)) l_oR.m_pMeshes = &l_vMeshes[l_nI];
                else std::fprintf(stderr, "%s: warning: %s\n", l_strFile.c_str(), l_strMerr.c_str());
            }
        }
    }
    const std::vector<Problem> l_vProblems = f_CheckAnim(l_oF, l_vRefs);
    for (const Problem& l_oP : l_vProblems)
        std::printf("%s: %s: %s: %s\n", l_strFile.c_str(), f_LevelName(l_oP.m_eLevel), l_oP.m_strWhere.c_str(), l_oP.m_strMessage.c_str());
    const int l_iErrors = f_CountLevel(l_vProblems, Problem::Error), l_iWarnings = f_CountLevel(l_vProblems, Problem::Warning);
    std::printf("%s: %zu clip(s), %zu atlas(es), %d error(s), %d warning(s)%s\n", l_strFile.c_str(), l_oF.m_vClips.size(), l_vNames.size(), l_iErrors,
                l_iWarnings, l_iLoaded ? "" : " (sprites not checked: no atlas loaded)");
    return l_iErrors ? 2 : 0;
}

}  // namespace animed
