// game_inventory.cpp — items: data/items.json lookups (the all-item file), pickup and use effects,
// level-ups (data/stats.json), the player's attributes and owned gear. The screens that show them
// are the player menu's tabs (game_player_menu.cpp). Split out of game.cpp 2026-09-13.
#include "game_internal.h"
#include "player_menu_rules.h"   // toms::levelUps

using namespace toms::game_detail;

// ---------- data/items.json ----------

std::string Game::f_Txt(const nlohmann::json& l_jJ) const {
    if (l_jJ.is_string()) return m_oLocale.f_Tr(l_jJ.get<std::string>());   // a text.json key (an unknown key reads as itself)
    return m_oLocale.f_Field(l_jJ);
}

const nlohmann::json* Game::f_ItemDef(const std::string& l_strId) const {
    auto l_itIt = m_mapItemDefs.find(l_strId);
    return l_itIt == m_mapItemDefs.end() ? nullptr : &l_itIt->second;
}

std::string Game::f_ItemType(const std::string& l_strId) const {
    const nlohmann::json* l_pD = f_ItemDef(l_strId);
    if (!l_pD) return std::string();
    return l_pD->value("type", l_strId.rfind("key_", 0) == 0 ? std::string("key") : std::string("consumable"));
}

bool Game::f_ItemImportant(const std::string& l_strId) const {
    const nlohmann::json* l_pD = f_ItemDef(l_strId);
    return l_pD && l_pD->value("important", false);
}

int Game::f_ItemPrice(const std::string& l_strId) const {
    const nlohmann::json* l_pD = f_ItemDef(l_strId);
    return l_pD ? l_pD->value("price", -1) : -1;
}

const nlohmann::json& Game::f_ItemStats(const std::string& l_strId) const {
    static const nlohmann::json l_jNone = nlohmann::json::object();
    const nlohmann::json* l_pD = f_ItemDef(l_strId);
    if (!l_pD) return l_jNone;
    if (l_pD->contains("stats") && (*l_pD)["stats"].is_object()) return (*l_pD)["stats"];
    if (l_pD->contains("effect") && (*l_pD)["effect"].is_object()) return (*l_pD)["effect"];   // the pre-2026-10 shape
    return l_jNone;
}

std::string Game::f_ItemName(const std::string& l_strId) const {
    const nlohmann::json* l_pD = f_ItemDef(l_strId);
    if (!l_pD || !l_pD->contains("name")) return l_strId;
    std::string l_strS = f_Txt((*l_pD)["name"]);
    return l_strS.empty() ? l_strId : l_strS;
}

std::string Game::f_ItemDesc(const std::string& l_strId) const {
    const nlohmann::json* l_pD = f_ItemDef(l_strId);
    if (!l_pD || !l_pD->contains("desc")) return "";
    return f_Txt((*l_pD)["desc"]);
}

// ---------- effects ----------

void Game::f_ApplyStats(const std::string& l_strId, const nlohmann::json& l_jEff) {
    auto l_fnNum = [&](const char* l_pszK) { return l_jEff.contains(l_pszK) && l_jEff[l_pszK].is_number() ? l_jEff[l_pszK].get<int>() : 0; };
    if (l_jEff.contains("hp"))    m_oPl.m_iHp    = std::min(f_EffectiveMaxHp(), m_oPl.m_iHp + l_fnNum("hp"));
    if (l_jEff.contains("maxhp")) { m_oPl.m_iMaxhp += l_fnNum("maxhp"); m_oPl.m_iHp += l_fnNum("maxhp"); }
    m_oPl.m_iAtk += l_fnNum("atk");
    m_oPl.m_iDef += l_fnNum("def");
    m_oPl.m_iGold += l_fnNum("gold");
    m_oPl.m_iKeyYellow += l_fnNum("key_yellow");
    m_oPl.m_iKeyBlue   += l_fnNum("key_blue");
    m_oPl.m_iKeyRed    += l_fnNum("key_red");
    for (const AttrDef& l_oA : m_vAttrDefs) m_oPl.m_mapAttrs[l_oA.m_strId] += l_fnNum(l_oA.m_strId.c_str());
    if (l_jEff.contains("exp")) f_GainExp(l_fnNum("exp"));
    if (l_jEff.contains("warp")) {
        const nlohmann::json* l_pD = f_ItemDef(l_strId);
        std::string l_strDst = l_pD ? l_pD->value("warp_to", std::string("")) : std::string("");
        if (!l_strDst.empty() && toms::f_VfsExists(f_PathFor(toms::pattern::g_pszStage, l_strDst)))
            f_LoadStage(l_strDst);
    }
}

void Game::f_ApplyItem(const std::string& l_strId) {
    if (!f_ItemDef(l_strId)) return;
    f_ApplyStats(l_strId, f_ItemStats(l_strId));
    m_oAudio.f_Play("get_item");
}

void Game::f_ReceiveItem(const std::string& l_strId) {
    const nlohmann::json* l_pD = f_ItemDef(l_strId);
    if (!l_pD) { m_oPl.m_vInv.push_back(l_strId); return; }
    const std::string l_strType = f_ItemType(l_strId);
    if (l_strType == "key" || l_pD->value("autoUse", false)) f_ApplyItem(l_strId);
    else if (f_IsGearType(l_strType)) { f_OwnGear(l_strId, false); m_oAudio.f_Play("get_item"); }
    else m_oPl.m_vInv.push_back(l_strId);
}

void Game::f_DebugGive(const std::string& l_strId) {
    if (l_strId.rfind("gold:", 0) == 0) { m_oPl.m_iGold += std::atoi(l_strId.c_str() + 5); return; }
    if (l_strId.rfind("crystals:", 0) == 0) { m_oPl.m_iCrystals += std::max(0, std::atoi(l_strId.c_str() + 9)); return; }
    if (l_strId.rfind("exp:", 0) == 0) { f_GainExp(std::atoi(l_strId.c_str() + 4)); return; }
    if (l_strId.rfind("points:", 0) == 0) { m_oPl.m_iAttrPoints += std::max(0, std::atoi(l_strId.c_str() + 7)); return; }
    f_ReceiveItem(l_strId);
}

// ---------- levels and attributes (data/stats.json) ----------

void Game::f_GainExp(int l_iExp) {
    m_oPl.m_iExp += l_iExp;
    const int l_iGained = toms::f_LevelUps(m_oPl.m_iExp, m_oPl.m_iLv, m_oLevelUp.m_iExpPerLevel, m_oLevelUp.m_iMaxLevel);
    for (int l_iI = 0; l_iI < l_iGained; l_iI++) {
        const int l_iReached = m_oPl.m_iLv - l_iGained + 1 + l_iI;
        m_oPl.m_iAtk += m_oLevelUp.m_iAtk;
        m_oPl.m_iDef += m_oLevelUp.m_iDef;
        m_oPl.m_iMaxhp += m_oLevelUp.m_iMaxhp;
        for (const AttrDef& l_oA : m_vAttrDefs) m_oPl.m_mapAttrs[l_oA.m_strId] += l_oA.m_iPerLevel;
        m_oPl.m_iAttrPoints += toms::f_PointsForLevel(m_oLevelUp.m_vPoints, l_iReached, m_oLevelUp.m_iFreePoints);   // placed on the Status tab
    }
    if (l_iGained > 0) f_PushNotification(m_oLocale.f_Tr("battle.levelup") + std::to_string(m_oPl.m_iLv));
}

void Game::f_ResetAttrs() {
    m_oPl.m_mapAttrs.clear();
    for (const AttrDef& l_oA : m_vAttrDefs) m_oPl.m_mapAttrs[l_oA.m_strId] = l_oA.m_iBase + l_oA.m_iPerLevel * std::max(0, m_oPl.m_iLv - 1);
}

float Game::f_AttrEffect(const std::string& l_strStat) const {
    float l_fTotal = 0.0f;
    for (const AttrDef& l_oA : m_vAttrDefs)
        for (const auto& [l_strS, l_fPer] : l_oA.m_vEffects)
            if (l_strS == l_strStat) l_fTotal += (float)(f_AttrValue(l_oA.m_strId) - l_oA.m_iBase) * l_fPer;
    return l_fTotal;
}

toms::PowerBarParams Game::f_AttackBarParams() const {
    toms::PowerBarParams l_oBar = toms::f_EffectiveAttackBar(m_oEquipped, m_mapEquipmentDefs);
    l_oBar.m_fGreenHalf = std::max(1.0f, std::min(l_oBar.m_fBlueOuter - 1.0f, l_oBar.m_fGreenHalf + f_AttrEffect("hitZone")));   // DEX
    return l_oBar;
}

int Game::f_BarCooldownMs() const {
    return std::max(200, m_iBarCooldownMs + (int)std::lround(f_AttrEffect("cooldownMs")));   // AGI (per is negative)
}

// The Status tab's points (the owner 2026-10-24): + and - only move points in a draft, which the screen shows; Confirm
// makes them the attributes and saves the run at once, Reset (or closing the menu) drops them.
void Game::f_SpendAttrPoint(int l_iIndex) {
    if (m_oPl.m_iAttrPoints - f_AttrDraftTotal() <= 0 || l_iIndex < 0 || l_iIndex >= (int)m_vAttrDefs.size()) { m_oAudio.f_Play("deny"); return; }
    m_mapAttrDraft[m_vAttrDefs[l_iIndex].m_strId] += 1;
    m_oAudio.f_Play("confirm_click");
}

void Game::f_UnspendAttrPoint(int l_iIndex) {
    if (l_iIndex < 0 || l_iIndex >= (int)m_vAttrDefs.size()) return;
    auto l_itIt = m_mapAttrDraft.find(m_vAttrDefs[l_iIndex].m_strId);
    if (l_itIt == m_mapAttrDraft.end()) { m_oAudio.f_Play("deny"); return; }   // only a point placed in this draft comes back
    if (--l_itIt->second <= 0) m_mapAttrDraft.erase(l_itIt);
    m_oAudio.f_Play("confirm_click");
}

void Game::f_ConfirmAttrPoints() {
    const int l_iN = std::min(f_AttrDraftTotal(), m_oPl.m_iAttrPoints);
    if (l_iN <= 0) { m_oAudio.f_Play("deny"); return; }
    for (const auto& [l_strId, l_iK] : m_mapAttrDraft) m_oPl.m_mapAttrs[l_strId] += l_iK;
    m_oPl.m_iAttrPoints -= l_iN;
    m_mapAttrDraft.clear();
    f_MarkProgressDirty();
    f_SaveCurrentRun();   // kept now, not at the next autosave
    f_PushNotification(m_oLocale.f_Tr("status.points_saved"));
    m_oAudio.f_Play("get_item");
}

void Game::f_ResetAttrDraft() {
    if (m_mapAttrDraft.empty()) return;
    m_mapAttrDraft.clear();
    m_oAudio.f_Play("close_ui");
}

int Game::f_AttrValue(const std::string& l_strId) const {
    auto l_itIt = m_oPl.m_mapAttrs.find(l_strId);
    int l_iV = l_itIt == m_oPl.m_mapAttrs.end() ? 0 : l_itIt->second;
    for (const std::string& l_strG : {m_oEquipped.m_strWeaponId, m_oEquipped.m_strArmorId, m_oEquipped.m_strTalentId}) {
        auto l_itE = m_mapEquipmentDefs.find(l_strG);
        if (l_itE == m_mapEquipmentDefs.end()) continue;
        auto l_itS = l_itE->second.m_mapStats.find(l_strId);
        if (l_itS != l_itE->second.m_mapStats.end()) l_iV += l_itS->second;
    }
    // The equipped weapon's level (docs/gameplay/07 phase 1, data/weapon_play.json "levels"): its bonuses at that level.
    const std::string l_strKey = f_WeaponKey();
    const auto& l_mapLvStats = toms::f_WeaponLevelEntry(f_WeaponPlayOf(l_strKey), f_WeaponLevel(l_strKey)).m_mapStats;
    if (auto l_itS = l_mapLvStats.find(l_strId); l_itS != l_mapLvStats.end()) l_iV += l_itS->second;
    return l_iV;
}

std::string Game::f_WeaponKey() const { return m_oEquipped.m_strWeaponId.empty() ? std::string("unarmed") : m_oEquipped.m_strWeaponId; }

const toms::WeaponPlay& Game::f_WeaponPlayOf(const std::string& l_strKey) const { return m_oWeaponPlay.f_Get(l_strKey); }

int Game::f_WeaponLevel(const std::string& l_strKey) const {
    auto l_itIt = m_mapWeaponUses.find(l_strKey);
    return toms::f_WeaponLevelFromUses(f_WeaponPlayOf(l_strKey), l_itIt == m_mapWeaponUses.end() ? 0 : l_itIt->second);
}

float Game::f_ComboCharge() const {
    const int l_iFull = std::max(1, f_WeaponPlayOf(f_WeaponKey()).m_iChargeMs);
    return std::min(1.0f, float(m_oCs.m_iChargeMs) / float(l_iFull));
}

// ---------- owned gear ----------

void Game::f_OwnGear(const std::string& l_strId, bool l_bEquip) {
    if (m_mapEquipmentDefs.find(l_strId) == m_mapEquipmentDefs.end()) return;
    if (std::find(m_vGearOwned.begin(), m_vGearOwned.end(), l_strId) == m_vGearOwned.end()) {
        m_vGearOwned.push_back(l_strId);
        m_bNewsGear = true;
    }
    if (l_bEquip) f_EquipGear(l_strId);
}

void Game::f_EquipGear(const std::string& l_strId) {
    auto l_itIt = m_mapEquipmentDefs.find(l_strId);
    if (l_itIt == m_mapEquipmentDefs.end()) return;
    switch (l_itIt->second.m_eSlot) {
        case toms::EquipmentSlot::Weapon: m_oEquipped.m_strWeaponId = l_strId; break;
        case toms::EquipmentSlot::Armor:  m_oEquipped.m_strArmorId  = l_strId; break;
        case toms::EquipmentSlot::Talent: m_oEquipped.m_strTalentId = l_strId; break;
    }
    f_MarkProgressDirty();
}
