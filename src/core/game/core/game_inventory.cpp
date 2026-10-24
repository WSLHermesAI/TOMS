// game_inventory.cpp — items: data/items.json lookups (the all-item file), pickup and use effects,
// level-ups (data/stats.json), the player's attributes and owned gear. The screens that show them
// are the player menu's tabs (game_player_menu.cpp). Split out of game.cpp 2026-09-13.
#include "game_internal.h"
#include "player_menu_rules.h"   // toms::levelUps

using namespace toms::game_detail;

// ---------- data/items.json ----------

std::string Game::txt(const nlohmann::json& j) const {
    if (j.is_string()) return locale_.tr(j.get<std::string>());   // a text.json key (an unknown key reads as itself)
    return locale_.field(j);
}

const nlohmann::json* Game::itemDef(const std::string& id) const {
    auto it = itemDefs.find(id);
    return it == itemDefs.end() ? nullptr : &it->second;
}

std::string Game::itemType(const std::string& id) const {
    const nlohmann::json* d = itemDef(id);
    if (!d) return std::string();
    return d->value("type", id.rfind("key_", 0) == 0 ? std::string("key") : std::string("consumable"));
}

bool Game::itemImportant(const std::string& id) const {
    const nlohmann::json* d = itemDef(id);
    return d && d->value("important", false);
}

int Game::itemPrice(const std::string& id) const {
    const nlohmann::json* d = itemDef(id);
    return d ? d->value("price", -1) : -1;
}

const nlohmann::json& Game::itemStats(const std::string& id) const {
    static const nlohmann::json kNone = nlohmann::json::object();
    const nlohmann::json* d = itemDef(id);
    if (!d) return kNone;
    if (d->contains("stats") && (*d)["stats"].is_object()) return (*d)["stats"];
    if (d->contains("effect") && (*d)["effect"].is_object()) return (*d)["effect"];   // the pre-2026-10 shape
    return kNone;
}

std::string Game::itemName(const std::string& id) const {
    const nlohmann::json* d = itemDef(id);
    if (!d || !d->contains("name")) return id;
    std::string s = txt((*d)["name"]);
    return s.empty() ? id : s;
}

std::string Game::itemDesc(const std::string& id) const {
    const nlohmann::json* d = itemDef(id);
    if (!d || !d->contains("desc")) return "";
    return txt((*d)["desc"]);
}

// ---------- effects ----------

void Game::applyStats(const std::string& id, const nlohmann::json& eff) {
    auto num = [&](const char* k) { return eff.contains(k) && eff[k].is_number() ? eff[k].get<int>() : 0; };
    if (eff.contains("hp"))    pl.hp    = std::min(effectiveMaxHp(), pl.hp + num("hp"));
    if (eff.contains("maxhp")) { pl.maxhp += num("maxhp"); pl.hp += num("maxhp"); }
    pl.atk += num("atk");
    pl.def += num("def");
    pl.gold += num("gold");
    pl.key_yellow += num("key_yellow");
    pl.key_blue   += num("key_blue");
    pl.key_red    += num("key_red");
    for (const AttrDef& a : attrDefs_) pl.attrs[a.id] += num(a.id.c_str());
    if (eff.contains("exp")) gainExp(num("exp"));
    if (eff.contains("warp")) {
        const nlohmann::json* d = itemDef(id);
        std::string dst = d ? d->value("warp_to", std::string("")) : std::string("");
        if (!dst.empty() && toms::vfsExists(pathFor(toms::pattern::kStage, dst)))
            loadStage(dst);
    }
}

void Game::applyItem(const std::string& id) {
    if (!itemDef(id)) return;
    applyStats(id, itemStats(id));
    audio.play("get_item");
}

void Game::receiveItem(const std::string& id) {
    const nlohmann::json* d = itemDef(id);
    if (!d) { pl.inv.push_back(id); return; }
    const std::string type = itemType(id);
    if (type == "key" || d->value("autoUse", false)) applyItem(id);
    else if (isGearType(type)) { ownGear(id, false); audio.play("get_item"); }
    else pl.inv.push_back(id);
}

void Game::debugGive(const std::string& id) {
    if (id.rfind("gold:", 0) == 0) { pl.gold += std::atoi(id.c_str() + 5); return; }
    if (id.rfind("crystals:", 0) == 0) { pl.crystals += std::max(0, std::atoi(id.c_str() + 9)); return; }
    if (id.rfind("exp:", 0) == 0) { gainExp(std::atoi(id.c_str() + 4)); return; }
    if (id.rfind("points:", 0) == 0) { pl.attrPoints += std::max(0, std::atoi(id.c_str() + 7)); return; }
    receiveItem(id);
}

// ---------- levels and attributes (data/stats.json) ----------

void Game::gainExp(int exp) {
    pl.exp += exp;
    const int gained = toms::levelUps(pl.exp, pl.lv, levelUp_.expPerLevel, levelUp_.maxLevel);
    for (int i = 0; i < gained; i++) {
        const int reached = pl.lv - gained + 1 + i;
        pl.atk += levelUp_.atk;
        pl.def += levelUp_.def;
        pl.maxhp += levelUp_.maxhp;
        for (const AttrDef& a : attrDefs_) pl.attrs[a.id] += a.perLevel;
        pl.attrPoints += toms::pointsForLevel(levelUp_.points, reached, levelUp_.freePoints);   // placed on the Status tab
    }
    if (gained > 0) pushNotification(locale_.tr("battle.levelup") + std::to_string(pl.lv));
}

void Game::resetAttrs() {
    pl.attrs.clear();
    for (const AttrDef& a : attrDefs_) pl.attrs[a.id] = a.base + a.perLevel * std::max(0, pl.lv - 1);
}

float Game::attrEffect(const std::string& stat) const {
    float total = 0.0f;
    for (const AttrDef& a : attrDefs_)
        for (const auto& [s, per] : a.effects)
            if (s == stat) total += (float)(attrValue(a.id) - a.base) * per;
    return total;
}

toms::PowerBarParams Game::attackBarParams() const {
    toms::PowerBarParams bar = toms::effectiveAttackBar(equipped_, equipmentDefs_);
    bar.greenHalf = std::max(1.0f, std::min(bar.blueOuter - 1.0f, bar.greenHalf + attrEffect("hitZone")));   // DEX
    return bar;
}

int Game::barCooldownMs() const {
    return std::max(200, barCooldownMs_ + (int)std::lround(attrEffect("cooldownMs")));   // AGI (per is negative)
}

// The Status tab's points (the owner 2026-10-24): + and - only move points in a draft, which the screen shows; Confirm
// makes them the attributes and saves the run at once, Reset (or closing the menu) drops them.
void Game::spendAttrPoint(int index) {
    if (pl.attrPoints - attrDraftTotal() <= 0 || index < 0 || index >= (int)attrDefs_.size()) { audio.play("deny"); return; }
    attrDraft_[attrDefs_[index].id] += 1;
    audio.play("confirm_click");
}

void Game::unspendAttrPoint(int index) {
    if (index < 0 || index >= (int)attrDefs_.size()) return;
    auto it = attrDraft_.find(attrDefs_[index].id);
    if (it == attrDraft_.end()) { audio.play("deny"); return; }   // only a point placed in this draft comes back
    if (--it->second <= 0) attrDraft_.erase(it);
    audio.play("confirm_click");
}

void Game::confirmAttrPoints() {
    const int n = std::min(attrDraftTotal(), pl.attrPoints);
    if (n <= 0) { audio.play("deny"); return; }
    for (const auto& [id, k] : attrDraft_) pl.attrs[id] += k;
    pl.attrPoints -= n;
    attrDraft_.clear();
    markProgressDirty();
    saveCurrentRun();   // kept now, not at the next autosave
    pushNotification(locale_.tr("status.points_saved"));
    audio.play("get_item");
}

void Game::resetAttrDraft() {
    if (attrDraft_.empty()) return;
    attrDraft_.clear();
    audio.play("close_ui");
}

int Game::attrValue(const std::string& id) const {
    auto it = pl.attrs.find(id);
    int v = it == pl.attrs.end() ? 0 : it->second;
    for (const std::string& g : {equipped_.weaponId, equipped_.armorId, equipped_.talentId}) {
        auto e = equipmentDefs_.find(g);
        if (e == equipmentDefs_.end()) continue;
        auto s = e->second.stats.find(id);
        if (s != e->second.stats.end()) v += s->second;
    }
    // The equipped weapon's level (docs/gameplay/07 phase 1, data/weapon_play.json "levels"): its bonuses at that level.
    const std::string key = weaponKey();
    const auto& lvStats = toms::weaponLevelEntry(weaponPlayOf(key), weaponLevel(key)).stats;
    if (auto s = lvStats.find(id); s != lvStats.end()) v += s->second;
    return v;
}

std::string Game::weaponKey() const { return equipped_.weaponId.empty() ? std::string("unarmed") : equipped_.weaponId; }

const toms::WeaponPlay& Game::weaponPlayOf(const std::string& key) const { return weaponPlay_.get(key); }

int Game::weaponLevel(const std::string& key) const {
    auto it = weaponUses_.find(key);
    return toms::weaponLevelFromUses(weaponPlayOf(key), it == weaponUses_.end() ? 0 : it->second);
}

float Game::comboCharge() const {
    const int full = std::max(1, weaponPlayOf(weaponKey()).chargeMs);
    return std::min(1.0f, float(cs.chargeMs) / float(full));
}

// ---------- owned gear ----------

void Game::ownGear(const std::string& id, bool equip) {
    if (equipmentDefs_.find(id) == equipmentDefs_.end()) return;
    if (std::find(gearOwned_.begin(), gearOwned_.end(), id) == gearOwned_.end()) {
        gearOwned_.push_back(id);
        newsGear_ = true;
    }
    if (equip) equipGear(id);
}

void Game::equipGear(const std::string& id) {
    auto it = equipmentDefs_.find(id);
    if (it == equipmentDefs_.end()) return;
    switch (it->second.slot) {
        case toms::EquipmentSlot::Weapon: equipped_.weaponId = id; break;
        case toms::EquipmentSlot::Armor:  equipped_.armorId  = id; break;
        case toms::EquipmentSlot::Talent: equipped_.talentId = id; break;
    }
    markProgressDirty();
}
