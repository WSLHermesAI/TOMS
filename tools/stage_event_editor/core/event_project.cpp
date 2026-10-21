// event_project.cpp -- see event_project.h. Ported from tools/web_editors/event_editor.html (readLiveData, fresh,
// problems, liveWrites, the talk preview) and make_data.py.
#include "event_project.h"

#include "stage_save.h"   // timeStamp

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>
#include <regex>

namespace fs = std::filesystem;

namespace stage {

const char* const kLangs[6] = {"zh_TW", "en", "zh_CN", "ja", "ko", "es"};

namespace {

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool endsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

std::vector<std::string> names(const std::string& dir, const std::string& prefix, const std::string& suffix) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::u8path(dir), ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string n = e.path().filename().u8string();
        if (startsWith(n, prefix.c_str()) && endsWith(n, suffix)) out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

const Json* member(const Json& j, const char* key) {
    if (!j.is_object()) return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}
std::string str(const Json& j, const char* key, const std::string& def = std::string()) {
    const Json* v = member(j, key);
    return v && v->is_string() ? v->get<std::string>() : def;
}
Json noUnderscore(const Json& o) {
    Json out = Json::object();
    if (o.is_object())
        for (auto& [k, v] : o.items()) if (!startsWith(k, "_")) out[k] = v;
    return out;
}
bool contains(const Json& arr, const std::string& s) {
    return arr.is_array() && std::find(arr.begin(), arr.end(), Json(s)) != arr.end();
}
bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
double num(const Json& v) {   // JS +v, with true -> 1
    if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) { try { return std::stod(v.get<std::string>()); } catch (...) { return 0; } }
    return 0;
}
bool truthy(const Json& v) {
    if (v.is_null()) return false;
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_string()) return !v.get_ref<const std::string&>().empty();
    if (v.is_number()) return v.get<double>() != 0;
    return true;
}
std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) out += (i ? sep : "") + v[i];
    return out;
}

const Json& defaultKinds() {
    static const Json k = Json::parse(R"({
  "relic":         { "name": { "zh_TW": "遺物", "en": "Relic" },          "color": "#e0b44c" },
  "whisper":       { "name": { "zh_TW": "低語", "en": "Whisper" },        "color": "#a98bff" },
  "cache":         { "name": { "zh_TW": "補給", "en": "Cache" },          "color": "#3fc2b5" },
  "trap":          { "name": { "zh_TW": "陷阱", "en": "Trap" },           "color": "#ff6b6b" },
  "rescue":        { "name": { "zh_TW": "救援", "en": "Rescue" },         "color": "#7bd86a" },
  "merchant_echo": { "name": { "zh_TW": "商人殘影", "en": "Merchant echo" }, "color": "#ff8fc7" },
  "memory_shard":  { "name": { "zh_TW": "記憶碎片", "en": "Memory shard" }, "color": "#74c6ff" }
})");
    return k;
}

// event logic in the editor's shape (logicFrom)
void logicInto(Json& out, const Json& e) {
    const Json* rq = member(e, "requires");
    Json req = Json::array(), reqRaw = nullptr;
    if (rq && rq->is_object() && rq->contains("type")) req.push_back(*rq);
    else if (rq && rq->is_object() && rq->contains("all") && (*rq)["all"].is_array()
             && std::all_of((*rq)["all"].begin(), (*rq)["all"].end(), [](const Json& x) { return x.is_object() && x.contains("type"); }))
        req = (*rq)["all"];
    else if (rq && !rq->is_null()) reqRaw = *rq;
    out["trigger"] = member(e, "trigger") ? e["trigger"] : Json{{"on", "step"}};
    out["req"] = req;
    out["reqRaw"] = reqRaw;
    out["actions"] = member(e, "actions") && e["actions"].is_array() ? e["actions"] : Json::array();
    out["next"] = member(e, "next") && e["next"].is_array() ? e["next"] : Json::array();
    out["dialogue"] = str(e, "dialogue");
    out["afterDialogue"] = str(e, "afterDialogue");
    out["topicOrder"] = member(e, "topicOrder") && e["topicOrder"].is_number_integer() ? e["topicOrder"] : Json();
    out["once"] = !(member(e, "once") && e["once"] == false);
    out["maxTimes"] = member(e, "maxTimes") && e["maxTimes"].is_number_integer() ? e["maxTimes"].get<int>() : 0;
}

// the line of a record in flags.json / counters.json (hand-aligned: each record on one line)
std::string oneLine(const Json& v) {
    if (v.is_array()) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); i++) s += (i ? ", " : "") + oneLine(v[i]);
        return s + "]";
    }
    if (v.is_object()) {
        if (v.empty()) return "{}";
        std::string s = "{ ";
        bool first = true;
        for (auto& [k, x] : v.items()) { s += (first ? "" : ", ") + Json(k).dump() + ": " + oneLine(x); first = false; }
        return s + " }";
    }
    return v.dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::string asText(const Json& obj, const std::string* before) {
    std::string s = obj.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    if (before && before->find("\r\n") != std::string::npos) {
        std::string out;
        for (char c : s) { if (c == '\n') out += '\r'; out += c; }
        return out;
    }
    return s;
}

std::string regexEscape(const std::string& s) {
    static const std::string special = R"(.^$|()[]{}*+?\)";
    std::string out;
    for (char c : s) { if (special.find(c) != std::string::npos) out += '\\'; out += c; }
    return out;
}

// flags.json / counters.json: replace the changed entries' one-line records, add new ones before the closing brace;
// "" when the file cannot be patched that way (the caller rewrites it)
std::string patchEntries(const std::string& raw, const Json& obj, const std::vector<std::pair<std::string, Json>>& changes) {
    const std::string nl = raw.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::string> lines;
    {
        std::string cur;
        for (size_t i = 0; i < raw.size(); i++) {
            if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') continue;
            if (raw[i] == '\n') { lines.push_back(cur); cur.clear(); } else cur += raw[i];
        }
        lines.push_back(cur);
    }
    auto trim = [](const std::string& s) { size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t"); return a == std::string::npos ? std::string() : s.substr(a, b - a + 1); };
    for (const auto& [k, v] : changes) {
        const std::regex re("^(\\s*)" + regexEscape(Json(k).dump()) + "\\s*:\\s*\\{.*\\}(,?)\\s*$");
        bool done = false;
        for (std::string& l : lines) {
            std::smatch m;
            if (std::regex_match(l, m, re)) { l = m[1].str() + Json(k).dump() + ": " + oneLine(v) + m[2].str(); done = true; break; }
        }
        if (done) continue;
        if (obj.contains(k)) return std::string();   // a multi-line record: rewrite the file
        int j = (int)lines.size() - 1;
        while (j >= 0 && trim(lines[(size_t)j]) != "}") j--;
        int p = j - 1;
        while (p >= 0 && trim(lines[(size_t)p]).empty()) p--;
        if (j < 0 || p < 0) return std::string();
        const std::string lp = trim(lines[(size_t)p]);
        if (!lp.empty() && lp.back() != ',' && lp.back() != '{' && lp.back() != '[') lines[(size_t)p] += ",";
        lines.insert(lines.begin() + j, "  " + Json(k).dump() + ": " + oneLine(v));
    }
    std::string out;
    for (size_t i = 0; i < lines.size(); i++) out += (i ? nl : "") + lines[i];
    Json want = obj;
    for (const auto& [k, v] : changes) want[k] = v;
    const Json got = Json::parse(out, nullptr, false);
    return !got.is_discarded() && got == want ? out : std::string();
}

}  // namespace

// ---- rules and condition rows --------------------------------------------------------------------------------------

const std::vector<std::pair<std::string, int>>& evRules() {
    static const std::vector<std::pair<std::string, int>> r = {
        {"missing_event", 0}, {"missing_text", 0}, {"orphan", 1}, {"no_kind", 1}, {"placeholder", 1}, {"missing_floor", 0},
        {"bad_unlock", 0}, {"unreachable", 1}, {"unknown_kind", 1}, {"unknown_ref", 0}, {"bad_next", 0}, {"never_fires", 1},
        {"never_active", 1}, {"logic_cycle", 1}, {"unused_var", 2}, {"untranslated", 2}, {"missing_title", 1},
        {"missing_dialogue", 0}, {"bad_dialogue", 0}};
    return r;
}

int ruleSev(const std::string& rule) {
    for (const auto& [k, s] : evRules()) if (k == rule) return s;
    return 1;
}

std::string leafRef(const Json& l) {
    const std::string t = str(l, "type");
    if (t == "varAtLeast" || t == "varEquals") return "var:" + str(l, "var");
    if (t == "counterAtLeast") return "counter:" + str(l, "counter");
    if (t == "runFlagSet") { const std::string f = str(l, "flag"); return startsWith(f, "event_") ? "event:" + f.substr(6) : "flag:" + f; }
    return std::string();
}

std::string actRef(const Json& a) {
    const std::string t = str(a, "type");
    if (t == "addVar" || t == "setVar") return "var:" + str(a, "var");
    if (t == "addCounter") return "counter:" + str(a, "counter");
    if (t == "setFlag") return "flag:" + str(a, "flag");
    if (t == "give") return "item:" + str(a, "itemId");
    return std::string();
}

std::vector<std::pair<std::string, std::string>> opsFor(const std::string& k) {
    if (k == "var") return {{">=", "≥"}, {"=", "="}};
    if (k == "flag") return {{"set", "opSet"}, {"not", "opNotSet"}};
    if (k == "event") return {{"set", "opDone"}, {"not", "opNotDone"}};
    if (k == "floor") return {{"set", "opCleared"}, {"not", "opNotCleared"}};
    if (k == "stage") return {{"set", "opOnStage"}, {"not", "opNotOnStage"}};
    if (k == "choice") return {{"=", "opChose"}};
    return {{">=", "≥"}};
}

CondRow leafToRow(const Json& l) {
    CondRow r;
    if (l.is_object() && l.contains("not") && l["not"].is_object()) {
        CondRow in = leafToRow(l["not"]);
        const std::string k = in.subj.substr(0, in.subj.find(':'));
        for (const auto& [op, lb] : opsFor(k)) { (void)lb; if (op == "not" && !in.subj.empty()) { in.op = "not"; return in; } }
        return CondRow{};
    }
    const std::string t = str(l, "type");
    auto v = [&](const char* key, const Json& def) { return l.contains(key) ? l[key] : def; };
    const auto varNum = [](const Json& x) { return Json(x.is_boolean() ? (x.get<bool>() ? 1 : 0) : (int)num(x)); };
    if (t == "varAtLeast") return {"var:" + str(l, "var"), ">=", varNum(v("value", 0))};
    if (t == "varEquals") return {"var:" + str(l, "var"), "=", varNum(v("value", 0))};
    if (t == "counterAtLeast") return {"counter:" + str(l, "counter"), ">=", v("min", 0)};
    if (t == "runFlagSet") {
        const std::string f = str(l, "flag");
        return startsWith(f, "event_") ? CondRow{"event:" + f.substr(6), "set", Json()} : CondRow{"flag:" + f, "set", Json()};
    }
    if (t == "talkCountAtLeast") return {"talks:" + str(l, "npc"), ">=", v("value", 0)};
    if (t == "statAtLeast") return {"stat:" + str(l, "stat"), ">=", v("value", 0)};
    if (t == "stageCleared") return {"floor:" + str(l, "stageId"), "set", Json()};
    if (t == "itemHeld") return {"item:" + str(l, "itemId"), ">=", v("count", 1)};
    if (t == "onStage") return {"stage:" + str(l, "stageId"), "set", Json()};
    if (t == "choiceMade") return {"choice:" + str(l, "choiceId"), "=", v("optionId", "")};
    return r;
}

Json rowToLeaf(const std::string& subj, const std::string& op, const Json& val) {
    const size_t c = subj.find(':');
    const std::string k = subj.substr(0, c), n = c == std::string::npos ? std::string() : subj.substr(c + 1);
    const int v = val.is_number() ? (int)val.get<double>() : val.is_string() ? (int)num(val) : 0;
    Json leaf;
    if (k == "var") leaf = op == "=" ? Json{{"type", "varEquals"}, {"var", n}, {"value", v}} : Json{{"type", "varAtLeast"}, {"var", n}, {"value", v}};
    else if (k == "counter") leaf = Json{{"type", "counterAtLeast"}, {"counter", n}, {"min", v}};
    else if (k == "flag") leaf = Json{{"type", "runFlagSet"}, {"flag", n}};
    else if (k == "event") leaf = Json{{"type", "runFlagSet"}, {"flag", "event_" + n}};
    else if (k == "talks") leaf = Json{{"type", "talkCountAtLeast"}, {"npc", n}, {"value", v}};
    else if (k == "stat") leaf = Json{{"type", "statAtLeast"}, {"stat", n}, {"value", v}};
    else if (k == "floor") leaf = Json{{"type", "stageCleared"}, {"stageId", n}};
    else if (k == "item") leaf = Json{{"type", "itemHeld"}, {"itemId", n}, {"count", std::max(1, v)}};
    else if (k == "stage") leaf = Json{{"type", "onStage"}, {"stageId", n}};
    else leaf = Json{{"type", "choiceMade"}, {"choiceId", n}, {"optionId", val.is_string() ? val.get<std::string>() : val.is_null() ? std::string() : val.dump()}};
    return op == "not" ? Json{{"not", leaf}} : leaf;
}

CondRows condToRows(const Json& c) {
    CondRows r;
    if (c.is_null()) return r;
    auto rowable = [](const Json& x) { return x.is_object() && !leafToRow(x).subj.empty(); };
    if (rowable(c)) { r.rows.push_back(c); return r; }
    for (const char* m : {"all", "any"})
        if (c.is_object() && c.contains(m) && c[m].is_array() && std::all_of(c[m].begin(), c[m].end(), rowable)) {
            r.mode = m;
            r.rows.assign(c[m].begin(), c[m].end());
            return r;
        }
    r.raw = true;
    return r;
}

Json rowsToCond(const std::string& mode, const std::vector<Json>& rows) {
    if (rows.empty()) return Json();
    if (rows.size() == 1) return rows[0];
    Json o = Json::object();
    o[mode] = rows;
    return o;
}

Json makeAction(const std::string& type, const std::string& target, const Json& val) {
    int n = 1;
    if (val.is_number()) n = (int)val.get<double>();
    else if (val.is_boolean()) n = val.get<bool>() ? 1 : 0;
    else if (val.is_string()) { try { n = std::stoi(val.get<std::string>()); } catch (...) { n = 1; } }
    if (type == "addVar") return Json{{"type", type}, {"var", target}, {"delta", n}};
    if (type == "setVar") return Json{{"type", type}, {"var", target}, {"value", n}};
    if (type == "addCounter") return Json{{"type", type}, {"counter", target}, {"delta", n}};
    if (type == "setFlag") return Json{{"type", type}, {"flag", target}};
    if (type == "addStat") return Json{{"type", type}, {"stat", target}, {"delta", n}};
    if (type == "take") return Json{{"type", type}, {"itemId", target}};
    return Json{{"type", "give"}, {"itemId", target}};
}

std::string actTarget(const Json& a) {
    for (const char* k : {"var", "counter", "flag", "stat", "itemId"})
        if (a.contains(k) && a[k].is_string()) return a[k].get<std::string>();
    return std::string();
}

Json actValue(const Json& a) {
    if (a.contains("delta")) return a["delta"];
    if (a.contains("value")) return a["value"];
    return Json("");
}

bool nameOk(const std::string& s, bool file) {
    if (s.empty() || s.front() == ' ' || s.back() == ' ') return false;
    for (unsigned char c : s) {
        if (c < 0x20 || std::string("\"'<>&|\\/").find((char)c) != std::string::npos) return false;
        if (file && std::string(":*?").find((char)c) != std::string::npos) return false;
    }
    return true;
}

bool idOk(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}

// ---- loading -------------------------------------------------------------------------------------------------------

bool EventProject::load(const std::string& tomsRoot, std::string& err) {
    root = tomsRoot;
    while (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    std::replace(root.begin(), root.end(), '\\', '/');
    const std::string A = root + "/assets/data/";
    Json pools = Json::array(), events = Json::array(), floors = Json::array();
    Json j;
    for (const std::string& name : names(A + "events", "pool_", ".json")) {
        if (!readJson(A + "events/" + name, j, nullptr, &err)) return false;
        const std::string pid = name.substr(0, name.size() - 5);
        pools.push_back(pid);
        if (const Json* evs = member(j, "events"); evs && evs->is_array())
            for (const Json& e : *evs) {
                const std::string id = str(e, "eventId");
                Json rec = Json::object();
                rec["id"] = id;
                rec["kind"] = str(e, "kind");
                rec["pool"] = pid;
                rec["text"] = str(e, "text");
                rec["title"] = str(e, "title", id + ".title");
                rec["desc"] = str(e, "desc", id + ".desc");
                if (e.contains("inLog")) rec["inLog"] = e["inLog"];
                logicInto(rec, e);
                events.push_back(rec);
            }
    }
    std::map<std::string, std::string> prev;   // a floor without a stairs list: up to its next, down to the floor whose next it is
    std::vector<Json> specs;
    for (const std::string& name : names(A + "story/floors", "", ".json")) {
        if (endsWith(name, ".stage.json")) continue;
        Json d;
        if (!readJson(A + "story/floors/" + name, d, nullptr, &err)) return false;
        specs.push_back(d);
        if (const std::string nx = str(d, "nextFloor"); !nx.empty()) prev[nx] = str(d, "id");
    }
    for (const Json& d : specs) {
        Json f = Json::object();
        const std::string id = str(d, "id");
        f["id"] = id;
        f["act"] = str(d, "act");
        f["role"] = str(d, "role", "normal");
        f["events"] = Json::array();
        if (const Json* evs = member(d, "events"); evs && evs->is_array())
            for (const Json& x : *evs) f["events"].push_back(x);
        const std::string nx = str(d, "nextFloor");
        f["next"] = nx.empty() ? Json() : Json(nx);
        const bool has = d.contains("stairs") && d["stairs"].is_array();
        f["derived"] = !has;
        if (has) f["stairs"] = d["stairs"];
        else {
            Json st = Json::array();
            if (!nx.empty()) st.push_back(Json{{"dir", "up"}, {"to", nx}});
            if (prev.count(id)) st.push_back(Json{{"dir", "down"}, {"to", prev[id]}});
            f["stairs"] = st;
        }
        floors.push_back(f);
    }
    Json strings;
    if (!readJson(A + "text.json", j, nullptr, &err)) return false;
    strings = member(j, "strings") ? j["strings"] : Json::object();
    std::set<std::string> known, wanted;
    for (const Json& e : events) known.insert(e["id"].get<std::string>());
    for (const Json& e : events) for (const char* k : {"text", "title", "desc"}) wanted.insert(e[k].get<std::string>());
    for (const Json& f : floors) for (const Json& x : f["events"]) if (x.is_string() && !known.count(x.get<std::string>())) wanted.insert(x.get<std::string>());
    Json npcFiles = Json::object();
    for (const std::string& n : names(A + "npcs", "", ".json"))
        if (readJson(A + "npcs/" + n, j)) npcFiles[n.substr(0, n.size() - 5)] = j;
    for (auto& [k, n] : npcFiles.items()) { (void)k; for (const char* f : {"name", "greeting"}) if (!str(n, f).empty()) wanted.insert(str(n, f)); }
    Json texts = Json::object();
    for (const std::string& k : wanted) if (strings.contains(k)) texts[k] = strings[k];
    auto opt = [&](const std::string& p) { Json o; return readJson(A + p, o) ? noUnderscore(o) : Json::object(); };
    Json varsRaw;
    Json cats = Json::array();
    if (readJson(A + "story/vars.json", varsRaw) && varsRaw.contains("_categories") && varsRaw["_categories"].is_array()) cats = varsRaw["_categories"];
    Json kinds = opt("events/kinds.json");
    Json dialogues = Json::object();
    for (const std::string& n : names(A + "dialogue", "", ".json"))
        if (readJson(A + "dialogue/" + n, j)) dialogues[n.substr(0, n.size() - 5)] = j;

    items.clear();
    const Json itemsJ = opt("items.json");   // a named value: items() of a temporary would dangle
    for (auto& [k, v] : itemsJ.items()) { (void)v; items.push_back(k); }
    std::sort(items.begin(), items.end());
    stats = {"hp", "maxhp", "atk", "def", "gold", "exp", "lv"};
    if (readJson(A + "stats.json", j) && j.contains("attributes") && j["attributes"].is_array())
        for (const Json& a : j["attributes"]) if (!str(a, "id").empty()) stats.push_back(str(a, "id"));
    sprites.clear();
    {
        std::string atlas;
        if (readText(root + "/assets/media/atlas/game.atlas", atlas)) {
            std::string line;
            size_t pos = 0, li = 0;
            while (pos <= atlas.size()) {
                const size_t e = atlas.find('\n', pos);
                line = atlas.substr(pos, (e == std::string::npos ? atlas.size() : e) - pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (li++ > 0 && !line.empty() && line[0] != ' ' && line[0] != '\t' && line.find(':') == std::string::npos && !endsWith(line, ".png"))
                    sprites.push_back(line);
                if (e == std::string::npos) break;
                pos = e + 1;
            }
            std::sort(sprites.begin(), sprites.end());
        }
    }
    // who can be talked to, and where: npc ids placed on each floor's grid (boss floors: stages/<handAuthoredStage>)
    npcPlaces.clear();
    for (const std::string& name : names(A + "story/floors", "", ".json")) {
        if (endsWith(name, ".stage.json")) continue;
        Json spec, grid;
        if (!readJson(A + "story/floors/" + name, spec)) continue;
        const std::string hand = str(spec, "handAuthoredStage");
        const std::string gp = hand.empty() ? A + "story/floors/" + name.substr(0, name.size() - 5) + ".stage.json" : A + "stages/" + hand;
        if (!readJson(gp, grid)) continue;
        std::string cells;
        if (grid.contains("tiles")) for (const Json& r : grid["tiles"]) if (r.is_string()) cells += r.get<std::string>();
        if (grid.contains("legend") && grid["legend"].is_object())
            for (auto& [ch, sem] : grid["legend"].items())
                if (sem.is_string() && startsWith(sem.get<std::string>(), "npc:") && ch.size() == 1 && cells.find(ch[0]) != std::string::npos)
                    npcPlaces[sem.get<std::string>().substr(4)].push_back(str(spec, "id"));
    }

    S = Json::object();
    S["pools"] = pools;
    S["events"] = events;
    S["vars"] = opt("story/vars.json");
    S["cats"] = cats;
    S["counters"] = opt("story/counters.json");
    S["flags"] = opt("story/flags.json");
    S["kinds"] = kinds.empty() ? defaultKinds() : kinds;
    S["floors"] = floors;
    S["texts"] = texts;
    S["npcs"] = npcFiles;
    S["dialogues"] = dialogues;
    base = S;
    undoStack.clear();
    selected.clear();
    return true;
}

// ---- edits ---------------------------------------------------------------------------------------------------------

void EventProject::edit(const std::string& label, const std::function<void()>& fn) {
    undoStack.push_back({S, selected, label});
    if (undoStack.size() > 300) undoStack.erase(undoStack.begin());
    fn();
}

bool EventProject::undo(std::string* label) {
    if (undoStack.empty()) return false;
    Step u = std::move(undoStack.back());
    undoStack.pop_back();
    S = std::move(u.s);
    selected = u.sel;
    if (label) *label = u.label;
    return true;
}

// ---- lookups -------------------------------------------------------------------------------------------------------

Json* EventProject::ev(const std::string& id) {
    for (Json& e : S["events"]) if (e["id"] == id) return &e;
    return nullptr;
}

Json* EventProject::flo(const std::string& id) {
    for (Json& f : S["floors"]) if (f["id"] == id) return &f;
    return nullptr;
}

std::vector<const Json*> EventProject::floorsOf(const std::string& id) const {
    std::vector<const Json*> out;
    for (const Json& f : S["floors"]) if (contains(f["events"], id)) out.push_back(&f);
    return out;
}

bool EventProject::isMissing(const std::string& id) const { return !ev(id) && !floorsOf(id).empty(); }

bool EventProject::isNewEvent(const std::string& id) const {
    for (const Json& e : base["events"]) if (e["id"] == id) return false;
    return true;
}
bool EventProject::isNewPool(const std::string& pool) const { return !contains(base["pools"], pool); }
bool EventProject::isNewFloor(const std::string& id) const {
    for (const Json& f : base["floors"]) if (f["id"] == id) return false;
    return true;
}

// which floors the player can reach from F01: open stairs always, locked ones once their event can be done on a reached floor
std::set<std::string> EventProject::reachable() const {
    std::set<std::string> seen, done;
    const Json* start = flo("F01");
    if (!start) { if (S["floors"].empty()) return seen; start = &S["floors"][0]; }
    seen.insert((*start)["id"].get<std::string>());
    for (bool changed = true; changed;) {
        changed = false;
        for (const Json& f : S["floors"]) {
            if (!seen.count(f["id"].get<std::string>())) continue;
            for (const Json& e : f["events"]) if (e.is_string()) done.insert(e.get<std::string>());
            for (const Json& st : f["stairs"]) {
                const std::string to = str(st, "to");
                bool open = true;   // every event of its lock can be done (W38: all of them)
                for (const std::string& lock : stairUnlockEvents(st)) if (!done.count(lock)) open = false;
                if (!to.empty() && flo(to) && !seen.count(to) && open) { seen.insert(to); changed = true; }
            }
        }
    }
    return seen;
}

Json EventProject::text(const std::string& key) const {
    const Json* t = member(S["texts"], key.c_str());
    return t && t->is_object() ? *t : Json::object();
}

bool EventProject::isPlaceholder(const std::string& key) const {
    const Json* t = member(S["texts"], key.c_str());
    if (!t || !t->is_object()) return true;
    if (t->contains("_todo") && truthy((*t)["_todo"])) return true;
    const std::string zh = str(*t, "zh_TW");
    std::string bare = key;
    if (endsWith(bare, ".text")) bare.resize(bare.size() - 5);
    return zh.empty() || zh == key || zh == bare;
}

Json EventProject::reqJson(const Json& e) const {
    if (!e["reqRaw"].is_null()) return e["reqRaw"];
    const Json& r = e["req"];
    if (r.size() == 1) return r[0];
    if (!r.empty()) return Json{{"all", r}};
    return Json();
}

bool EventProject::hasLogic(const Json& e) const {
    return e["trigger"].value("on", std::string("step")) != "step" || !e["req"].empty() || !e["reqRaw"].is_null() || !e["actions"].empty() || !e["next"].empty();
}

std::vector<std::string> EventProject::pools() const {
    std::vector<std::string> out;
    for (const Json& p : S["pools"]) out.push_back(p.get<std::string>());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> EventProject::kindsList() const {
    std::vector<std::string> out;
    for (auto& [k, v] : S["kinds"].items()) { (void)v; out.push_back(k); }
    return out;
}

std::string EventProject::kindName(const std::string& k, const std::string& lang) const {
    const Json* r = member(S["kinds"], k.c_str());
    if (!r || !r->contains("name")) return std::string();
    const Json& n = (*r)["name"];
    for (const std::string& l : {lang, std::string("en"), std::string("zh_TW")}) if (!str(n, l.c_str()).empty()) return str(n, l.c_str());
    return std::string();
}

std::string EventProject::kindColor(const std::string& k) const {
    const Json* r = member(S["kinds"], k.c_str());
    if (r && !str(*r, "color").empty()) return str(*r, "color");
    const Json* d = member(defaultKinds(), k.c_str());
    return d ? str(*d, "color") : std::string("#777777");
}

std::vector<std::string> EventProject::dialogueList() const {
    std::vector<std::string> out;
    for (auto& [k, v] : S["dialogues"].items()) { (void)v; out.push_back(k); }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Json> EventProject::dialogueActs(const std::string& id) const {
    std::vector<Json> out;
    const Json* d = member(S["dialogues"], id.c_str());
    if (!d || !d->contains("nodes")) return out;
    for (auto& [k, n] : (*d)["nodes"].items()) {
        (void)k;
        if (!n.is_object() || !n.contains("choices")) continue;
        for (const Json& c : n["choices"]) {
            if (!c.contains("actions")) continue;
            if (c["actions"].is_array()) for (const Json& a : c["actions"]) out.push_back(a);
            else if (c["actions"].is_object()) out.push_back(c["actions"]);
        }
    }
    return out;
}

std::vector<const Json*> EventProject::changers(const std::string& ref) const {
    std::vector<const Json*> out;
    for (const Json& e : S["events"]) {
        bool hit = std::any_of(e["actions"].begin(), e["actions"].end(), [&](const Json& a) { return actRef(a) == ref; });
        if (!hit && startsWith(ref, "flag:")) { const Json* f = member(S["flags"], ref.substr(5).c_str()); hit = f && str(*f, "setBy") == e["id"].get<std::string>(); }
        if (!hit && e["trigger"].value("on", std::string()) == "talk" && !e["dialogue"].get<std::string>().empty())
            for (const Json& a : dialogueActs(e["dialogue"].get<std::string>())) if (actRef(a) == ref) { hit = true; break; }
        if (hit) out.push_back(&e);
    }
    return out;
}

std::vector<const Json*> EventProject::readersOnly(const std::string& ref) const {
    std::vector<const Json*> out;
    for (const Json& e : S["events"])
        if (std::any_of(e["req"].begin(), e["req"].end(), [&](const Json& l) { return leafRef(l) == ref; })) out.push_back(&e);
    return out;
}

std::vector<const Json*> EventProject::watchers(const std::string& ref) const {
    std::vector<const Json*> out;
    if (!startsWith(ref, "var:")) return out;
    for (const Json& e : S["events"])
        if (e["trigger"].value("on", std::string()) == "varChanged" && "var:" + e["trigger"].value("var", std::string()) == ref) out.push_back(&e);
    return out;
}

std::vector<const Json*> EventProject::readers(const std::string& ref) const {
    std::vector<const Json*> out;
    for (const Json& e : S["events"]) {
        const bool r = std::any_of(e["req"].begin(), e["req"].end(), [&](const Json& l) { return leafRef(l) == ref; });
        const bool w = e["trigger"].value("on", std::string()) == "varChanged" && "var:" + e["trigger"].value("var", std::string()) == ref;
        if (r || w) out.push_back(&e);
    }
    return out;
}

std::vector<std::string> EventProject::linkedValues(const std::string& ref) const {
    std::vector<std::string> out;
    std::vector<const Json*> evs = changers(ref);
    for (const Json* e : readers(ref)) evs.push_back(e);
    for (const Json* e : evs) {
        std::vector<std::string> refs;
        for (const Json& a : (*e)["actions"]) refs.push_back(actRef(a));
        for (const Json& l : (*e)["req"]) refs.push_back(leafRef(l));
        if ((*e)["trigger"].value("on", std::string()) == "varChanged") refs.push_back("var:" + (*e)["trigger"].value("var", std::string()));
        for (const std::string& r : refs)
            if (!r.empty() && r != ref && (startsWith(r, "var:") || startsWith(r, "counter:") || startsWith(r, "flag:")) && !contains(out, r)) out.push_back(r);
    }
    return out;
}

bool EventProject::refExists(const std::string& r) const {
    const size_t c = r.find(':');
    const std::string k = r.substr(0, c), n = r.substr(c + 1);
    if (k == "var") return S["vars"].contains(n);
    if (k == "counter") return S["counters"].contains(n);
    if (k == "flag") return S["flags"].contains(n);
    if (k == "event") return ev(n) != nullptr;
    if (k == "item") return contains(items, n);
    return true;
}

std::vector<std::string> EventProject::valueRefs() const {
    std::vector<std::string> out;
    for (auto& [k, v] : S["vars"].items()) { (void)v; out.push_back("var:" + k); }
    for (auto& [k, v] : S["counters"].items()) { (void)v; out.push_back("counter:" + k); }
    for (auto& [k, v] : S["flags"].items()) { (void)v; out.push_back("flag:" + k); }
    return out;
}

Json* EventProject::recOf(const std::string& ref) {
    const size_t c = ref.find(':');
    const std::string k = ref.substr(0, c), n = ref.substr(c + 1);
    Json& o = S[k == "var" ? "vars" : k == "counter" ? "counters" : "flags"];
    auto it = o.find(n);
    return it == o.end() ? nullptr : &*it;
}

std::vector<std::string> EventProject::npcIds() const {
    std::set<std::string> ids;
    for (auto& [k, v] : S["npcs"].items()) { (void)v; ids.insert(k); }
    for (const auto& [k, v] : npcPlaces) { (void)v; ids.insert(k); }
    for (const Json& e : S["events"])
        if (e["trigger"].value("on", std::string()) == "talk" && !e["trigger"].value("npc", std::string()).empty()) ids.insert(e["trigger"]["npc"].get<std::string>());
    return std::vector<std::string>(ids.begin(), ids.end());
}

// the NPC's talk events in menu order: topicOrder first (lower first), then pool order
std::vector<Json*> EventProject::topicsOf(const std::string& npc) {
    std::vector<std::pair<size_t, Json*>> list;
    Json& evs = S["events"];
    for (size_t i = 0; i < evs.size(); i++)
        if (evs[i]["trigger"].value("on", std::string()) == "talk" && evs[i]["trigger"].value("npc", std::string()) == npc) list.push_back({i, &evs[i]});
    const std::vector<std::string> ps = pools();
    auto order = [](const Json* e) { return (*e)["topicOrder"].is_number() ? (*e)["topicOrder"].get<double>() : 1e9; };
    auto pi = [&](const Json* e) { return std::find(ps.begin(), ps.end(), (*e)["pool"].get<std::string>()) - ps.begin(); };
    std::stable_sort(list.begin(), list.end(), [&](const auto& a, const auto& b) {
        if (order(a.second) != order(b.second)) return order(a.second) < order(b.second);
        if (pi(a.second) != pi(b.second)) return pi(a.second) < pi(b.second);
        return a.first < b.first;
    });
    std::vector<Json*> out;
    for (auto& x : list) out.push_back(x.second);
    return out;
}

bool EventProject::woken(const std::string& id) const {
    for (const Json& o : S["events"]) if (contains(o["next"], id)) return true;
    return false;
}

std::string EventProject::talkFloorOf(const Json& e, bool* conditional) const {
    const CondRows r = condToRows(reqJson(e));
    if (conditional) *conditional = r.raw || (r.mode == "any" && r.rows.size() > 1);
    if (r.raw || (r.mode == "any" && r.rows.size() > 1)) return std::string();
    for (const Json& l : r.rows) if (str(l, "type") == "onStage") return str(l, "stageId");
    return std::string();
}

void EventProject::setEventReq(Json& e, const Json& c) {
    Json tmp = Json::object();
    if (!c.is_null()) tmp["requires"] = c;
    Json parsed = Json::object();
    logicInto(parsed, tmp);
    e["req"] = parsed["req"];
    e["reqRaw"] = parsed["reqRaw"];
}

void EventProject::setTalkFloor(Json& e, const std::string& floor) {
    const CondRows r = condToRows(reqJson(e));
    std::vector<Json> keep;
    for (const Json& l : r.rows) if (str(l, "type") != "onStage") keep.push_back(l);
    if (!floor.empty()) keep.push_back(Json{{"type", "onStage"}, {"stageId", floor}});
    setEventReq(e, rowsToCond(r.mode, keep));
}

std::string EventProject::guessKind(const std::string& id) const {
    static const std::regex variant("_v\\d+$");
    const std::string baseId = std::regex_replace(id, variant, "");
    if (baseId != id) if (const Json* b = ev(baseId)) return (*b)["kind"].get<std::string>();
    for (const std::string& k : kindsList()) {
        std::string w = k;
        if (startsWith(w, "memory_")) w = w.substr(7);
        if (id.find(w) != std::string::npos) return k;
    }
    return std::string();
}

std::vector<std::string> EventProject::choiceIdsOf(const std::string& evId) const {
    std::vector<std::string> out;
    const Json* e = ev(evId);
    const Json* d = e ? member(S["dialogues"], (*e)["dialogue"].get<std::string>().c_str()) : nullptr;
    if (!d || !d->contains("nodes")) return out;
    for (auto& [k, n] : (*d)["nodes"].items()) {
        (void)k;
        if (!n.contains("choices")) continue;
        for (const Json& c : n["choices"]) { const std::string id = str(c, "id"); if (!id.empty() && !contains(out, id)) out.push_back(id); }
    }
    return out;
}

// ---- problems ------------------------------------------------------------------------------------------------------

std::vector<EvProblem> EventProject::problems() const {
    std::vector<EvProblem> out;
    auto add = [&](const std::string& rule, const std::string& id, const std::string& code, std::vector<std::string> args) {
        EvProblem p;
        p.rule = rule;
        p.id = id;
        p.whereCode = code;
        p.whereArgs = std::move(args);
        out.push_back(p);
        return &out.back();
    };
    std::set<std::string> seen;
    for (const Json& f : S["floors"])
        for (const Json& x : f["events"]) {
            const std::string id = x.get<std::string>();
            if (ev(id) || seen.count(id)) continue;
            seen.insert(id);
            std::vector<std::string> fl;
            const auto fs2 = floorsOf(id);
            for (const Json* g : fs2) fl.push_back((*g)["id"].get<std::string>());
            add("missing_event", id, "", {join(fl, ", ")})->act = (*fs2[0])["act"].get<std::string>();
        }
    const std::vector<std::string> dlgs = dialogueList();
    for (const Json& e : S["events"]) {
        const std::string id = e["id"].get<std::string>(), on = e["trigger"].value("on", std::string("step"));
        const Json* tx = member(S["texts"], e["text"].get<std::string>().c_str());
        if (floorsOf(id).empty() && on == "step") add("orphan", id, "", {e["pool"].get<std::string>()});
        if (on == "talk" && !dlgs.empty())
            for (const char* k : {"dialogue", "afterDialogue"}) {
                const std::string d = e[k].get<std::string>();
                if (!d.empty() && !contains(dlgs, d)) add("missing_dialogue", id, "", {"dialogue/" + d + ".json"});
            }
        if (!tx) add("missing_text", id, "", {e["text"].get<std::string>()});
        else if (isPlaceholder(e["text"].get<std::string>())) add("placeholder", id, "", {e["text"].get<std::string>()});
        else if (str(*tx, "en").empty() || str(*tx, "en") == str(*tx, "zh_TW")) add("untranslated", id, "", {e["text"].get<std::string>()});
        const bool noTi = str(text(e["title"].get<std::string>()), "zh_TW").empty(), noDe = str(text(e["desc"].get<std::string>()), "zh_TW").empty();
        if (noTi || noDe) add("missing_title", id, "wNoTitle", {noTi && noDe ? "title / desc" : noTi ? "title" : "desc"});
        const std::string kind = e["kind"].get<std::string>();
        if (kind.empty()) add("no_kind", id, "", {e["pool"].get<std::string>()});
        else if (!S["kinds"].contains(kind)) add("unknown_kind", id, "wUnknownKind", {kind});
    }
    // event logic: every reference must exist, every event must be able to fire, and links must not loop
    std::set<std::string> linked;
    for (const Json& e : S["events"]) for (const Json& n : e["next"]) linked.insert(n.get<std::string>());
    for (const Json& e : S["events"]) {
        const std::string id = e["id"].get<std::string>(), on = e["trigger"].value("on", std::string("step"));
        std::vector<std::string> refs;
        for (const Json& l : e["req"]) if (const std::string r = leafRef(l); !r.empty()) refs.push_back(r);
        for (const Json& a : e["actions"]) if (const std::string r = actRef(a); !r.empty()) refs.push_back(r);
        if (on == "varChanged") refs.push_back("var:" + e["trigger"].value("var", std::string()));
        std::vector<std::string> uniq;
        for (const std::string& r : refs) if (!contains(uniq, r)) uniq.push_back(r);
        for (const std::string& r : uniq)
            if (!refExists(r)) add("unknown_ref", id, "wUnknown", {r.substr(0, r.find(':')), r.substr(r.find(':') + 1)});
        for (const Json& n : e["next"]) if (!ev(n.get<std::string>())) add("bad_next", id, "wBadNext", {n.get<std::string>()});
        if (on == "chain" && !linked.count(id)) add("never_fires", id, "wNoIncoming", {});
        if (on == "talk" && !npcPlaces.count(e["trigger"].value("npc", std::string()))) add("never_fires", id, "wNpcNowhere", {e["trigger"].value("npc", std::string())});
        for (const Json& l : e["req"]) {
            if (str(l, "type") != "varAtLeast") continue;
            const std::string v = str(l, "var");
            const Json* rec = member(S["vars"], v.c_str());
            if (rec && changers("var:" + v).empty() && num(rec->value("default", Json(0))) < num(l.value("value", Json(0))))
                add("never_active", id, "wNeverActive", {v, l.value("value", Json(0)).dump()});
        }
    }
    std::set<std::string> onLoop;
    for (const Json& e0 : S["events"]) {
        const std::string id0 = e0["id"].get<std::string>();
        std::vector<std::pair<std::string, std::vector<std::string>>> stack{{id0, {id0}}};
        std::set<std::string> vis;
        while (!stack.empty()) {
            auto [id, path] = stack.back();
            stack.pop_back();
            const Json* e = ev(id);
            if (!e) continue;
            for (const Json& nj : (*e)["next"]) {
                const std::string n = nj.get<std::string>();
                if (n == id0 && !onLoop.count(id0)) {
                    onLoop.insert(id0);
                    std::vector<std::string> p2 = path;
                    p2.push_back(n);
                    add("logic_cycle", id0, "", {join(p2, " → ")});
                } else if (!vis.count(n)) {
                    vis.insert(n);
                    std::vector<std::string> p2 = path;
                    p2.push_back(n);
                    stack.push_back({n, p2});
                }
            }
        }
    }
    for (auto& [v, rec] : S["vars"].items()) {
        (void)rec;
        if (changers("var:" + v).empty() && readers("var:" + v).empty()) add("unused_var", v, "wUnusedVar", {});
    }
    // floors: every stair must lead to a floor that exists, a lock must name a real event, every floor must be reachable
    for (const Json& f : S["floors"])
        for (const Json& st : f["stairs"]) {
            const std::string fid = f["id"].get<std::string>(), to = str(st, "to");
            if (!to.empty() && !flo(to)) add("missing_floor", fid, "stairWhere", {fid, str(st, "dir", "up"), to})->to = to;
            for (const std::string& lock : stairUnlockEvents(st))
                if (!ev(lock)) add("bad_unlock", fid, "badUnlockWhere", {fid, to, lock})->to = to;
        }
    const std::set<std::string> reach = reachable();
    for (const Json& f : S["floors"]) if (!reach.count(f["id"].get<std::string>())) add("unreachable", f["id"].get<std::string>(), "unreachWhere", {});
    // dialogues: a start / next naming a node the file lacks
    for (auto& [id, d] : S["dialogues"].items()) {
        const Json nodes = d.value("nodes", Json::object());
        std::vector<std::string> starts;
        if (d.contains("start") && d["start"].is_array()) { for (const Json& s : d["start"]) starts.push_back(str(s, "node")); }
        else starts.push_back(str(d, "start"));
        for (const std::string& s : starts) if (!nodes.contains(s)) add("bad_dialogue", id, "wDlgStart", {s});
        for (auto& [nid, n] : nodes.items())
            if (n.is_object() && n.contains("choices"))
                for (const Json& c : n["choices"]) {
                    const std::string nx = str(c, "next");
                    if (!nx.empty() && !nodes.contains(nx)) add("bad_dialogue", id, "wDlgNext", {nid, nx});
                }
    }
    return out;
}

// ---- event actions -------------------------------------------------------------------------------------------------

std::string EventProject::newEventId() {
    for (int n = 1;; n++) {
        const std::string id = "ev_new_event_" + std::to_string(n);
        if (!ev(id) && !S["texts"].contains(id + ".text")) return id;
    }
}

static Json emptyText() {
    Json t = Json::object();
    for (const char* l : kLangs) t[l] = "";
    return t;
}

Json EventProject::blankEvent(const std::string& id, const std::string& pool) const {
    Json e = Json::object();
    e["id"] = id;
    e["kind"] = "";
    e["pool"] = pool;
    e["text"] = id + ".text";
    e["title"] = id + ".title";
    e["desc"] = id + ".desc";
    logicInto(e, Json::object());
    return e;
}

// a missing id becomes a real event: ev_common_* variants go to pool_common, the rest to their chapter's pool. The text
// key is the placeholder string that already exists under the bare id, so text.json needs no new key.
void EventProject::makeRecord(const std::string& id, const std::string& act) {
    const std::string pool = startsWith(id, "ev_common_") ? "pool_common" : "pool_act" + act.substr(std::min<size_t>(3, act.size()));
    if (!contains(S["pools"], pool)) S["pools"].push_back(pool);
    const std::string textKey = S["texts"].contains(id) ? id : id + ".text";
    if (!S["texts"].contains(textKey)) S["texts"][textKey] = emptyText();
    for (const char* f : {".title", ".desc"}) if (!S["texts"].contains(id + f)) S["texts"][id + f] = emptyText();
    Json e = blankEvent(id, pool);
    e["kind"] = guessKind(id);
    e["text"] = textKey;
    S["events"].push_back(e);
}

void EventProject::renameEvent(Json& e, const std::string& nu) {
    const std::string old = e["id"].get<std::string>();
    for (Json& f : S["floors"]) for (Json& x : f["events"]) if (x == old) x = nu;
    for (Json& o : S["events"]) {
        for (Json& n : o["next"]) if (n == old) n = nu;
        for (Json& l : o["req"]) if (str(l, "type") == "runFlagSet" && str(l, "flag") == "event_" + old) l["flag"] = "event_" + nu;
    }
    Json tx = S["texts"].contains(e["text"].get<std::string>()) ? S["texts"][e["text"].get<std::string>()] : Json::object();
    S["texts"].erase(e["text"].get<std::string>());
    e["id"] = nu;
    e["text"] = nu + ".text";
    S["texts"][nu + ".text"] = tx;
    for (const char* f : {"title", "desc"}) {   // keys that follow the id move with it
        if (e[f] != old + "." + f) continue;
        const std::string ok = e[f].get<std::string>();
        Json v = S["texts"].contains(ok) ? S["texts"][ok] : Json();
        S["texts"].erase(ok);
        e[f] = nu + "." + f;
        if (!v.is_null()) S["texts"][nu + "." + f] = v;
    }
    selected = nu;
}

std::string EventProject::idError(const std::string& id, const Json* self) const {
    if (!nameOk(id)) return "format";
    for (const Json& e : S["events"]) if (e["id"] == id && &e != self) return "dup:" + e["pool"].get<std::string>();
    return std::string();
}

// ---- save ----------------------------------------------------------------------------------------------------------

static Json poolRec(const EventProject& p, const Json& e) {
    Json o = Json::object();
    o["eventId"] = e["id"];
    o["kind"] = e["kind"];
    o["text"] = e["text"];
    o["title"] = e["title"];
    o["desc"] = e["desc"];
    if (e.contains("inLog")) o["inLog"] = e["inLog"];
    if (e["trigger"].value("on", std::string("step")) != "step") o["trigger"] = e["trigger"];
    const Json rq = p.reqJson(e);
    if (!rq.is_null()) o["requires"] = rq;
    if (!e["actions"].empty()) o["actions"] = e["actions"];
    if (!e["next"].empty()) o["next"] = e["next"];
    if (e["once"] == false) { o["once"] = false; if (e["maxTimes"].get<int>() > 0) o["maxTimes"] = e["maxTimes"]; }
    if (e["trigger"].value("on", std::string()) == "talk") {
        if (!e["dialogue"].get<std::string>().empty()) o["dialogue"] = e["dialogue"];
        if (!e["afterDialogue"].get<std::string>().empty()) o["afterDialogue"] = e["afterDialogue"];
        if (e["topicOrder"].is_number_integer()) o["topicOrder"] = e["topicOrder"];
    }
    return o;
}

std::vector<EventProject::FileWrite> EventProject::liveWrites(const std::map<std::string, std::string>& comments) const {
    const std::string A = "assets/data/";
    std::vector<FileWrite> out;
    auto put = [&](const std::string& path, const std::string* before, const std::string& text) {
        if (before && *before == text) return;
        out.push_back({path, before ? *before : std::string(), text, before != nullptr});
    };
    auto read = [&](const std::string& path, std::string& s) { return readText(root + "/" + path, s); };
    // pools
    std::set<std::string> allPools;
    for (const Json& p : base["pools"]) allPools.insert(p.get<std::string>());
    for (const Json& p : S["pools"]) allPools.insert(p.get<std::string>());
    for (const std::string& pid : allPools) {
        std::vector<const Json*> bEv, sEv;
        for (const Json& e : base["events"]) if (e["pool"] == pid) bEv.push_back(&e);
        for (const Json& e : S["events"]) if (e["pool"] == pid) sEv.push_back(&e);
        Json a = Json::array(), b = Json::array();
        for (const Json* e : bEv) a.push_back(poolRec(*this, *e));
        for (const Json* e : sEv) b.push_back(poolRec(*this, *e));
        if (a == b && contains(base["pools"], pid)) continue;
        const std::string path = A + "events/" + pid + ".json";
        std::string before;
        const bool had = read(path, before);
        Json obj = had ? Json::parse(before, nullptr, false) : Json{{"events", Json::array()}};
        if (obj.is_discarded()) obj = Json{{"events", Json::array()}};
        Json disk = obj.value("events", Json::array());
        std::set<std::string> mine;
        for (const Json* e : bEv) mine.insert((*e)["id"].get<std::string>());
        for (const Json* e : sEv) mine.insert((*e)["id"].get<std::string>());
        Json evs = Json::array();
        for (const Json* e : sEv) {
            Json r = Json::object();
            for (const Json& d : disk) if (d.value("eventId", std::string()) == (*e)["id"].get<std::string>()) { r = d; break; }
            for (const char* k : {"trigger", "requires", "actions", "next", "inLog", "dialogue", "afterDialogue", "topicOrder", "once", "maxTimes"}) r.erase(k);
            const Json rec = poolRec(*this, *e);
            for (auto& [k, v] : rec.items()) r[k] = v;   // the editor's fields win; a removed inLog goes
            evs.push_back(r);
        }
        for (const Json& d : disk) if (!mine.count(d.value("eventId", std::string()))) evs.push_back(d);   // added by someone else since
        obj["events"] = evs;
        put(path, had ? &before : nullptr, asText(obj, had ? &before : nullptr));
    }
    // floors
    for (const Json& f : S["floors"]) {
        const std::string id = f["id"].get<std::string>();
        const Json* bf = nullptr;
        for (const Json& x : base["floors"]) if (x["id"] == id) bf = &x;
        const bool stairsChanged = !f["derived"].get<bool>() && (!bf || (*bf)["derived"].get<bool>() || (*bf)["stairs"] != f["stairs"]);
        if (bf && (*bf)["events"] == f["events"] && (*bf)["act"] == f["act"] && !stairsChanged) continue;
        const std::string path = A + "story/floors/" + id + ".json";
        std::string before;
        const bool had = read(path, before);
        Json obj = had ? Json::parse(before, nullptr, false) : Json();
        if (!had || obj.is_discarded()) obj = Json{{"id", id}, {"act", f["act"]}, {"role", f["role"]}, {"events", Json::array()}};
        if (!bf || !had) obj["events"] = f["events"];
        else if ((*bf)["events"] != f["events"]) {
            const Json diskEv = obj.value("events", Json::array());
            std::vector<std::string> add, del;
            for (const Json& x : f["events"]) if (!contains((*bf)["events"], x.get<std::string>())) add.push_back(x.get<std::string>());
            for (const Json& x : (*bf)["events"]) if (!contains(f["events"], x.get<std::string>())) del.push_back(x.get<std::string>());
            if (diskEv == (*bf)["events"]) obj["events"] = f["events"];
            else {
                Json m = Json::array();
                for (const Json& x : diskEv) if (!x.is_string() || !contains(del, x.get<std::string>())) m.push_back(x);
                for (const std::string& x : add) if (!contains(diskEv, x)) m.push_back(x);
                obj["events"] = m;
            }
        }
        if (!bf || (*bf)["act"] != f["act"]) obj["act"] = f["act"];
        if (stairsChanged) obj["stairs"] = f["stairs"];
        put(path, had ? &before : nullptr, asText(obj, had ? &before : nullptr));
    }
    // text.json
    {
        std::set<std::string> keys;
        for (auto& [k, v] : base["texts"].items()) { (void)v; keys.insert(k); }
        for (auto& [k, v] : S["texts"].items()) { (void)v; keys.insert(k); }
        std::vector<std::string> changed;
        for (const std::string& k : keys) {
            const Json a = base["texts"].contains(k) ? base["texts"][k] : Json(), b = S["texts"].contains(k) ? S["texts"][k] : Json();
            if (a != b) changed.push_back(k);
        }
        if (!changed.empty()) {
            const std::string path = A + "text.json";
            std::string before;
            if (read(path, before)) {
                Json obj = Json::parse(before, nullptr, false);
                if (!obj.is_discarded()) {
                    for (const std::string& k : changed) {
                        if (S["texts"].contains(k) && !S["texts"][k].is_null()) obj["strings"][k] = S["texts"][k];
                        else obj["strings"].erase(k);
                    }
                    put(path, &before, asText(obj, &before));
                }
            }
        }
    }
    // vars.json, kinds.json: a head of "_" keys, then the records
    for (const auto& [key, file] : std::vector<std::pair<std::string, std::string>>{{"vars", "story/vars.json"}, {"kinds", "events/kinds.json"}}) {
        const bool catsChanged = key == "vars" && base["cats"] != S["cats"];
        if (base[key] == S[key] && !catsChanged) continue;
        const std::string path = A + file;
        std::string before;
        const bool had = read(path, before);
        Json old = had ? Json::parse(before, nullptr, false) : Json::object();
        if (old.is_discarded()) old = Json::object();
        Json o = Json::object();
        if (had) { for (auto& [k, v] : old.items()) if (startsWith(k, "_")) o[k] = v; }
        else { auto c = comments.find(key); o["_comment"] = Json::array({c != comments.end() ? c->second : std::string()}); }
        if (key == "vars") { if (!S["cats"].empty()) o["_categories"] = S["cats"]; else o.erase("_categories"); }
        for (auto& [k, v] : S[key].items()) o[k] = v;
        put(path, had ? &before : nullptr, asText(o, had ? &before : nullptr));
    }
    // NPC and dialogue files: whole files
    for (const auto& [key, folder] : std::vector<std::pair<std::string, std::string>>{{"npcs", "npcs"}, {"dialogues", "dialogue"}})
        for (auto& [id, v] : S[key].items()) {
            if (base[key].contains(id) && base[key][id] == v) continue;
            const std::string path = A + folder + "/" + id + ".json";
            std::string before;
            const bool had = read(path, before);
            put(path, had ? &before : nullptr, asText(v, had ? &before : nullptr));
        }
    // flags.json, counters.json: hand-aligned, so only the changed entries' lines are replaced
    for (const std::string key : {"flags", "counters"}) {
        std::vector<std::pair<std::string, Json>> changes;
        for (auto& [k, v] : S[key].items()) if (!base[key].contains(k) || base[key][k] != v) changes.push_back({k, v});
        if (changes.empty()) continue;
        const std::string path = A + "story/" + key + ".json";
        std::string before;
        if (!read(path, before)) continue;
        Json obj = Json::parse(before, nullptr, false);
        if (obj.is_discarded()) continue;
        std::string text = patchEntries(before, obj, changes);
        if (text.empty()) { for (auto& [k, v] : changes) obj[k] = v; text = asText(obj, &before); }
        put(path, &before, text);
    }
    return out;
}

bool EventProject::writeAll(const std::vector<FileWrite>& files, const std::string& stamp, std::string& err) {
    for (const FileWrite& f : files)
        if (f.existed && !writeText(root + "/Build/editor_backups/" + stamp + "/" + f.path, f.before)) {
            err = "Build/editor_backups/" + stamp + "/" + f.path + ": cannot write the backup";
            return false;
        }
    for (const FileWrite& f : files)
        if (!writeText(root + "/" + f.path, f.text)) { err = f.path + ": cannot write"; return false; }
    base = S;
    return true;
}

// ---- the talk preview ----------------------------------------------------------------------------------------------

void TalkPreview::reset(EventProject& p, const std::string& id) {
    *this = TalkPreview();
    npc = id;
    for (auto& [k, v] : p.S["vars"].items()) vars[k] = num(v.value("default", Json(0)));
    stats = {{"hp", 100}, {"atk", 10}, {"def", 5}, {"gold", 0}, {"lv", 1}};
    auto it = p.npcPlaces.find(id);
    stage = it != p.npcPlaces.end() && !it->second.empty() ? it->second[0] : std::string();
}

bool TalkPreview::eval(const Json& c) const {
    if (c.is_null()) return true;
    if (c.contains("all") && c["all"].is_array()) return std::all_of(c["all"].begin(), c["all"].end(), [&](const Json& x) { return eval(x); });
    if (c.contains("any") && c["any"].is_array()) return std::any_of(c["any"].begin(), c["any"].end(), [&](const Json& x) { return eval(x); });
    if (c.contains("not")) return !eval(c["not"]);
    const std::string t = str(c, "type");
    auto get = [](const std::map<std::string, double>& m, const std::string& k) { auto it = m.find(k); return it == m.end() ? 0.0 : it->second; };
    if (t == "runFlagSet") { const std::string f = str(c, "flag"); return startsWith(f, "event_") && finished.count(f.substr(6)); }
    if (t == "varAtLeast") return get(vars, str(c, "var")) >= num(c.value("value", Json(0)));
    if (t == "varEquals") return get(vars, str(c, "var")) == num(c.value("value", Json(0)));
    if (t == "counterAtLeast") return get(counters, str(c, "counter")) >= num(c.value("min", Json(0)));
    if (t == "talkCountAtLeast") { auto it = talks.find(str(c, "npc")); return (it == talks.end() ? 0 : it->second.count) >= num(c.value("value", Json(0))); }
    if (t == "stageCleared") return cleared.count(str(c, "stageId")) > 0;
    if (t == "statAtLeast") return get(stats, str(c, "stat")) >= num(c.value("value", Json(0)));
    if (t == "always") return true;
    if (t == "onStage") return stage == str(c, "stageId");
    return false;   // as the game: an unknown leaf (or one the preview cannot know, e.g. itemHeld) fails
}

std::vector<TalkPreview::MenuItem> TalkPreview::menu(EventProject& p, const std::string& id) {
    std::vector<MenuItem> open, done;
    for (Json* e : p.topicsOf(id)) {
        const std::string eid = (*e)["id"].get<std::string>();
        const int cnt = std::max(counts.count(eid) ? counts[eid] : 0, finished.count(eid) ? 1 : 0);
        const bool left = (*e)["once"] == false && ((*e)["maxTimes"].get<int>() == 0 || cnt < (*e)["maxTimes"].get<int>());
        if (finished.count(eid) && !left) { if (!(*e)["afterDialogue"].get<std::string>().empty()) done.push_back({eid, true}); }
        else if ((!p.woken(eid) || started.count(eid) || finished.count(eid)) && eval(p.reqJson(*e))) open.push_back({eid, false});
    }
    open.insert(open.end(), done.begin(), done.end());
    return open;
}

int TalkPreview::chat(const Json& f) {
    const std::string id = str(f, "npcId");
    TalkState& st = talks[id];
    std::vector<Json> lines;
    if (f.contains("chat") && f["chat"].contains("lines"))
        for (const Json& l : f["chat"]["lines"]) lines.push_back(l.is_string() ? Json{{"dialogue", l}} : l);
    std::vector<int> avail;
    for (size_t i = 0; i < lines.size(); i++) if (eval(lines[i].value("requires", Json()))) avail.push_back((int)i);
    if (avail.empty()) return -1;
    const std::string order = f.contains("chat") ? str(f["chat"], "order", "loop") : std::string("loop");
    const int n = (int)avail.size();
    if (n == 1) { if (order == "loop") st.index++; return avail[0]; }
    if (order == "random") {
        static std::mt19937 rng{std::random_device{}()};
        int p = (int)(rng() % (unsigned)n);
        if (avail[(size_t)p] == st.last) p = (p + 1) % n;
        st.last = avail[(size_t)p];
        return avail[(size_t)p];
    }
    const int at = order == "stay" ? std::min(st.index, n - 1) : st.index;
    const int line = avail[(size_t)(at % n)];
    if (order == "loop" || st.index < n - 1) st.index++;
    return line;
}

std::string TalkPreview::firstText(const EventProject& p, const std::string& dlg, const std::string& lang, bool* missing) const {
    const Json* d = member(p.S["dialogues"], dlg.c_str());
    if (missing) *missing = !d;
    if (!d) return std::string();
    std::string start;
    if (d->contains("start") && (*d)["start"].is_array()) {
        for (const Json& s : (*d)["start"]) if (eval(s.value("requires", Json()))) { start = str(s, "node"); break; }
    } else start = str(*d, "start");
    const Json* node = d->contains("nodes") ? member((*d)["nodes"], start.c_str()) : nullptr;
    if (!node) return "?";
    const Json t = node->value("text", Json());
    if (t.is_string()) return t.get<std::string>();
    for (const std::string& l : {lang, std::string("en")}) if (!str(t, l.c_str()).empty()) return str(t, l.c_str());
    return std::string();
}

void TalkPreview::talk(EventProject& p, const std::string& id, const std::string& lang) {
    Json f = p.S["npcs"].contains(id) ? p.S["npcs"][id] : Json{{"npcId", id}, {"chat", Json{{"lines", Json::array({Json{{"dialogue", id}}})}}}};
    if (!f.contains("npcId")) f["npcId"] = id;
    TalkState& st = talks[id];
    const std::vector<MenuItem> m = menu(p, id);
    int open = 0;
    for (const MenuItem& x : m) if (!x.finished) open++;
    Entry e;
    e.n = st.count + 1;
    const bool always = f.contains("topics") && str(f["topics"], "menu") == "always";
    if (m.empty()) {
        const int l = chat(f);
        e.kind = "chat";
        if (l >= 0) {
            const Json& line = f["chat"]["lines"][(size_t)l];
            e.dialogue = line.is_string() ? line.get<std::string>() : str(line, "dialogue");
            bool miss = false;
            e.text = firstText(p, e.dialogue, lang, &miss);
            if (miss) e.text = "\x01missing";
        }
    } else if (m.size() == 1 && open == 1 && !always) { e.kind = "topic"; e.menu = m; }
    else { e.kind = "menu"; e.menu = m; }
    talks[id].count++;
    log.insert(log.begin(), e);
}

void TalkPreview::finish(EventProject& p, const std::string& id) {
    const Json* e = p.ev(id);
    if (!e) return;
    const bool again = finished.count(id) > 0;
    counts[id] = std::max(counts.count(id) ? counts[id] : 0, again ? 1 : 0) + 1;
    finished.insert(id);
    started.insert(id);
    if (!again) for (const Json& x : (*e)["next"]) started.insert(x.get<std::string>());
    for (const Json& a : (*e)["actions"]) {
        const std::string t = str(a, "type");
        if (t == "setVar") vars[str(a, "var")] = num(a.value("value", Json(0)));
        if (t == "addVar") vars[str(a, "var")] += num(a.value("delta", Json(0)));
        if (t == "addCounter") counters[str(a, "counter")] += num(a.value("delta", Json(0)));
    }
}

void TalkPreview::tick(EventProject& p, const std::string& id, bool on) {
    if (!on) { finished.erase(id); return; }
    finished.insert(id);
    started.insert(id);
    if (const Json* e = p.ev(id)) for (const Json& x : (*e)["next"]) started.insert(x.get<std::string>());
}

}  // namespace stage
