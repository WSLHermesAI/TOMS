// resource.cpp -- see resource.h.
#include "resource.h"

#include "resource_types.h"
#include "vfs.h"

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#if !defined(__ANDROID__)
#  include <filesystem>
#  include <system_error>
#endif

namespace toms::res {

namespace {
std::string fold(const std::string& path, bool lower) {
    std::vector<std::string> parts;
    std::string seg;
    auto flush = [&] {
        if (seg == ".." && !parts.empty() && parts.back() != "..") parts.pop_back();
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        seg.clear();
    };
    for (char c : path) {
        if (c == '/' || c == '\\') flush();
        else seg += lower ? (char)std::tolower((unsigned char)c) : c;
    }
    flush();
    std::string out = !path.empty() && (path[0] == '/' || path[0] == '\\') ? "/" : "";
    for (size_t i = 0; i < parts.size(); i++) out += (i ? "/" : "") + parts[i];
    return out;
}
}  // namespace

std::string ResourceManager::key(const std::string& path) { return fold(path, true); }

namespace {
bool isAbsolute(const std::string& p) {
    return !p.empty() && (p[0] == '/' || p[0] == '\\' || p[0] == '@' || (p.size() > 1 && p[1] == ':'));
}
}  // namespace

// ---- names: the manifest and the root folder ------------------------------------------------------------------------

void ResourceManager::setRoot(const std::string& dir) { root_ = dir.empty() ? std::string() : normalize(dir) + "/"; }

std::string ResourceManager::resolve(const std::string& name) const {
    auto m = manifest_.find(name);
    std::string p = normalize(m != manifest_.end() ? m->second.path : name);
    // under the root, unless it is absolute or already there (a dependency's path, made from a resolved one)
    if (!root_.empty() && !isAbsolute(p) && p.compare(0, root_.size(), root_) != 0) p = root_ + p;
    return p;
}

bool ResourceManager::loadManifest(const std::string& path, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        if (onMissing_) onMissing_(path, resolve(path), why);
        std::fprintf(stderr, "[res] manifest %s: %s\n", resolve(path).c_str(), why.c_str());
        return false;
    };
    std::string text;
    if (!toms::vfsReadAll(resolve(path), text)) return fail("cannot read");
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail("not valid JSON");
    auto res = j.find("resources");
    if (res == j.end() || !res->is_object()) return fail("no \"resources\" object");
    const std::string dir = dirOf(path);   // entries are relative to the manifest
    for (auto& [k, v] : res->items()) {
        if (!v.is_object() || !v.contains("path") || !v["path"].is_string()) {
            std::fprintf(stderr, "[res] manifest %s: '%s' has no \"path\"\n", resolve(path).c_str(), k.c_str());
            continue;
        }
        ManifestEntry e;
        e.type = v.value("type", std::string());
        e.path = join(dir, v["path"].get<std::string>());
        e.preload = v.value("preload", false);
        manifest_[k] = std::move(e);
    }
    return true;
}

const ResourceManager::ManifestEntry* ResourceManager::entry(const std::string& key) const {
    auto it = manifest_.find(key);
    return it == manifest_.end() ? nullptr : &it->second;
}

std::vector<std::string> ResourceManager::keys() const {
    std::vector<std::string> out;
    for (const auto& [k, e] : manifest_) out.push_back(k);
    std::sort(out.begin(), out.end());
    return out;
}

std::string ResourceManager::manifestType(const char* staticType) {
    static const std::pair<const char*, const char*> kTypes[] = {{"TextureRes", "texture"}, {"AtlasRes", "atlas"},
                                                                 {"AnimRes", "anim"},       {"ParticleRes", "particle"},
                                                                 {"FontRes", "font"},       {"JsonRes", "json"}};
    for (const auto& [cls, name] : kTypes)
        if (std::string(staticType) == cls) return name;
    return std::string();
}

// The cache's key: the type and the folded path, relative paths made absolute against the working folder, so
// "media/atlas/fx.atlas" and "D:/.../assets/media/atlas/fx.atlas" are one atlas.
std::string ResourceManager::cacheKey(const char* type, const std::string& path) {
    std::string p = path;
#if !defined(__ANDROID__)
    const bool absolute = !p.empty() && (p[0] == '/' || p[0] == '\\' || p[0] == '@' || (p.size() > 1 && p[1] == ':'));
    if (!absolute) {
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec) p = toms::vfsUtf8(cwd) + "/" + p;
    }
#endif
    return std::string(type) + "|" + key(p);
}
std::string ResourceManager::normalize(const std::string& path) { return fold(path, false); }

std::string ResourceManager::dirOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string ResourceManager::join(const std::string& dir, const std::string& rel) {
    const bool absolute = !rel.empty() && (rel[0] == '/' || rel[0] == '\\' || (rel.size() > 1 && rel[1] == ':'));
    return absolute ? rel : dir + rel;
}

ResourceManager::ResourceManager(IRenderer* ren) : link_(std::make_shared<GpuLink>()) { link_->ren = ren; }

ResourceManager::~ResourceManager() {
    // Dependencies first would not matter: each resource frees its own GPU handle while the link is still set.
    cache_.clear();
    link_->ren = nullptr;
}

void ResourceManager::detachRenderer() {
    for (auto& [k, e] : cache_)
        if (auto* t = dynamic_cast<TextureRes*>(e.res.get())) t->releaseGpu();
    link_->ren = nullptr;
}

std::shared_ptr<Resource> ResourceManager::findImpl(const char* type, const std::string& name) const {
    auto it = cache_.find(cacheKey(type, mapped(resolve(name))));
    return it == cache_.end() ? nullptr : it->second.res;
}

std::shared_ptr<Resource> ResourceManager::getImpl(const char* type, const std::string& name,
                                                  const std::function<std::shared_ptr<Resource>()>& make) {
    const std::string real = mapped(resolve(name));
    const std::string k = cacheKey(type, real);
    if (auto it = cache_.find(k); it != cache_.end()) {
        it->second.lastUse = ++clock_;
        return it->second.res;
    }
    std::shared_ptr<Resource> r = make();
    r->path_ = normalize(real);
    r->SetName(r->path_);
    Entry e;
    e.stamp = currentStamp();
    e.file = statFile(r->path_);
    std::string err;
    const ManifestEntry* m = entry(name);
    const std::string want = manifestType(type);
    if (m && !m->type.empty() && m->type != want) {
        err = "'" + name + "' is a " + m->type + " in the manifest, not a " + (want.empty() ? std::string(type) : want);
    } else if (r->load(*this, err)) {   // may get<> its dependencies: they are cached before it
        r->state_ = Resource::State::Ready;
    }
    if (r->state_ != Resource::State::Ready) {
        r->state_ = Resource::State::Failed;
        r->error_ = err.empty() ? "cannot read" : err;
        std::fprintf(stderr, "[res] %s %s: %s\n", type, r->path_.c_str(), r->error_.c_str());
        if (onMissing_) onMissing_(name, r->path_, r->error_);
    }
    e.res = r;
    e.lastUse = ++clock_;
    cache_[k] = std::move(e);
    trim();
    return r;
}

void ResourceManager::provideImpl(const std::shared_ptr<Resource>& r, const std::string& path, bool fresh) {
    if (fresh) {
        r->path_ = normalize(path);
        r->SetName(r->path_);
        Entry e;
        e.res = r;
        e.provided = true;
        e.lastUse = ++clock_;
        cache_[cacheKey(r->Type(), path)] = std::move(e);
    } else {
        r->version_++;
    }
    r->state_ = Resource::State::Ready;
    r->error_.clear();
}

// ---- memory ------------------------------------------------------------------------------------------------------

void ResourceManager::setBudget(size_t bytes) {
    budget_ = bytes;
    trim();
}

size_t ResourceManager::totalBytes() const {
    size_t n = 0;
    for (const auto& [k, e] : cache_) n += e.res->bytes();
    return n;
}

size_t ResourceManager::unusedBytes() const {
    size_t n = 0;
    for (const auto& [k, e] : cache_)
        if (!e.provided && e.res.use_count() == 1) n += e.res->bytes();
    return n;
}

// Freeing a clip file can leave its atlases unused, and those its textures: each pass looks again until nothing changes.
size_t ResourceManager::releaseUnused() {
    size_t freed = 0;
    for (bool again = true; again;) {
        again = false;
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (!it->second.provided && it->second.res.use_count() == 1) {
                freed += it->second.res->bytes();
                it = cache_.erase(it);
                again = true;
            } else {
                ++it;
            }
        }
    }
    return freed;
}

void ResourceManager::trim() {
    while (unusedBytes() > budget_) {
        auto oldest = cache_.end();
        for (auto it = cache_.begin(); it != cache_.end(); ++it)
            if (!it->second.provided && it->second.res.use_count() == 1 && (oldest == cache_.end() || it->second.lastUse < oldest->second.lastUse))
                oldest = it;
        if (oldest == cache_.end()) return;
        cache_.erase(oldest);
    }
}

// ---- stamps ------------------------------------------------------------------------------------------------------

int ResourceManager::pushStamp(const std::string& label) {
    stamps_.push_back({nextStamp_++, label});
    return stamps_.back().id;
}

void ResourceManager::popStamp(int stamp) {
    auto pos = std::find_if(stamps_.begin(), stamps_.end(), [&](const Stamp& s) { return s.id == stamp; });
    if (pos == stamps_.end()) return;
    std::vector<int> ending;
    for (auto it = pos; it != stamps_.end(); ++it) ending.push_back(it->id);
    stamps_.erase(pos, stamps_.end());
    const int outer = currentStamp();
    auto inEnding = [&](int s) { return std::find(ending.begin(), ending.end(), s) != ending.end(); };
    size_t freed = 0, kept = 0;
    for (bool again = true; again;) {
        again = false;
        for (auto it = cache_.begin(); it != cache_.end();) {
            Entry& e = it->second;
            if (!e.provided && inEnding(e.stamp) && e.res.use_count() == 1) {
                freed++;
                it = cache_.erase(it);
                again = true;
            } else {
                ++it;
            }
        }
    }
    for (auto& [k, e] : cache_)
        if (inEnding(e.stamp)) { e.stamp = outer; kept++; }
    if (freed || kept) std::fprintf(stderr, "[res] stamp %d ended: %zu freed, %zu still held\n", stamp, freed, kept);
}

void ResourceManager::keep(const std::shared_ptr<Resource>& r) {
    for (auto& [k, e] : cache_)
        if (e.res == r) e.stamp = 0;
}

// ---- reloading ---------------------------------------------------------------------------------------------------

ResourceManager::FileStamp ResourceManager::statFile(const std::string& path) {
    FileStamp s;
#if !defined(__ANDROID__)   // APK entries never change while the game runs
    if (path.empty() || path[0] == '@') return s;
    std::error_code ec;
    const std::filesystem::path p = toms::vfsPath(path);
    const auto t = std::filesystem::last_write_time(p, ec);
    if (ec) return s;
    const auto n = std::filesystem::file_size(p, ec);
    if (ec) return s;
    s.time = (long long)t.time_since_epoch().count();
    s.size = (long long)n;
#else
    (void)path;
#endif
    return s;
}

// Only the resource is used after load(): a load that get<>s a new dependency can rehash the map and move `e`.
bool ResourceManager::reload(Entry& e) {
    std::string err;
    Resource& r = *e.res;
    if (!r.load(*this, err)) {   // the old contents stay
        std::fprintf(stderr, "[res] reload %s: %s (kept the old contents)\n", r.path_.c_str(), err.empty() ? "cannot read" : err.c_str());
        return false;
    }
    r.state_ = Resource::State::Ready;
    r.error_.clear();
    r.version_++;
    std::fprintf(stderr, "[res] reloaded %s\n", r.path_.c_str());
    return true;
}

int ResourceManager::checkFiles() {
    int n = 0;
    std::vector<std::string> keys;   // reloading may add entries (a new atlas page): walk a copy of the keys
    keys.reserve(cache_.size());
    for (const auto& [k, e] : cache_) keys.push_back(k);
    for (const std::string& k : keys) {
        auto it = cache_.find(k);
        if (it == cache_.end() || it->second.provided) continue;
        const FileStamp now = statFile(it->second.res->path_);
        if (now.size < 0 || now == it->second.file) continue;
        it->second.file = now;
        if (reload(it->second)) n++;
    }
    return n;
}

bool ResourceManager::refresh(const std::shared_ptr<Resource>& r) {
    for (auto& [k, e] : cache_) {
        if (e.res != r || e.provided) continue;
        const FileStamp now = statFile(r->path_);
        if (now.size < 0 || now == e.file) return false;
        e.file = now;
        return reload(e);
    }
    return false;
}

std::vector<ResourceManager::Info> ResourceManager::report() const {
    std::vector<Info> out;
    for (const auto& [k, e] : cache_) {
        Info i;
        i.type = e.res->Type();
        i.path = e.res->path();
        i.error = e.res->error();
        i.bytes = e.res->bytes();
        i.users = (long)e.res.use_count() - 1;
        i.stamp = e.stamp;
        i.version = e.res->version();
        i.ok = e.res->ok();
        i.provided = e.provided;
        out.push_back(std::move(i));
    }
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) { return a.path != b.path ? a.path < b.path : a.type < b.type; });
    return out;
}

}  // namespace toms::res
