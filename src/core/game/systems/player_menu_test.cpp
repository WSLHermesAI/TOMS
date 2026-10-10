// Tests for the player menu (docs/20_PLAYER_MENU.md): its rules (player_menu_rules.h) and the data it
// reads -- data/items.json (the all-item file), data/store.json, data/stats.json, the chapters' and
// missions' titles, and their text in data/text.json. Run in assets/ (tests/CMakeLists.txt).
#include "player_menu_rules.h"
#include "equipment_system.h"
#include "vfs.h"

#include <json.hpp>

#include <cstdio>
#include <set>
#include <string>

using namespace toms;

static int g_checks = 0;
static int g_failed = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL: %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static nlohmann::json readJson(const std::string& path) {
    std::string text;
    if (!vfsReadAll(path, text)) return nlohmann::json();
    try { return nlohmann::json::parse(text); } catch (...) { return nlohmann::json(); }
}

int main() {
    // ---- stacking ----
    {
        auto s = stackItems({"potion_red", "gem_atk", "potion_red", "potion_red", "scroll"});
        CHECK(s.size() == 3, "3 different items, got %zu", s.size());
        CHECK(s.size() == 3 && s[0].id == "potion_red" && s[0].count == 3, "first picked up first, with its count");
        CHECK(s.size() == 3 && s[1].id == "gem_atk" && s[1].count == 1, "a single copy is x1");
        CHECK(stackItems({}).empty(), "an empty backpack has no cells");
    }
    // ---- the 3-per-row cursor ----
    {
        int sel = 0;
        CHECK(gridMove(sel, 7, 1, 0) && sel == 1, "right moves one cell");
        CHECK(gridMove(sel, 7, 0, 1) && sel == 4, "down moves one row (3 cells)");
        sel = 4;
        CHECK(gridMove(sel, 7, 0, 1) && sel == 6, "down into a short last row lands on its last cell");
        sel = 3;
        CHECK(!gridMove(sel, 7, -1, 0) && sel == 3, "left off the first column leaves the grid (switch filter)");
        sel = 2;
        CHECK(!gridMove(sel, 7, 1, 0) && sel == 2, "right off the last column leaves the grid");
        sel = 6;
        CHECK(!gridMove(sel, 7, 1, 0), "right off the very last cell leaves the grid");
        sel = 1;
        CHECK(gridMove(sel, 7, 0, -1) && sel == 1, "up on the first row stays");
        sel = 0;
        CHECK(!gridMove(sel, 0, 1, 0) && gridMove(sel, 0, 0, 1), "an empty grid: sideways switches, up/down does nothing");
    }
    // ---- dropping ----
    CHECK(canDrop(false, false), "an ordinary item can be dropped");
    CHECK(!canDrop(true, false), "an important item cannot");
    CHECK(!canDrop(false, true), "worn gear cannot");
    // ---- level-ups (lv * expPerLevel per level) ----
    {
        int exp = 29, lv = 1;
        CHECK(levelUps(exp, lv, 30) == 0 && lv == 1 && exp == 29, "29 EXP is not a level at 30 per level");
        exp = 30 + 60 + 5; lv = 1;
        CHECK(levelUps(exp, lv, 30) == 2 && lv == 3 && exp == 5, "95 EXP: Lv 1 -> 3 with 5 left, got lv %d exp %d", lv, exp);
        exp = 10; lv = 1;
        CHECK(levelUps(exp, lv, 0) == 4 && lv == 5 && exp == 0, "expPerLevel 0 is read as 1 (1+2+3+4 = 10 EXP: Lv 5)");
    }

    // ---- the data ----
    const nlohmann::json text = readJson("data/text.json");
    const nlohmann::json& strings = text.is_object() && text.contains("strings") ? text["strings"] : nlohmann::json();
    CHECK(strings.is_object(), "data/text.json has strings");
    auto hasText = [&](const std::string& key) {
        return strings.is_object() && strings.contains(key) && strings[key].contains("zh_TW") && strings[key].contains("en");
    };

    const nlohmann::json stats = readJson("data/stats.json");
    std::set<std::string> attrIds;
    CHECK(stats.contains("attributes") && stats["attributes"].is_array() && !stats["attributes"].empty(), "data/stats.json lists attributes");
    if (stats.contains("attributes"))
        for (const auto& a : stats["attributes"]) {
            const std::string id = a.value("id", std::string());
            CHECK(!id.empty() && attrIds.insert(id).second, "attribute ids are present and unique ('%s')", id.c_str());
            CHECK(hasText(a.value("name", std::string())) && hasText(a.value("desc", std::string())),
                  "attribute '%s' has its name and description in text.json (zh_TW + en)", id.c_str());
        }
    for (const char* need : {"str", "dex", "agi"})
        CHECK(attrIds.count(need), "the owner asked for %s (Q11)", need);
    CHECK(stats.contains("levelUp") && stats["levelUp"].value("expPerLevel", 0) > 0, "levelUp.expPerLevel is set");

    const nlohmann::json items = readJson("data/items.json");
    CHECK(items.is_object() && items.size() > 10, "data/items.json holds the items");
    const std::set<std::string> types = {"consumable", "key", "weapon", "armor", "talent", "story"};
    std::set<std::string> statKeys = {"hp", "maxhp", "atk", "def", "gold", "exp", "key_yellow", "key_blue", "key_red", "warp"};
    statKeys.insert(attrIds.begin(), attrIds.end());
    int gear = 0;
    for (auto it = items.begin(); it != items.end(); ++it) {
        const std::string id = it.key();
        if (id.empty() || id[0] == '_') continue;
        const nlohmann::json& d = it.value();
        const std::string type = d.value("type", std::string());
        CHECK(types.count(type), "item '%s': unknown type '%s'", id.c_str(), type.c_str());
        CHECK(d.contains("price") && d["price"].is_number_integer() && d["price"].get<int>() >= 0, "item '%s' has a price", id.c_str());
        CHECK(d.contains("sprite") && !d.value("sprite", std::string()).empty(), "item '%s' has a sprite", id.c_str());
        CHECK(hasText(d.value("name", std::string())), "item '%s': its name key is in text.json", id.c_str());
        CHECK(hasText(d.value("desc", std::string())), "item '%s': its desc key is in text.json", id.c_str());
        if (d.contains("stats"))
            for (auto s = d["stats"].begin(); s != d["stats"].end(); ++s)
                CHECK(statKeys.count(s.key()), "item '%s': stat '%s' is not one the game applies", id.c_str(), s.key().c_str());
        if (type == "key") CHECK(d.value("important", false), "key '%s' is important (never dropped)", id.c_str());
        if (type == "weapon" || type == "armor" || type == "talent") {
            ++gear;
            const EquipmentDefinition e = equipmentFromJson(d);
            const EquipmentSlot want = type == "weapon" ? EquipmentSlot::Weapon : type == "armor" ? EquipmentSlot::Armor : EquipmentSlot::Talent;
            CHECK(e.slot == want, "gear '%s' reads as its own slot", id.c_str());
        }
    }
    CHECK(gear >= 9, "the weapons, armor and talents of the old equipment.json are in items.json (%d)", gear);
    {
        const EquipmentDefinition d = equipmentFromJson(items.value("war_hammer", nlohmann::json()));
        CHECK(d.statAtk == 8, "war_hammer still gives ATK +8, got %d", d.statAtk);
    }

    const nlohmann::json store = readJson("data/store.json");
    CHECK(store.contains("items") && store["items"].is_array() && !store["items"].empty(), "data/store.json sells something");
    if (store.contains("items"))
        for (const auto& e : store["items"]) {
            const std::string id = e.value("item", std::string());
            CHECK(items.contains(id), "the store sells '%s', which is not in items.json", id.c_str());
            CHECK(e.value("growth", 1) >= 1, "store entry '%s': growth >= 1", id.c_str());
        }

    // The Events tab: every chapter and mission has a title and a description (Q14).
    for (const char* ch : {"ch_01", "ch_02", "ch_03", "ch_04"}) {
        const nlohmann::json c = readJson(std::string("data/story/chapters/") + ch + ".json");
        CHECK(hasText(c.value("title", std::string())) && hasText(c.value("desc", std::string())),
              "chapter %s has its title and description in text.json", ch);
    }
    const nlohmann::json missions = readJson("data/missions.json");
    for (auto it = missions.begin(); it != missions.end(); ++it)
        CHECK(hasText(it.value().value("title", std::string())) && hasText(it.value().value("desc", std::string())),
              "mission %s has its title and description in text.json", it.key().c_str());

    std::printf("player_menu_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
