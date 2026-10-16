// stage_save.cpp -- see stage_save.h.
#include "stage_save.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace stage {

namespace {

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool endsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

const Json* member(const Json& j, const char* key) {
    if (!j.is_object()) return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\r') continue;
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> sortedNames(const std::string& dir, const std::string& prefix, const std::string& suffix) {
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

std::vector<std::string> eventsList(const Json& spec) {
    std::vector<std::string> out;
    if (const Json* e = member(spec, "events"); e && e->is_array())
        for (const Json& x : *e) if (x.is_string()) out.push_back(x.get<std::string>());
    return out;
}
bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}  // namespace

std::vector<ChangedFile> changedFiles(const Project& p) {
    std::vector<ChangedFile> out;
    for (const Floor& f : p.floors) {
        if (!p.hasDoc(f.id)) continue;
        const StageDoc& d = const_cast<Project&>(p).doc(f.id);
        const std::string st = p.stageText(d), sp = p.specText(d);
        if (st != d.origStage) out.push_back({ChangedFile::Stage, f.stagePath, st, d.origStage, f.id});
        if (sp != d.origSpec) out.push_back({ChangedFile::Spec, f.specPath, sp, d.origSpec, f.id});
    }
    for (const auto& [path, df] : p.dataFiles) {
        auto orig = p.dataOrig.find(path);
        if (df.deleted) {
            if (orig != p.dataOrig.end()) out.push_back({ChangedFile::Delete, path, std::string(), orig->second, std::string()});
            continue;
        }
        const std::string text = fmtJson(df.obj, df.eol);
        if (orig == p.dataOrig.end() || text != orig->second)
            out.push_back({ChangedFile::Data, path, text, orig == p.dataOrig.end() ? std::string() : orig->second, std::string()});
    }
    if (!p.textEdits.empty()) {
        std::string lines;
        for (const auto& [k, v] : p.textEdits) lines += (lines.empty() ? "" : "\n") + Json(k).dump() + ": " + v.dump();
        out.push_back({ChangedFile::Text, "assets/data/text.json", lines, std::string(), std::string()});
    }
    return out;
}

std::vector<std::pair<char, std::string>> lineDiff(const std::string& a, const std::string& b) {
    const std::vector<std::string> A = splitLines(a), B = splitLines(b);
    const size_t n = A.size(), m = B.size();
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;) dp[i][j] = A[i] == B[j] ? dp[i + 1][j + 1] + 1 : std::max(dp[i + 1][j], dp[i][j + 1]);
    std::vector<std::pair<char, std::string>> all;
    size_t i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && A[i] == B[j]) { all.push_back({' ', A[i]}); i++; j++; }
        else if (j < m && (i >= n || dp[i][j + 1] >= dp[i + 1][j])) all.push_back({'+', B[j++]});
        else all.push_back({'-', A[i++]});
    }
    // keep only changes and two lines of context around them
    std::vector<std::pair<char, std::string>> res;
    for (size_t k = 0; k < all.size(); k++) {
        bool keep = all[k].first != ' ';
        for (size_t q = (k >= 2 ? k - 2 : 0); !keep && q < std::min(all.size(), k + 3); q++) keep = all[q].first != ' ';
        if (keep) res.push_back(all[k]);
        else if (!res.empty() && res.back().second != "…") res.push_back({' ', "…"});
    }
    return res;
}

std::string timeStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d_%H%M%S", &tm);
    return buf;
}

bool writeFiles(Project& p, const std::string& stamp, std::vector<std::string>& written, std::string& err) {
    std::vector<ChangedFile> files = changedFiles(p);
    // a floor spec on disk may have changed since it was read (the event editor saves floors' events lists too): this
    // editor's change to the events list is applied to the file as it is now
    for (ChangedFile& f : files) {
        if (f.kind == ChangedFile::Spec) {
            StageDoc& d = p.doc(f.floorId);
            std::string now;
            if (!readText(p.abs(f.path), now) || now == d.origSpec) continue;
            Json disk = Json::parse(now, nullptr, false);
            const Json was = Json::parse(d.origSpec, nullptr, false);
            if (disk.is_discarded() || was.is_discarded()) continue;
            const std::vector<std::string> before = eventsList(was), mine = eventsList(d.spec), cur = eventsList(disk);
            Json merged = Json::array();
            for (const std::string& x : cur) if (!(contains(before, x) && !contains(mine, x))) merged.push_back(x);
            for (const std::string& x : mine) if (!contains(before, x) && !contains(cur, x)) merged.push_back(x);
            disk["events"] = merged;
            d.spec = disk;
            f.text = p.specText(d);
            f.before = now;
        } else if (f.kind == ChangedFile::Text) {   // text.json as it is now, with this editor's keys
            std::string raw;
            if (!readText(p.abs(f.path), raw)) { err = f.path + ": cannot read"; return false; }
            Json obj = Json::parse(raw, nullptr, false);
            if (obj.is_discarded()) { err = f.path + ": not valid JSON"; return false; }
            for (const auto& [k, v] : p.textEdits) {
                Json& s = obj["strings"][k];
                if (!s.is_object()) s = Json::object();
                for (auto& [l, t] : v.items()) s[l] = t;
            }
            Eol eol;
            eol.crlf = raw.find("\r\n") != std::string::npos;
            eol.finalNewline = !raw.empty() && raw.back() == '\n';
            f.text = fmtJson(obj, eol);
            f.before = raw;
        }
    }
    for (const ChangedFile& f : files) {   // every file is backed up first
        std::string old;
        if (readText(p.abs(f.path), old) && !writeText(p.abs("Build/editor_backups/" + stamp + "/" + f.path), old)) {
            err = "Build/editor_backups/" + stamp + "/" + f.path + ": cannot write the backup";
            return false;
        }
    }
    for (const ChangedFile& f : files) {
        if (f.kind == ChangedFile::Delete) {
            std::error_code ec;
            fs::remove(fs::u8path(p.abs(f.path)), ec);
        } else if (!writeText(p.abs(f.path), f.text)) {
            err = f.path + ": cannot write";
            return false;
        }
        written.push_back(f.path);
    }
    // what is on disk now is what this editor has
    for (const ChangedFile& f : files) {
        switch (f.kind) {
        case ChangedFile::Stage: p.doc(f.floorId).origStage = f.text; break;
        case ChangedFile::Spec: p.doc(f.floorId).origSpec = f.text; break;
        case ChangedFile::Data: p.dataOrig[f.path] = f.text; break;
        case ChangedFile::Delete: p.dataOrig.erase(f.path); p.dataFiles.erase(f.path); break;
        case ChangedFile::Text:
            for (const auto& [k, v] : p.textEdits) {
                Json& s = p.strings[k];
                if (!s.is_object()) s = Json::object();
                for (auto& [l, t] : v.items()) s[l] = t;
            }
            p.textEdits.clear();
            break;
        }
    }
    return true;
}

// ---- the per-floor event index (tools/web_editors/floor_index.js, the owner's Q10 answer) ---------------------------

Json buildFloorIndex(const std::string& root) {
    const std::string A = root + "/assets/data/";
    Json j;
    Json strings = Json::object();
    if (readJson(A + "text.json", j) && member(j, "strings")) strings = j["strings"];
    auto english = [&](const std::string& key) -> std::string {
        const Json* v = member(strings, key.c_str());
        const Json* en = v && v->is_object() ? member(*v, "en") : nullptr;
        return en && en->is_string() ? en->get<std::string>() : std::string();
    };
    std::map<std::string, std::pair<std::string, Json>> pools;
    for (const std::string& n : sortedNames(A + "events", "pool_", ".json")) {
        if (!readJson(A + "events/" + n, j)) continue;
        if (const Json* evs = member(j, "events"); evs && evs->is_array())
            for (const Json& e : *evs)
                if (const Json* id = member(e, "eventId"); id && id->is_string()) pools[id->get<std::string>()] = {n.substr(0, n.size() - 5), e};
    }
    std::map<std::string, std::vector<Json>> beats;
    for (const std::string& n : sortedNames(A + "story/chapters", "", ".json")) {
        if (!readJson(A + "story/chapters/" + n, j)) continue;
        if (const Json* bs = member(j, "beats"); bs && bs->is_array())
            for (const Json& b : *bs) {
                const Json* fl = member(b, "floor");
                if (!fl || !fl->is_string() || fl->get<std::string>().empty()) continue;
                Json pick = Json::object();
                for (const char* k : {"id", "kind", "eventId", "nodeId", "enemyId", "shardId"})
                    if (const Json* v = member(b, k)) pick[k] = *v;
                beats[fl->get<std::string>()].push_back(pick);
            }
    }
    Json floors = Json::object();
    for (const std::string& n : sortedNames(A + "story/floors", "F", ".json")) {
        if (endsWith(n, ".stage.json")) continue;
        Json spec;
        if (!readJson(A + "story/floors/" + n, spec)) continue;
        const std::string fid = spec.value("id", std::string());
        const std::string hand = spec.contains("handAuthoredStage") && spec["handAuthoredStage"].is_string() ? spec["handAuthoredStage"].get<std::string>() : std::string();
        const std::string map = !hand.empty() ? "stages/" + hand : "story/floors/" + fid + ".stage.json";
        std::vector<std::pair<std::string, Json>> tiles;   // event id -> [[x, y], ...], first-seen order
        Json grid;
        if (readJson(A + map, grid)) {
            const Json* legend = member(grid, "legend");
            if (const Json* rows = member(grid, "tiles"); rows && rows->is_array())
                for (size_t y = 0; y < rows->size(); y++) {
                    if (!(*rows)[y].is_string()) continue;
                    const std::string row = (*rows)[y].get<std::string>();
                    for (size_t x = 0; x < row.size(); x++) {
                        const Json* k = legend ? member(*legend, std::string(1, row[x]).c_str()) : nullptr;
                        const std::string kind = k && k->is_string() ? k->get<std::string>() : std::string();
                        if (!startsWith(kind, "event:")) continue;
                        const std::string id = kind.substr(6);
                        auto it = std::find_if(tiles.begin(), tiles.end(), [&](const auto& t) { return t.first == id; });
                        if (it == tiles.end()) { tiles.push_back({id, Json::array()}); it = tiles.end() - 1; }
                        it->second.push_back(Json::array({(int)x, (int)y}));
                    }
                }
        }
        const std::vector<std::string> listed = eventsList(spec);
        std::vector<std::string> placedSorted;
        for (const auto& t : tiles) placedSorted.push_back(t.first);
        std::sort(placedSorted.begin(), placedSorted.end());
        std::vector<std::string> ids;
        for (const std::string& id : listed) if (!contains(ids, id)) ids.push_back(id);
        for (const std::string& id : placedSorted) if (!contains(ids, id)) ids.push_back(id);
        Json events = Json::array();
        for (const std::string& id : ids) {
            auto pl = pools.find(id);
            const Json rec = pl != pools.end() ? pl->second.second : Json::object();
            const std::string recTitle = rec.contains("title") && rec["title"].is_string() ? rec["title"].get<std::string>() : std::string();
            const std::string title = english(!recTitle.empty() ? recTitle : id + ".title");
            Json e = Json::object();
            e["id"] = id;
            e["kind"] = rec.contains("kind") && !rec["kind"].is_null() ? rec["kind"] : Json();
            e["title"] = title.empty() ? Json() : Json(title);
            e["pool"] = pl != pools.end() ? Json(pl->second.first) : Json();
            e["listed"] = contains(listed, id);
            auto t = std::find_if(tiles.begin(), tiles.end(), [&](const auto& x) { return x.first == id; });
            e["tiles"] = t != tiles.end() ? t->second : Json::array();
            events.push_back(e);
        }
        Json entry = Json::object();
        entry["act"] = spec.contains("act") && !spec["act"].is_null() ? spec["act"] : Json();
        entry["map"] = map;
        entry["events"] = events;
        Json bl = Json::array();
        if (beats.count(fid)) for (const Json& b : beats[fid]) bl.push_back(b);
        entry["beats"] = bl;
        if (const Json* stairs = member(spec, "stairs"); stairs && stairs->is_array()) {
            Json sl = Json::array();
            for (const Json& s : *stairs) {
                Json o = Json::object();
                const auto truthy = [](const Json* v) {
                    return v && !v->is_null() && !(v->is_string() && v->get<std::string>().empty()) && !(v->is_boolean() && !v->get<bool>())
                        && !(v->is_number() && v->get<double>() == 0);
                };
                const Json* dir = member(s, "dir");
                o["dir"] = truthy(dir) ? *dir : Json("up");
                if (const Json* to = member(s, "to"); truthy(to)) o["to"] = *to;
                const Json* un = member(s, "unlock");
                if (const Json* ed = un ? member(*un, "eventDone") : nullptr; truthy(ed)) o["unlock"] = *ed;
                sl.push_back(o);
            }
            entry["stairs"] = sl;
        }
        floors[fid] = entry;
    }
    std::map<std::string, std::vector<std::string>> byEvent;
    auto add = [&](const std::string& id, const std::string& fid) {
        std::vector<std::string>& l = byEvent[id];
        if (!contains(l, fid)) l.push_back(fid);
    };
    for (auto& [fid, f] : floors.items()) {
        for (const Json& e : f["events"]) byEvent[e["id"].get<std::string>()].push_back(fid);
        for (const Json& b : f["beats"]) if (b.contains("eventId") && b["eventId"].is_string() && !b["eventId"].get<std::string>().empty()) add(b["eventId"].get<std::string>(), fid);
        if (f.contains("stairs")) for (const Json& s : f["stairs"]) if (s.contains("unlock") && s["unlock"].is_string()) add(s["unlock"].get<std::string>(), fid);
    }
    Json sorted = Json::object();
    for (const auto& [k, v] : byEvent) sorted[k] = v;   // std::map: sorted by key, as the JS sort()
    Json out = Json::object();
    out["_comment"] = "Generated by the HTML editors (tools/web_editors/floor_index.js) -- do not edit. The events each floor lists "
                      "or places, its chapter beats and its stair locks; \"events\" maps an event to its floors (Q10).";
    out["floors"] = floors;
    out["events"] = sorted;
    return out;
}

bool syncFloorIndex(const std::string& root, std::string* err) {
    const std::string text = buildFloorIndex(root).dump(1, ' ', false, Json::error_handler_t::replace) + "\n";
    const std::string path = root + "/assets/data/events/floor_index.json";
    std::string now;
    if (readText(path, now) && now == text) return false;
    if (!writeText(path, text)) { if (err) *err = path + ": cannot write"; return false; }
    return true;
}

}  // namespace stage
