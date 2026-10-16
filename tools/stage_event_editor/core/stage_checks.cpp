// stage_checks.cpp -- see stage_checks.h.
#include "stage_checks.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <set>

namespace stage {

const char* const kKeyColors[3] = {"yellow", "blue", "red"};

namespace {

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
std::string num(long long v) { return std::to_string(v); }
std::string cellChar(char c) { return std::string(1, c); }
int doorColor(char c) { return c == 'y' ? Yellow : c == 'b' ? Blue : c == 'r' ? Red : -1; }
int keyColor(const std::string& name) {
    for (int i = 0; i < 3; i++) if (name == kKeyColors[i]) return i;
    return -1;
}
int popcount(uint32_t m) { int n = 0; while (m) { n += (int)(m & 1u); m >>= 1; } return n; }

const Json* member(const Json& j, const char* key) {
    if (!j.is_object()) return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}
std::string str(const Json& j, const char* key) {
    const Json* v = member(j, key);
    return v && v->is_string() ? v->get<std::string>() : std::string();
}
bool truthy(const Json* v) {
    if (!v || v->is_null()) return false;
    if (v->is_boolean()) return v->get<bool>();
    if (v->is_string()) return !v->get_ref<const std::string&>().empty();
    if (v->is_number()) return v->get<double>() != 0;
    return true;
}
double numberOf(const Json& v) {   // JS +value (true -> 1, "3" -> 3, anything else -> 0 here)
    if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) { try { return std::stod(v.get<std::string>()); } catch (...) { return 0; } }
    return 0;
}

Issue issue(Sev sev, const char* code, std::vector<std::string> args = {}, std::vector<Cell> cells = {}) {
    Issue i;
    i.sev = sev;
    i.code = code;
    i.args = std::move(args);
    i.cells = std::move(cells);
    return i;
}

}  // namespace

std::string numText(const Json& v) {
    const double d = numberOf(v);
    if (std::floor(d) == d && std::fabs(d) < 1e15) return std::to_string((long long)d);
    std::string s = std::to_string(d);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

// Where the player appears, in the game's order (game_assets.cpp, loadStage): the stairs down (arriving from below),
// else '@', else any stairs up.
std::optional<Cell> startCell(const Project& p, const StageDoc& d) {
    auto find = [&](const std::function<bool(int, int)>& test) -> std::optional<Cell> {
        for (int y = 0; y < d.height(); y++)
            for (int x = 0; x < d.width(); x++) if (test(x, y)) return Cell{x, y};
        return std::nullopt;
    };
    if (auto c = find([&](int x, int y) { return p.semAt(d, x, y) == "stairs_down"; })) return c;
    if (auto c = find([&](int x, int y) { return d.at(x, y) == '@'; })) return c;
    return find([&](int x, int y) { return p.semAt(d, x, y) == "stairs_up"; });
}

std::vector<char> reachableCells(const Project& p, const StageDoc& d) {
    const int W = d.width(), H = d.height();
    const std::optional<Cell> start = startCell(p, d);
    if (!start) return {};
    std::vector<char> seen((size_t)W * H, 0);
    std::deque<Cell> q{*start};
    seen[(size_t)start->y * W + start->x] = 1;
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!q.empty()) {
        const Cell c = q.front();
        q.pop_front();
        for (const auto& dd : dirs) {
            const int nx = c.x + dd[0], ny = c.y + dd[1];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H || seen[(size_t)ny * W + nx] || d.at(nx, ny) == '#') continue;
            seen[(size_t)ny * W + nx] = 1;
            q.push_back({nx, ny});
        }
    }
    return seen;
}

std::vector<Issue> problems(const Project& p, const StageDoc& d) {
    std::vector<Issue> out;
    const int H = d.height(), W = d.width();
    std::vector<Cell> starts;
    std::vector<std::pair<std::string, std::vector<Cell>>> evCells;   // in first-seen order
    bool up = false, down = false, border = false;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const char c = d.at(x, y);
            if (c == '@') starts.push_back({x, y});
            const std::string sem = p.semAt(d, x, y);
            if (sem.empty()) out.push_back(issue(Sev::Error, "pUnknownChar", {cellChar(c), num(x), num(y)}, {{x, y}}));
            if (sem == "stairs_up") up = true;
            if (sem == "stairs_down") down = true;
            if ((y == 0 || x == 0 || y == H - 1 || x == W - 1) && c != '#') border = true;
            if (startsWith(sem, "event:")) {
                const std::string id = sem.substr(6);
                auto it = std::find_if(evCells.begin(), evCells.end(), [&](const auto& e) { return e.first == id; });
                if (it == evCells.end()) evCells.push_back({id, {{x, y}}});
                else it->second.push_back({x, y});
            }
            if (startsWith(sem, "monster:")) {
                const Footprint fp = p.footprintOf(d, c, sem);
                bool bad = false;
                for (int dy = 0; dy < fp.h && !bad; dy++)
                    for (int dx = 0; dx < fp.w && !bad; dx++)
                        if ((dx || dy) && (!d.inside(x + dx, y + dy) || d.at(x + dx, y + dy) != '.')) bad = true;
                if (bad) out.push_back(issue(Sev::Warn, "pFootprint", {sem.substr(8), num(x), num(y)}, {{x, y}}));
            }
        }
    if (!startCell(p, d)) out.push_back(issue(Sev::Error, "pNoStart"));
    if (const Json* lg = member(d.st, "legend"))
        for (auto& [c, v] : lg->items()) {
            if (!v.is_string()) continue;
            auto r = reservedChars().find(v.get<std::string>());
            if (r != reservedChars().end() && c != cellChar(r->second))
                out.push_back(issue(Sev::Error, "pReservedChar", {v.get<std::string>(), c, cellChar(r->second)}));
        }
    if (starts.size() > 1) out.push_back(issue(Sev::Warn, "pManyStart", {num((long long)starts.size())}, starts));
    std::vector<std::string> listed;
    if (const Json* evs = member(d.spec, "events"); evs && evs->is_array())
        for (const Json& e : *evs) if (e.is_string()) listed.push_back(e.get<std::string>());
    const auto isListed = [&](const std::string& id) { return std::find(listed.begin(), listed.end(), id) != listed.end(); };
    for (const auto& [id, cells] : evCells) {
        if (!p.event(id)) out.push_back(issue(Sev::Error, "pEvUndefined", {id}, cells));
        if (!isListed(id)) { Issue i = issue(Sev::Warn, "pEvNotListed", {id}, cells); i.fixList = id; out.push_back(i); }
        if (cells.size() > 1) out.push_back(issue(Sev::Warn, "pEvDup", {id, num((long long)cells.size())}, cells));
    }
    for (const std::string& id : listed)
        if (std::none_of(evCells.begin(), evCells.end(), [&](const auto& e) { return e.first == id; })) {
            Issue i = issue(Sev::Info, "pEvUnplaced", {id});
            i.brush = "event:" + id;
            out.push_back(i);
        }
    std::string cu, cd;
    if (const Json* cn = member(d.st, "connect")) { cu = str(*cn, "up"); cd = str(*cn, "down"); }
    if (!cu.empty() && !up) out.push_back(issue(Sev::Warn, "pNoUp", {cu}));
    if (cu.empty() && up) out.push_back(issue(Sev::Warn, "pUpNoTarget"));
    if (!cd.empty() && !down) out.push_back(issue(Sev::Warn, "pNoDown", {cd}));
    if (cd.empty() && down) out.push_back(issue(Sev::Warn, "pDownNoTarget"));
    {
        const std::vector<char> reach = reachableCells(p, d);
        std::vector<Cell> lost;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (Project::isEntity(p.semAt(d, x, y)) && (reach.empty() || !reach[(size_t)y * W + x])) lost.push_back({x, y});
        if (!lost.empty()) out.push_back(issue(Sev::Warn, "pUnreach", {num((long long)lost.size())}, lost));
    }
    if (const Json* objs = member(d.st, "objects"); objs && objs->is_object())
        for (auto& [k, o] : objs->items()) {
            const size_t comma = k.find(',');
            if (comma == std::string::npos) continue;
            const int ox = std::atoi(k.c_str()), oy = std::atoi(k.c_str() + comma + 1);
            if (!Project::isEntity(p.semAt(d, ox, oy))) out.push_back(issue(Sev::Warn, "pObjEmpty", {num(ox), num(oy)}, {{ox, oy}}));
            if (const Json* evs = member(o, "events"); evs && evs->is_array())
                for (const Json& e : *evs)
                    if (e.is_string() && !p.event(e.get<std::string>()))
                        out.push_back(issue(Sev::Error, "pObjEvent", {e.get<std::string>(), num(ox), num(oy)}, {{ox, oy}}));
        }
    if (border) out.push_back(issue(Sev::Info, "pBorder"));
    return out;
}

// ---- story ---------------------------------------------------------------------------------------------------------
// The game's rules: a y / b / r door takes one key of its colour; keys are picked up on contact; U and D end the floor.
// Monsters are treated as beatable (their stats are not simulated). Every order of opening doors is tried (doors are
// few), so "the player spent the only key on the wrong door" is found, not guessed.

namespace {

FloorFacts floorFacts(const Project& p, const StageDoc& d) {
    FloorFacts F;
    F.W = d.width();
    F.H = d.height();
    F.keyAt.assign((size_t)F.W * F.H, -1);
    F.doorIdx.assign((size_t)F.W * F.H, -1);
    for (int y = 0; y < F.H; y++)
        for (int x = 0; x < F.W; x++) {
            const char c = d.at(x, y);
            const int dc = doorColor(c);
            if (dc >= 0) { F.doorIdx[(size_t)y * F.W + x] = (int)F.doors.size(); F.doors.push_back({x, y, dc}); }
            const std::string s2 = p.semAt(d, x, y);
            if (startsWith(s2, "key:")) F.keyAt[(size_t)y * F.W + x] = keyColor(s2.substr(4));
            if (c == 'U') F.ups.push_back(y * F.W + x);
        }
    F.start = startCell(p, d);
    return F;
}

void floodMask(const StageDoc& d, const FloorFacts& F, FloodState& s) {
    s.seen.assign((size_t)F.W * F.H, 0);
    if (!F.start) return;
    std::vector<int> q{F.start->y * F.W + F.start->x};
    s.seen[(size_t)q[0]] = 1;
    s.seenOrder.push_back(q[0]);
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!q.empty()) {
        const int k = q.back();
        q.pop_back();
        const int x = k % F.W, y = k / F.W;
        if (F.keyAt[(size_t)k] >= 0) s.found.n[F.keyAt[(size_t)k]]++;
        const char c0 = d.at(x, y);
        if ((c0 == 'U' || c0 == 'D') && !(x == F.start->x && y == F.start->y)) continue;   // a stair ends the floor
        for (const auto& dd : dirs) {
            const int nx = x + dd[0], ny = y + dd[1];
            if (nx < 0 || ny < 0 || nx >= F.W || ny >= F.H) continue;
            const int nk = ny * F.W + nx;
            if (s.seen[(size_t)nk] || d.at(nx, ny) == '#') continue;
            const int di = F.doorIdx[(size_t)nk];
            if (di >= 0 && !(di < 32 && (s.mask & (1u << di)))) {
                if (std::find(s.frontier.begin(), s.frontier.end(), di) == s.frontier.end()) s.frontier.push_back(di);
                continue;
            }
            s.seen[(size_t)nk] = 1;
            s.seenOrder.push_back(nk);
            q.push_back(nk);
        }
    }
}

}  // namespace

bool Explore::hasUp() const { return std::any_of(all.begin(), all.end(), [](const FloodState& s) { return s.up; }); }

Explore explore(const Project& p, const StageDoc& d, const Keys& keysIn) {
    Explore ex;
    ex.F = floorFacts(p, d);
    const FloorFacts& F = ex.F;
    std::map<uint32_t, size_t> index;
    auto mk = [&](uint32_t mask) {
        FloodState s;
        s.mask = mask;
        floodMask(d, F, s);
        for (size_t i = 0; i < F.doors.size() && i < 32; i++) if (mask & (1u << i)) s.opened.n[F.doors[i].col]++;
        for (int c = 0; c < 3; c++) s.left.n[c] = keysIn.n[c] + s.found.n[c] - s.opened.n[c];
        s.up = std::any_of(F.ups.begin(), F.ups.end(), [&](int u) { return s.seen[(size_t)u] != 0; });
        s.n = popcount(mask);
        index[mask] = ex.all.size();
        ex.all.push_back(std::move(s));
    };
    mk(0);
    std::deque<uint32_t> q{0};
    while (!q.empty() && ex.all.size() < 4096) {
        const uint32_t m = q.front();
        q.pop_front();
        const std::vector<int> frontier = ex.all[index[m]].frontier;
        const Keys left = ex.all[index[m]].left;
        for (int i : frontier) {
            if (i > 30 || left.n[F.doors[(size_t)i].col] <= 0) continue;
            const uint32_t nm = m | (1u << i);
            if (!index.count(nm)) { mk(nm); q.push_back(nm); }
        }
    }
    std::vector<int> ups, every;
    for (size_t i = 0; i < ex.all.size(); i++) { every.push_back((int)i); if (ex.all[i].up) ups.push_back((int)i); }
    const auto& A = ex.all;
    std::vector<int> r = ups;
    std::stable_sort(r.begin(), r.end(), [&](int a, int b) {
        if (A[a].n != A[b].n) return A[a].n < A[b].n;
        return A[a].left.total() > A[b].left.total(); });
    ex.route = r.empty() ? -1 : r[0];
    r = ups;
    std::stable_sort(r.begin(), r.end(), [&](int a, int b) {
        if (A[a].left.total() != A[b].left.total()) return A[a].left.total() > A[b].left.total();
        return A[a].seenOrder.size() > A[b].seenOrder.size(); });
    ex.carry = r.empty() ? -1 : r[0];
    r = every;
    std::stable_sort(r.begin(), r.end(), [&](int a, int b) { return A[a].seenOrder.size() > A[b].seenOrder.size(); });
    ex.widest = r.empty() ? -1 : r[0];
    ex.anySeen.assign((size_t)F.W * F.H, 0);
    for (const FloodState& s : ex.all) for (int k : s.seenOrder) ex.anySeen[(size_t)k] = 1;
    return ex;
}

std::vector<std::string> chainOrder(Project& p) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    std::string f = p.floor("F01") ? "F01" : p.floors.front().id;
    while (!f.empty() && p.floor(f) && !seen.count(f)) {
        seen.insert(f);
        out.push_back(f);
        f = str(p.doc(f).spec, "nextFloor");
    }
    for (const Floor& x : p.floors) if (!seen.count(x.id)) out.push_back(x.id);
    return out;
}

namespace {

// event logic (docs/event_editor/06_EVENT_LOGIC.md): what an event needs must be possible on its floor or before
void logicOrderIssues(Project& p, std::vector<StoryRow>& rows) {
    std::map<std::string, int> idxOf;
    auto at = [&](const std::string& id, int i) {
        auto it = idxOf.find(id);
        if (it == idxOf.end() || i < it->second) { idxOf[id] = i; return true; }
        return false;
    };
    for (const StoryRow& r : rows) {
        const StageDoc& d = p.doc(r.fid);
        for (int y = 0; y < d.height(); y++)
            for (int x = 0; x < d.width(); x++) {
                const std::string s2 = p.semAt(d, x, y);
                if (startsWith(s2, "event:")) at(s2.substr(6), r.idx);
                if (startsWith(s2, "npc:")) at("npc:" + s2.substr(4), r.idx);
            }
    }
    auto has = [&](const std::string& id) { return idxOf.count(id) > 0; };
    for (int pass = 0, ch = 1; ch && pass < 20; pass++) {   // talk / linked / variable-fired events appear where what fires them is
        ch = 0;
        for (const Event& e : p.events) {
            const Json* tr = member(e.rec, "trigger");
            const std::string on = tr ? str(*tr, "on") : std::string();
            if (on == "talk" && has("npc:" + str(*tr, "npc"))) ch = at(e.id, idxOf["npc:" + str(*tr, "npc")]) || ch;
            if (on == "varChanged") {
                const std::string var = str(*tr, "var");
                for (const Event& o : p.events) {
                    const Json* acts = member(o.rec, "actions");
                    if (!acts || !acts->is_array() || !has(o.id)) continue;
                    if (std::any_of(acts->begin(), acts->end(), [&](const Json& a) { return str(a, "var") == var && !var.empty(); }))
                        ch = at(e.id, idxOf[o.id]) || ch;
                }
            }
            if (has(e.id))
                if (const Json* nx = member(e.rec, "next"); nx && nx->is_array())
                    for (const Json& n : *nx) if (n.is_string()) ch = at(n.get<std::string>(), idxOf[e.id]) || ch;
        }
    }
    auto rowAt = [&](int i) -> StoryRow* {
        for (StoryRow& r : rows) if (r.idx == i) return &r;
        return nullptr;
    };
    for (const Event& e : p.events) {
        if (!has(e.id)) continue;
        const Json* req = member(e.rec, "requires");
        if (!req || !req->is_object()) continue;
        const int i = idxOf[e.id];
        std::vector<Json> leaves;
        if (req->contains("type")) leaves.push_back(*req);
        else if (const Json* all = member(*req, "all"); all && all->is_array()) leaves.assign(all->begin(), all->end());
        for (const Json& l : leaves) {
            const std::string type = str(l, "type");
            if (type == "runFlagSet" && startsWith(str(l, "flag"), "event_")) {
                const std::string x = str(l, "flag").substr(6);
                const bool known = has(x);
                if (!known || idxOf[x] > i)
                    if (StoryRow* r = rowAt(i)) {
                        StoryRow* rj = known ? rowAt(idxOf[x]) : nullptr;
                        r->issues.push_back(issue(Sev::Warn, "sNeedsEvent", {e.id, x, rj ? rj->fid : std::string()}));
                    }
            }
            if (type == "varAtLeast") {
                const std::string var = str(l, "var");
                double can = 0;
                for (const Event& o : p.events) {
                    std::vector<Json> acts;
                    if (const Json* a = member(o.rec, "actions"); a && a->is_array()) acts.assign(a->begin(), a->end());
                    const Json* tr = member(o.rec, "trigger");
                    if (tr && str(*tr, "on") == "talk" && !str(o.rec, "dialogue").empty()) {
                        auto it = p.dialogueActions.find(str(o.rec, "dialogue"));
                        if (it != p.dialogueActions.end()) acts.insert(acts.end(), it->second.begin(), it->second.end());
                    }
                    for (const Json& a : acts) {
                        if (!a.is_object() || str(a, "var") != var || !has(o.id) || idxOf[o.id] > i) continue;
                        const std::string at2 = str(a, "type");
                        if (at2 == "addVar" && a.contains("delta") && numberOf(a["delta"]) > 0) can += numberOf(a["delta"]);
                        if (at2 == "setVar") {
                            const Json v = a.value("value", Json());
                            can = std::max(can, v.is_boolean() && v.get<bool>() ? 1.0 : numberOf(v));
                        }
                    }
                }
                const double want = numberOf(l.value("value", Json()));
                if (can < want)
                    if (StoryRow* r = rowAt(i)) r->issues.push_back(issue(Sev::Warn, "sNeedsVar", {e.id, var, numText(l.value("value", Json())), numText(Json(can))}));
            }
        }
    }
    for (StoryRow& r : rows) {
        const Json* stairs = member(p.doc(r.fid).spec, "stairs");
        if (!stairs || !stairs->is_array()) continue;
        for (const Json& sp : *stairs) {
            const Json* un = member(sp, "unlock");
            const std::string x = un ? str(*un, "eventDone") : std::string();
            if (x.empty()) continue;
            if (!has(x) || idxOf[x] > r.idx) r.issues.push_back(issue(Sev::Error, "sStairLock", {str(sp, "to"), x}));
        }
    }
}

struct StoryCache {
    const Project* p = nullptr;
    int ver = -1;
    std::vector<StoryRow> rows;
};
StoryCache& storyCache() {
    static StoryCache c;
    return c;
}

}  // namespace

const std::vector<StoryRow>& storyCheck(Project& p) {
    StoryCache& c = storyCache();
    if (c.p == &p && c.ver == p.storyVer) return c.rows;
    std::vector<StoryRow> rows;
    Keys keys;
    const Keys plenty{{99, 99, 99}};
    int idx = 0;
    for (const std::string& fid : chainOrder(p)) {
        const StageDoc& d = p.doc(fid);
        StoryRow row;
        row.fid = fid;
        row.idx = idx++;
        row.keysIn = keys;
        row.ex = explore(p, d, keys);
        const Json* cn = member(d.st, "connect");
        row.top = row.ex.F.ups.empty() && !(cn && truthy(member(*cn, "up")));
        if (!row.ex.F.start) row.issues.push_back(issue(Sev::Error, "sNoStart"));
        else if (!row.top && row.ex.route < 0) {
            const Explore inf = explore(p, d, plenty);
            if (inf.route < 0) row.issues.push_back(issue(Sev::Error, "sUpWalled"));
            else {
                const FloodState& rt = inf.all[(size_t)inf.route];
                for (int c = 0; c < 3; c++)
                    if (rt.opened.n[c] > keys.n[c] + rt.found.n[c])
                        row.issues.push_back(issue(Sev::Error, "sNeedKeys", {num(rt.opened.n[c]), kKeyColors[c], num(keys.n[c]), num(rt.found.n[c])}));
            }
        }
        std::unique_ptr<Explore> inf;
        const int W = d.width();
        for (int y = 0; y < d.height(); y++)
            for (int x = 0; x < W; x++) {
                const std::string s2 = p.semAt(d, x, y);
                if (!startsWith(s2, "event:")) continue;
                const size_t k = (size_t)y * W + x;
                if (!row.ex.anySeen[k]) {
                    if (!inf) inf = std::make_unique<Explore>(explore(p, d, plenty));
                    row.issues.push_back(issue(Sev::Error, inf->anySeen[k] ? "sEvNoKey" : "sEvUnreach", {s2.substr(6)}, {{x, y}}));
                } else if (!row.top && row.ex.hasUp()
                           && std::none_of(row.ex.all.begin(), row.ex.all.end(), [&](const FloodState& s) { return s.up && s.seen[k]; })) {
                    row.issues.push_back(issue(Sev::Warn, "sEvOptional", {s2.substr(6)}, {{x, y}}));
                }
            }
        if (row.ex.carry >= 0) keys = row.ex.all[(size_t)row.ex.carry].left;
        rows.push_back(std::move(row));
    }
    logicOrderIssues(p, rows);
    c.p = &p;
    c.ver = p.storyVer;
    c.rows = std::move(rows);
    return c.rows;
}

// ---- safe cells and auto-placement ---------------------------------------------------------------------------------
// A safe cell is free floor the player reaches on the fewest-doors way up (on the top floor: without any door), not
// next to stairs, the start or a door, so an event there can never cost the key the way up needs.
std::vector<Cell> safeCells(Project& p, const std::string& fid) {
    const std::vector<StoryRow>& rows = storyCheck(p);
    const StoryRow* row = nullptr;
    for (const StoryRow& r : rows) if (r.fid == fid) row = &r;
    if (!row) return {};
    const Explore& ex = row->ex;
    if (ex.all.empty()) return {};
    const FloodState& base = ex.all[(size_t)(ex.route >= 0 ? ex.route : 0)];
    const StageDoc& d = p.doc(fid);
    auto near = [&](int x, int y) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!d.inside(x + dx, y + dy)) continue;
                const char c = d.at(x + dx, y + dy);
                if (c == 'U' || c == 'D' || c == '@' || doorColor(c) >= 0) return true;
            }
        return false;
    };
    std::vector<Cell> out;
    const int W = ex.F.W;
    for (int k : base.seenOrder) {
        const int x = k % W, y = k / W;
        if (d.inside(x, y) && d.at(x, y) == '.' && !near(x, y)) out.push_back({x, y});
    }
    return out;
}

bool isStepEvent(const Project& p, const std::string& id) {
    const Event* e = p.event(id);
    if (!e) return true;
    const Json* tr = member(e->rec, "trigger");
    return !tr || str(*tr, "on") == "step";
}

std::function<double()> mulberry(uint32_t seed) {
    auto s = std::make_shared<uint32_t>(seed);
    return [s]() {
        *s = *s + 0x6D2B79F5u;
        uint32_t x = (*s ^ (*s >> 15)) * (1u | *s);
        x = (x + (x ^ (x >> 7)) * (61u | x)) ^ x;
        return (double)(x ^ (x >> 14)) / 4294967296.0;
    };
}

uint32_t hashStr(const std::string& str) {
    uint32_t h = 2166136261u;
    for (unsigned char c : str) h = (h ^ c) * 16777619u;   // the seeds are ASCII (as the HTML editor's code units)
    return h;
}

// picks spread-out cells: far from other objects, a little random, rooms over corridors
std::optional<Cell> pickCell(const Project& p, const StageDoc& d, const std::vector<Cell>& cands, const std::function<double()>& rng) {
    (void)p;
    if (cands.empty()) return std::nullopt;
    std::vector<Cell> objs;
    for (int y = 0; y < d.height(); y++)
        for (int x = 0; x < d.width(); x++) if (d.at(x, y) != '.' && d.at(x, y) != '#') objs.push_back({x, y});
    auto open = [&](int x, int y) {
        int n = 0;
        static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& dd : dirs) if (d.inside(x + dd[0], y + dd[1]) && d.at(x + dd[0], y + dd[1]) != '#') n++;
        return n;
    };
    std::optional<Cell> best;
    double bs = -1;
    for (const Cell& c : cands) {
        int spread = 10;
        if (!objs.empty()) {
            spread = 1 << 30;
            for (const Cell& o : objs) spread = std::min(spread, std::abs(o.x - c.x) + std::abs(o.y - c.y));
        }
        const double sc = std::min(spread, 8) + (open(c.x, c.y) >= 3 ? 2 : 0) + rng() * 3;
        if (sc > bs) { bs = sc; best = c; }
    }
    return best;
}

AutoPlaceResult autoPlace(Project& p, const std::vector<std::string>& fids, const std::string& mode, const std::string& seed,
                          const std::function<std::string(int)>& labelFor) {
    AutoPlaceResult res;
    int undef = 0;
    const int ver = p.storyVer;   // events do not change keys or doors: the story check from before the loop holds
    for (const std::string& fid : fids) {
        StageDoc& d = p.doc(fid);
        std::vector<std::string> listed;
        if (const Json* evs = member(d.spec, "events"); evs && evs->is_array())
            for (const Json& e : *evs) if (e.is_string() && isStepEvent(p, e.get<std::string>())) listed.push_back(e.get<std::string>());
        auto rng = mulberry(hashStr(seed + ":" + fid));
        auto isPlaced = [&](const std::string& id) { return p.find(d, "event:" + id).has_value(); };
        std::vector<std::string> todo;
        for (const std::string& id : listed) {
            if (!p.event(id)) { undef++; continue; }
            if (mode == "all" || !isPlaced(id)) todo.push_back(id);
        }
        if (todo.empty()) continue;
        p.beginEdit(d);
        if (mode == "all")
            for (int y = 0; y < d.height(); y++)
                for (int x = 0; x < d.width(); x++) {
                    const std::string s2 = p.semAt(d, x, y);
                    if (startsWith(s2, "event:") && std::find(todo.begin(), todo.end(), s2.substr(6)) != todo.end())
                        d.grid[(size_t)y][(size_t)x] = '.';
                }
        std::vector<Cell> cands = safeCells(p, fid);
        int n = 0;
        for (const std::string& id : todo) {
            const std::optional<Cell> cell = pickCell(p, d, cands, rng);
            if (!cell) { res.skipped.push_back(issue(Sev::Warn, "apSkipNoCell", {id, fid})); continue; }
            p.putSem(d, cell->x, cell->y, "event:" + id);
            n++;
            cands.erase(std::remove_if(cands.begin(), cands.end(),
                                       [&](const Cell& c) { return std::abs(c.x - cell->x) + std::abs(c.y - cell->y) <= 1; }), cands.end());
        }
        p.commitEdit(d, labelFor(n));
        p.storyVer = ver;
        res.placed += n;
        if (n) res.floors++;
    }
    if (undef) res.skipped.push_back(issue(Sev::Warn, "apSkipUndef", {num(undef)}));
    p.storyVer = ver + 1;
    return res;
}

}  // namespace stage
