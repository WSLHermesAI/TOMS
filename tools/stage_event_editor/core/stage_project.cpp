// stage_project.cpp -- see stage_project.h. Ported from tools/web_editors/stage_editor.html and make_bundle.py;
// the two must keep the same rules (a floor saved by either editor reads the same in the other).
#include "stage_project.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace stage {

namespace {

const char* const kFreeChars = "EFGIJKLMNOPQSTVWXefgijlnoqtuwxz7890ACM$%&*+=?!^~<>";

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool endsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

std::pair<std::string, std::string> splitSem(const std::string& sem) {
    const size_t c = sem.find(':');
    return c == std::string::npos ? std::make_pair(sem, std::string()) : std::make_pair(sem.substr(0, c), sem.substr(c + 1));
}

// file names in a folder matching a prefix / suffix, sorted (as the HTML editors' sort())
std::vector<std::string> listFiles(const std::string& dir, const std::string& prefix, const std::string& suffix) {
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

std::string cellKey(int x, int y) { return std::to_string(x) + "," + std::to_string(y); }

const std::map<std::string, std::pair<const char*, const char*>>& builtinNpcNames() {
    static const std::map<std::string, std::pair<const char*, const char*>> m = {
        {"sorcerer", {"Sorcerer", "法師"}}, {"villager", {"Villager", "村民"}}, {"king", {"King", "國王"}},
        {"princess", {"Princess", "公主"}}, {"handmaiden", {"Handmaiden", "侍女"}}, {"skeleton_scholar", {"Skeleton scholar", "骷髏學者"}}};
    return m;
}

}  // namespace

const std::map<std::string, char>& reservedChars() {
    static const std::map<std::string, char> m = {
        {"door:yellow", 'y'}, {"door:blue", 'b'}, {"door:red", 'r'}, {"stairs_up", 'U'}, {"stairs_down", 'D'}};
    return m;
}

const std::map<std::string, char>& preferredChars() {
    static const std::map<std::string, char> m = {
        {"wall", '#'}, {"floor", '.'}, {"player_start", '@'}, {"stairs_up", 'U'}, {"stairs_down", 'D'},
        {"door:yellow", 'y'}, {"door:blue", 'b'}, {"door:red", 'r'}, {"key:yellow", 'Y'}, {"key:blue", 'B'}, {"key:red", 'R'},
        {"npc:sorcerer", 's'}, {"npc:villager", 'v'}, {"npc:king", 'k'}, {"npc:princess", 'p'}, {"npc:handmaiden", 'm'},
        {"monster:slime", '1'}, {"monster:bat", '2'}, {"monster:golem", '3'}, {"monster:skeleton", '4'}, {"monster:wraith", '5'},
        {"monster:demon", '6'}, {"monster:demonlord_vorkath", 'Z'}, {"item:gem_atk", 'a'}, {"item:gem_def", 'd'},
        {"item:potion_red", 'h'}, {"item:potion_blue", 'H'}, {"item:coin", 'c'}};
    return m;
}

// ---- files ---------------------------------------------------------------------------------------------------------

std::string fmtJson(const Json& j, const Eol& eol, int indent) {
    std::string s = j.dump(indent, ' ', false, Json::error_handler_t::replace);
    if (eol.finalNewline) s += '\n';
    if (!eol.crlf) return s;
    std::string out;
    out.reserve(s.size() + s.size() / 16);
    for (char c : s) {
        if (c == '\n') out += '\r';
        out += c;
    }
    return out;
}

bool readText(const std::string& path, std::string& out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeText(const std::string& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(fs::u8path(path).parent_path(), ec);
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), (std::streamsize)text.size());
    return (bool)f;
}

bool readJson(const std::string& path, Json& out, Eol* eol, std::string* err) {
    std::string raw;
    if (!readText(path, raw)) { if (err) *err = path + ": cannot read"; return false; }
    out = Json::parse(raw, nullptr, false);
    if (out.is_discarded()) { if (err) *err = path + ": not valid JSON"; return false; }
    if (eol) {
        eol->crlf = raw.find("\r\n") != std::string::npos;
        eol->finalNewline = !raw.empty() && raw.back() == '\n';
    }
    return true;
}

// ---- loading -------------------------------------------------------------------------------------------------------

bool Project::load(const std::string& tomsRoot, std::string& err) {
    root = tomsRoot;
    while (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    std::replace(root.begin(), root.end(), '\\', '/');
    const std::string data = root + "/assets/data/";
    floors.clear();
    docs_.clear();
    for (const std::string& n : listFiles(data + "story/floors", "", ".json")) {
        if (endsWith(n, ".stage.json")) continue;
        Floor f;
        f.specPath = "assets/data/story/floors/" + n;
        if (!readJson(abs(f.specPath), f.spec, &f.specEol, &err)) return false;
        const std::string hand = str(f.spec, "handAuthoredStage");
        f.stagePath = hand.empty() ? f.specPath.substr(0, f.specPath.size() - 5) + ".stage.json" : "assets/data/stages/" + hand;
        if (!fs::exists(fs::u8path(abs(f.stagePath)))) continue;
        if (!readJson(abs(f.stagePath), f.stage, &f.stageEol, &err)) return false;
        f.id = str(f.spec, "id", n.substr(0, n.size() - 5));
        const Json* tiles = member(f.stage, "tiles");
        if (!tiles || !tiles->is_array() || tiles->empty()) { err = f.stagePath + ": no tiles"; return false; }
        for (const Json& row : *tiles)
            if (!row.is_string() || std::any_of(row.get_ref<const std::string&>().begin(), row.get_ref<const std::string&>().end(),
                                                [](char c) { return (unsigned char)c >= 0x80; })) {
                err = f.stagePath + ": a tiles row is not plain ASCII";
                return false;
            }
        floors.push_back(std::move(f));
    }
    if (floors.empty()) { err = root + ": no floors in assets/data/story/floors (pick the TOMS folder)"; return false; }

    Json j;
    footprints = Json::object();
    if (readJson(data + "footprints.json", j))
        for (auto& [k, v] : j.items()) if (!startsWith(k, "_")) footprints[k] = v;
    kinds = Json::object();
    if (readJson(data + "events/kinds.json", j))
        for (auto& [k, v] : j.items()) if (!startsWith(k, "_")) kinds[k] = v;
    sprites.clear();
    if (readJson(root + "/assets/media/sprites/manifest.json", j) && member(j, "sprites"))
        for (const Json& s : j["sprites"]) if (s.is_string()) sprites.push_back(s.get<std::string>());

    // the palette's type records, whole, for the detail windows (written back by Save)
    dataFiles.clear();
    dataOrig.clear();
    std::vector<std::string> typePaths = {enemiesPath(), itemsPath(), palettePath()};   // (the palette file may be absent)
    for (const std::string& n : listFiles(data + "npcs", "", ".json")) typePaths.push_back("assets/data/npcs/" + n);
    for (const std::string& p : typePaths) {
        DataFile df;
        if (!readJson(abs(p), df.obj, &df.eol)) continue;
        dataOrig[p] = fmtJson(df.obj, df.eol);
        dataFiles[p] = std::move(df);
    }
    // each .anim file's clip names (a type's or an object's animation: "anim/<file>.anim#<clip>")
    dataVer++;
    anims.clear();
    for (const std::string& n : listFiles(root + "/assets/media/anim", "", ".anim")) {
        std::vector<std::string> clips;
        if (readJson(root + "/assets/media/anim/" + n, j) && member(j, "clips"))
            for (const Json& c : j["clips"]) { const std::string nm = str(c, "name"); if (!nm.empty()) clips.push_back(nm); }
        anims.emplace_back("anim/" + n, std::move(clips));
    }
    return reloadEvents(err);
}

bool Project::reloadEvents(std::string& err) {
    const std::string data = root + "/assets/data/";
    Json j;
    std::vector<Event> fresh;
    for (const std::string& n : listFiles(data + "events", "pool_", ".json")) {
        if (!readJson(data + "events/" + n, j, nullptr, &err)) return false;
        const Json* list = member(j, "events");
        if (!list || !list->is_array()) continue;
        for (const Json& e : *list) {
            Event ev;
            ev.id = str(e, "eventId");
            ev.kind = str(e, "kind");
            ev.pool = n.substr(0, n.size() - 5);
            ev.text = str(e, "text");
            ev.title = str(e, "title", ev.id + ".title");
            ev.rec = Json::object();
            for (const char* k : {"trigger", "requires", "actions", "next", "dialogue", "afterDialogue", "topicOrder", "once", "maxTimes"})
                if (const Json* v = member(e, k)) ev.rec[k] = *v;
            fresh.push_back(std::move(ev));
        }
    }
    for (const Event& p : events)   // place mode's not-yet-saved events stay until a pool defines them
        if (p.pending && std::none_of(fresh.begin(), fresh.end(), [&](const Event& e) { return e.id == p.id; })) fresh.push_back(p);
    events = std::move(fresh);
    indexEvents();

    if (!readJson(data + "text.json", j, nullptr, &err)) return false;
    strings = member(j, "strings") ? j["strings"] : Json::object();

    npcs.clear();
    for (const std::string& n : listFiles(data + "npcs", "", ".json"))
        if (readJson(data + "npcs/" + n, j)) npcs[n.substr(0, n.size() - 5)] = j;
    // what a talk event can change: every action its dialogue's choices run (the story check's variable reach)
    dialogueActions.clear();
    for (const std::string& n : listFiles(data + "dialogue", "", ".json")) {
        if (!readJson(data + "dialogue/" + n, j)) continue;
        std::vector<Json> acts;
        if (const Json* nodes = member(j, "nodes"))
            for (auto& [k, node] : nodes->items()) {
                (void)k;
                const Json* choices = member(node, "choices");
                if (!choices || !choices->is_array()) continue;
                for (const Json& c : *choices) {
                    const Json* a = member(c, "actions");
                    if (!a) continue;
                    if (a->is_array()) for (const Json& x : *a) acts.push_back(x);
                    else if (a->is_object()) acts.push_back(*a);
                }
            }
        if (!acts.empty()) dialogueActions[n.substr(0, n.size() - 5)] = std::move(acts);
    }
    // NPC files the palette edits: the ones on disk now, unless changed here (Save would write them)
    for (const auto& [id, obj] : npcs) {
        const std::string p = npcPath(id);
        auto it = dataFiles.find(p);
        if (it == dataFiles.end()) {
            DataFile df;
            readJson(abs(p), df.obj, &df.eol);
            dataOrig[p] = fmtJson(df.obj, df.eol);
            dataFiles[p] = std::move(df);
        } else if (!it->second.deleted && fmtJson(it->second.obj, it->second.eol) == dataOrig[p]) {
            readJson(abs(p), it->second.obj, &it->second.eol);
            dataOrig[p] = fmtJson(it->second.obj, it->second.eol);
        }
    }
    storyVer++;
    dataVer++;
    return true;
}

int Project::refreshCleanFloors() {
    int n = 0;
    for (Floor& f : floors) {
        if (isDirty(f.id)) continue;
        Floor fresh = f;
        if (!readJson(abs(f.specPath), fresh.spec, &fresh.specEol) || !readJson(abs(f.stagePath), fresh.stage, &fresh.stageEol)) continue;
        if (fresh.spec == f.spec && fresh.stage == f.stage && fresh.specEol.crlf == f.specEol.crlf) continue;
        f = std::move(fresh);
        docs_.erase(f.id);   // rebuilt from the new files on next use
        n++;
    }
    if (n) storyVer++;
    return n;
}

void Project::indexEvents() {
    evIndex_.clear();
    for (size_t i = 0; i < events.size(); i++) evIndex_.emplace(events[i].id, i);
}

const Floor* Project::floor(const std::string& id) const {
    for (const Floor& f : floors) if (f.id == id) return &f;
    return nullptr;
}

int Project::floorIndex(const std::string& id) const {
    for (size_t i = 0; i < floors.size(); i++) if (floors[i].id == id) return (int)i;
    return -1;
}

StageDoc& Project::doc(const std::string& fid) {
    auto it = docs_.find(fid);
    if (it != docs_.end()) return *it->second;
    auto d = std::make_unique<StageDoc>();
    const Floor* f = floor(fid);
    d->f = f;
    d->st = f->stage;
    d->spec = f->spec;
    for (const Json& row : f->stage.at("tiles")) d->grid.push_back(row.get<std::string>());
    d->origStage = fmtJson(f->stage, f->stageEol);
    d->origSpec = fmtJson(f->spec, f->specEol);
    return *docs_.emplace(fid, std::move(d)).first->second;
}

const Event* Project::event(const std::string& id) const {
    auto it = evIndex_.find(id);
    return it == evIndex_.end() ? nullptr : &events[it->second];
}

Event& Project::addPendingEvent(const std::string& id, const std::string& kind) {
    Event e;
    e.id = id;
    e.kind = kind;
    e.pool = "(event editor)";
    e.text = id + ".text";
    e.title = id + ".title";
    e.rec = Json::object();
    e.pending = true;
    events.push_back(std::move(e));
    indexEvents();
    return events.back();
}

std::vector<std::string> Project::pools() const {
    std::vector<std::string> out;
    for (const Event& e : events) if (std::find(out.begin(), out.end(), e.pool) == out.end()) out.push_back(e.pool);
    return out;
}

// ---- text and names --------------------------------------------------------------------------------------------------

std::string Project::localized(const Json& o) const {
    if (o.is_string()) {
        const Json t = textOf(o.get<std::string>());
        return t.empty() ? o.get<std::string>() : localized(t);
    }
    if (!o.is_object()) return std::string();
    for (const char* l : {lang.c_str(), "en", "zh_TW"}) {
        const std::string s = str(o, l);
        if (!s.empty()) return s;
    }
    return std::string();
}

Json Project::textOf(const std::string& key) const {
    auto it = textEdits.find(key);
    if (it != textEdits.end()) return it->second;
    const Json* v = member(strings, key.c_str());
    return v && v->is_object() ? *v : Json::object();
}

std::string Project::nameOf(const std::string& sem) const {
    const bool zh = lang == "zh_TW";
    static const std::map<std::string, std::pair<const char*, const char*>> basic = {
        {"wall", {"Wall", "牆壁"}}, {"floor", {"Floor", "地板"}}, {"player_start", {"Player start", "玩家起點"}},
        {"stairs_up", {"Stairs up", "往上樓梯"}}, {"stairs_down", {"Stairs down", "往下樓梯"}}};
    static const std::map<std::string, std::pair<const char*, const char*>> colors = {
        {"yellow", {"yellow", "黃"}}, {"blue", {"blue", "藍"}}, {"red", {"red", "紅"}}};
    auto b = basic.find(sem);
    if (b != basic.end()) return zh ? b->second.second : b->second.first;
    const auto [cat, id] = splitSem(sem);
    if (cat == "monster") {
        const Json* r = typeRec(sem);
        const std::string n = r ? localized(r->value("name", Json())) : std::string();
        return n.empty() ? id : n;
    }
    if (cat == "item" || cat == "gate") {
        const Json* r = typeRec("item:" + id);
        std::string n = r ? localized(r->value("name", Json())) : std::string();
        if (n.empty()) n = id;
        if (cat == "item") return n;
        return zh ? "專屬鑰匙門（" + n + "）" : "Gate (" + n + ")";
    }
    if (cat == "npc") {
        const Json* r = typeRec(sem);
        if (r && member(*r, "name")) { const std::string n = localized((*r)["name"]); if (!n.empty()) return n; }
        auto bn = builtinNpcNames().find(id);
        return bn != builtinNpcNames().end() ? (zh ? bn->second.second : bn->second.first) : id;
    }
    if (cat == "door" || cat == "key") {
        auto c = colors.find(id);
        const std::string col = c != colors.end() ? (zh ? c->second.second : c->second.first) : id;
        return zh ? col + (cat == "door" ? "門" : "鑰匙") : col + (cat == "door" ? " door" : " key");
    }
    if (cat == "event") return id;
    return sem;
}

// ---- types ---------------------------------------------------------------------------------------------------------

Json* Project::typeRec(const std::string& sem) {
    const auto [cat, rawId] = splitSem(sem);
    // a map key "key:<colour>" is the item key_<colour> (items.json), as the game reads it (stage.h): its look and tint
    // are that record's
    const std::string id = cat == "key" ? "key_" + rawId : rawId;
    auto inFile = [&](const std::string& p, bool whole) -> Json* {
        auto it = dataFiles.find(p);
        if (it == dataFiles.end() || it->second.deleted) return nullptr;
        if (whole) return &it->second.obj;
        Json& o = it->second.obj;
        if (!o.is_object()) return nullptr;
        auto f = o.find(id);
        return f == o.end() || !f->is_object() ? nullptr : &*f;
    };
    if (cat == "monster") return inFile(enemiesPath(), false);
    if (cat == "item" || cat == "gate" || cat == "key") return inFile(itemsPath(), false);
    if (cat == "npc") return inFile(npcPath(id), true);
    return nullptr;
}

// ---- the palette's own categories ---------------------------------------------------------------------------------

std::vector<std::string> Project::paletteGroups() const {
    std::vector<std::string> out;
    auto it = dataFiles.find(palettePath());
    if (it == dataFiles.end() || it->second.deleted) return out;
    if (const Json* g = member(it->second.obj, "groups"); g && g->is_array())
        for (const Json& n : *g)
            if (n.is_string() && !n.get<std::string>().empty()) out.push_back(n.get<std::string>());
    return out;
}

void Project::setPaletteGroups(const std::vector<std::string>& groups) {
    auto it = dataFiles.find(palettePath());
    if (groups.empty() && (it == dataFiles.end() || !dataOrig.count(palettePath()))) {   // never was a file: nothing to write
        if (it != dataFiles.end()) dataFiles.erase(it);
        return;
    }
    DataFile& df = dataFiles[palettePath()];
    df.deleted = false;
    if (!df.obj.is_object()) df.obj = Json::object();
    df.obj["groups"] = groups;
}

std::string Project::groupOf(const std::string& sem) const {
    const Json* r = typeRec(sem);
    return r ? str(*r, "group", "") : std::string();
}

bool Project::setGroup(const std::string& sem, const std::string& group) {
    Json* r = typeRec(sem);
    if (!r) return false;
    if (group.empty()) r->erase("group");
    else (*r)["group"] = group;
    return true;
}

namespace {
// every type record: the monsters, the items, each NPC file
template <class F> void forEachRecord(std::map<std::string, DataFile>& files, F&& f) {
    for (auto& [path, df] : files) {
        if (df.deleted || !df.obj.is_object()) continue;
        if (path == Project::enemiesPath() || path == Project::itemsPath()) {
            for (auto& [k, v] : df.obj.items()) if (v.is_object()) f(v);
        } else if (startsWith(path, "assets/data/npcs/")) {
            f(df.obj);
        }
    }
}
}  // namespace

int Project::renameGroup(const std::string& from, const std::string& to) {
    int n = 0;
    forEachRecord(dataFiles, [&](Json& r) { if (str(r, "group", "") == from) { r["group"] = to; n++; } });
    std::vector<std::string> g = paletteGroups();
    for (std::string& s : g) if (s == from) s = to;
    setPaletteGroups(g);
    return n;
}

int Project::clearGroup(const std::string& group) {
    int n = 0;
    forEachRecord(dataFiles, [&](Json& r) { if (str(r, "group", "") == group) { r.erase("group"); n++; } });
    return n;
}

Json Project::typeLook(const std::string& sem) const {
    Json l = Json::object();
    const Json* r = typeRec(sem);
    if (!r) return l;
    if (const Json* s = member(*r, "sprite"); s && s->is_string()) l["sprite"] = *s;
    if (const Json* lk = member(*r, "look"); lk && lk->is_object())
        for (auto& [k, v] : lk->items()) l[k] = v;
    return l;
}

std::string Project::spriteOf(const std::string& sem) const {
    auto has = [&](const std::string& n) { return std::find(sprites.begin(), sprites.end(), n) != sprites.end(); };
    if (sem == "player_start") return "player";
    if (sem == "wall" || sem == "floor" || sem == "stairs_up" || sem == "stairs_down") return sem;
    const auto [cat, id] = splitSem(sem);
    if (cat == "monster" || cat == "item" || cat == "npc" || cat == "key") {   // a type's own sprite (its record)
        std::string own = str(typeLook(sem), "sprite");
        if (endsWith(own, ".png")) own.resize(own.size() - 4);
        if (!own.empty() && has(own)) return own;
    }
    if (cat == "door" || cat == "key") return cat + "_" + id;
    if (cat == "gate") return "door_yellow";   // the game draws a gate as a door with its key's colour over it
    if (cat == "monster") return id == "demonlord_vorkath" ? "boss_demonlord" : id;
    if (cat == "npc") return has("npc_" + id) ? "npc_" + id : "skeleton";
    if (cat == "item") return id;
    return std::string();   // events are drawn as plates
}

std::vector<std::string> Project::enemyIds() const {
    std::vector<std::string> out;
    auto it = dataFiles.find(enemiesPath());
    if (it != dataFiles.end() && it->second.obj.is_object())
        for (auto& [k, v] : it->second.obj.items()) if (!startsWith(k, "_") && v.is_object()) out.push_back(k);
    return out;
}

std::vector<std::string> Project::itemIds() const {
    std::vector<std::string> out;
    auto it = dataFiles.find(itemsPath());
    if (it != dataFiles.end() && it->second.obj.is_object())
        for (auto& [k, v] : it->second.obj.items()) if (!startsWith(k, "_") && v.is_object()) out.push_back(k);
    return out;
}

std::string Project::itemType(const std::string& id) const {
    const Json* r = typeRec("item:" + id);
    return r ? str(*r, "type", "consumable") : std::string("consumable");
}

std::vector<std::string> Project::npcIds() const {
    std::vector<std::string> out = {"sorcerer", "villager", "king", "princess", "handmaiden"};
    auto add = [&](const std::string& id) { if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id); };
    for (const auto& [p, df] : dataFiles) {
        const std::string pre = "assets/data/npcs/";
        if (startsWith(p, pre.c_str()) && !df.deleted) add(p.substr(pre.size(), p.size() - pre.size() - 5));
    }
    for (const Floor& f : floors)
        if (const Json* lg = member(f.stage, "legend"))
            for (auto& [c, v] : lg->items()) { (void)c; if (v.is_string() && startsWith(v.get<std::string>(), "npc:")) add(v.get<std::string>().substr(4)); }
    return out;
}

std::vector<std::string> Project::typeUses(const std::string& sem) {
    std::vector<std::string> out;
    const bool item = startsWith(sem, "item:");
    const std::string gate = item ? "gate:" + sem.substr(5) : std::string();
    // the item key_<colour> is also every "key:<colour>" cell
    const std::string mapKey = item && startsWith(sem, "item:key_") ? "key:" + sem.substr(9) : std::string();
    for (const Floor& f : floors) {
        const bool open = hasDoc(f.id);
        const Json* lgP = member(open ? doc(f.id).st : f.stage, "legend");
        if (!lgP) continue;
        std::string tiles;
        if (open) for (const std::string& r : doc(f.id).grid) tiles += r;
        else for (const Json& r : f.stage.at("tiles")) tiles += r.get<std::string>();
        for (auto& [c, v] : lgP->items()) {
            if (!v.is_string() || c.size() != 1) continue;
            const std::string s = v.get<std::string>();
            if ((s == sem || (item && s == gate) || (!mapKey.empty() && s == mapKey)) && tiles.find(c[0]) != std::string::npos) {
                out.push_back(f.id);
                break;
            }
        }
    }
    return out;
}

// ---- cells ---------------------------------------------------------------------------------------------------------

std::string Project::semAt(const StageDoc& d, int x, int y) const {
    if (!d.inside(x, y)) return std::string();
    const char c = d.at(x, y);
    if (c == '#') return "wall";
    if (c == '.') return "floor";
    if (c == '@') return "player_start";
    const Json* lg = member(d.st, "legend");
    const Json* v = lg ? member(*lg, std::string(1, c).c_str()) : nullptr;
    return v && v->is_string() ? v->get<std::string>() : std::string();
}

std::map<std::string, int> Project::placedEvents(const StageDoc& d) const {
    int count[256] = {};
    for (const std::string& r : d.grid) for (char c : r) count[(unsigned char)c]++;
    std::map<std::string, int> out;
    const Json* lg = member(d.st, "legend");
    if (!lg || !lg->is_object()) return out;
    for (const auto& [k, v] : lg->items())
        if (k.size() == 1 && count[(unsigned char)k[0]] && v.is_string()) {
            const std::string& s = v.get_ref<const std::string&>();
            if (s.rfind("event:", 0) == 0) out[s.substr(6)] += count[(unsigned char)k[0]];
        }
    return out;
}

char Project::charFor(StageDoc& d, const std::string& sem) const {
    if (sem == "wall") return '#';
    if (sem == "floor") return '.';
    if (sem == "player_start") return '@';
    Json& lg = d.st["legend"];
    auto usedChars = [&] {
        std::set<char> used;
        for (auto& [k, v] : lg.items()) { (void)v; if (k.size() == 1) used.insert(k[0]); }
        for (const std::string& r : d.grid) used.insert(r.begin(), r.end());
        return used;
    };
    auto firstFree = [&](const std::set<char>& used) {
        for (const char* p = kFreeChars; *p; p++) if (!used.count(*p)) return *p;
        return '\0';
    };
    auto res = reservedChars().find(sem);
    if (res != reservedChars().end()) {   // a door or stair always gets its own char; whatever used it moves to a free one
        const char c = res->second;
        const std::string ck(1, c);
        if (lg.contains(ck) && lg[ck] != sem) {
            const Json other = lg[ck];
            const char nc = firstFree(usedChars());
            lg.erase(ck);
            if (nc) {
                lg[std::string(1, nc)] = other;
                for (std::string& r : d.grid) std::replace(r.begin(), r.end(), c, nc);
            }
        }
        lg[ck] = sem;
        return c;
    }
    for (auto& [k, v] : lg.items()) if (k.size() == 1 && v == sem) return k[0];
    const std::set<char> used = usedChars();
    auto pref = preferredChars().find(sem);
    const char pick = pref != preferredChars().end() && !used.count(pref->second) ? pref->second : firstFree(used);
    if (!pick) return '\0';
    lg[std::string(1, pick)] = sem;
    return pick;
}

bool Project::putSem(StageDoc& d, int x, int y, const std::string& sem, bool* listed) const {
    if (!d.inside(x, y)) return false;
    if (sem == "player_start")
        for (std::string& r : d.grid) std::replace(r.begin(), r.end(), '@', '.');
    const char c = charFor(d, sem);
    if (!c || d.grid[(size_t)y][(size_t)x] == c) return false;
    d.grid[(size_t)y][(size_t)x] = c;
    dropObj(d, x, y);
    if (startsWith(sem, "event:")) {
        const std::string id = sem.substr(6);
        if (!d.spec.contains("events") || !d.spec["events"].is_array()) d.spec["events"] = Json::array();
        Json& evs = d.spec["events"];
        if (std::find(evs.begin(), evs.end(), Json(id)) == evs.end()) {
            evs.push_back(id);
            if (listed) *listed = true;
        }
    }
    return true;
}

Footprint Project::footprintOf(const StageDoc& d, char ch, const std::string& sem) const {
    const Json* own = member(d.st, "footprints");
    const Json* o = own && ch ? member(*own, std::string(1, ch).c_str()) : nullptr;
    const Json* f = o && o->is_object() ? o : member(footprints, sem.c_str());
    Footprint fp;
    if (f && f->is_object()) {
        fp.w = std::max(1, f->value("w", 1));
        fp.h = std::max(1, f->value("h", 1));
    }
    return fp;
}

std::vector<Cell> Project::fpCells(const StageDoc& d, const std::string& sem, int x, int y) const {
    if (!startsWith(sem, "monster:")) return {{x, y}};
    char ch = 0;
    if (const Json* lg = member(d.st, "legend"))
        for (auto& [k, v] : lg->items()) if (k.size() == 1 && v == sem) { ch = k[0]; break; }
    const Footprint fp = footprintOf(d, ch, sem);
    std::vector<Cell> out;
    for (int dy = 0; dy < fp.h; dy++)
        for (int dx = 0; dx < fp.w; dx++) out.push_back({x + dx, y + dy});
    return out;
}

PlaceCheck Project::placeCheck(const StageDoc& d, int x, int y, const std::string& sem, const Cell* ignore) const {
    PlaceCheck r;
    r.cells = fpCells(d, sem, x, y);
    if (!isEntity(sem)) return r;
    if (!ignore && semAt(d, x, y) == sem) { r.same = true; return r; }   // painting over the same object does nothing
    // cells covered by the extra part of big monsters (their anchor cell already holds their char)
    std::map<Cell, std::string> cover;
    for (int yy = 0; yy < d.height(); yy++)
        for (int xx = 0; xx < d.width(); xx++) {
            const std::string s2 = semAt(d, xx, yy);
            if (!startsWith(s2, "monster:") || (ignore && ignore->x == xx && ignore->y == yy)) continue;
            const std::vector<Cell> cs = fpCells(d, s2, xx, yy);
            for (size_t i = 1; i < cs.size(); i++) cover[cs[i]] = s2;
        }
    for (const Cell& c : r.cells) {
        if (!d.inside(c.x, c.y)) { r.ok = false; r.why = PlaceCheck::Edge; return r; }
        const bool own = ignore && c.x == ignore->x && c.y == ignore->y;
        const char ch = d.at(c.x, c.y);
        if (!own && ch == '#') { r.ok = false; r.why = PlaceCheck::Wall; r.x = c.x; r.y = c.y; return r; }
        if (!own && ch != '.') {
            r.ok = false; r.why = PlaceCheck::Obj; r.x = c.x; r.y = c.y;
            const std::string s2 = semAt(d, c.x, c.y);
            r.what = nameOf(s2.empty() ? "?" : s2);
            return r;
        }
        auto cv = cover.find(c);
        if (cv != cover.end()) { r.ok = false; r.why = PlaceCheck::Obj; r.x = c.x; r.y = c.y; r.what = nameOf(cv->second); return r; }
    }
    return r;
}

Json* Project::objOf(StageDoc& d, int x, int y) {
    auto o = d.st.find("objects");
    if (o == d.st.end() || !o->is_object()) return nullptr;
    auto it = o->find(cellKey(x, y));
    return it == o->end() ? nullptr : &*it;
}

const Json* Project::objOf(const StageDoc& d, int x, int y) { return objOf(const_cast<StageDoc&>(d), x, y); }

void Project::dropObj(StageDoc& d, int x, int y) {
    auto o = d.st.find("objects");
    if (o == d.st.end() || !o->is_object()) return;
    o->erase(cellKey(x, y));
    if (o->empty()) d.st.erase("objects");
}

Json Project::lookAt(const StageDoc& d, int x, int y, const std::string& sem) const {
    Json l = typeLook(sem);
    if (const Json* o = objOf(d, x, y))
        if (const Json* lk = member(*o, "look"); lk && lk->is_object())
            for (auto& [k, v] : lk->items()) l[k] = v;
    return l;
}

std::optional<Cell> Project::find(const StageDoc& d, const std::string& sem) const {
    for (int y = 0; y < d.height(); y++)
        for (int x = 0; x < d.width(); x++) if (semAt(d, x, y) == sem) return Cell{x, y};
    return std::nullopt;
}

// ---- undo ----------------------------------------------------------------------------------------------------------

std::string Project::snapshot(const StageDoc& d) {
    Json s = Json::object();
    s["grid"] = d.grid;
    s["legend"] = d.st.value("legend", Json::object());
    s["spec"] = d.spec;
    if (d.st.contains("connect")) s["connect"] = d.st["connect"];
    if (d.st.contains("name")) s["name"] = d.st["name"];
    if (d.st.contains("objects")) s["objects"] = d.st["objects"];
    return s.dump();
}

void Project::restore(StageDoc& d, const std::string& snap) {
    const Json s = Json::parse(snap);
    d.grid = s["grid"].get<std::vector<std::string>>();
    d.st["legend"] = s["legend"];
    d.spec = s["spec"];
    for (const char* k : {"connect", "name", "objects"}) {
        if (s.contains(k)) d.st[k] = s[k];
        else d.st.erase(k);
    }
}

bool Project::commitEdit(StageDoc& d, const std::string& label) {
    if (!d.pending) return false;
    std::string before = std::move(*d.pending);
    d.pending.reset();
    if (before == snapshot(d)) return false;
    d.undo.push_back({std::move(before), label});
    d.redo.clear();
    d.ver++;
    storyVer++;
    return true;
}

bool Project::undo(StageDoc& d, std::string* label) {
    if (d.undo.empty()) return false;
    StageDoc::Step u = std::move(d.undo.back());
    d.undo.pop_back();
    d.redo.push_back({snapshot(d), u.label});
    restore(d, u.snap);
    if (label) *label = u.label;
    d.ver++;
    storyVer++;
    return true;
}

bool Project::redo(StageDoc& d, std::string* label) {
    if (d.redo.empty()) return false;
    StageDoc::Step r = std::move(d.redo.back());
    d.redo.pop_back();
    d.undo.push_back({snapshot(d), r.label});
    restore(d, r.snap);
    if (label) *label = r.label;
    d.ver++;
    storyVer++;
    return true;
}

// ---- saving --------------------------------------------------------------------------------------------------------

// The stage as it would be written: rows back to strings, legend in its order. A char this session added and no cell
// uses any more is dropped; entries the file already had stay (hand-made stages list them all).
Json Project::stageOut(const StageDoc& d) const {
    Json st = d.st;
    st["tiles"] = d.grid;
    st["height"] = d.height();
    st["width"] = d.width();
    std::set<char> used;
    for (const std::string& r : d.grid) used.insert(r.begin(), r.end());
    static const Json kNone = Json::object();
    const Json* origP = member(d.f->stage, "legend");
    const Json& orig = origP ? *origP : kNone;
    Json lg = Json::object();
    if (st.contains("legend"))
        for (auto& [c, v] : st["legend"].items())
            if ((c.size() == 1 && used.count(c[0])) || (orig.contains(c) && orig.at(c) == v)) lg[c] = v;
    st["legend"] = lg;
    return st;
}

bool Project::isDirty(const std::string& fid) const {
    auto it = docs_.find(fid);
    if (it == docs_.end()) return false;
    const StageDoc& d = *it->second;
    if (d.dirtyAt != d.ver) {
        d.dirtyVal = stageText(d) != d.origStage || specText(d) != d.origSpec;
        d.dirtyAt = d.ver;
    }
    return d.dirtyVal;
}

bool Project::dataDirty() const {
    if (!textEdits.empty()) return true;
    if (dataDirtyAt_ != dataVer) {
        dataDirtyVal_ = false;
        for (const auto& [path, df] : dataFiles) {
            auto orig = dataOrig.find(path);
            if (df.deleted ? orig != dataOrig.end() : (orig == dataOrig.end() || fmtJson(df.obj, df.eol) != orig->second)) { dataDirtyVal_ = true; break; }
        }
        dataDirtyAt_ = dataVer;
    }
    return dataDirtyVal_;
}

bool Project::anyDirty() const {
    for (const auto& [id, d] : docs_) { (void)d; if (isDirty(id)) return true; }
    return false;
}

}  // namespace stage
