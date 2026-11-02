// stage_save.cpp -- see stage_save.h.
#include "stage_save.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace stage {

namespace {

bool f_StartsWith(const std::string& l_strS, const char* l_pszP) { return l_strS.rfind(l_pszP, 0) == 0; }
bool f_EndsWith(const std::string& l_strS, const std::string& l_strP) { return l_strS.size() >= l_strP.size() && l_strS.compare(l_strS.size() - l_strP.size(), l_strP.size(), l_strP) == 0; }

const Json* f_Member(const Json& l_jJ, const char* l_pszKey) {
    if (!l_jJ.is_object()) return nullptr;
    auto l_oIt = l_jJ.find(l_pszKey);
    return l_oIt == l_jJ.end() ? nullptr : &*l_oIt;
}

std::vector<std::string> f_SplitLines(const std::string& l_strS) {
    std::vector<std::string> l_vOut;
    std::string l_strCur;
    for (char l_cC : l_strS) {
        if (l_cC == '\r') continue;
        if (l_cC == '\n') { l_vOut.push_back(l_strCur); l_strCur.clear(); }
        else l_strCur += l_cC;
    }
    l_vOut.push_back(l_strCur);
    return l_vOut;
}

std::vector<std::string> f_SortedNames(const std::string& l_strDir, const std::string& l_strPrefix, const std::string& l_strSuffix) {
    std::vector<std::string> l_vOut;
    std::error_code l_oEc;
    for (const auto& l_oE : fs::directory_iterator(fs::u8path(l_strDir), l_oEc)) {
        if (!l_oE.is_regular_file(l_oEc)) continue;
        const std::string l_strN = l_oE.path().filename().u8string();
        if (f_StartsWith(l_strN, l_strPrefix.c_str()) && f_EndsWith(l_strN, l_strSuffix)) l_vOut.push_back(l_strN);
    }
    std::sort(l_vOut.begin(), l_vOut.end());
    return l_vOut;
}

std::vector<std::string> f_EventsList(const Json& l_jSpec) {
    std::vector<std::string> l_vOut;
    if (const Json* l_pE = f_Member(l_jSpec, "events"); l_pE && l_pE->is_array())
        for (const Json& l_jX : *l_pE) if (l_jX.is_string()) l_vOut.push_back(l_jX.get<std::string>());
    return l_vOut;
}
bool f_Contains(const std::vector<std::string>& l_vV, const std::string& l_strS) { return std::find(l_vV.begin(), l_vV.end(), l_strS) != l_vV.end(); }

}  // namespace

std::vector<ChangedFile> f_ChangedFiles(const Project& l_oP) {
    std::vector<ChangedFile> l_vOut;
    for (const Floor& l_oF : l_oP.m_vFloors) {
        if (!l_oP.f_HasDoc(l_oF.m_strId)) continue;
        const StageDoc& l_oD = const_cast<Project&>(l_oP).f_Doc(l_oF.m_strId);
        const std::string l_strSt = l_oP.f_StageText(l_oD), l_strSp = l_oP.f_SpecText(l_oD);
        if (l_strSt != l_oD.m_strOrigStage) l_vOut.push_back({ChangedFile::Stage, l_oF.m_strStagePath, l_strSt, l_oD.m_strOrigStage, l_oF.m_strId});
        if (l_strSp != l_oD.m_strOrigSpec) l_vOut.push_back({ChangedFile::Spec, l_oF.m_strSpecPath, l_strSp, l_oD.m_strOrigSpec, l_oF.m_strId});
    }
    for (const auto& [l_strPath, l_oDf] : l_oP.m_mapDataFiles) {
        auto l_itOrig = l_oP.m_mapDataOrig.find(l_strPath);
        if (l_oDf.m_bDeleted) {
            if (l_itOrig != l_oP.m_mapDataOrig.end()) l_vOut.push_back({ChangedFile::Delete, l_strPath, std::string(), l_itOrig->second, std::string()});
            continue;
        }
        const std::string l_strText = f_FmtJson(l_oDf.m_jObj, l_oDf.m_oEol);
        if (l_itOrig == l_oP.m_mapDataOrig.end() || l_strText != l_itOrig->second)
            l_vOut.push_back({ChangedFile::Data, l_strPath, l_strText, l_itOrig == l_oP.m_mapDataOrig.end() ? std::string() : l_itOrig->second, std::string()});
    }
    if (!l_oP.m_mapTextEdits.empty()) {
        std::string l_strLines;
        for (const auto& [l_strK, l_jV] : l_oP.m_mapTextEdits) l_strLines += (l_strLines.empty() ? "" : "\n") + Json(l_strK).dump() + ": " + l_jV.dump();
        l_vOut.push_back({ChangedFile::Text, "assets/data/text.json", l_strLines, std::string(), std::string()});
    }
    return l_vOut;
}

std::vector<std::pair<char, std::string>> f_LineDiff(const std::string& l_strA, const std::string& l_strB) {
    const std::vector<std::string> l_vA = f_SplitLines(l_strA), l_vB = f_SplitLines(l_strB);
    const size_t l_nN = l_vA.size(), l_nM = l_vB.size();
    std::vector<std::vector<int>> l_vDp(l_nN + 1, std::vector<int>(l_nM + 1, 0));
    for (size_t l_nI = l_nN; l_nI-- > 0;)
        for (size_t l_nJ = l_nM; l_nJ-- > 0;) l_vDp[l_nI][l_nJ] = l_vA[l_nI] == l_vB[l_nJ] ? l_vDp[l_nI + 1][l_nJ + 1] + 1 : std::max(l_vDp[l_nI + 1][l_nJ], l_vDp[l_nI][l_nJ + 1]);
    std::vector<std::pair<char, std::string>> l_vAll;
    size_t l_nI = 0, l_nJ = 0;
    while (l_nI < l_nN || l_nJ < l_nM) {
        if (l_nI < l_nN && l_nJ < l_nM && l_vA[l_nI] == l_vB[l_nJ]) { l_vAll.push_back({' ', l_vA[l_nI]}); l_nI++; l_nJ++; }
        else if (l_nJ < l_nM && (l_nI >= l_nN || l_vDp[l_nI][l_nJ + 1] >= l_vDp[l_nI + 1][l_nJ])) l_vAll.push_back({'+', l_vB[l_nJ++]});
        else l_vAll.push_back({'-', l_vA[l_nI++]});
    }
    // keep only changes and two lines of context around them
    std::vector<std::pair<char, std::string>> l_vRes;
    for (size_t l_nK = 0; l_nK < l_vAll.size(); l_nK++) {
        bool l_bKeep = l_vAll[l_nK].first != ' ';
        for (size_t l_nQ = (l_nK >= 2 ? l_nK - 2 : 0); !l_bKeep && l_nQ < std::min(l_vAll.size(), l_nK + 3); l_nQ++) l_bKeep = l_vAll[l_nQ].first != ' ';
        if (l_bKeep) l_vRes.push_back(l_vAll[l_nK]);
        else if (!l_vRes.empty() && l_vRes.back().second != "…") l_vRes.push_back({' ', "…"});
    }
    return l_vRes;
}

std::string f_TimeStamp() {
    const std::time_t l_i64Now = std::time(nullptr);
    std::tm l_oTm{};
#ifdef _WIN32
    localtime_s(&l_oTm, &l_i64Now);
#else
    localtime_r(&now, &tm);
#endif
    char l_pszBuf[32];
    std::strftime(l_pszBuf, sizeof l_pszBuf, "%Y-%m-%d_%H%M%S", &l_oTm);
    return l_pszBuf;
}

bool f_WriteFiles(Project& l_oP, const std::string& l_strStamp, std::vector<std::string>& l_vWritten, std::string& l_strErr) {
    std::vector<ChangedFile> l_vFiles = f_ChangedFiles(l_oP);
    // a floor spec on disk may have changed since it was read (the event editor saves floors' events lists too): this
    // editor's change to the events list is applied to the file as it is now
    for (ChangedFile& l_oF : l_vFiles) {
        if (l_oF.m_eKind == ChangedFile::Spec) {
            StageDoc& l_oD = l_oP.f_Doc(l_oF.m_strFloorId);
            std::string l_strNow;
            if (!f_ReadText(l_oP.f_Abs(l_oF.m_strPath), l_strNow) || l_strNow == l_oD.m_strOrigSpec) continue;
            Json l_jDisk = Json::parse(l_strNow, nullptr, false);
            const Json l_jWas = Json::parse(l_oD.m_strOrigSpec, nullptr, false);
            if (l_jDisk.is_discarded() || l_jWas.is_discarded()) continue;
            const std::vector<std::string> l_vBefore = f_EventsList(l_jWas), l_vMine = f_EventsList(l_oD.m_jSpec), l_vCur = f_EventsList(l_jDisk);
            Json l_jMerged = Json::array();
            for (const std::string& l_strX : l_vCur) if (!(f_Contains(l_vBefore, l_strX) && !f_Contains(l_vMine, l_strX))) l_jMerged.push_back(l_strX);
            for (const std::string& l_strX : l_vMine) if (!f_Contains(l_vBefore, l_strX) && !f_Contains(l_vCur, l_strX)) l_jMerged.push_back(l_strX);
            l_jDisk["events"] = l_jMerged;
            l_oD.m_jSpec = l_jDisk;
            l_oF.m_strText = l_oP.f_SpecText(l_oD);
            l_oF.m_strBefore = l_strNow;
        } else if (l_oF.m_eKind == ChangedFile::Text) {   // text.json as it is now, with this editor's keys
            std::string l_strRaw;
            if (!f_ReadText(l_oP.f_Abs(l_oF.m_strPath), l_strRaw)) { l_strErr = l_oF.m_strPath + ": cannot read"; return false; }
            Json l_jObj = Json::parse(l_strRaw, nullptr, false);
            if (l_jObj.is_discarded()) { l_strErr = l_oF.m_strPath + ": not valid JSON"; return false; }
            for (const auto& [l_strK, l_jV] : l_oP.m_mapTextEdits) {
                Json& l_jS = l_jObj["strings"][l_strK];
                if (!l_jS.is_object()) l_jS = Json::object();
                for (auto& [l_strL, l_jT] : l_jV.items()) l_jS[l_strL] = l_jT;
            }
            Eol l_oEol;
            l_oEol.m_bCrlf = l_strRaw.find("\r\n") != std::string::npos;
            l_oEol.m_bFinalNewline = !l_strRaw.empty() && l_strRaw.back() == '\n';
            l_oF.m_strText = f_FmtJson(l_jObj, l_oEol);
            l_oF.m_strBefore = l_strRaw;
        }
    }
    for (const ChangedFile& l_oF : l_vFiles) {   // every file is backed up first
        std::string l_strOld;
        if (f_ReadText(l_oP.f_Abs(l_oF.m_strPath), l_strOld) && !f_WriteText(l_oP.f_Abs("Build/editor_backups/" + l_strStamp + "/" + l_oF.m_strPath), l_strOld)) {
            l_strErr = "Build/editor_backups/" + l_strStamp + "/" + l_oF.m_strPath + ": cannot write the backup";
            return false;
        }
    }
    for (const ChangedFile& l_oF : l_vFiles) {
        if (l_oF.m_eKind == ChangedFile::Delete) {
            std::error_code l_oEc;
            fs::remove(fs::u8path(l_oP.f_Abs(l_oF.m_strPath)), l_oEc);
        } else if (!f_WriteText(l_oP.f_Abs(l_oF.m_strPath), l_oF.m_strText)) {
            l_strErr = l_oF.m_strPath + ": cannot write";
            return false;
        }
        l_vWritten.push_back(l_oF.m_strPath);
    }
    // what is on disk now is what this editor has
    for (const ChangedFile& l_oF : l_vFiles) {
        switch (l_oF.m_eKind) {
        case ChangedFile::Stage: l_oP.f_Doc(l_oF.m_strFloorId).m_strOrigStage = l_oF.m_strText; l_oP.f_Doc(l_oF.m_strFloorId).m_iVer++; break;
        case ChangedFile::Spec: l_oP.f_Doc(l_oF.m_strFloorId).m_strOrigSpec = l_oF.m_strText; l_oP.f_Doc(l_oF.m_strFloorId).m_iVer++; break;
        case ChangedFile::Data: l_oP.m_mapDataOrig[l_oF.m_strPath] = l_oF.m_strText; l_oP.m_iDataVer++; break;
        case ChangedFile::Delete: l_oP.m_mapDataOrig.erase(l_oF.m_strPath); l_oP.m_mapDataFiles.erase(l_oF.m_strPath); l_oP.m_iDataVer++; break;
        case ChangedFile::Text:
            for (const auto& [l_strK, l_jV] : l_oP.m_mapTextEdits) {
                Json& l_jS = l_oP.m_jStrings[l_strK];
                if (!l_jS.is_object()) l_jS = Json::object();
                for (auto& [l_strL, l_jT] : l_jV.items()) l_jS[l_strL] = l_jT;
            }
            l_oP.m_mapTextEdits.clear();
            break;
        }
    }
    return true;
}

// ---- the per-floor event index (tools/web_editors/floor_index.js, the owner's Q10 answer) ---------------------------

Json f_BuildFloorIndex(const std::string& l_strRoot) {
    const std::string l_strA = l_strRoot + "/assets/data/";
    Json l_jJ;
    Json l_jStrings = Json::object();
    if (f_ReadJson(l_strA + "text.json", l_jJ) && f_Member(l_jJ, "strings")) l_jStrings = l_jJ["strings"];
    auto l_fnEnglish = [&](const std::string& l_strKey) -> std::string {
        const Json* l_pV = f_Member(l_jStrings, l_strKey.c_str());
        const Json* l_pEn = l_pV && l_pV->is_object() ? f_Member(*l_pV, "en") : nullptr;
        return l_pEn && l_pEn->is_string() ? l_pEn->get<std::string>() : std::string();
    };
    std::map<std::string, std::pair<std::string, Json>> l_mapPools;
    for (const std::string& l_strN : f_SortedNames(l_strA + "events", "pool_", ".json")) {
        if (!f_ReadJson(l_strA + "events/" + l_strN, l_jJ)) continue;
        if (const Json* l_pEvs = f_Member(l_jJ, "events"); l_pEvs && l_pEvs->is_array())
            for (const Json& l_jE : *l_pEvs)
                if (const Json* l_pId = f_Member(l_jE, "eventId"); l_pId && l_pId->is_string()) l_mapPools[l_pId->get<std::string>()] = {l_strN.substr(0, l_strN.size() - 5), l_jE};
    }
    std::map<std::string, std::vector<Json>> l_mapBeats;
    for (const std::string& l_strN : f_SortedNames(l_strA + "story/chapters", "", ".json")) {
        if (!f_ReadJson(l_strA + "story/chapters/" + l_strN, l_jJ)) continue;
        if (const Json* l_pBs = f_Member(l_jJ, "beats"); l_pBs && l_pBs->is_array())
            for (const Json& l_jB : *l_pBs) {
                const Json* l_pFl = f_Member(l_jB, "floor");
                if (!l_pFl || !l_pFl->is_string() || l_pFl->get<std::string>().empty()) continue;
                Json l_jPick = Json::object();
                for (const char* l_pszK : {"id", "kind", "eventId", "nodeId", "enemyId", "shardId"})
                    if (const Json* l_pV = f_Member(l_jB, l_pszK)) l_jPick[l_pszK] = *l_pV;
                l_mapBeats[l_pFl->get<std::string>()].push_back(l_jPick);
            }
    }
    Json l_jFloors = Json::object();
    for (const std::string& l_strN : f_SortedNames(l_strA + "story/floors", "F", ".json")) {
        if (f_EndsWith(l_strN, ".stage.json")) continue;
        Json l_jSpec;
        if (!f_ReadJson(l_strA + "story/floors/" + l_strN, l_jSpec)) continue;
        const std::string l_strFid = l_jSpec.value("id", std::string());
        const std::string l_strHand = l_jSpec.contains("handAuthoredStage") && l_jSpec["handAuthoredStage"].is_string() ? l_jSpec["handAuthoredStage"].get<std::string>() : std::string();
        const std::string l_strMap = !l_strHand.empty() ? "stages/" + l_strHand : "story/floors/" + l_strFid + ".stage.json";
        std::vector<std::pair<std::string, Json>> l_vTiles;   // event id -> [[x, y], ...], first-seen order
        Json l_jGrid;
        if (f_ReadJson(l_strA + l_strMap, l_jGrid)) {
            const Json* l_pLegend = f_Member(l_jGrid, "legend");
            if (const Json* l_pRows = f_Member(l_jGrid, "tiles"); l_pRows && l_pRows->is_array())
                for (size_t l_nY = 0; l_nY < l_pRows->size(); l_nY++) {
                    if (!(*l_pRows)[l_nY].is_string()) continue;
                    const std::string l_strRow = (*l_pRows)[l_nY].get<std::string>();
                    for (size_t l_nX = 0; l_nX < l_strRow.size(); l_nX++) {
                        const Json* l_pK = l_pLegend ? f_Member(*l_pLegend, std::string(1, l_strRow[l_nX]).c_str()) : nullptr;
                        const std::string l_strKind = l_pK && l_pK->is_string() ? l_pK->get<std::string>() : std::string();
                        if (!f_StartsWith(l_strKind, "event:")) continue;
                        const std::string l_strId = l_strKind.substr(6);
                        auto l_itIt = std::find_if(l_vTiles.begin(), l_vTiles.end(), [&](const auto& l_oT) { return l_oT.first == l_strId; });
                        if (l_itIt == l_vTiles.end()) { l_vTiles.push_back({l_strId, Json::array()}); l_itIt = l_vTiles.end() - 1; }
                        l_itIt->second.push_back(Json::array({(int)l_nX, (int)l_nY}));
                    }
                }
        }
        const std::vector<std::string> l_vListed = f_EventsList(l_jSpec);
        std::vector<std::string> l_vPlacedSorted;
        for (const auto& l_pairT : l_vTiles) l_vPlacedSorted.push_back(l_pairT.first);
        std::sort(l_vPlacedSorted.begin(), l_vPlacedSorted.end());
        std::vector<std::string> l_vIds;
        for (const std::string& l_strId : l_vListed) if (!f_Contains(l_vIds, l_strId)) l_vIds.push_back(l_strId);
        for (const std::string& l_strId : l_vPlacedSorted) if (!f_Contains(l_vIds, l_strId)) l_vIds.push_back(l_strId);
        Json l_jEvents = Json::array();
        for (const std::string& l_strId : l_vIds) {
            auto l_itPl = l_mapPools.find(l_strId);
            const Json l_jRec = l_itPl != l_mapPools.end() ? l_itPl->second.second : Json::object();
            const std::string l_strRecTitle = l_jRec.contains("title") && l_jRec["title"].is_string() ? l_jRec["title"].get<std::string>() : std::string();
            const std::string l_strTitle = l_fnEnglish(!l_strRecTitle.empty() ? l_strRecTitle : l_strId + ".title");
            Json l_jE = Json::object();
            l_jE["id"] = l_strId;
            l_jE["kind"] = l_jRec.contains("kind") && !l_jRec["kind"].is_null() ? l_jRec["kind"] : Json();
            l_jE["title"] = l_strTitle.empty() ? Json() : Json(l_strTitle);
            l_jE["pool"] = l_itPl != l_mapPools.end() ? Json(l_itPl->second.first) : Json();
            l_jE["listed"] = f_Contains(l_vListed, l_strId);
            auto l_itT = std::find_if(l_vTiles.begin(), l_vTiles.end(), [&](const auto& l_oX) { return l_oX.first == l_strId; });
            l_jE["tiles"] = l_itT != l_vTiles.end() ? l_itT->second : Json::array();
            l_jEvents.push_back(l_jE);
        }
        Json l_jEntry = Json::object();
        l_jEntry["act"] = l_jSpec.contains("act") && !l_jSpec["act"].is_null() ? l_jSpec["act"] : Json();
        l_jEntry["map"] = l_strMap;
        l_jEntry["events"] = l_jEvents;
        Json l_jBl = Json::array();
        if (l_mapBeats.count(l_strFid)) for (const Json& l_jB : l_mapBeats[l_strFid]) l_jBl.push_back(l_jB);
        l_jEntry["beats"] = l_jBl;
        if (const Json* l_pStairs = f_Member(l_jSpec, "stairs"); l_pStairs && l_pStairs->is_array()) {
            Json l_jSl = Json::array();
            for (const Json& l_jS : *l_pStairs) {
                Json l_jO = Json::object();
                const auto l_fnTruthy = [](const Json* l_pV) {
                    return l_pV && !l_pV->is_null() && !(l_pV->is_string() && l_pV->get<std::string>().empty()) && !(l_pV->is_boolean() && !l_pV->get<bool>())
                        && !(l_pV->is_number() && l_pV->get<double>() == 0);
                };
                const Json* l_pDir = f_Member(l_jS, "dir");
                l_jO["dir"] = l_fnTruthy(l_pDir) ? *l_pDir : Json("up");
                if (const Json* l_pTo = f_Member(l_jS, "to"); l_fnTruthy(l_pTo)) l_jO["to"] = *l_pTo;
                // the first event as "unlock" (as before W38); a lock with several events also lists them all
                const std::vector<std::string> l_vLocks = f_StairUnlockEvents(l_jS);
                if (!l_vLocks.empty()) l_jO["unlock"] = l_vLocks[0];
                if (l_vLocks.size() > 1) l_jO["unlockAll"] = l_vLocks;
                l_jSl.push_back(l_jO);
            }
            l_jEntry["stairs"] = l_jSl;
        }
        l_jFloors[l_strFid] = l_jEntry;
    }
    std::map<std::string, std::vector<std::string>> l_mapByEvent;
    auto l_fnAdd = [&](const std::string& l_strId, const std::string& l_strFid) {
        std::vector<std::string>& l_vL = l_mapByEvent[l_strId];
        if (!f_Contains(l_vL, l_strFid)) l_vL.push_back(l_strFid);
    };
    for (auto& [l_strFid, l_jF] : l_jFloors.items()) {
        for (const Json& l_jE : l_jF["events"]) l_mapByEvent[l_jE["id"].get<std::string>()].push_back(l_strFid);
        for (const Json& l_jB : l_jF["beats"]) if (l_jB.contains("eventId") && l_jB["eventId"].is_string() && !l_jB["eventId"].get<std::string>().empty()) l_fnAdd(l_jB["eventId"].get<std::string>(), l_strFid);
        if (l_jF.contains("stairs")) for (const Json& l_jS : l_jF["stairs"]) {
            if (l_jS.contains("unlockAll") && l_jS["unlockAll"].is_array()) { for (const Json& l_jE : l_jS["unlockAll"]) if (l_jE.is_string()) l_fnAdd(l_jE.get<std::string>(), l_strFid); }
            else if (l_jS.contains("unlock") && l_jS["unlock"].is_string()) l_fnAdd(l_jS["unlock"].get<std::string>(), l_strFid);
        }
    }
    Json l_jSorted = Json::object();
    for (const auto& [l_strK, l_vV] : l_mapByEvent) l_jSorted[l_strK] = l_vV;   // std::map: sorted by key, as the JS sort()
    Json l_jOut = Json::object();
    l_jOut["_comment"] = "Generated by the HTML editors (tools/web_editors/floor_index.js) -- do not edit. The events each floor lists "
                      "or places, its chapter beats and its stair locks; \"events\" maps an event to its floors (Q10).";
    l_jOut["floors"] = l_jFloors;
    l_jOut["events"] = l_jSorted;
    return l_jOut;
}

bool f_SyncFloorIndex(const std::string& l_strRoot, std::string* l_pErr) {
    const std::string l_strText = f_BuildFloorIndex(l_strRoot).dump(1, ' ', false, Json::error_handler_t::replace) + "\n";
    const std::string l_strPath = l_strRoot + "/assets/data/events/floor_index.json";
    std::string l_strNow;
    if (f_ReadText(l_strPath, l_strNow) && l_strNow == l_strText) return false;
    if (!f_WriteText(l_strPath, l_strText)) { if (l_pErr) *l_pErr = l_strPath + ": cannot write"; return false; }
    return true;
}

}  // namespace stage
