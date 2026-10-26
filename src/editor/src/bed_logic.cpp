// bed_logic.cpp -- see bed_logic.h.
#include "bed_logic.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace bed {

// ---- the growth calculator ----

std::vector<int> levelPoints(int total, int maxLevel, double k) {
    std::vector<int> out;
    if (maxLevel < 2 || total < 0) return out;
    k = std::max(0.05, k);
    const double denom = std::pow((double)(maxLevel - 1), k);
    std::vector<double> raw;
    for (int n = 2; n <= maxLevel; n++)
        raw.push_back(total * (std::pow((double)(n - 1), k) - std::pow((double)(n - 2), k)) / denom);
    int sum = 0;
    for (double r : raw) { out.push_back((int)std::floor(r)); sum += out.back(); }
    std::vector<size_t> order(raw.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const double fa = raw[a] - std::floor(raw[a]), fb = raw[b] - std::floor(raw[b]);
        if (std::fabs(fa - fb) > 1e-9) return fa > fb;
        return a > b;   // a tie: the later level
    });
    for (size_t i = 0; i < order.size() && sum < total; i++, sum++) out[order[i]]++;
    return out;
}

int pointsAt(const ojson& rows, int level) {
    int best = -1, pts = 0;
    if (!rows.is_array()) return 0;
    for (const auto& r : rows) {
        const int from = r.value("from", 0);
        if (from <= level && from > best) { best = from; pts = r.value("points", 0); }
    }
    return pts;
}

ojson pointsRows(const std::vector<int>& perLevel, int maxLevel, const ojson& oldRows) {
    ojson rows = ojson::array();
    for (size_t i = 0; i < perLevel.size(); i++) {
        const int lv = (int)i + 2;
        if (!rows.empty() && rows.back()["points"].get<int>() == perLevel[i]) continue;
        ojson r = ojson::object();
        r["from"] = lv;
        r["points"] = perLevel[i];
        rows.push_back(r);
    }
    // past the calculator's levels the old table goes on
    const int after = pointsAt(oldRows, maxLevel + 1);
    if (rows.empty() || rows.back()["points"].get<int>() != after) {
        ojson r = ojson::object();
        r["from"] = maxLevel + 1;
        r["points"] = after;
        rows.push_back(r);
    }
    if (oldRows.is_array())
        for (const auto& r : oldRows)
            if (r.value("from", 0) > maxLevel + 1 && rows.back()["points"].get<int>() != r.value("points", 0)) rows.push_back(r);
    return rows;
}

// ---- the test setup ----

ojson setupToScenario(const Setup& s) {
    ojson j = ojson::object();
    j["_comment"] = "battle_editor's test setup \"" + s.name + "\" (docs/gameplay/13_BATTLE_EDITOR.md §2): not a test, an editor file.";
    j["scene"] = "battle";
    ojson p = ojson::object();
    if (s.player.weapon.empty()) p["unarmed"] = true;
    else {
        p["give"] = ojson::array({s.player.weapon});
        p["equip"] = ojson::array({s.player.weapon});
        ojson uses = ojson::object();
        uses[s.player.weapon] = s.player.weaponWins;
        p["weaponUses"] = uses;
    }
    p["lv"] = s.player.lv;
    p["maxhp"] = s.player.maxhp;
    p["hp"] = std::min(s.player.hp, s.player.maxhp);
    if (!s.player.attrs.empty()) p["attrs"] = s.player.attrs;
    if (s.player.attrPoints > 0) p["attrPoints"] = s.player.attrPoints;
    j["player"] = p;
    ojson b = ojson::object();
    if (!s.foes.empty()) {
        b["enemy"] = s.foes[0].id;
        for (auto it = s.foes[0].overrides.begin(); it != s.foes[0].overrides.end(); ++it) b[it.key()] = it.value();
        ojson party = ojson::array();
        for (size_t i = 1; i < s.foes.size(); i++) {
            ojson m = ojson::object();
            m["enemy"] = s.foes[i].id;
            for (auto it = s.foes[i].overrides.begin(); it != s.foes[i].overrides.end(); ++it) m[it.key()] = it.value();
            party.push_back(m);
        }
        if (!party.empty()) b["party"] = party;
    }
    if (!s.pace.empty()) b["pace"] = s.pace;
    j["battle"] = b;
    ojson ed = ojson::object();   // the setup as the editor holds it
    ed["name"] = s.name;
    ed["pace"] = s.pace;
    ojson foes = ojson::array();
    for (const SetupFoe& f : s.foes) foes.push_back(f.id);
    ed["foes"] = foes;
    ojson pl = ojson::object();
    pl["lv"] = s.player.lv; pl["hp"] = s.player.hp; pl["maxhp"] = s.player.maxhp;
    pl["weapon"] = s.player.weapon; pl["weaponWins"] = s.player.weaponWins;
    pl["attrs"] = s.player.attrs; pl["attrPoints"] = s.player.attrPoints;
    ed["player"] = pl;
    j["_editor"] = ed;
    return j;
}

bool setupFromScenario(const ojson& j, Setup& out) {
    if (!j.is_object() || !j.contains("_editor") || !j["_editor"].is_object()) return false;
    const ojson& ed = j["_editor"];
    Setup s;
    s.name = ed.value("name", std::string());
    s.pace = ed.value("pace", std::string());
    if (ed.contains("foes") && ed["foes"].is_array())
        for (const auto& id : ed["foes"])
            if (id.is_string()) s.foes.push_back({id.get<std::string>(), ojson::object()});
    if (ed.contains("player") && ed["player"].is_object()) {
        const ojson& p = ed["player"];
        s.player.lv = p.value("lv", 1);
        s.player.hp = p.value("hp", 120);
        s.player.maxhp = p.value("maxhp", 120);
        s.player.weapon = p.value("weapon", std::string("bow"));
        s.player.weaponWins = p.value("weaponWins", 0);
        if (p.contains("attrs") && p["attrs"].is_object()) s.player.attrs = p["attrs"];
        s.player.attrPoints = p.value("attrPoints", 0);
    }
    out = s;
    return true;
}

// ---- writing the data files back ----

namespace {

std::string oneLine(const ojson& j) {
    if (j.is_object()) {
        if (j.empty()) return "{}";
        std::string s = "{ ";
        bool first = true;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!first) s += ", ";
            first = false;
            s += ojson(it.key()).dump(-1, ' ', false) + ": " + oneLine(it.value());
        }
        return s + " }";
    }
    if (j.is_array()) {
        if (j.empty()) return "[]";
        std::string s = "[";
        bool first = true;
        for (const auto& v : j) {
            if (!first) s += ", ";
            first = false;
            s += oneLine(v);
        }
        return s + "]";
    }
    return j.dump(-1, ' ', false);
}

int utf8Length(const std::string& s) {   // characters, not bytes (the width is what the eye sees)
    int n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

void dumpTo(std::string& out, const ojson& j, int depth, int inlineFrom, int width) {
    const std::string pad((size_t)depth * 2, ' ');
    if (!j.is_structured() || j.empty()) { out += oneLine(j); return; }
    if (depth >= inlineFrom) {
        const std::string one = oneLine(j);
        if (utf8Length(one) + (int)pad.size() <= width) { out += one; return; }
    }
    const std::string inner((size_t)(depth + 1) * 2, ' ');
    if (j.is_object()) {
        out += "{\n";
        bool first = true;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!first) out += ",\n";
            first = false;
            out += inner + ojson(it.key()).dump(-1, ' ', false) + ": ";
            dumpTo(out, it.value(), depth + 1, inlineFrom, width);
        }
        out += "\n" + pad + "}";
    } else {
        out += "[\n";
        bool first = true;
        for (const auto& v : j) {
            if (!first) out += ",\n";
            first = false;
            out += inner;
            dumpTo(out, v, depth + 1, inlineFrom, width);
        }
        out += "\n" + pad + "]";
    }
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::vector<std::string> splitComma(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, ',')) {
        part = trim(part);
        if (!part.empty()) out.push_back(part);
    }
    return out;
}

} // namespace

std::string dumpCompact(const ojson& j, int inlineFrom, int width) {
    std::string out;
    dumpTo(out, j, 0, inlineFrom, width);
    return out + "\n";
}

bool replaceLevelPoints(std::string& text, const ojson& rows) {
    const size_t lu = text.find("\"levelUp\"");
    if (lu == std::string::npos) return false;
    const size_t key = text.find("\"points\"", lu);
    if (key == std::string::npos) return false;
    const size_t open = text.find('[', key);
    if (open == std::string::npos) return false;
    int depth = 0;
    size_t close = std::string::npos;
    for (size_t i = open; i < text.size(); i++) {
        if (text[i] == '[') depth++;
        else if (text[i] == ']' && --depth == 0) { close = i; break; }
    }
    if (close == std::string::npos) return false;
    // the rows one per line, indented as the "points" key is
    const size_t lineStart = text.rfind('\n', key) + 1;
    const std::string pad = text.substr(lineStart, key - lineStart);
    std::string arr = "[\n";
    for (size_t i = 0; i < rows.size(); i++)
        arr += pad + "  " + oneLine(rows[i]) + (i + 1 < rows.size() ? ",\n" : "\n");
    arr += pad + "]";
    text.replace(open, close - open + 1, arr);
    return true;
}

ojson movesFromText(const std::string& text) {
    ojson out = ojson::array();
    for (const std::string& part : splitComma(text)) {
        const size_t colon = part.find(':');
        if (colon == std::string::npos) { out.push_back(part); continue; }
        ojson m = ojson::object();
        m["id"] = trim(part.substr(0, colon));
        m["weight"] = std::max(0, std::atoi(part.substr(colon + 1).c_str()));
        out.push_back(m);
    }
    return out;
}

std::string movesToText(const ojson& moves) {
    std::string s;
    if (!moves.is_array()) return s;
    for (const auto& m : moves) {
        if (!s.empty()) s += ", ";
        if (m.is_string()) s += m.get<std::string>();
        else if (m.is_object()) s += m.value("id", std::string()) + ":" + std::to_string(m.value("weight", 1));
    }
    return s;
}

ojson idsFromText(const std::string& text) {
    ojson out = ojson::array();
    for (const std::string& part : splitComma(text)) out.push_back(part);
    return out;
}

std::string idsToText(const ojson& ids) {
    std::string s;
    if (!ids.is_array()) return s;
    for (const auto& m : ids)
        if (m.is_string()) s += (s.empty() ? "" : ", ") + m.get<std::string>();
    return s;
}

} // namespace bed
