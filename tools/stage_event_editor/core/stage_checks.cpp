// stage_checks.cpp -- see stage_checks.h.
#include "stage_checks.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <set>

namespace stage {

const char* const g_arrKeyColors[3] = {"yellow", "blue", "red"};

namespace {

bool f_StartsWith(const std::string& l_strS, const char* l_pszP) { return l_strS.rfind(l_pszP, 0) == 0; }
std::string f_Num(long long l_i64V) { return std::to_string(l_i64V); }
std::string f_CellChar(char l_cC) { return std::string(1, l_cC); }
int f_DoorColor(char l_cC) { return l_cC == 'y' ? Yellow : l_cC == 'b' ? Blue : l_cC == 'r' ? Red : -1; }
int f_KeyColor(const std::string& l_strName) {
    for (int l_iI = 0; l_iI < 3; l_iI++) if (l_strName == g_arrKeyColors[l_iI]) return l_iI;
    return -1;
}
int f_Popcount(uint32_t l_uM) { int l_iN = 0; while (l_uM) { l_iN += (int)(l_uM & 1u); l_uM >>= 1; } return l_iN; }

const Json* f_Member(const Json& l_jJ, const char* l_pszKey) {
    if (!l_jJ.is_object()) return nullptr;
    auto l_oIt = l_jJ.find(l_pszKey);
    return l_oIt == l_jJ.end() ? nullptr : &*l_oIt;
}
std::string f_Str(const Json& l_jJ, const char* l_pszKey) {
    const Json* l_pV = f_Member(l_jJ, l_pszKey);
    return l_pV && l_pV->is_string() ? l_pV->get<std::string>() : std::string();
}
bool f_Truthy(const Json* l_pV) {
    if (!l_pV || l_pV->is_null()) return false;
    if (l_pV->is_boolean()) return l_pV->get<bool>();
    if (l_pV->is_string()) return !l_pV->get_ref<const std::string&>().empty();
    if (l_pV->is_number()) return l_pV->get<double>() != 0;
    return true;
}
double f_NumberOf(const Json& l_jV) {   // JS +value (true -> 1, "3" -> 3, anything else -> 0 here)
    if (l_jV.is_boolean()) return l_jV.get<bool>() ? 1 : 0;
    if (l_jV.is_number()) return l_jV.get<double>();
    if (l_jV.is_string()) { try { return std::stod(l_jV.get<std::string>()); } catch (...) { return 0; } }
    return 0;
}

Issue f_Issue(Sev l_eSev, const char* l_pszCode, std::vector<std::string> l_vArgs = {}, std::vector<Cell> l_vCells = {}) {
    Issue l_oI;
    l_oI.m_eSev = l_eSev;
    l_oI.m_strCode = l_pszCode;
    l_oI.m_vArgs = std::move(l_vArgs);
    l_oI.m_vCells = std::move(l_vCells);
    return l_oI;
}

}  // namespace

std::string f_NumText(const Json& l_jV) {
    const double l_dD = f_NumberOf(l_jV);
    if (std::floor(l_dD) == l_dD && std::fabs(l_dD) < 1e15) return std::to_string((long long)l_dD);
    std::string l_strS = std::to_string(l_dD);
    while (!l_strS.empty() && l_strS.back() == '0') l_strS.pop_back();
    if (!l_strS.empty() && l_strS.back() == '.') l_strS.pop_back();
    return l_strS;
}

// Where the player appears, in the game's order (game_assets.cpp, loadStage): the stairs down (arriving from below),
// else '@', else any stairs up.
std::optional<Cell> f_StartCell(const Project& l_oP, const StageDoc& l_oD) {
    auto l_fnFind = [&](const std::function<bool(int, int)>& l_fnTest) -> std::optional<Cell> {
        for (int l_iY = 0; l_iY < l_oD.f_Height(); l_iY++)
            for (int l_iX = 0; l_iX < l_oD.f_Width(); l_iX++) if (l_fnTest(l_iX, l_iY)) return Cell{l_iX, l_iY};
        return std::nullopt;
    };
    if (auto l_optC = l_fnFind([&](int l_iX, int l_iY) { return l_oP.f_SemAt(l_oD, l_iX, l_iY) == "stairs_down"; })) return l_optC;
    if (auto l_optC = l_fnFind([&](int l_iX, int l_iY) { return l_oD.f_At(l_iX, l_iY) == '@'; })) return l_optC;
    return l_fnFind([&](int l_iX, int l_iY) { return l_oP.f_SemAt(l_oD, l_iX, l_iY) == "stairs_up"; });
}

std::vector<char> f_ReachableCells(const Project& l_oP, const StageDoc& l_oD) {
    const int l_iW = l_oD.f_Width(), l_iH = l_oD.f_Height();
    const std::optional<Cell> l_optStart = f_StartCell(l_oP, l_oD);
    if (!l_optStart) return {};
    std::vector<char> l_vSeen((size_t)l_iW * l_iH, 0);
    std::deque<Cell> l_dqQ{*l_optStart};
    l_vSeen[(size_t)l_optStart->m_iY * l_iW + l_optStart->m_iX] = 1;
    static const int l_arrDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!l_dqQ.empty()) {
        const Cell l_oC = l_dqQ.front();
        l_dqQ.pop_front();
        for (const auto& l_arrDd : l_arrDirs) {
            const int l_iNx = l_oC.m_iX + l_arrDd[0], l_iNy = l_oC.m_iY + l_arrDd[1];
            if (l_iNx < 0 || l_iNy < 0 || l_iNx >= l_iW || l_iNy >= l_iH || l_vSeen[(size_t)l_iNy * l_iW + l_iNx] || l_oD.f_At(l_iNx, l_iNy) == '#') continue;
            l_vSeen[(size_t)l_iNy * l_iW + l_iNx] = 1;
            l_dqQ.push_back({l_iNx, l_iNy});
        }
    }
    return l_vSeen;
}

std::vector<Issue> f_Problems(const Project& l_oP, const StageDoc& l_oD) {
    std::vector<Issue> l_vOut;
    const int l_iH = l_oD.f_Height(), l_iW = l_oD.f_Width();
    std::vector<Cell> l_vStarts;
    std::vector<std::pair<std::string, std::vector<Cell>>> l_vEvCells;   // in first-seen order
    bool l_bUp = false, l_bDown = false, l_bBorder = false;
    for (int l_iY = 0; l_iY < l_iH; l_iY++)
        for (int l_iX = 0; l_iX < l_iW; l_iX++) {
            const char l_cC = l_oD.f_At(l_iX, l_iY);
            if (l_cC == '@') l_vStarts.push_back({l_iX, l_iY});
            const std::string l_strSem = l_oP.f_SemAt(l_oD, l_iX, l_iY);
            if (l_strSem.empty()) l_vOut.push_back(f_Issue(Sev::Error, "pUnknownChar", {f_CellChar(l_cC), f_Num(l_iX), f_Num(l_iY)}, {{l_iX, l_iY}}));
            if (l_strSem == "stairs_up") l_bUp = true;
            if (l_strSem == "stairs_down") l_bDown = true;
            if ((l_iY == 0 || l_iX == 0 || l_iY == l_iH - 1 || l_iX == l_iW - 1) && l_cC != '#') l_bBorder = true;
            if (f_StartsWith(l_strSem, "event:")) {
                const std::string l_strId = l_strSem.substr(6);
                auto l_itIt = std::find_if(l_vEvCells.begin(), l_vEvCells.end(), [&](const auto& l_oE) { return l_oE.first == l_strId; });
                if (l_itIt == l_vEvCells.end()) l_vEvCells.push_back({l_strId, {{l_iX, l_iY}}});
                else l_itIt->second.push_back({l_iX, l_iY});
            }
            if (f_StartsWith(l_strSem, "monster:")) {
                const Footprint l_oFp = l_oP.f_FootprintOf(l_oD, l_cC, l_strSem);
                bool l_bBad = false;
                for (int l_iDy = 0; l_iDy < l_oFp.m_iH && !l_bBad; l_iDy++)
                    for (int l_iDx = 0; l_iDx < l_oFp.m_iW && !l_bBad; l_iDx++)
                        if ((l_iDx || l_iDy) && (!l_oD.f_Inside(l_iX + l_iDx, l_iY + l_iDy) || l_oD.f_At(l_iX + l_iDx, l_iY + l_iDy) != '.')) l_bBad = true;
                if (l_bBad) l_vOut.push_back(f_Issue(Sev::Warn, "pFootprint", {l_strSem.substr(8), f_Num(l_iX), f_Num(l_iY)}, {{l_iX, l_iY}}));
            }
        }
    if (!f_StartCell(l_oP, l_oD)) l_vOut.push_back(f_Issue(Sev::Error, "pNoStart"));
    if (const Json* l_pLg = f_Member(l_oD.m_jSt, "legend"))
        for (auto& [l_strC, l_jV] : l_pLg->items()) {
            if (!l_jV.is_string()) continue;
            auto l_itR = f_ReservedChars().find(l_jV.get<std::string>());
            if (l_itR != f_ReservedChars().end() && l_strC != f_CellChar(l_itR->second))
                l_vOut.push_back(f_Issue(Sev::Error, "pReservedChar", {l_jV.get<std::string>(), l_strC, f_CellChar(l_itR->second)}));
        }
    if (l_vStarts.size() > 1) l_vOut.push_back(f_Issue(Sev::Warn, "pManyStart", {f_Num((long long)l_vStarts.size())}, l_vStarts));
    std::vector<std::string> l_vListed;
    if (const Json* l_pEvs = f_Member(l_oD.m_jSpec, "events"); l_pEvs && l_pEvs->is_array())
        for (const Json& l_jE : *l_pEvs) if (l_jE.is_string()) l_vListed.push_back(l_jE.get<std::string>());
    const auto l_fnIsListed = [&](const std::string& l_strId) { return std::find(l_vListed.begin(), l_vListed.end(), l_strId) != l_vListed.end(); };
    for (const auto& [l_strId, l_vCells] : l_vEvCells) {
        if (!l_oP.f_Event(l_strId)) l_vOut.push_back(f_Issue(Sev::Error, "pEvUndefined", {l_strId}, l_vCells));
        if (!l_fnIsListed(l_strId)) { Issue l_oI = f_Issue(Sev::Warn, "pEvNotListed", {l_strId}, l_vCells); l_oI.m_strFixList = l_strId; l_vOut.push_back(l_oI); }
        if (l_vCells.size() > 1) l_vOut.push_back(f_Issue(Sev::Warn, "pEvDup", {l_strId, f_Num((long long)l_vCells.size())}, l_vCells));
    }
    for (const std::string& l_strId : l_vListed)
        if (std::none_of(l_vEvCells.begin(), l_vEvCells.end(), [&](const auto& l_oE) { return l_oE.first == l_strId; })) {
            Issue l_oI = f_Issue(Sev::Info, "pEvUnplaced", {l_strId});
            l_oI.m_strBrush = "event:" + l_strId;
            l_vOut.push_back(l_oI);
        }
    std::string l_strCu, l_strCd;
    if (const Json* l_pCn = f_Member(l_oD.m_jSt, "connect")) { l_strCu = f_Str(*l_pCn, "up"); l_strCd = f_Str(*l_pCn, "down"); }
    if (!l_strCu.empty() && !l_bUp) l_vOut.push_back(f_Issue(Sev::Warn, "pNoUp", {l_strCu}));
    if (l_strCu.empty() && l_bUp) l_vOut.push_back(f_Issue(Sev::Warn, "pUpNoTarget"));
    if (!l_strCd.empty() && !l_bDown) l_vOut.push_back(f_Issue(Sev::Warn, "pNoDown", {l_strCd}));
    if (l_strCd.empty() && l_bDown) l_vOut.push_back(f_Issue(Sev::Warn, "pDownNoTarget"));
    {
        const std::vector<char> l_vReach = f_ReachableCells(l_oP, l_oD);
        std::vector<Cell> l_vLost;
        for (int l_iY = 0; l_iY < l_iH; l_iY++)
            for (int l_iX = 0; l_iX < l_iW; l_iX++)
                if (Project::f_IsEntity(l_oP.f_SemAt(l_oD, l_iX, l_iY)) && (l_vReach.empty() || !l_vReach[(size_t)l_iY * l_iW + l_iX])) l_vLost.push_back({l_iX, l_iY});
        if (!l_vLost.empty()) l_vOut.push_back(f_Issue(Sev::Warn, "pUnreach", {f_Num((long long)l_vLost.size())}, l_vLost));
    }
    if (const Json* l_pObjs = f_Member(l_oD.m_jSt, "objects"); l_pObjs && l_pObjs->is_object())
        for (auto& [l_strK, l_jO] : l_pObjs->items()) {
            const size_t l_nComma = l_strK.find(',');
            if (l_nComma == std::string::npos) continue;
            const int l_iOx = std::atoi(l_strK.c_str()), l_iOy = std::atoi(l_strK.c_str() + l_nComma + 1);
            if (!Project::f_IsEntity(l_oP.f_SemAt(l_oD, l_iOx, l_iOy))) l_vOut.push_back(f_Issue(Sev::Warn, "pObjEmpty", {f_Num(l_iOx), f_Num(l_iOy)}, {{l_iOx, l_iOy}}));
            if (const Json* l_pEvs = f_Member(l_jO, "events"); l_pEvs && l_pEvs->is_array())
                for (const Json& l_jE : *l_pEvs)
                    if (l_jE.is_string() && !l_oP.f_Event(l_jE.get<std::string>()))
                        l_vOut.push_back(f_Issue(Sev::Error, "pObjEvent", {l_jE.get<std::string>(), f_Num(l_iOx), f_Num(l_iOy)}, {{l_iOx, l_iOy}}));
        }
    if (l_bBorder) l_vOut.push_back(f_Issue(Sev::Info, "pBorder"));
    return l_vOut;
}

// ---- story ---------------------------------------------------------------------------------------------------------
// The game's rules: a y / b / r door takes one key of its colour; keys are picked up on contact; U and D end the floor.
// Monsters are treated as beatable (their stats are not simulated). Every order of opening doors is tried (doors are
// few), so "the player spent the only key on the wrong door" is found, not guessed.

namespace {

FloorFacts f_FloorFacts(const Project& l_oP, const StageDoc& l_oD) {
    FloorFacts l_oF;
    l_oF.m_iW = l_oD.f_Width();
    l_oF.m_iH = l_oD.f_Height();
    l_oF.m_vKeyAt.assign((size_t)l_oF.m_iW * l_oF.m_iH, -1);
    l_oF.m_vDoorIdx.assign((size_t)l_oF.m_iW * l_oF.m_iH, -1);
    for (int l_iY = 0; l_iY < l_oF.m_iH; l_iY++)
        for (int l_iX = 0; l_iX < l_oF.m_iW; l_iX++) {
            const char l_cC = l_oD.f_At(l_iX, l_iY);
            const int l_iDc = f_DoorColor(l_cC);
            if (l_iDc >= 0) { l_oF.m_vDoorIdx[(size_t)l_iY * l_oF.m_iW + l_iX] = (int)l_oF.m_vDoors.size(); l_oF.m_vDoors.push_back({l_iX, l_iY, l_iDc}); }
            const std::string l_strS2 = l_oP.f_SemAt(l_oD, l_iX, l_iY);
            if (f_StartsWith(l_strS2, "key:")) l_oF.m_vKeyAt[(size_t)l_iY * l_oF.m_iW + l_iX] = f_KeyColor(l_strS2.substr(4));
            if (l_cC == 'U') l_oF.m_vUps.push_back(l_iY * l_oF.m_iW + l_iX);
        }
    l_oF.m_optStart = f_StartCell(l_oP, l_oD);
    return l_oF;
}

void f_FloodMask(const StageDoc& l_oD, const FloorFacts& l_oF, FloodState& l_oS) {
    l_oS.m_vSeen.assign((size_t)l_oF.m_iW * l_oF.m_iH, 0);
    if (!l_oF.m_optStart) return;
    std::vector<int> l_vQ{l_oF.m_optStart->m_iY * l_oF.m_iW + l_oF.m_optStart->m_iX};
    l_oS.m_vSeen[(size_t)l_vQ[0]] = 1;
    l_oS.m_vSeenOrder.push_back(l_vQ[0]);
    static const int l_arrDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!l_vQ.empty()) {
        const int l_iK = l_vQ.back();
        l_vQ.pop_back();
        const int l_iX = l_iK % l_oF.m_iW, l_iY = l_iK / l_oF.m_iW;
        if (l_oF.m_vKeyAt[(size_t)l_iK] >= 0) l_oS.m_oFound.m_arrN[l_oF.m_vKeyAt[(size_t)l_iK]]++;
        const char l_cC0 = l_oD.f_At(l_iX, l_iY);
        if ((l_cC0 == 'U' || l_cC0 == 'D') && !(l_iX == l_oF.m_optStart->m_iX && l_iY == l_oF.m_optStart->m_iY)) continue;   // a stair ends the floor
        for (const auto& l_arrDd : l_arrDirs) {
            const int l_iNx = l_iX + l_arrDd[0], l_iNy = l_iY + l_arrDd[1];
            if (l_iNx < 0 || l_iNy < 0 || l_iNx >= l_oF.m_iW || l_iNy >= l_oF.m_iH) continue;
            const int l_iNk = l_iNy * l_oF.m_iW + l_iNx;
            if (l_oS.m_vSeen[(size_t)l_iNk] || l_oD.f_At(l_iNx, l_iNy) == '#') continue;
            const int l_iDi = l_oF.m_vDoorIdx[(size_t)l_iNk];
            if (l_iDi >= 0 && !(l_iDi < 32 && (l_oS.m_uMask & (1u << l_iDi)))) {
                if (std::find(l_oS.m_vFrontier.begin(), l_oS.m_vFrontier.end(), l_iDi) == l_oS.m_vFrontier.end()) l_oS.m_vFrontier.push_back(l_iDi);
                continue;
            }
            l_oS.m_vSeen[(size_t)l_iNk] = 1;
            l_oS.m_vSeenOrder.push_back(l_iNk);
            l_vQ.push_back(l_iNk);
        }
    }
}

}  // namespace

bool Explore::f_HasUp() const { return std::any_of(m_vAll.begin(), m_vAll.end(), [](const FloodState& l_oS) { return l_oS.m_bUp; }); }

Explore f_Explore(const Project& l_oP, const StageDoc& l_oD, const Keys& l_oKeysIn) {
    Explore l_oEx;
    l_oEx.m_oF = f_FloorFacts(l_oP, l_oD);
    const FloorFacts& l_oF = l_oEx.m_oF;
    std::map<uint32_t, size_t> l_mapIndex;
    auto l_fnMk = [&](uint32_t l_uMask) {
        FloodState l_oS;
        l_oS.m_uMask = l_uMask;
        f_FloodMask(l_oD, l_oF, l_oS);
        for (size_t l_nI = 0; l_nI < l_oF.m_vDoors.size() && l_nI < 32; l_nI++) if (l_uMask & (1u << l_nI)) l_oS.m_oOpened.m_arrN[l_oF.m_vDoors[l_nI].m_iCol]++;
        for (int l_iC = 0; l_iC < 3; l_iC++) l_oS.m_oLeft.m_arrN[l_iC] = l_oKeysIn.m_arrN[l_iC] + l_oS.m_oFound.m_arrN[l_iC] - l_oS.m_oOpened.m_arrN[l_iC];
        l_oS.m_bUp = std::any_of(l_oF.m_vUps.begin(), l_oF.m_vUps.end(), [&](int l_iU) { return l_oS.m_vSeen[(size_t)l_iU] != 0; });
        l_oS.m_iN = f_Popcount(l_uMask);
        l_mapIndex[l_uMask] = l_oEx.m_vAll.size();
        l_oEx.m_vAll.push_back(std::move(l_oS));
    };
    l_fnMk(0);
    std::deque<uint32_t> l_dqQ{0};
    while (!l_dqQ.empty() && l_oEx.m_vAll.size() < 4096) {
        const uint32_t l_uM = l_dqQ.front();
        l_dqQ.pop_front();
        const std::vector<int> l_vFrontier = l_oEx.m_vAll[l_mapIndex[l_uM]].m_vFrontier;
        const Keys l_oLeft = l_oEx.m_vAll[l_mapIndex[l_uM]].m_oLeft;
        for (int l_iI : l_vFrontier) {
            if (l_iI > 30 || l_oLeft.m_arrN[l_oF.m_vDoors[(size_t)l_iI].m_iCol] <= 0) continue;
            const uint32_t l_uNm = l_uM | (1u << l_iI);
            if (!l_mapIndex.count(l_uNm)) { l_fnMk(l_uNm); l_dqQ.push_back(l_uNm); }
        }
    }
    std::vector<int> l_vUps, l_vEvery;
    for (size_t l_nI = 0; l_nI < l_oEx.m_vAll.size(); l_nI++) { l_vEvery.push_back((int)l_nI); if (l_oEx.m_vAll[l_nI].m_bUp) l_vUps.push_back((int)l_nI); }
    const auto& l_vA = l_oEx.m_vAll;
    std::vector<int> l_vR = l_vUps;
    std::stable_sort(l_vR.begin(), l_vR.end(), [&](int l_iA, int l_iB) {
        if (l_vA[l_iA].m_iN != l_vA[l_iB].m_iN) return l_vA[l_iA].m_iN < l_vA[l_iB].m_iN;
        return l_vA[l_iA].m_oLeft.f_Total() > l_vA[l_iB].m_oLeft.f_Total(); });
    l_oEx.m_iRoute = l_vR.empty() ? -1 : l_vR[0];
    l_vR = l_vUps;
    std::stable_sort(l_vR.begin(), l_vR.end(), [&](int l_iA, int l_iB) {
        if (l_vA[l_iA].m_oLeft.f_Total() != l_vA[l_iB].m_oLeft.f_Total()) return l_vA[l_iA].m_oLeft.f_Total() > l_vA[l_iB].m_oLeft.f_Total();
        return l_vA[l_iA].m_vSeenOrder.size() > l_vA[l_iB].m_vSeenOrder.size(); });
    l_oEx.m_iCarry = l_vR.empty() ? -1 : l_vR[0];
    l_vR = l_vEvery;
    std::stable_sort(l_vR.begin(), l_vR.end(), [&](int l_iA, int l_iB) { return l_vA[l_iA].m_vSeenOrder.size() > l_vA[l_iB].m_vSeenOrder.size(); });
    l_oEx.m_iWidest = l_vR.empty() ? -1 : l_vR[0];
    l_oEx.m_vAnySeen.assign((size_t)l_oF.m_iW * l_oF.m_iH, 0);
    for (const FloodState& l_oS : l_oEx.m_vAll) for (int l_iK : l_oS.m_vSeenOrder) l_oEx.m_vAnySeen[(size_t)l_iK] = 1;
    return l_oEx;
}

std::vector<std::string> f_ChainOrder(Project& l_oP) {
    std::vector<std::string> l_vOut;
    std::set<std::string> l_setSeen;
    std::string l_strF = l_oP.f_Floor("F01") ? "F01" : l_oP.m_vFloors.front().m_strId;
    while (!l_strF.empty() && l_oP.f_Floor(l_strF) && !l_setSeen.count(l_strF)) {
        l_setSeen.insert(l_strF);
        l_vOut.push_back(l_strF);
        l_strF = f_Str(l_oP.f_Doc(l_strF).m_jSpec, "nextFloor");
    }
    for (const Floor& l_oX : l_oP.m_vFloors) if (!l_setSeen.count(l_oX.m_strId)) l_vOut.push_back(l_oX.m_strId);
    return l_vOut;
}

namespace {

// event logic (docs/gameplay/event_editor/06_EVENT_LOGIC.md): what an event needs must be possible on its floor or before
void f_LogicOrderIssues(Project& l_oP, std::vector<StoryRow>& l_vRows) {
    std::map<std::string, int> l_mapIdxOf;
    auto l_fnAt = [&](const std::string& l_strId, int l_iI) {
        auto l_itIt = l_mapIdxOf.find(l_strId);
        if (l_itIt == l_mapIdxOf.end() || l_iI < l_itIt->second) { l_mapIdxOf[l_strId] = l_iI; return true; }
        return false;
    };
    for (const StoryRow& l_oR : l_vRows) {
        const StageDoc& l_oD = l_oP.f_Doc(l_oR.m_strFid);
        for (int l_iY = 0; l_iY < l_oD.f_Height(); l_iY++)
            for (int l_iX = 0; l_iX < l_oD.f_Width(); l_iX++) {
                const std::string l_strS2 = l_oP.f_SemAt(l_oD, l_iX, l_iY);
                if (f_StartsWith(l_strS2, "event:")) l_fnAt(l_strS2.substr(6), l_oR.m_iIdx);
                if (f_StartsWith(l_strS2, "npc:")) l_fnAt("npc:" + l_strS2.substr(4), l_oR.m_iIdx);
            }
    }
    auto l_fnHas = [&](const std::string& l_strId) { return l_mapIdxOf.count(l_strId) > 0; };
    for (int l_iPass = 0, l_iCh = 1; l_iCh && l_iPass < 20; l_iPass++) {   // talk / linked / variable-fired events appear where what fires them is
        l_iCh = 0;
        for (const Event& l_oE : l_oP.m_vEvents) {
            const Json* l_pTr = f_Member(l_oE.m_jRec, "trigger");
            const std::string l_strOn = l_pTr ? f_Str(*l_pTr, "on") : std::string();
            if (l_strOn == "talk" && l_fnHas("npc:" + f_Str(*l_pTr, "npc"))) l_iCh = l_fnAt(l_oE.m_strId, l_mapIdxOf["npc:" + f_Str(*l_pTr, "npc")]) || l_iCh;
            if (l_strOn == "varChanged") {
                const std::string l_strVar = f_Str(*l_pTr, "var");
                for (const Event& l_oO : l_oP.m_vEvents) {
                    const Json* l_pActs = f_Member(l_oO.m_jRec, "actions");
                    if (!l_pActs || !l_pActs->is_array() || !l_fnHas(l_oO.m_strId)) continue;
                    if (std::any_of(l_pActs->begin(), l_pActs->end(), [&](const Json& l_jA) { return f_Str(l_jA, "var") == l_strVar && !l_strVar.empty(); }))
                        l_iCh = l_fnAt(l_oE.m_strId, l_mapIdxOf[l_oO.m_strId]) || l_iCh;
                }
            }
            if (l_fnHas(l_oE.m_strId))
                if (const Json* l_pNx = f_Member(l_oE.m_jRec, "next"); l_pNx && l_pNx->is_array())
                    for (const Json& l_jN : *l_pNx) if (l_jN.is_string()) l_iCh = l_fnAt(l_jN.get<std::string>(), l_mapIdxOf[l_oE.m_strId]) || l_iCh;
        }
    }
    auto l_fnRowAt = [&](int l_iI) -> StoryRow* {
        for (StoryRow& l_oR : l_vRows) if (l_oR.m_iIdx == l_iI) return &l_oR;
        return nullptr;
    };
    for (const Event& l_oE : l_oP.m_vEvents) {
        if (!l_fnHas(l_oE.m_strId)) continue;
        const Json* l_pReq = f_Member(l_oE.m_jRec, "requires");
        if (!l_pReq || !l_pReq->is_object()) continue;
        const int l_iI = l_mapIdxOf[l_oE.m_strId];
        std::vector<Json> l_vLeaves;
        if (l_pReq->contains("type")) l_vLeaves.push_back(*l_pReq);
        else if (const Json* l_pAll = f_Member(*l_pReq, "all"); l_pAll && l_pAll->is_array()) l_vLeaves.assign(l_pAll->begin(), l_pAll->end());
        for (const Json& l_jL : l_vLeaves) {
            const std::string l_strType = f_Str(l_jL, "type");
            if (l_strType == "runFlagSet" && f_StartsWith(f_Str(l_jL, "flag"), "event_")) {
                const std::string l_strX = f_Str(l_jL, "flag").substr(6);
                const bool l_bKnown = l_fnHas(l_strX);
                if (!l_bKnown || l_mapIdxOf[l_strX] > l_iI)
                    if (StoryRow* l_pR = l_fnRowAt(l_iI)) {
                        StoryRow* l_pRj = l_bKnown ? l_fnRowAt(l_mapIdxOf[l_strX]) : nullptr;
                        l_pR->m_vIssues.push_back(f_Issue(Sev::Warn, "sNeedsEvent", {l_oE.m_strId, l_strX, l_pRj ? l_pRj->m_strFid : std::string()}));
                    }
            }
            if (l_strType == "varAtLeast") {
                const std::string l_strVar = f_Str(l_jL, "var");
                double l_dCan = 0;
                for (const Event& l_oO : l_oP.m_vEvents) {
                    std::vector<Json> l_vActs;
                    if (const Json* l_pA = f_Member(l_oO.m_jRec, "actions"); l_pA && l_pA->is_array()) l_vActs.assign(l_pA->begin(), l_pA->end());
                    const Json* l_pTr = f_Member(l_oO.m_jRec, "trigger");
                    if (l_pTr && f_Str(*l_pTr, "on") == "talk" && !f_Str(l_oO.m_jRec, "dialogue").empty()) {
                        auto l_itIt = l_oP.m_mapDialogueActions.find(f_Str(l_oO.m_jRec, "dialogue"));
                        if (l_itIt != l_oP.m_mapDialogueActions.end()) l_vActs.insert(l_vActs.end(), l_itIt->second.begin(), l_itIt->second.end());
                    }
                    for (const Json& l_jA : l_vActs) {
                        if (!l_jA.is_object() || f_Str(l_jA, "var") != l_strVar || !l_fnHas(l_oO.m_strId) || l_mapIdxOf[l_oO.m_strId] > l_iI) continue;
                        const std::string l_strAt2 = f_Str(l_jA, "type");
                        if (l_strAt2 == "addVar" && l_jA.contains("delta") && f_NumberOf(l_jA["delta"]) > 0) l_dCan += f_NumberOf(l_jA["delta"]);
                        if (l_strAt2 == "setVar") {
                            const Json l_jV = l_jA.value("value", Json());
                            l_dCan = std::max(l_dCan, l_jV.is_boolean() && l_jV.get<bool>() ? 1.0 : f_NumberOf(l_jV));
                        }
                    }
                }
                const double l_dWant = f_NumberOf(l_jL.value("value", Json()));
                if (l_dCan < l_dWant)
                    if (StoryRow* l_pR = l_fnRowAt(l_iI)) l_pR->m_vIssues.push_back(f_Issue(Sev::Warn, "sNeedsVar", {l_oE.m_strId, l_strVar, f_NumText(l_jL.value("value", Json())), f_NumText(Json(l_dCan))}));
            }
        }
    }
    for (StoryRow& l_oR : l_vRows) {
        const Json* l_pStairs = f_Member(l_oP.f_Doc(l_oR.m_strFid).m_jSpec, "stairs");
        if (!l_pStairs || !l_pStairs->is_array()) continue;
        for (const Json& l_jSp : *l_pStairs) {
            for (const std::string& l_strX : f_StairUnlockEvents(l_jSp))   // W38: each event of the lock
                if (!l_fnHas(l_strX) || l_mapIdxOf[l_strX] > l_oR.m_iIdx) l_oR.m_vIssues.push_back(f_Issue(Sev::Error, "sStairLock", {f_Str(l_jSp, "to"), l_strX}));
        }
    }
}

struct StoryCache {
    const Project* m_pP = nullptr;
    int m_iVer = -1;
    std::vector<StoryRow> m_vRows;
};
StoryCache& f_StoryCache() {
    static StoryCache l_oC;
    return l_oC;
}

}  // namespace

const std::vector<StoryRow>& f_StoryCheck(Project& l_oP) {
    StoryCache& l_oC = f_StoryCache();
    if (l_oC.m_pP == &l_oP && l_oC.m_iVer == l_oP.m_iStoryVer) return l_oC.m_vRows;
    std::vector<StoryRow> l_vRows;
    Keys l_oKeys;
    const Keys l_oPlenty{{99, 99, 99}};
    int l_iIdx = 0;
    for (const std::string& l_strFid : f_ChainOrder(l_oP)) {
        const StageDoc& l_oD = l_oP.f_Doc(l_strFid);
        StoryRow l_oRow;
        l_oRow.m_strFid = l_strFid;
        l_oRow.m_iIdx = l_iIdx++;
        l_oRow.m_oKeysIn = l_oKeys;
        l_oRow.m_oEx = f_Explore(l_oP, l_oD, l_oKeys);
        const Json* l_pCn = f_Member(l_oD.m_jSt, "connect");
        l_oRow.m_bTop = l_oRow.m_oEx.m_oF.m_vUps.empty() && !(l_pCn && f_Truthy(f_Member(*l_pCn, "up")));
        if (!l_oRow.m_oEx.m_oF.m_optStart) l_oRow.m_vIssues.push_back(f_Issue(Sev::Error, "sNoStart"));
        else if (!l_oRow.m_bTop && l_oRow.m_oEx.m_iRoute < 0) {
            const Explore l_oInf = f_Explore(l_oP, l_oD, l_oPlenty);
            if (l_oInf.m_iRoute < 0) l_oRow.m_vIssues.push_back(f_Issue(Sev::Error, "sUpWalled"));
            else {
                const FloodState& l_oRt = l_oInf.m_vAll[(size_t)l_oInf.m_iRoute];
                for (int l_iC = 0; l_iC < 3; l_iC++)
                    if (l_oRt.m_oOpened.m_arrN[l_iC] > l_oKeys.m_arrN[l_iC] + l_oRt.m_oFound.m_arrN[l_iC])
                        l_oRow.m_vIssues.push_back(f_Issue(Sev::Error, "sNeedKeys", {f_Num(l_oRt.m_oOpened.m_arrN[l_iC]), g_arrKeyColors[l_iC], f_Num(l_oKeys.m_arrN[l_iC]), f_Num(l_oRt.m_oFound.m_arrN[l_iC])}));
            }
        }
        std::unique_ptr<Explore> l_upInf;
        const int l_iW = l_oD.f_Width();
        for (int l_iY = 0; l_iY < l_oD.f_Height(); l_iY++)
            for (int l_iX = 0; l_iX < l_iW; l_iX++) {
                const std::string l_strS2 = l_oP.f_SemAt(l_oD, l_iX, l_iY);
                if (!f_StartsWith(l_strS2, "event:")) continue;
                const size_t l_nK = (size_t)l_iY * l_iW + l_iX;
                if (!l_oRow.m_oEx.m_vAnySeen[l_nK]) {
                    if (!l_upInf) l_upInf = std::make_unique<Explore>(f_Explore(l_oP, l_oD, l_oPlenty));
                    l_oRow.m_vIssues.push_back(f_Issue(Sev::Error, l_upInf->m_vAnySeen[l_nK] ? "sEvNoKey" : "sEvUnreach", {l_strS2.substr(6)}, {{l_iX, l_iY}}));
                } else if (!l_oRow.m_bTop && l_oRow.m_oEx.f_HasUp()
                           && std::none_of(l_oRow.m_oEx.m_vAll.begin(), l_oRow.m_oEx.m_vAll.end(), [&](const FloodState& l_oS) { return l_oS.m_bUp && l_oS.m_vSeen[l_nK]; })) {
                    l_oRow.m_vIssues.push_back(f_Issue(Sev::Warn, "sEvOptional", {l_strS2.substr(6)}, {{l_iX, l_iY}}));
                }
            }
        if (l_oRow.m_oEx.m_iCarry >= 0) l_oKeys = l_oRow.m_oEx.m_vAll[(size_t)l_oRow.m_oEx.m_iCarry].m_oLeft;
        l_vRows.push_back(std::move(l_oRow));
    }
    f_LogicOrderIssues(l_oP, l_vRows);
    l_oC.m_pP = &l_oP;
    l_oC.m_iVer = l_oP.m_iStoryVer;
    l_oC.m_vRows = std::move(l_vRows);
    return l_oC.m_vRows;
}

// ---- safe cells and auto-placement ---------------------------------------------------------------------------------
// A safe cell is free floor the player reaches on the fewest-doors way up (on the top floor: without any door), not
// next to stairs, the start or a door, so an event there can never cost the key the way up needs.
std::vector<Cell> f_SafeCells(Project& l_oP, const std::string& l_strFid) {
    const std::vector<StoryRow>& l_vRows = f_StoryCheck(l_oP);
    const StoryRow* l_pRow = nullptr;
    for (const StoryRow& l_oR : l_vRows) if (l_oR.m_strFid == l_strFid) l_pRow = &l_oR;
    if (!l_pRow) return {};
    const Explore& l_oEx = l_pRow->m_oEx;
    if (l_oEx.m_vAll.empty()) return {};
    const FloodState& l_oBase = l_oEx.m_vAll[(size_t)(l_oEx.m_iRoute >= 0 ? l_oEx.m_iRoute : 0)];
    const StageDoc& l_oD = l_oP.f_Doc(l_strFid);
    auto l_fnNear = [&](int l_iX, int l_iY) {
        for (int l_iDy = -1; l_iDy <= 1; l_iDy++)
            for (int l_iDx = -1; l_iDx <= 1; l_iDx++) {
                if (!l_oD.f_Inside(l_iX + l_iDx, l_iY + l_iDy)) continue;
                const char l_cC = l_oD.f_At(l_iX + l_iDx, l_iY + l_iDy);
                if (l_cC == 'U' || l_cC == 'D' || l_cC == '@' || f_DoorColor(l_cC) >= 0) return true;
            }
        return false;
    };
    std::vector<Cell> l_vOut;
    const int l_iW = l_oEx.m_oF.m_iW;
    for (int l_iK : l_oBase.m_vSeenOrder) {
        const int l_iX = l_iK % l_iW, l_iY = l_iK / l_iW;
        if (l_oD.f_Inside(l_iX, l_iY) && l_oD.f_At(l_iX, l_iY) == '.' && !l_fnNear(l_iX, l_iY)) l_vOut.push_back({l_iX, l_iY});
    }
    return l_vOut;
}

bool f_IsStepEvent(const Project& l_oP, const std::string& l_strId) {
    const Event* l_pE = l_oP.f_Event(l_strId);
    if (!l_pE) return true;
    const Json* l_pTr = f_Member(l_pE->m_jRec, "trigger");
    return !l_pTr || f_Str(*l_pTr, "on") == "step";
}

std::function<double()> f_Mulberry(uint32_t l_uSeed) {
    auto l_spS = std::make_shared<uint32_t>(l_uSeed);
    return [l_spS]() {
        *l_spS = *l_spS + 0x6D2B79F5u;
        uint32_t l_uX = (*l_spS ^ (*l_spS >> 15)) * (1u | *l_spS);
        l_uX = (l_uX + (l_uX ^ (l_uX >> 7)) * (61u | l_uX)) ^ l_uX;
        return (double)(l_uX ^ (l_uX >> 14)) / 4294967296.0;
    };
}

uint32_t f_HashStr(const std::string& l_strStr) {
    uint32_t l_uH = 2166136261u;
    for (unsigned char l_u8C : l_strStr) l_uH = (l_uH ^ l_u8C) * 16777619u;   // the seeds are ASCII (as the HTML editor's code units)
    return l_uH;
}

// picks spread-out cells: far from other objects, a little random, rooms over corridors
std::optional<Cell> f_PickCell(const Project& l_oP, const StageDoc& l_oD, const std::vector<Cell>& l_vCands, const std::function<double()>& l_fnRng) {
    (void)l_oP;
    if (l_vCands.empty()) return std::nullopt;
    std::vector<Cell> l_vObjs;
    for (int l_iY = 0; l_iY < l_oD.f_Height(); l_iY++)
        for (int l_iX = 0; l_iX < l_oD.f_Width(); l_iX++) if (l_oD.f_At(l_iX, l_iY) != '.' && l_oD.f_At(l_iX, l_iY) != '#') l_vObjs.push_back({l_iX, l_iY});
    auto l_fnOpen = [&](int l_iX, int l_iY) {
        int l_iN = 0;
        static const int l_arrDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& l_arrDd : l_arrDirs) if (l_oD.f_Inside(l_iX + l_arrDd[0], l_iY + l_arrDd[1]) && l_oD.f_At(l_iX + l_arrDd[0], l_iY + l_arrDd[1]) != '#') l_iN++;
        return l_iN;
    };
    std::optional<Cell> l_optBest;
    double l_dBs = -1;
    for (const Cell& l_oC : l_vCands) {
        int l_iSpread = 10;
        if (!l_vObjs.empty()) {
            l_iSpread = 1 << 30;
            for (const Cell& l_oO : l_vObjs) l_iSpread = std::min(l_iSpread, std::abs(l_oO.m_iX - l_oC.m_iX) + std::abs(l_oO.m_iY - l_oC.m_iY));
        }
        const double l_dSc = std::min(l_iSpread, 8) + (l_fnOpen(l_oC.m_iX, l_oC.m_iY) >= 3 ? 2 : 0) + l_fnRng() * 3;
        if (l_dSc > l_dBs) { l_dBs = l_dSc; l_optBest = l_oC; }
    }
    return l_optBest;
}

AutoPlaceResult f_AutoPlace(Project& l_oP, const std::vector<std::string>& l_vFids, const std::string& l_strMode, const std::string& l_strSeed,
                          const std::function<std::string(int)>& l_fnLabelFor) {
    AutoPlaceResult l_oRes;
    int l_iUndef = 0;
    const int l_iVer = l_oP.m_iStoryVer;   // events do not change keys or doors: the story check from before the loop holds
    for (const std::string& l_strFid : l_vFids) {
        StageDoc& l_oD = l_oP.f_Doc(l_strFid);
        std::vector<std::string> l_vListed;
        if (const Json* l_pEvs = f_Member(l_oD.m_jSpec, "events"); l_pEvs && l_pEvs->is_array())
            for (const Json& l_jE : *l_pEvs) if (l_jE.is_string() && f_IsStepEvent(l_oP, l_jE.get<std::string>())) l_vListed.push_back(l_jE.get<std::string>());
        auto l_fnRng = f_Mulberry(f_HashStr(l_strSeed + ":" + l_strFid));
        auto l_fnIsPlaced = [&](const std::string& l_strId) { return l_oP.f_Find(l_oD, "event:" + l_strId).has_value(); };
        std::vector<std::string> l_vTodo;
        for (const std::string& l_strId : l_vListed) {
            if (!l_oP.f_Event(l_strId)) { l_iUndef++; continue; }
            if (l_strMode == "all" || !l_fnIsPlaced(l_strId)) l_vTodo.push_back(l_strId);
        }
        if (l_vTodo.empty()) continue;
        l_oP.f_BeginEdit(l_oD);
        if (l_strMode == "all")
            for (int l_iY = 0; l_iY < l_oD.f_Height(); l_iY++)
                for (int l_iX = 0; l_iX < l_oD.f_Width(); l_iX++) {
                    const std::string l_strS2 = l_oP.f_SemAt(l_oD, l_iX, l_iY);
                    if (f_StartsWith(l_strS2, "event:") && std::find(l_vTodo.begin(), l_vTodo.end(), l_strS2.substr(6)) != l_vTodo.end())
                        l_oD.m_vGrid[(size_t)l_iY][(size_t)l_iX] = '.';
                }
        std::vector<Cell> l_vCands = f_SafeCells(l_oP, l_strFid);
        int l_iN = 0;
        for (const std::string& l_strId : l_vTodo) {
            const std::optional<Cell> l_optCell = f_PickCell(l_oP, l_oD, l_vCands, l_fnRng);
            if (!l_optCell) { l_oRes.m_vSkipped.push_back(f_Issue(Sev::Warn, "apSkipNoCell", {l_strId, l_strFid})); continue; }
            l_oP.f_PutSem(l_oD, l_optCell->m_iX, l_optCell->m_iY, "event:" + l_strId);
            l_iN++;
            l_vCands.erase(std::remove_if(l_vCands.begin(), l_vCands.end(),
                                       [&](const Cell& l_oC) { return std::abs(l_oC.m_iX - l_optCell->m_iX) + std::abs(l_oC.m_iY - l_optCell->m_iY) <= 1; }), l_vCands.end());
        }
        l_oP.f_CommitEdit(l_oD, l_fnLabelFor(l_iN));
        l_oP.m_iStoryVer = l_iVer;
        l_oRes.m_iPlaced += l_iN;
        if (l_iN) l_oRes.m_iFloors++;
    }
    if (l_iUndef) l_oRes.m_vSkipped.push_back(f_Issue(Sev::Warn, "apSkipUndef", {f_Num(l_iUndef)}));
    l_oP.m_iStoryVer = l_iVer + 1;
    return l_oRes;
}

}  // namespace stage
