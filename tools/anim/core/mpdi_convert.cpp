// mpdi_convert.cpp -- see mpdi_convert.h. The rules follow the old engine's reader and renderer:
// FM79979Engine/Core/GameplayUT/Render/MPDI/MPDIDataParse.cpp (ProcessMPDI, GetTexBehaviorDataWithImageIndexData),
// SubMPDI.cpp (UpdateData, GetRenderPuzzleDataAndMatrix), Render/Curve/Curve.cpp (DoLOD) and Glh.cpp
// (IndexToBlendingIndex).
#include "mpdi_convert.h"

#include "anim_clip.h"
#include "atlas_export.h"
#include "atlas_store.h"
#include "atlas_xml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using namespace toms::anim;

namespace animed {
namespace {

std::string u8(const fs::path& p) { return p.u8string(); }

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// "1.0,2.5,3," -> {1, 2.5, 3}
std::vector<float> numbers(const std::string& s) {
    std::vector<float> out;
    std::string cur;
    for (char c : s + ",") {
        if (c == ',' || c == ':') {
            const std::string t = trim(cur);
            if (!t.empty()) out.push_back((float)std::atof(t.c_str()));
            cur.clear();
        } else {
            cur += c;
        }
    }
    return out;
}

std::vector<std::string> fields(const std::string& s) {   // the oldest format's "w,h,angle,r,g,b,a,image,mirror"
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == ':') { out.push_back(trim(cur)); cur.clear(); }
        else cur += c;
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

bool truthy(const std::string& v) { return v == "1" || lower(v) == "true"; }

// The engine's curve LOD: Chaikin corner cutting (Curve.cpp, TemplateDataIncreaseLod), on the points and the times.
template <class T> std::vector<T> chaikin(const std::vector<T>& in) {
    if (in.size() < 2) return in;
    std::vector<T> out{in.front()};
    for (size_t i = 0; i + 1 < in.size(); i++) {
        out.push_back(in[i] * 0.75f + in[i + 1] * 0.25f);
        out.push_back(in[i] * 0.25f + in[i + 1] * 0.75f);
    }
    out.push_back(in.back());
    return out;
}

// The P0, P1, ... attributes of an element, in index order.
std::vector<std::string> indexedAttrs(const atlas::XmlNode& n) {
    std::vector<std::pair<int, std::string>> v;
    for (const auto& [k, val] : n.attrs)
        if (k.size() > 1 && k[0] == 'P' && std::isdigit((unsigned char)k[1])) v.push_back({std::atoi(k.c_str() + 1), val});
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (auto& p : v) out.push_back(p.second);
    return out;
}

struct Point {
    glm::vec3 pos{0.0f};
    float t = 0.0f;
    glm::vec2 size{0.0f};
    bool hasScale = false;
    glm::vec2 scale{1.0f};
    glm::vec3 angle{0.0f};
    glm::vec4 color{1.0f};
    std::string pi, image;
    bool mirror = false;
};

struct Path {
    std::string name;
    float start = 0.0f;
    bool behavior = false, loop = false, blending = false;
    int src = -1, dst = -1;
    glm::vec3 rotOffset{0.0f};
    int lod = 1;
    std::vector<Point> points;
    std::vector<std::pair<int, std::string>> hints;   // point index, event
};

struct PiEntry {
    std::string id;          // the atlas id in the .anim (the .pi's name)
    std::string atlasPath;   // the converted .atlas, absolute
    atlas::BuildResult src;  // the .pi as it was (sizes the MPDI's numbers are relative to)
    bool ok = false;
};

struct Ctx {
    MpdiConvertReport& report;
    std::map<std::string, PiEntry> pis;   // key: the .pi's absolute path + the output folder
};

// A .pi named in the file: next to the .mpdi, with or without ".pi", any letter case.
fs::path findPi(const fs::path& dir, const std::string& name) {
    std::string n = trim(name);
    if (lower(fs::u8path(n).extension().u8string()) != ".pi") n += ".pi";
    const fs::path direct = dir / fs::u8path(n);
    std::error_code ec;
    if (fs::exists(direct, ec)) return direct;
    const std::string want = lower(fs::u8path(n).filename().u8string());
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file() && lower(e.path().filename().u8string()) == want) return e.path();
    return {};
}

PiEntry* piFor(Ctx& ctx, const fs::path& piPath, const fs::path& outDir) {
    const std::string key = u8(fs::absolute(piPath)) + "|" + u8(fs::absolute(outDir));
    auto it = ctx.pis.find(key);
    if (it != ctx.pis.end()) return it->second.ok ? &it->second : nullptr;
    PiEntry& e = ctx.pis[key];
    e.id = fs::u8path(piPath.filename().u8string()).stem().u8string();
    std::string err;
    if (!atlas::importPi(u8(piPath), e.src, &err)) {
        ctx.report.errors.push_back(u8(piPath) + ": " + err);
        return nullptr;
    }
    e.atlasPath = convertPi(u8(piPath), u8(outDir), ctx.report);
    e.ok = !e.atlasPath.empty();
    return e.ok ? &e : nullptr;
}

}  // namespace

std::string convertPi(const std::string& piPath, const std::string& outDir, MpdiConvertReport& report) {
    atlas::BuildResult in;
    std::string err;
    if (!atlas::importPi(piPath, in, &err)) {
        report.errors.push_back(piPath + ": " + err);
        return {};
    }
    const fs::path dir = outDir.empty() ? fs::u8path(piPath).parent_path() : fs::u8path(outDir);
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string stem = fs::u8path(piPath).stem().u8string();
    const fs::path proj = dir / fs::u8path(stem + ".atlasproj");
    atlas::Project p;
    if (!atlas::importToProject(in, u8(proj), "", p, &err)) {
        report.errors.push_back(piPath + ": " + err);
        return {};
    }
    p.output.formats = {"atlas", "plist"};
    atlas::SaveResult sr;
    if (!atlas::saveProjectAll(u8(proj), p, {"plist"}, &sr, &err)) {
        report.errors.push_back(piPath + ": " + err);
        return {};
    }
    report.piFiles++;
    report.written.push_back(u8(proj));
    for (const std::string& w : sr.written) report.written.push_back(w);
    for (const atlas::Diagnostic& d : sr.diagnostics)
        if (d.level != atlas::Diagnostic::Info) report.warnings.push_back(piPath + ": " + d.sprite + ": " + d.message);
    const std::string name = p.output.name.empty() ? p.name : p.output.name;
    return u8((proj.parent_path() / fs::u8path(p.output.dir) / fs::u8path(name + ".atlas")).lexically_normal());
}

static bool convertMpdiWith(Ctx& ctx, const std::string& mpdiPath, const std::string& outDirIn);

bool convertMpdiFile(const std::string& mpdiPath, const std::string& outDir, MpdiConvertReport& report) {
    Ctx ctx{report, {}};   // one call, one cache of converted sheets (convertMpdiFolder shares one across files)
    return convertMpdiWith(ctx, mpdiPath, outDir);
}

static bool convertMpdiWith(Ctx& ctx, const std::string& mpdiPath, const std::string& outDirIn) {
    MpdiConvertReport& R = ctx.report;
    const fs::path src = fs::u8path(mpdiPath);
    const fs::path outDir = outDirIn.empty() ? src.parent_path() : fs::u8path(outDirIn);
    const std::string where = u8(src);
    auto warn = [&](const std::string& w) {
        const std::string line = where + ": " + w;
        if (std::find(R.warnings.begin(), R.warnings.end(), line) == R.warnings.end()) R.warnings.push_back(line);
    };
    std::string text;
    if (!readFile(src, text)) { R.errors.push_back(where + ": cannot read the file"); return false; }
    std::string err;
    std::unique_ptr<atlas::XmlNode> doc = atlas::parseXml(text, &err);
    const atlas::XmlNode* root = doc.get();
    std::string versionText;
    if (!root || root->name != "MPDIList") {
        // MPDI 1.0 files start with <Version MPDIVersion="1.0" /> and then <MPDIList>: two top-level elements, which
        // the old engine's reader took. Read them inside a made-up root.
        std::string body = text;
        if (body.compare(0, 3, "\xEF\xBB\xBF") == 0) body.erase(0, 3);
        const size_t decl = body.find("?>");
        if (body.compare(0, 5, "<?xml") == 0 && decl != std::string::npos) body.erase(0, decl + 2);
        std::string err2;
        doc = atlas::parseXml("<mpdi_file>" + body + "</mpdi_file>", &err2);
        root = nullptr;
        if (doc)
            for (const auto& c : doc->children) {
                if (c->name == "MPDIList" && !root) root = c.get();
                if (c->name == "Version") versionText = c->attrOr("MPDIVersion");
            }
        if (!root) {
            R.errors.push_back(where + ": not an MPDI file" + (err.empty() ? std::string() : " (" + err + ")"));
            return false;
        }
    }
    if (const std::string* v = root->attr("MPDIVersion")) versionText = *v;
    const float version = (float)std::atof(versionText.empty() ? "0" : versionText.c_str());
    const bool centred = version <= 1.0f + 1e-4f;          // 1.0 and older store the image centre
    const bool elementPoints = version >= 1.0f - 1e-4f;    // one PointDataList element per point

    // The sprite sheets.
    std::vector<std::string> piNames;
    for (const std::string& n : fields(root->attrOr("ImageName"))) if (!n.empty()) piNames.push_back(n);
    std::map<std::string, PiEntry*> piById;   // lower-case id -> entry
    PiEntry* firstPi = nullptr;
    for (const std::string& n : piNames) {
        const fs::path pp = findPi(src.parent_path(), n);
        if (pp.empty()) { R.errors.push_back(where + ": the sprite sheet " + n + " is not next to it"); return false; }
        PiEntry* e = piFor(ctx, pp, outDir);
        if (!e) { R.errors.push_back(where + ": " + n + " could not be converted"); return false; }
        piById[lower(e->id)] = e;
        if (!firstPi) firstPi = e;
    }
    if (!firstPi) { R.errors.push_back(where + ": no sprite sheet (ImageName) named"); return false; }
    auto pick = [&](const std::string& piName) -> PiEntry* {
        if (piName.empty()) return firstPi;
        auto it = piById.find(lower(fs::u8path(trim(piName)).stem().u8string()));
        if (it != piById.end()) return it->second;
        // a sheet used by a point but not listed in ImageName: next to the .mpdi
        const fs::path pp = findPi(src.parent_path(), piName);
        if (!pp.empty())
            if (PiEntry* e = piFor(ctx, pp, outDir)) { piById[lower(e->id)] = e; return e; }
        warn("the sprite sheet " + piName + " is missing; the first sheet is used");
        return firstPi;
    };

    AnimFile file;
    std::set<std::string> atlasIds;
    auto useAtlas = [&](PiEntry* e) {
        if (atlasIds.insert(e->id).second) {
            std::error_code ec;
            fs::path rel = fs::relative(fs::u8path(e->atlasPath), outDir, ec);
            file.atlases.push_back({e->id, (ec ? fs::u8path(e->atlasPath) : rel).generic_u8string()});
        }
    };
    for (const auto& [id, e] : piById) useAtlas(e);

    for (const auto& clipNode : root->children) {
        if (clipNode->name != "MultiPathDynamicImage") continue;
        Clip clip;
        clip.name = clipNode->attrOr("Name", "clip" + std::to_string(file.clips.size()));
        clip.length = (float)std::atof(clipNode->attrOr("TotalPlayTime", "0").c_str());
        const int pc = std::atoi(clipNode->attrOr("PlayCount", "1").c_str());
        clip.playCount = pc == 0 ? 1 : pc;
        clip.stayAtLastFrame = clipNode->attr("StayAtLastFrame") || clipNode->attr("StopAtLastFrame");
        if (const std::string* cv = clipNode->attr("CameraView")) {
            const std::vector<float> m = numbers(*cv);
            bool identity = m.size() == 16;
            for (int i = 0; identity && i < 16; i++) identity = std::fabs(m[(size_t)i] - ((i % 5 == 0) ? 1.0f : 0.0f)) < 1e-4f;
            if (!identity) warn("clip " + clip.name + ": a CameraView transform is not converted");
        }
        if (clipNode->attr("ViewPort")) warn("clip " + clip.name + ": its ViewPort clipping is not converted");
        clip.root.name = clip.name;
        std::set<std::string> names;

        for (const auto& pn : clipNode->children) {
            if (pn->name != "CueToStartCurvesWithTime" && pn->name != "CueToStart2DCurvesWithTime") continue;
            Path path;
            path.name = pn->attrOr("Name", "path" + std::to_string(clip.root.children.size()));
            path.start = (float)std::atof(pn->attrOr("StartTime", "0").c_str());
            path.behavior = pn->attr("BehaviorUpdate") != nullptr;
            path.loop = truthy(pn->attrOr("Loop", "0"));
            path.blending = pn->attr("Blending") != nullptr;
            path.src = std::atoi(pn->attrOr("SrcBlendingData", "-1").c_str());
            path.dst = std::atoi(pn->attrOr("DestBlendingData", "-1").c_str());
            path.lod = std::max(1, std::atoi(pn->attrOr("LOD", "1").c_str()));
            if (const std::string* r = pn->attr("RotationPosOffset")) {
                const std::vector<float> v = numbers(*r);
                if (v.size() >= 2) path.rotOffset = {v[0], v[1], v.size() > 2 ? v[2] : 0.0f};
            }
            const std::string anchor = pn->attrOr("AnchorType", "CenterCenter");
            static const char* const kAnchors[] = {"LeftUp", "CenterUp", "RightUp", "LeftCenter", "CenterCenter", "RightCenter",
                                                   "LeftDown", "CenterDown", "RightDown"};
            int anchorIndex = 4;
            for (int k = 0; k < 9; k++) if (anchor == kAnchors[k]) anchorIndex = k;
            if (anchorIndex == 4 && anchor != "CenterCenter") warn("path " + path.name + ": unknown anchor " + anchor + " is drawn centred");
            // Points.
            std::vector<glm::vec3> oldPos;
            std::vector<float> oldTime;
            for (const auto& c : pn->children) {
                if (c->name == "CurvePoint") {
                    for (const std::string& v : indexedAttrs(*c)) {
                        const std::vector<float> f = numbers(v);
                        oldPos.push_back({f.size() > 0 ? f[0] : 0.0f, f.size() > 1 ? f[1] : 0.0f, f.size() > 2 ? f[2] : 0.0f});
                    }
                } else if (c->name == "CurveTime") {
                    for (const std::string& v : indexedAttrs(*c)) oldTime.push_back((float)std::atof(v.c_str()));
                } else if (c->name == "PointDataList") {
                    if (elementPoints) {
                        Point p;
                        const std::vector<float> pos = numbers(c->attrOr("Position", "0,0,0"));
                        p.pos = {pos.size() > 0 ? pos[0] : 0.0f, pos.size() > 1 ? pos[1] : 0.0f, pos.size() > 2 ? pos[2] : 0.0f};
                        p.t = (float)std::atof(c->attrOr("Time", "0").c_str());
                        const std::vector<float> size = numbers(c->attrOr("Size", "0,0"));
                        if (size.size() >= 2) p.size = {size[0], size[1]};
                        if (const std::string* s = c->attr("Scale")) {
                            const std::vector<float> v = numbers(*s);
                            if (v.size() >= 2) { p.hasScale = true; p.scale = {v[0], v[1]}; }
                        }
                        if (const std::string* a = c->attr("Angle")) p.angle.z = (float)std::atof(a->c_str());
                        if (const std::string* a = c->attr("Angles")) {
                            const std::vector<float> v = numbers(*a);
                            if (v.size() >= 3) p.angle = {v[0], v[1], v[2]};
                        }
                        if (const std::string* col = c->attr("Color")) {
                            const std::vector<float> v = numbers(*col);
                            if (v.size() >= 4) p.color = {v[0], v[1], v[2], v[3]};
                        }
                        p.pi = c->attrOr("PIName");
                        p.image = c->attrOr("ImageName");
                        p.mirror = truthy(c->attrOr("Mirror", "0"));
                        path.points.push_back(p);
                    } else {   // the oldest format: "w,h,angle,r,g,b,a,image[,mirror]" per point, positions elsewhere
                        for (const std::string& v : indexedAttrs(*c)) {
                            const std::vector<std::string> f = fields(v);
                            Point p;
                            if (f.size() >= 8) {
                                p.size = {(float)std::atof(f[0].c_str()), (float)std::atof(f[1].c_str())};
                                p.angle.z = (float)std::atof(f[2].c_str());
                                p.color = glm::vec4((float)std::atoi(f[3].c_str()), (float)std::atoi(f[4].c_str()),
                                                    (float)std::atoi(f[5].c_str()), (float)std::atoi(f[6].c_str())) / 255.0f;
                                p.image = f[7];
                                p.mirror = f.size() > 8 && std::atoi(f[8].c_str()) != 0;
                            }
                            path.points.push_back(p);
                        }
                    }
                } else if (c->name == "HintPoint") {
                    const int index = std::atoi(c->attrOr("PointIndex", "-1").c_str());
                    const int event = std::atoi(c->attrOr("Event", "0").c_str());
                    const std::string name = c->attrOr("EventName");
                    if (index >= 0) path.hints.push_back({index, (event == 2 ? "sound:" : "") + name});   // 2 = eAE_SOUND
                }
            }
            if (!elementPoints)
                for (size_t i = 0; i < path.points.size(); i++) {
                    if (i < oldPos.size()) path.points[i].pos = oldPos[i];
                    if (i < oldTime.size()) path.points[i].t = oldTime[i];
                }
            if (path.points.empty()) { warn("path " + path.name + ": no points (skipped)"); continue; }

            // Each point's sprite: its sheet's region gives the trimmed size (what Size scales) and the original size.
            struct Resolved { std::string ref; glm::vec2 frame{1.0f}, orig{1.0f}, trim{0.0f}; };
            std::vector<Resolved> res;
            for (const Point& p : path.points) {
                PiEntry* e = pick(p.pi);
                useAtlas(e);
                Resolved r;
                r.ref = spriteRef(e->id, p.image);
                if (const atlas::Region* g = e->src.find(p.image)) {
                    r.frame = {std::max(1, g->frame.w), std::max(1, g->frame.h)};
                    r.orig = {std::max(1, g->origW), std::max(1, g->origH)};
                    r.trim = {g->offsetX, g->offsetY};
                } else {
                    warn("path " + path.name + ": the image " + p.image + " is not in " + e->id);
                    r.frame = r.orig = glm::max(p.size, glm::vec2(1.0f));
                }
                res.push_back(r);
            }

            // ---- the node(s) ----
            std::string nodeName = path.name;
            for (int k = 2; !names.insert(nodeName).second; k++) nodeName = path.name + "_" + std::to_string(k);
            Node image;
            image.name = nodeName;
            image.sprite = res[0].ref;
            image.loop = path.loop;
            image.stayAtLastFrame = clip.stayAtLastFrame;
            if (path.blending) {
                // 1.3 and later store an index (Glh.cpp IndexToBlendingIndex), older files the GL enum itself.
                // Destination GL_ONE, or GL_DST_ALPHA (which is 1 on the opaque screen the games drew on): additive.
                const int dst = version >= 1.3f - 1e-4f ? (path.dst == 1 ? 1 : path.dst == 6 ? 771 : path.dst == 7 ? 772 : path.dst + 10000)
                                                        : path.dst;
                const bool add = dst == 1 || dst == 772;
                const bool normal = dst == 771 || path.dst < 0;
                if (add) image.blend = Blend::Add;
                else if (!normal) warn("path " + path.name + ": blending " + std::to_string(path.src) + "/" + std::to_string(path.dst) +
                                       " is drawn as normal alpha blending");
            }
            Ease linear, stepped;
            parseEase("linear", linear);
            parseEase("stepped", stepped);
            const Ease& dataEase = path.behavior ? linear : stepped;
            const size_t n = path.points.size();
            std::vector<float> times(n);
            std::vector<glm::vec2> centres(n);
            for (size_t i = 0; i < n; i++) {
                times[i] = path.start + path.points[i].t;
                const glm::vec2 p(path.points[i].pos);
                centres[i] = centred ? p : p + res[i].orig * 0.5f;
            }
            // Position: the engine's LOD smoothing, then linear between the (smoothed) points.
            std::vector<glm::vec2> lodPos = centres;
            std::vector<float> lodTime = times;
            for (int k = 1; k < path.lod; k++) {
                lodPos = chaikin(lodPos);
                lodTime = chaikin(lodTime);
            }
            std::vector<Key<glm::vec3>> posKeys;
            for (size_t i = 0; i < lodPos.size(); i++) posKeys.push_back({lodTime[i], glm::vec3(lodPos[i], 0.0f), linear});
            std::vector<Key<glm::vec3>> rotKeys;
            std::vector<Key<glm::vec3>> scaleKeys;
            std::vector<Key<glm::vec4>> colorKeys;
            bool anyRot = false, anyScale = false, anyColor = false;
            for (size_t i = 0; i < n; i++) {
                const Point& p = path.points[i];
                glm::vec2 s = p.hasScale ? p.scale : p.size / res[i].frame;
                if (p.mirror) s.x = -s.x;
                // x, y, z as they are: the runtime's rotation order is the engine's (Rx * Ry * Rz)
                rotKeys.push_back({times[i], glm::vec3(p.angle), dataEase});
                scaleKeys.push_back({times[i], glm::vec3(s, 1.0f), dataEase});
                colorKeys.push_back({times[i], p.color, dataEase});
                anyRot |= glm::vec3(p.angle) != glm::vec3(0.0f);
                anyScale |= std::fabs(s.x - 1.0f) > 1e-4f || std::fabs(s.y - 1.0f) > 1e-4f;
                anyColor |= p.color != glm::vec4(1.0f);
            }
            if (anyScale) image.scaleKeys = scaleKeys;
            if (anyColor) image.colorKeys = colorKeys;
            for (size_t i = 0; i < n; i++)
                if (i == 0 ? res.size() > 1 && std::any_of(res.begin(), res.end(), [&](const Resolved& r) { return r.ref != res[0].ref; })
                           : res[i].ref != res[i - 1].ref)
                    image.spriteKeys.push_back({times[i], res[i].ref, stepped});
            for (const auto& [index, event] : path.hints)
                if (index >= 0 && index < (int)n) image.eventKeys.push_back({times[(size_t)index], event, stepped});

            // A non-centred anchor (SubMPDI.cpp GetRenderPuzzleDataAndMatrix): the drawn rectangle is the anchor's
            // rectangle of the drawn size, moved by the trim offset from the image centre (unscaled there), about the
            // point it turns around. Our sprite draws its whole original image centred and scaled, so the image sits
            // at anchorRect.xy + centreToTrim * (1 - scale) from that point.
            std::vector<Key<glm::vec3>> imageOffsetKeys;
            if (anchorIndex != 4) {
                for (size_t i = 0; i < n; i++) {
                    const Point& p = path.points[i];
                    const glm::vec2 drawn = p.hasScale ? p.scale * res[i].frame : p.size;
                    const glm::vec2 sc = drawn / res[i].frame;
                    const int ax = anchorIndex % 3, ay = anchorIndex / 3;   // 0 left / up, 1 centre, 2 right / down
                    const glm::vec2 rectXY(ax == 0 ? 0.0f : ax == 1 ? -drawn.x * 0.5f : -drawn.x,
                                           ay == 0 ? 0.0f : ay == 1 ? -drawn.y * 0.5f : -drawn.y);
                    const glm::vec2 centreToTrim = res[i].trim - res[i].orig * 0.5f;
                    // where our centred image's centre must be, from the turning point
                    const glm::vec2 offset = rectXY + centreToTrim * (glm::vec2(1.0f) - sc);
                    imageOffsetKeys.push_back({times[i], glm::vec3(offset, 0.0f) + glm::vec3(path.rotOffset), dataEase});
                }
            }
            Node result;
            if (path.rotOffset == glm::vec3(0.0f) && imageOffsetKeys.empty()) {
                image.posKeys = posKeys;
                if (anyRot) image.rotKeys = rotKeys;
                image.pos = posKeys.front().v;
                result = image;
            } else {   // the image sits rotOffset from the point it turns about: a turning group with the image inside
                Node group;
                group.name = nodeName + "_pivot";
                group.posKeys = posKeys;
                if (anyRot) group.rotKeys = rotKeys;
                group.pos = posKeys.front().v;
                group.loop = path.loop;
                group.stayAtLastFrame = clip.stayAtLastFrame;
                image.loop = false;
                image.pos = glm::vec3(path.rotOffset);
                if (!imageOffsetKeys.empty()) {
                    image.posKeys = imageOffsetKeys;
                    image.pos = imageOffsetKeys.front().v;
                }
                group.children.push_back(image);
                result = group;
            }
            if (path.start > 0.0f) {   // nothing shows before the path starts
                if (result.loop) {     // looping from its start, not from 0: a group holds the delay
                    Node gate;
                    gate.name = nodeName + "_start";
                    gate.visibleKeys = {{0.0f, false, stepped}, {path.start, true, stepped}};
                    gate.children.push_back(result);
                    result = gate;
                } else {
                    result.visibleKeys = {{0.0f, false, stepped}, {path.start, true, stepped}};
                }
            }
            clip.root.children.push_back(result);
            R.nodes++;
        }
        file.clips.push_back(clip);
        R.clips++;
    }
    if (file.clips.empty()) { R.errors.push_back(where + ": no animations (MultiPathDynamicImage) in it"); return false; }

    std::error_code ec;
    fs::create_directories(outDir, ec);
    const fs::path animPath = outDir / fs::u8path(src.stem().u8string() + ".anim");
    std::ofstream o(animPath, std::ios::binary);
    if (!o) { R.errors.push_back(where + ": cannot write " + u8(animPath)); return false; }
    o << animToJson(file);
    o.close();
    R.animFiles++;
    R.written.push_back(u8(animPath));
    return true;
}

MpdiConvertReport convertMpdiFolder(const std::string& folder, const std::string& outDir) {
    MpdiConvertReport report;
    Ctx ctx{report, {}};
    std::error_code ec;
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(fs::u8path(folder), ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && lower(it->path().extension().u8string()) == ".mpdi") files.push_back(it->path());
    std::sort(files.begin(), files.end());
    report.mpdiFiles = (int)files.size();
    for (const fs::path& f : files) {
        std::string out;
        if (!outDir.empty()) {
            const fs::path rel = fs::relative(f.parent_path(), fs::u8path(folder), ec);
            out = u8((fs::u8path(outDir) / rel).lexically_normal());
        }
        convertMpdiWith(ctx, u8(f), out);
    }
    return report;
}

}  // namespace animed
