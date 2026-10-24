// resource.h -- loaded things (textures, atlases, clips, particle files, fonts, data) and the one cache that hands them out
// (docs/engine/12_RESOURCE_MANAGER.md).
//
// FM79979's cGameResourceManager + cSmartObject + cResourceStamp, in modern C++:
//   * a Resource is an Object (type, name, unique id, leak list): the name is its file path;
//   * ResourceManager::get<T>(path) returns the same std::shared_ptr<T> for the same file every time, loading it once.
//     Holding a resource = keeping the shared_ptr (its copy is AddRef, its destruction is Release: nothing to call by
//     hand, nothing to forget). A resource that depends on others holds them the same way (an atlas its page textures,
//     a clip file its atlases), so a page is uploaded once however many atlases and clips use it;
//   * nobody holding it is not enough to free it: the cache keeps unused resources until their size passes the budget
//     (setBudget), then frees the least recently asked for first. Leaving a floor and coming back costs nothing;
//   * a stamp (pushStamp / popStamp, or the ResourceStamp scope) frees what was first loaded inside it as soon as it
//     ends -- for things that come and go (a preview, an editor document);
//   * checkFiles() reloads what changed on disk, in place: the same object, new contents, version() + 1.
//
// One way to ask for anything: get<T>(name), where name is a key of the resource manifest (loadManifest:
// assets/media/resources.json, e.g. "anim.door_open") or a path relative to the root folder (setRoot: assets/media),
// e.g. "anim/door_open.anim". Not loaded yet: loaded now. Not there: a Failed resource (never null), and the missing
// handler (setOnMissing) is told once -- the game shows it on screen.
//
// Paths are UTF-8 (std::string); vfs.h turns them into what the platform opens. Main thread only: GPU uploads happen
// in get() and checkFiles().
#pragma once
#include "object.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

class IRenderer;

namespace toms::res {

class ResourceManager;

// The renderer textures are released through. The manager clears it when the renderer goes (detachRenderer), so a
// texture that outlives it never calls into a dead renderer.
struct GpuLink {
    IRenderer* ren = nullptr;
};

class Resource : public Object {
    TOMS_OBJECT(Resource)
public:
    enum class State { Ready, Failed };

    const std::string& path() const { return path_; }      // as asked for, '/' separators and ".." folded
    State state() const { return state_; }
    bool ok() const { return state_ == State::Ready; }
    const std::string& error() const { return error_; }    // why it failed (reported once, kept)
    int version() const { return version_; }               // +1 per reload
    // version() plus the versions of what it depends on: changes when anything it draws with changed
    virtual int contentVersion() const { return version_; }
    size_t bytes() const { return bytes_; }                 // its own CPU + GPU size (not its dependencies')

protected:
    friend class ResourceManager;
    // Reads path() (again, on a reload). Must leave the old contents as they were when it fails, so a half-saved file
    // never blanks what is on screen.
    virtual bool load(ResourceManager& rm, std::string& error) = 0;
    size_t bytes_ = 0;

private:
    std::string path_, error_;
    State state_ = State::Failed;
    int version_ = 0;
};

class ResourceManager {
public:
    // The virtual path of the game's own sprite atlas: Game provide()s it (the renderer's sprite texture); a clip or
    // effect that names no atlas draws from it.
    static constexpr const char* kSpriteAtlas = "@sprite-atlas";

    explicit ResourceManager(IRenderer* ren = nullptr);    // nullptr: CPU only (tests, QPainter tools)
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    IRenderer* renderer() const { return link_->ren; }
    const std::shared_ptr<GpuLink>& gpuLink() const { return link_; }
    void detachRenderer();                                  // frees every texture now; later frees do nothing

    // The resource of type T for this file: cached, or loaded now. Never null -- a file that cannot be loaded gives a
    // Failed resource (ok() false, error() says why), reported once and cached, so it is not read again every frame.
    template <class T> std::shared_ptr<T> get(const std::string& path) {
        static_assert(std::is_base_of_v<Resource, T>, "T must derive from toms::res::Resource");
        return std::static_pointer_cast<T>(getImpl(T::StaticType(), path, [] { return std::make_shared<T>(); }));
    }
    // Only when already cached (no load); null otherwise.
    template <class T> std::shared_ptr<T> find(const std::string& path) const {
        return std::static_pointer_cast<T>(findImpl(T::StaticType(), path));
    }
    // A resource made by its owner, not read from a file (the game's sprite atlas): cached under this path for good.
    // Providing the same path again replaces the contents through `update` on the cached object, so everything that
    // holds it sees the new contents (version() + 1).
    template <class T> std::shared_ptr<T> provide(const std::string& path, const std::function<void(T&)>& update) {
        std::shared_ptr<T> r = find<T>(path);
        const bool fresh = !r;
        if (fresh) r = std::make_shared<T>();
        update(*r);
        provideImpl(r, path, fresh);
        return r;
    }
    // Rewrites a path before it is looked up (the game sends every ".../atlas/game.atlas" to kSpriteAtlas, so clips
    // follow the chosen art style). Identity when unset.
    void setPathMapper(std::function<std::string(const std::string&)> mapper) { mapper_ = std::move(mapper); }

    // ---- names: the manifest and the root folder ----------------------------------------------------------------
    // Relative paths are read from this folder (the game: assets/media). "" = the working folder.
    void setRoot(const std::string& dir);
    const std::string& root() const { return root_; }
    // The resource manifest: {"resources": {"<key>": {"type": "anim", "path": "anim/door_open.anim", "preload": true}}}.
    // Paths are relative to the manifest's folder. Types: texture, atlas, anim, particle, font, json -- loaded by get<>;
    // file (a file something else reads: an RmlUi document, a data file with its own loader) and folder (a folder
    // read file by file) -- only named, resolve() gives their path. "optional": true = the file may not be there. "patterns": {"<name>": "../data/stages/{id}.json"}
    // name families of files (pathFor). False (and why) when it cannot be read; keys already known stay.
    bool loadManifest(const std::string& path, std::string* error = nullptr);
    struct ManifestEntry {
        std::string type, path;   // path: as read (relative to the root when the manifest is in it)
        bool preload = false;
        bool optional = false;   // may be missing (a file an editor makes when it is first needed)
    };
    bool has(const std::string& key) const { return manifest_.count(key) != 0; }
    const ManifestEntry* entry(const std::string& key) const;
    std::vector<std::string> keys() const;
    // Loads every manifest entry marked "preload" now (startup); returns how many failed. resource_types.cpp (it knows
    // the types).
    int preload();
    // The path a name reads: a manifest key's path, else the name itself; both under the root when relative.
    std::string resolve(const std::string& name) const;
    // A file of a family: the manifest pattern with {id} replaced, under the root ("stage", "stage01" ->
    // ".../data/stages/stage01.json"). An unknown pattern is told to the missing handler and gives "".
    std::string pathFor(const std::string& pattern, const std::string& id) const;
    bool hasPattern(const std::string& pattern) const { return patterns_.count(pattern) != 0; }
    std::vector<std::string> patternNames() const;
    // Told once per resource that cannot be loaded (missing file, broken file, wrong type): what was asked for and why.
    void setOnMissing(std::function<void(const std::string& name, const std::string& path, const std::string& error)> fn) {
        onMissing_ = std::move(fn);
    }
    // The manifest type name of a resource class ("anim" for AnimRes), "" when it has none.
    static std::string manifestType(const char* staticType);

    // ---- memory -----------------------------------------------------------------------------------------------
    // Unused resources (held by nobody but the cache) are kept until their bytes pass the budget; then the least
    // recently asked for go first. Default 64 MB (the whole game decodes to about 2 MB today).
    void setBudget(size_t bytes);
    size_t budget() const { return budget_; }
    size_t releaseUnused();                                 // frees every unused resource now; returns the bytes freed
    size_t totalBytes() const;
    size_t unusedBytes() const;

    // ---- stamps (FM79979 cResourceStamp) ------------------------------------------------------------------------
    // Everything first loaded after pushStamp belongs to it. popStamp frees the ones nobody holds; the ones still held
    // move to the enclosing stamp. Popping a stamp pops the ones opened after it too. Stamp 0 = the base (never popped).
    int pushStamp(const std::string& label);
    void popStamp(int stamp);
    int currentStamp() const { return stamps_.empty() ? 0 : stamps_.back().id; }
    // Keeps r past the current stamp ("remove it from m_Stamp2Data"): it moves to the base.
    void keep(const std::shared_ptr<Resource>& r);

    // ---- reloading ----------------------------------------------------------------------------------------------
    // Reloads every cached file whose time or size changed on disk; returns how many. Editors and debug builds call it
    // about once a second.
    int checkFiles();
    bool refresh(const std::shared_ptr<Resource>& r);       // that one, if its file changed; true when reloaded

    // ---- what is loaded -----------------------------------------------------------------------------------------
    struct Info {
        std::string type, path, error;
        size_t bytes = 0;
        long users = 0;          // holders besides the cache
        int stamp = 0, version = 0;
        bool ok = true, provided = false;
    };
    std::vector<Info> report() const;                       // by path
    size_t size() const { return cache_.size(); }

    // The lookup key of a path: '/' separators, "." and ".." folded, lower case (Windows and the VFS disagree on case).
    static std::string key(const std::string& path);
    // The path as it is read: separators and ".." folded, case kept.
    static std::string normalize(const std::string& path);
    static std::string dirOf(const std::string& path);      // "a/b/c.anim" -> "a/b/" ("" for a bare name)
    static std::string join(const std::string& dir, const std::string& rel);   // rel when it is absolute

private:
    struct FileStamp {
        long long time = 0, size = -1;
        bool operator==(const FileStamp& o) const { return time == o.time && size == o.size; }
    };
    struct Entry {
        std::shared_ptr<Resource> res;
        int stamp = 0;
        uint64_t lastUse = 0;
        FileStamp file;
        bool provided = false;   // provide(): never freed, never reloaded
    };
    struct Stamp {
        int id;
        std::string label;
    };

    std::shared_ptr<Resource> getImpl(const char* type, const std::string& name,
                                      const std::function<std::shared_ptr<Resource>()>& make);
    std::shared_ptr<Resource> findImpl(const char* type, const std::string& path) const;
    void provideImpl(const std::shared_ptr<Resource>& r, const std::string& path, bool fresh);
    std::string mapped(const std::string& path) const { return mapper_ ? mapper_(path) : path; }
    static FileStamp statFile(const std::string& path);
    static std::string cacheKey(const char* type, const std::string& path);
    bool reload(Entry& e);
    void trim();

    std::shared_ptr<GpuLink> link_;
    std::string root_;                                  // ends with '/' (or empty)
    std::unordered_map<std::string, ManifestEntry> manifest_;
    std::unordered_map<std::string, std::string> patterns_;   // name -> path with "{id}", relative like the entries
    std::function<void(const std::string&, const std::string&, const std::string&)> onMissing_;
    std::unordered_map<std::string, Entry> cache_;   // "<type>|<key>"
    std::function<std::string(const std::string&)> mapper_;
    std::vector<Stamp> stamps_;
    int nextStamp_ = 1;
    uint64_t clock_ = 0;
    size_t budget_ = 64u * 1024u * 1024u;
};

// A stamp for one scope: pushed when made, popped when destroyed (a preview, a loading screen, an editor document).
class ResourceStamp {
public:
    ResourceStamp(ResourceManager& rm, const std::string& label) : rm_(&rm), id_(rm.pushStamp(label)) {}
    ~ResourceStamp() { if (rm_) rm_->popStamp(id_); }
    ResourceStamp(const ResourceStamp&) = delete;
    ResourceStamp& operator=(const ResourceStamp&) = delete;
    int id() const { return id_; }

private:
    ResourceManager* rm_;
    int id_;
};

}  // namespace toms::res
