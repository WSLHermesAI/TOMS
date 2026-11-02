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
    IRenderer* m_pRen = nullptr;
};

class Resource : public Object {
    TOMS_OBJECT(Resource)
public:
    enum class State { Ready, Failed };

    const std::string& f_Path() const { return m_strPath; }      // as asked for, '/' separators and ".." folded
    State f_State() const { return m_eState; }
    bool f_Ok() const { return m_eState == State::Ready; }
    const std::string& f_Error() const { return m_strError; }    // why it failed (reported once, kept)
    int f_Version() const { return m_iVersion; }               // +1 per reload
    // version() plus the versions of what it depends on: changes when anything it draws with changed
    virtual int f_ContentVersion() const { return m_iVersion; }
    size_t f_Bytes() const { return m_nBytes; }                 // its own CPU + GPU size (not its dependencies')

protected:
    friend class ResourceManager;
    // Reads path() (again, on a reload). Must leave the old contents as they were when it fails, so a half-saved file
    // never blanks what is on screen.
    virtual bool f_Load(ResourceManager& l_oRm, std::string& l_strError) = 0;
    size_t m_nBytes = 0;

private:
    std::string m_strPath, m_strError;
    State m_eState = State::Failed;
    int m_iVersion = 0;
};

class ResourceManager {
public:
    // The virtual path of the game's own sprite atlas: Game provide()s it (the renderer's sprite texture); a clip or
    // effect that names no atlas draws from it.
    static constexpr const char* m_pszSpriteAtlas = "@sprite-atlas";

    explicit ResourceManager(IRenderer* l_pRen = nullptr);    // nullptr: CPU only (tests, QPainter tools)
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    IRenderer* f_Renderer() const { return m_spLink->m_pRen; }
    const std::shared_ptr<GpuLink>& f_GpuLink() const { return m_spLink; }
    void f_DetachRenderer();                                  // frees every texture now; later frees do nothing

    // The resource of type T for this file: cached, or loaded now. Never null -- a file that cannot be loaded gives a
    // Failed resource (ok() false, error() says why), reported once and cached, so it is not read again every frame.
    template <class T> std::shared_ptr<T> f_Get(const std::string& l_strPath) {
        static_assert(std::is_base_of_v<Resource, T>, "T must derive from toms::res::Resource");
        return std::static_pointer_cast<T>(f_GetImpl(T::f_StaticType(), l_strPath, [] { return std::make_shared<T>(); }));
    }
    // Only when already cached (no load); null otherwise.
    template <class T> std::shared_ptr<T> f_Find(const std::string& l_strPath) const {
        return std::static_pointer_cast<T>(f_FindImpl(T::f_StaticType(), l_strPath));
    }
    // A resource made by its owner, not read from a file (the game's sprite atlas): cached under this path for good.
    // Providing the same path again replaces the contents through `update` on the cached object, so everything that
    // holds it sees the new contents (version() + 1).
    template <class T> std::shared_ptr<T> f_Provide(const std::string& l_strPath, const std::function<void(T&)>& l_fnUpdate) {
        std::shared_ptr<T> l_spR = f_Find<T>(l_strPath);
        const bool l_bFresh = !l_spR;
        if (l_bFresh) l_spR = std::make_shared<T>();
        l_fnUpdate(*l_spR);
        f_ProvideImpl(l_spR, l_strPath, l_bFresh);
        return l_spR;
    }
    // Rewrites a path before it is looked up (the game sends every ".../atlas/game.atlas" to kSpriteAtlas, so clips
    // follow the chosen art style). Identity when unset.
    void f_SetPathMapper(std::function<std::string(const std::string&)> l_fnMapper) { m_fnMapper = std::move(l_fnMapper); }

    // ---- names: the manifest and the root folder ----------------------------------------------------------------
    // Relative paths are read from this folder (the game: assets/media). "" = the working folder.
    void f_SetRoot(const std::string& l_strDir);
    const std::string& f_Root() const { return m_strRoot; }
    // The resource manifest: {"resources": {"<key>": {"type": "anim", "path": "anim/door_open.anim", "preload": true}}}.
    // Paths are relative to the manifest's folder. Types: texture, atlas, anim, particle, font, json -- loaded by get<>;
    // file (a file something else reads: an RmlUi document, a data file with its own loader) and folder (a folder
    // read file by file) -- only named, resolve() gives their path. "optional": true = the file may not be there. "patterns": {"<name>": "../data/stages/{id}.json"}
    // name families of files (pathFor). False (and why) when it cannot be read; keys already known stay.
    bool f_LoadManifest(const std::string& l_strPath, std::string* l_pError = nullptr);
    struct ManifestEntry {
        std::string m_strType, m_strPath;   // path: as read (relative to the root when the manifest is in it)
        bool m_bPreload = false;
        bool m_bOptional = false;   // may be missing (a file an editor makes when it is first needed)
    };
    bool f_Has(const std::string& l_strKey) const { return m_mapManifest.count(l_strKey) != 0; }
    const ManifestEntry* f_Entry(const std::string& l_strKey) const;
    std::vector<std::string> f_Keys() const;
    // Loads every manifest entry marked "preload" now (startup); returns how many failed. resource_types.cpp (it knows
    // the types).
    int f_Preload();
    // The path a name reads: a manifest key's path, else the name itself; both under the root when relative.
    std::string f_Resolve(const std::string& l_strName) const;
    // A file of a family: the manifest pattern with {id} replaced, under the root ("stage", "stage01" ->
    // ".../data/stages/stage01.json"). An unknown pattern is told to the missing handler and gives "".
    std::string f_PathFor(const std::string& l_strPattern, const std::string& l_strId) const;
    bool f_HasPattern(const std::string& l_strPattern) const { return m_mapPatterns.count(l_strPattern) != 0; }
    std::vector<std::string> f_PatternNames() const;
    // Told once per resource that cannot be loaded (missing file, broken file, wrong type): what was asked for and why.
    void f_SetOnMissing(std::function<void(const std::string& l_strName, const std::string& l_strPath, const std::string& l_strError)> l_fnFn) {
        m_fnOnMissing = std::move(l_fnFn);
    }
    // The manifest type name of a resource class ("anim" for AnimRes), "" when it has none.
    static std::string f_ManifestType(const char* l_pszStaticType);

    // ---- memory -----------------------------------------------------------------------------------------------
    // Unused resources (held by nobody but the cache) are kept until their bytes pass the budget; then the least
    // recently asked for go first. Default 64 MB (the whole game decodes to about 2 MB today).
    void f_SetBudget(size_t l_nBytes);
    size_t f_Budget() const { return m_nBudget; }
    size_t f_ReleaseUnused();                                 // frees every unused resource now; returns the bytes freed
    size_t f_TotalBytes() const;
    size_t f_UnusedBytes() const;

    // ---- stamps (FM79979 cResourceStamp) ------------------------------------------------------------------------
    // Everything first loaded after pushStamp belongs to it. popStamp frees the ones nobody holds; the ones still held
    // move to the enclosing stamp. Popping a stamp pops the ones opened after it too. Stamp 0 = the base (never popped).
    int f_PushStamp(const std::string& l_strLabel);
    void f_PopStamp(int l_iStamp);
    int f_CurrentStamp() const { return m_vStamps.empty() ? 0 : m_vStamps.back().m_iId; }
    // Keeps r past the current stamp ("remove it from m_Stamp2Data"): it moves to the base.
    void f_Keep(const std::shared_ptr<Resource>& l_spR);

    // ---- reloading ----------------------------------------------------------------------------------------------
    // Reloads every cached file whose time or size changed on disk; returns how many. Editors and debug builds call it
    // about once a second.
    int f_CheckFiles();
    bool f_Refresh(const std::shared_ptr<Resource>& l_spR);       // that one, if its file changed; true when reloaded

    // ---- what is loaded -----------------------------------------------------------------------------------------
    struct Info {
        std::string m_strType, m_strPath, m_strError;
        size_t m_nBytes = 0;
        long m_iUsers = 0;          // holders besides the cache
        int m_iStamp = 0, m_iVersion = 0;
        bool m_bOk = true, m_bProvided = false;
    };
    std::vector<Info> f_Report() const;                       // by path
    size_t size() const { return m_mapCache.size(); }

    // The lookup key of a path: '/' separators, "." and ".." folded, lower case (Windows and the VFS disagree on case).
    static std::string f_Key(const std::string& l_strPath);
    // The path as it is read: separators and ".." folded, case kept.
    static std::string f_Normalize(const std::string& l_strPath);
    static std::string f_DirOf(const std::string& l_strPath);      // "a/b/c.anim" -> "a/b/" ("" for a bare name)
    static std::string f_Join(const std::string& l_strDir, const std::string& l_strRel);   // rel when it is absolute

private:
    struct FileStamp {
        long long m_i64Time = 0, m_i64Size = -1;
        bool operator==(const FileStamp& l_oO) const { return m_i64Time == l_oO.m_i64Time && m_i64Size == l_oO.m_i64Size; }
    };
    struct Entry {
        std::shared_ptr<Resource> m_spRes;
        int m_iStamp = 0;
        uint64_t m_u64LastUse = 0;
        FileStamp m_oFile;
        bool m_bProvided = false;   // provide(): never freed, never reloaded
    };
    struct Stamp {
        int m_iId;
        std::string m_strLabel;
    };

    std::shared_ptr<Resource> f_GetImpl(const char* l_pszType, const std::string& l_strName,
                                      const std::function<std::shared_ptr<Resource>()>& l_fnMake);
    std::shared_ptr<Resource> f_FindImpl(const char* l_pszType, const std::string& l_strPath) const;
    void f_ProvideImpl(const std::shared_ptr<Resource>& l_spR, const std::string& l_strPath, bool l_bFresh);
    std::string f_Mapped(const std::string& l_strPath) const { return m_fnMapper ? m_fnMapper(l_strPath) : l_strPath; }
    static FileStamp f_StatFile(const std::string& l_strPath);
    static std::string f_CacheKey(const char* l_pszType, const std::string& l_strPath);
    bool f_Reload(Entry& l_oE);
    void f_Trim();

    std::shared_ptr<GpuLink> m_spLink;
    std::string m_strRoot;                                  // ends with '/' (or empty)
    std::unordered_map<std::string, ManifestEntry> m_mapManifest;
    std::unordered_map<std::string, std::string> m_mapPatterns;   // name -> path with "{id}", relative like the entries
    std::function<void(const std::string&, const std::string&, const std::string&)> m_fnOnMissing;
    std::unordered_map<std::string, Entry> m_mapCache;   // "<type>|<key>"
    std::function<std::string(const std::string&)> m_fnMapper;
    std::vector<Stamp> m_vStamps;
    int m_iNextStamp = 1;
    uint64_t m_u64Clock = 0;
    size_t m_nBudget = 64u * 1024u * 1024u;
};

// A stamp for one scope: pushed when made, popped when destroyed (a preview, a loading screen, an editor document).
class ResourceStamp {
public:
    ResourceStamp(ResourceManager& l_oRm, const std::string& l_strLabel) : m_pRm(&l_oRm), m_iId(l_oRm.f_PushStamp(l_strLabel)) {}
    ~ResourceStamp() { if (m_pRm) m_pRm->f_PopStamp(m_iId); }
    ResourceStamp(const ResourceStamp&) = delete;
    ResourceStamp& operator=(const ResourceStamp&) = delete;
    int f_Id() const { return m_iId; }

private:
    ResourceManager* m_pRm;
    int m_iId;
};

}  // namespace toms::res
