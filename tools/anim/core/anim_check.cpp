// anim_check.cpp -- see anim_check.h.
#include "anim_check.h"

#include "anim_keys.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace animed {

namespace fs = std::filesystem;
using toms::anim::AnimFile;
using toms::anim::Clip;

namespace {

void add(std::vector<Problem>& out, Problem::Level l, int clip, const std::vector<int>& path, const std::string& where,
         const std::string& msg) {
    Problem p;
    p.level = l;
    p.clip = clip;
    p.path = path;
    p.where = where;
    p.message = msg;
    out.push_back(p);
}

bool finite4(const glm::vec4& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w); }

std::string seconds(float t) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3g s", t);
    return buf;
}

// What sprite references resolve to: the file's atlases (by id) and the loaded ones in lookup order.
struct SpriteLookup {
    std::vector<const CheckAtlas*> all;      // every atlas of the file, loaded or not
    std::vector<const CheckAtlas*> loaded;
    bool complete = true;      // every atlas loaded
    bool check() const { return !loaded.empty(); }
    const CheckAtlas* byId(const std::string& id) const {
        for (const CheckAtlas* a : all)
            if (a->id == id) return a;
        return nullptr;
    }
    // The atlases that have the sprite, in lookup order.
    std::vector<const CheckAtlas*> owners(const std::string& s) const {
        std::vector<const CheckAtlas*> out;
        for (const CheckAtlas* a : loaded)
            if (a->atlas->find(s)) out.push_back(a);
        return out;
    }
};

std::string idList(const std::vector<const CheckAtlas*>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); i++) s += (i ? ", " : "") + v[i]->id;
    return s;
}

void checkSprite(const std::string& ref, int clip, const std::vector<int>& path, const std::string& where,
                 const SpriteLookup& atlases, std::vector<Problem>& out) {
    const toms::anim::SpriteRef r = toms::anim::parseSpriteRef(ref);
    if (!r.atlas.empty()) {   // "id:name": that atlas, no fallback
        if (atlases.all.empty()) return;   // "no atlas" is reported once for the file
        const CheckAtlas* a = atlases.byId(r.atlas);
        if (!a) {
            add(out, Problem::Error, clip, path, where,
                "sprite '" + ref + "': no atlas has the id '" + r.atlas + "' (the atlases are: " + idList(atlases.all) + ")");
            return;
        }
        if (!a->atlas) return;   // it did not load: reported for the atlas
        if (!a->atlas->find(r.name)) {
            std::string msg = "sprite '" + ref + "': the atlas '" + r.atlas + "' (" + a->path + ") has no sprite '" + r.name + "'";
            const std::vector<const CheckAtlas*> others = atlases.owners(r.name);
            if (!others.empty()) msg += " (" + idList(others) + " has)";
            add(out, Problem::Error, clip, path, where, msg);
        }
        return;
    }
    if (!atlases.check()) return;
    const std::vector<const CheckAtlas*> owners = atlases.owners(r.name);
    if (owners.empty())
        add(out, Problem::Error, clip, path, where,
            "sprite '" + ref + (atlases.complete ? "' is in no atlas" : "' is in none of the atlases that loaded"));
    else if (owners.size() > 1)
        add(out, Problem::Warning, clip, path, where,
            "sprite '" + ref + "' is in several atlases (" + idList(owners) + ") and the reference does not name one: " +
                owners.front()->id + "'s is used. Pick it in the Sprites dock, or Key > Qualify Sprite References, to store the atlas");
}

void checkNode(const Node& n, int clip, const Clip& c, std::vector<int>& path, const std::string& parentWhere,
               const SpriteLookup& atlases, std::vector<Problem>& out) {
    const std::string where = parentWhere + "/" + (n.name.empty() ? std::string("(unnamed)") : n.name);
    if (n.name.empty()) add(out, Problem::Info, clip, path, where, "node has no name");
    {
        std::set<std::string> used;
        if (!n.sprite.empty()) used.insert(n.sprite);
        for (const auto& k : n.spriteKeys)
            if (!k.v.empty()) used.insert(k.v);
        for (const std::string& s : used) checkSprite(s, clip, path, where, atlases, out);
    }
    for (Channel ch : kAllChannels) {
        if (const int o = overlappingKeys(n, ch))
            add(out, Problem::Warning, clip, path, where,
                std::to_string(o) + " overlapping " + channelName(ch) + " key time(s): the later key is never reached");
        for (float t : keyTimes(n, ch)) {
            if (t < 0 || !std::isfinite(t)) {
                add(out, Problem::Error, clip, path, where, std::string(channelName(ch)) + " key at a negative or invalid time");
                break;
            }
            if (c.length > 0 && t > c.length + kTimeEps) {
                add(out, Problem::Warning, clip, path, where,
                    std::string(channelName(ch)) + " key at " + seconds(t) + ", after the clip's length (" + seconds(c.length) + ")");
                break;
            }
        }
    }
    bool bad = !finite4(glm::vec4(n.pos, n.scale)) || !std::isfinite(n.rot) || !finite4(n.color);
    for (const auto& k : n.posKeys) bad = bad || !std::isfinite(k.v.x) || !std::isfinite(k.v.y);
    for (const auto& k : n.scaleKeys) bad = bad || !std::isfinite(k.v.x) || !std::isfinite(k.v.y);
    for (const auto& k : n.rotKeys) bad = bad || !std::isfinite(k.v);
    for (const auto& k : n.colorKeys) bad = bad || !finite4(k.v);
    if (bad) add(out, Problem::Error, clip, path, where, "a value is not a finite number");
    if (n.hasPivot && (n.pivot.x < -1 || n.pivot.x > 2 || n.pivot.y < -1 || n.pivot.y > 2))
        add(out, Problem::Info, clip, path, where, "pivot lies far outside the sprite");
    for (size_t i = 0; i < n.children.size(); i++) {
        path.push_back((int)i);
        checkNode(n.children[i], clip, c, path, where, atlases, out);
        path.pop_back();
    }
}

}  // namespace

std::vector<Problem> checkAnim(const AnimFile& f, const std::vector<CheckAtlas>& atlases) {
    std::vector<Problem> out;
    SpriteLookup lookup;
    if (atlases.empty())
        add(out, Problem::Warning, -1, {}, "file", "no atlas: sprites cannot be shown or checked (add one in the Atlases list)");
    for (const CheckAtlas& a : atlases) {
        lookup.all.push_back(&a);
        if (a.atlas) lookup.loaded.push_back(&a);
        else {
            lookup.complete = false;
            add(out, Problem::Error, -1, {}, "atlas",
                "the atlas '" + a.id + "' (" + a.path + ") could not be loaded" + (a.error.empty() ? "" : ": " + a.error));
        }
        if (a.absolute)
            add(out, Problem::Warning, -1, {}, "atlas", "absolute atlas path '" + a.path + "': it will be stored relative to the .anim on save");
    }
    if (f.clips.empty()) add(out, Problem::Warning, -1, {}, "file", "the file has no clips");
    std::set<std::string> names;
    for (size_t i = 0; i < f.clips.size(); i++) {
        const Clip& c = f.clips[i];
        const std::string where = c.name.empty() ? std::string("(unnamed clip)") : c.name;
        if (c.name.empty()) add(out, Problem::Error, (int)i, {}, where, "clip has no name");
        else if (!names.insert(c.name).second) add(out, Problem::Error, (int)i, {}, where, "two clips are called '" + c.name + "'");
        if (c.playCount == 0) add(out, Problem::Warning, (int)i, {}, where, "playCount 0: the clip never plays");
        if (c.duration() <= 0) add(out, Problem::Info, (int)i, {}, where, "the clip has no keys and no length (a still pose)");
        std::vector<int> path;
        checkNode(c.root, (int)i, c, path, where, lookup, out);
    }
    return out;
}

const char* levelName(Problem::Level l) { return l == Problem::Error ? "error" : l == Problem::Warning ? "warning" : "info"; }

int countLevel(const std::vector<Problem>& p, Problem::Level l) {
    int n = 0;
    for (const Problem& x : p) n += x.level == l;
    return n;
}

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool isAtlasId(const std::string& id) {
    if (id.empty()) return false;
    for (char c : id)
        if (c == ':' || c == '/' || c == '\\' || c == '=' || std::isspace((unsigned char)c)) return false;
    return true;
}

bool isAbsoluteAtlasPath(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    return p.size() >= 2 && p[1] == ':' && std::isalpha((unsigned char)p[0]);
}

int headlessMain(const std::vector<std::string>& args) {
    if (args.size() < 2 || args[0] != "check") {
        std::fputs("usage: anim_editor --headless check <file.anim> [--atlas [<id>=]<file.atlas>]...\n"
                   "  parses the file, checks sprite references against its atlases and the keys; exit 0 = no errors, 2 = errors\n"
                   "  --atlas (repeatable) replaces the file's atlas list, in lookup order; the id defaults to the file name\n",
                   stderr);
        return 3;
    }
    const std::string file = args[1];
    std::vector<toms::anim::AtlasRef> overrides;
    for (size_t i = 2; i < args.size(); i++) {
        if (args[i] == "--atlas" && i + 1 < args.size()) {
            const std::string a = args[++i];
            toms::anim::AtlasRef r{toms::anim::defaultAtlasId(a), a};
            const size_t eq = a.find('=');
            if (eq != std::string::npos && eq > 0 && isAtlasId(a.substr(0, eq))) r = {a.substr(0, eq), a.substr(eq + 1)};
            for (const toms::anim::AtlasRef& o : overrides)
                if (o.id == r.id) {
                    std::fprintf(stderr, "two atlases have the id '%s': name one with --atlas <id>=<path>\n", r.id.c_str());
                    return 3;
                }
            overrides.push_back(r);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", args[i].c_str());
            return 3;
        }
    }
    std::string text, err;
    if (!readTextFile(file, text)) {
        std::fprintf(stderr, "%s: error: cannot read the file\n", file.c_str());
        return 2;
    }
    AnimFile f;
    if (!toms::anim::parseAnim(text, f, &err)) {
        std::fprintf(stderr, "%s: error: %s\n", file.c_str(), err.c_str());
        return 2;
    }
    // From the command line: as given (relative to the working directory); from the file:
    // relative to the .anim.
    const bool fromArgs = !overrides.empty();
    const std::vector<toms::anim::AtlasRef>& names = fromArgs ? overrides : f.atlases;
    std::vector<toms::AtlasFile> parsed(names.size());
    std::vector<CheckAtlas> refs(names.size());
    int loaded = 0;
    for (size_t i = 0; i < names.size(); i++) {
        CheckAtlas& r = refs[i];
        r.id = names[i].id;
        r.path = names[i].path;
        r.absolute = !fromArgs && isAbsoluteAtlasPath(r.path);
        const fs::path p = fromArgs || r.absolute ? fs::u8path(r.path)
                                                  : (fs::u8path(file).parent_path() / fs::u8path(r.path)).lexically_normal();
        std::string atext, aerr;
        if (!readTextFile(p.u8string(), atext)) r.error = "cannot read " + p.u8string();
        else if (!toms::parseAtlas(atext, parsed[i], &aerr)) r.error = aerr;
        else {
            r.atlas = &parsed[i];
            loaded++;
        }
    }
    const std::vector<Problem> problems = checkAnim(f, refs);
    for (const Problem& p : problems)
        std::printf("%s: %s: %s: %s\n", file.c_str(), levelName(p.level), p.where.c_str(), p.message.c_str());
    const int errors = countLevel(problems, Problem::Error), warnings = countLevel(problems, Problem::Warning);
    std::printf("%s: %zu clip(s), %zu atlas(es), %d error(s), %d warning(s)%s\n", file.c_str(), f.clips.size(), names.size(), errors,
                warnings, loaded ? "" : " (sprites not checked: no atlas loaded)");
    return errors ? 2 : 0;
}

}  // namespace animed
