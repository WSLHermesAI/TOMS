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
std::string f_Fold(const std::string& l_strPath, bool l_bLower) {
    std::vector<std::string> l_vParts;
    std::string l_strSeg;
    auto l_fnFlush = [&] {
        if (l_strSeg == ".." && !l_vParts.empty() && l_vParts.back() != "..") l_vParts.pop_back();
        else if (!l_strSeg.empty() && l_strSeg != ".") l_vParts.push_back(l_strSeg);
        l_strSeg.clear();
    };
    for (char l_cC : l_strPath) {
        if (l_cC == '/' || l_cC == '\\') l_fnFlush();
        else l_strSeg += l_bLower ? (char)std::tolower((unsigned char)l_cC) : l_cC;
    }
    l_fnFlush();
    std::string l_strOut = !l_strPath.empty() && (l_strPath[0] == '/' || l_strPath[0] == '\\') ? "/" : "";
    for (size_t l_nI = 0; l_nI < l_vParts.size(); l_nI++) l_strOut += (l_nI ? "/" : "") + l_vParts[l_nI];
    return l_strOut;
}
}  // namespace

std::string ResourceManager::f_Key(const std::string& l_strPath) { return f_Fold(l_strPath, true); }

namespace {
bool f_IsAbsolute(const std::string& l_strP) {
    return !l_strP.empty() && (l_strP[0] == '/' || l_strP[0] == '\\' || l_strP[0] == '@' || (l_strP.size() > 1 && l_strP[1] == ':'));
}
}  // namespace

// ---- names: the manifest and the root folder ------------------------------------------------------------------------

void ResourceManager::f_SetRoot(const std::string& l_strDir) { m_strRoot = l_strDir.empty() ? std::string() : f_Normalize(l_strDir) + "/"; }

std::string ResourceManager::f_Resolve(const std::string& l_strName) const {
    auto l_itM = m_mapManifest.find(l_strName);
    std::string l_strP = f_Normalize(l_itM != m_mapManifest.end() ? l_itM->second.m_strPath : l_strName);
    // under the root, unless it is absolute or already there (a dependency's path, made from a resolved one)
    if (!m_strRoot.empty() && !f_IsAbsolute(l_strP) && l_strP.compare(0, m_strRoot.size(), m_strRoot) != 0) l_strP = f_Normalize(m_strRoot + l_strP);   // "../data/x" leaves the root
    if (l_strP.empty()) l_strP = ".";   // the root's parent, when the root is one relative folder
    return l_strP;
}

bool ResourceManager::f_LoadManifest(const std::string& l_strPath, std::string* l_pError) {
    auto l_fnFail = [&](const std::string& l_strWhy) {
        if (l_pError) *l_pError = l_strWhy;
        if (m_fnOnMissing) m_fnOnMissing(l_strPath, f_Resolve(l_strPath), l_strWhy);
        std::fprintf(stderr, "[res] manifest %s: %s\n", f_Resolve(l_strPath).c_str(), l_strWhy.c_str());
        return false;
    };
    std::string l_strText;
    if (!toms::f_VfsReadAll(f_Resolve(l_strPath), l_strText)) return l_fnFail("cannot read");
    const nlohmann::json l_jJ = nlohmann::json::parse(l_strText, nullptr, false);
    if (l_jJ.is_discarded() || !l_jJ.is_object()) return l_fnFail("not valid JSON");
    auto l_oRes = l_jJ.find("resources");
    if (l_oRes == l_jJ.end() || !l_oRes->is_object()) return l_fnFail("no \"resources\" object");
    const std::string l_strDir = f_DirOf(l_strPath);   // entries are relative to the manifest
    for (auto& [l_strK, l_jV] : l_oRes->items()) {
        if (!l_jV.is_object() || !l_jV.contains("path") || !l_jV["path"].is_string()) {
            std::fprintf(stderr, "[res] manifest %s: '%s' has no \"path\"\n", f_Resolve(l_strPath).c_str(), l_strK.c_str());
            continue;
        }
        ManifestEntry l_oE;
        l_oE.m_strType = l_jV.value("type", std::string());
        l_oE.m_strPath = f_Join(l_strDir, l_jV["path"].get<std::string>());
        l_oE.m_bPreload = l_jV.value("preload", false);
        l_oE.m_bOptional = l_jV.value("optional", false);
        m_mapManifest[l_strK] = std::move(l_oE);
    }
    if (auto l_oPat = l_jJ.find("patterns"); l_oPat != l_jJ.end() && l_oPat->is_object())
        for (auto& [l_strK, l_jV] : l_oPat->items()) {
            if (!l_jV.is_string() || l_jV.get<std::string>().find("{id}") == std::string::npos) {
                std::fprintf(stderr, "[res] manifest %s: pattern '%s' needs a path with {id}\n", f_Resolve(l_strPath).c_str(), l_strK.c_str());
                continue;
            }
            m_mapPatterns[l_strK] = f_Join(l_strDir, l_jV.get<std::string>());
        }
    return true;
}

std::string ResourceManager::f_PathFor(const std::string& l_strPattern, const std::string& l_strId) const {
    auto l_itIt = m_mapPatterns.find(l_strPattern);
    if (l_itIt == m_mapPatterns.end()) {
        if (m_fnOnMissing) m_fnOnMissing(l_strPattern, std::string(), "no pattern '" + l_strPattern + "' in the manifest");
        return std::string();
    }
    std::string l_strP = l_itIt->second;
    l_strP.replace(l_strP.find("{id}"), 4, l_strId);
    return f_Resolve(l_strP);
}

std::vector<std::string> ResourceManager::f_PatternNames() const {
    std::vector<std::string> l_vOut;
    for (const auto& [l_strK, l_strP] : m_mapPatterns) l_vOut.push_back(l_strK);
    std::sort(l_vOut.begin(), l_vOut.end());
    return l_vOut;
}

const ResourceManager::ManifestEntry* ResourceManager::f_Entry(const std::string& l_strKey) const {
    auto l_itIt = m_mapManifest.find(l_strKey);
    return l_itIt == m_mapManifest.end() ? nullptr : &l_itIt->second;
}

std::vector<std::string> ResourceManager::f_Keys() const {
    std::vector<std::string> l_vOut;
    for (const auto& [l_strK, l_oE] : m_mapManifest) l_vOut.push_back(l_strK);
    std::sort(l_vOut.begin(), l_vOut.end());
    return l_vOut;
}

std::string ResourceManager::f_ManifestType(const char* l_pszStaticType) {
    static const std::pair<const char*, const char*> l_arrTypes[] = {{"TextureRes", "texture"}, {"AtlasRes", "atlas"},
                                                                 {"AnimRes", "anim"},       {"ParticleRes", "particle"},
                                                                 {"FontRes", "font"},       {"JsonRes", "json"}};
    for (const auto& [l_pszCls, l_pszName] : l_arrTypes)
        if (std::string(l_pszStaticType) == l_pszCls) return l_pszName;
    return std::string();
}

// The cache's key: the type and the folded path, relative paths made absolute against the working folder, so
// "media/atlas/fx.atlas" and "D:/.../assets/media/atlas/fx.atlas" are one atlas.
std::string ResourceManager::f_CacheKey(const char* l_pszType, const std::string& l_strPath) {
    std::string l_strP = l_strPath;
#if !defined(__ANDROID__)
    const bool l_bAbsolute = !l_strP.empty() && (l_strP[0] == '/' || l_strP[0] == '\\' || l_strP[0] == '@' || (l_strP.size() > 1 && l_strP[1] == ':'));
    if (!l_bAbsolute) {
        std::error_code l_oEc;
        const std::filesystem::path l_oCwd = std::filesystem::current_path(l_oEc);
        if (!l_oEc) l_strP = toms::f_VfsUtf8(l_oCwd) + "/" + l_strP;
    }
#endif
    return std::string(l_pszType) + "|" + f_Key(l_strP);
}
std::string ResourceManager::f_Normalize(const std::string& l_strPath) { return f_Fold(l_strPath, false); }

std::string ResourceManager::f_DirOf(const std::string& l_strPath) {
    const size_t l_nSlash = l_strPath.find_last_of("/\\");
    return l_nSlash == std::string::npos ? std::string() : l_strPath.substr(0, l_nSlash + 1);
}

std::string ResourceManager::f_Join(const std::string& l_strDir, const std::string& l_strRel) {
    const bool l_bAbsolute = !l_strRel.empty() && (l_strRel[0] == '/' || l_strRel[0] == '\\' || (l_strRel.size() > 1 && l_strRel[1] == ':'));
    return l_bAbsolute ? l_strRel : l_strDir + l_strRel;
}

ResourceManager::ResourceManager(IRenderer* l_pRen) : m_spLink(std::make_shared<GpuLink>()) { m_spLink->m_pRen = l_pRen; }

ResourceManager::~ResourceManager() {
    // Dependencies first would not matter: each resource frees its own GPU handle while the link is still set.
    m_mapCache.clear();
    m_spLink->m_pRen = nullptr;
}

void ResourceManager::f_DetachRenderer() {
    for (auto& [l_strK, l_oE] : m_mapCache)
        if (auto* l_pT = dynamic_cast<TextureRes*>(l_oE.m_spRes.get())) l_pT->f_ReleaseGpu();
    m_spLink->m_pRen = nullptr;
}

std::shared_ptr<Resource> ResourceManager::f_FindImpl(const char* l_pszType, const std::string& l_strName) const {
    auto l_itIt = m_mapCache.find(f_CacheKey(l_pszType, f_Mapped(f_Resolve(l_strName))));
    return l_itIt == m_mapCache.end() ? nullptr : l_itIt->second.m_spRes;
}

std::shared_ptr<Resource> ResourceManager::f_GetImpl(const char* l_pszType, const std::string& l_strName,
                                                  const std::function<std::shared_ptr<Resource>()>& l_fnMake) {
    const std::string l_strReal = f_Mapped(f_Resolve(l_strName));
    const std::string l_strK = f_CacheKey(l_pszType, l_strReal);
    if (auto l_itIt = m_mapCache.find(l_strK); l_itIt != m_mapCache.end()) {
        l_itIt->second.m_u64LastUse = ++m_u64Clock;
        return l_itIt->second.m_spRes;
    }
    std::shared_ptr<Resource> l_spR = l_fnMake();
    l_spR->m_strPath = f_Normalize(l_strReal);
    l_spR->f_SetName(l_spR->m_strPath);
    Entry l_oE;
    l_oE.m_iStamp = f_CurrentStamp();
    l_oE.m_oFile = f_StatFile(l_spR->m_strPath);
    std::string l_strErr;
    const ManifestEntry* l_pM = f_Entry(l_strName);
    const std::string l_strWant = f_ManifestType(l_pszType);
    if (l_pM && !l_pM->m_strType.empty() && l_pM->m_strType != l_strWant) {
        l_strErr = "'" + l_strName + "' is a " + l_pM->m_strType + " in the manifest, not a " + (l_strWant.empty() ? std::string(l_pszType) : l_strWant);
    } else if (l_spR->f_Load(*this, l_strErr)) {   // may get<> its dependencies: they are cached before it
        l_spR->m_eState = Resource::State::Ready;
    }
    if (l_spR->m_eState != Resource::State::Ready) {
        l_spR->m_eState = Resource::State::Failed;
        l_spR->m_strError = l_strErr.empty() ? "cannot read" : l_strErr;
        std::fprintf(stderr, "[res] %s %s: %s\n", l_pszType, l_spR->m_strPath.c_str(), l_spR->m_strError.c_str());
        if (m_fnOnMissing) m_fnOnMissing(l_strName, l_spR->m_strPath, l_spR->m_strError);
    }
    l_oE.m_spRes = l_spR;
    l_oE.m_u64LastUse = ++m_u64Clock;
    m_mapCache[l_strK] = std::move(l_oE);
    f_Trim();
    return l_spR;
}

void ResourceManager::f_ProvideImpl(const std::shared_ptr<Resource>& l_spR, const std::string& l_strPath, bool l_bFresh) {
    if (l_bFresh) {
        l_spR->m_strPath = f_Normalize(l_strPath);
        l_spR->f_SetName(l_spR->m_strPath);
        Entry l_oE;
        l_oE.m_spRes = l_spR;
        l_oE.m_bProvided = true;
        l_oE.m_u64LastUse = ++m_u64Clock;
        m_mapCache[f_CacheKey(l_spR->f_Type(), l_strPath)] = std::move(l_oE);
    } else {
        l_spR->m_iVersion++;
    }
    l_spR->m_eState = Resource::State::Ready;
    l_spR->m_strError.clear();
}

// ---- memory ------------------------------------------------------------------------------------------------------

void ResourceManager::f_SetBudget(size_t l_nBytes) {
    m_nBudget = l_nBytes;
    f_Trim();
}

size_t ResourceManager::f_TotalBytes() const {
    size_t l_nN = 0;
    for (const auto& [l_strK, l_oE] : m_mapCache) l_nN += l_oE.m_spRes->f_Bytes();
    return l_nN;
}

size_t ResourceManager::f_UnusedBytes() const {
    size_t l_nN = 0;
    for (const auto& [l_strK, l_oE] : m_mapCache)
        if (!l_oE.m_bProvided && l_oE.m_spRes.use_count() == 1) l_nN += l_oE.m_spRes->f_Bytes();
    return l_nN;
}

// Freeing a clip file can leave its atlases unused, and those its textures: each pass looks again until nothing changes.
size_t ResourceManager::f_ReleaseUnused() {
    size_t l_nFreed = 0;
    for (bool l_bAgain = true; l_bAgain;) {
        l_bAgain = false;
        for (auto l_itIt = m_mapCache.begin(); l_itIt != m_mapCache.end();) {
            if (!l_itIt->second.m_bProvided && l_itIt->second.m_spRes.use_count() == 1) {
                l_nFreed += l_itIt->second.m_spRes->f_Bytes();
                l_itIt = m_mapCache.erase(l_itIt);
                l_bAgain = true;
            } else {
                ++l_itIt;
            }
        }
    }
    return l_nFreed;
}

void ResourceManager::f_Trim() {
    while (f_UnusedBytes() > m_nBudget) {
        auto l_itOldest = m_mapCache.end();
        for (auto l_itIt = m_mapCache.begin(); l_itIt != m_mapCache.end(); ++l_itIt)
            if (!l_itIt->second.m_bProvided && l_itIt->second.m_spRes.use_count() == 1 && (l_itOldest == m_mapCache.end() || l_itIt->second.m_u64LastUse < l_itOldest->second.m_u64LastUse))
                l_itOldest = l_itIt;
        if (l_itOldest == m_mapCache.end()) return;
        m_mapCache.erase(l_itOldest);
    }
}

// ---- stamps ------------------------------------------------------------------------------------------------------

int ResourceManager::f_PushStamp(const std::string& l_strLabel) {
    m_vStamps.push_back({m_iNextStamp++, l_strLabel});
    return m_vStamps.back().m_iId;
}

void ResourceManager::f_PopStamp(int l_iStamp) {
    auto l_itPos = std::find_if(m_vStamps.begin(), m_vStamps.end(), [&](const Stamp& l_oS) { return l_oS.m_iId == l_iStamp; });
    if (l_itPos == m_vStamps.end()) return;
    std::vector<int> l_vEnding;
    for (auto l_itIt = l_itPos; l_itIt != m_vStamps.end(); ++l_itIt) l_vEnding.push_back(l_itIt->m_iId);
    m_vStamps.erase(l_itPos, m_vStamps.end());
    const int l_iOuter = f_CurrentStamp();
    auto l_fnInEnding = [&](int l_iS) { return std::find(l_vEnding.begin(), l_vEnding.end(), l_iS) != l_vEnding.end(); };
    size_t l_nFreed = 0, l_nKept = 0;
    for (bool l_bAgain = true; l_bAgain;) {
        l_bAgain = false;
        for (auto l_itIt = m_mapCache.begin(); l_itIt != m_mapCache.end();) {
            Entry& l_oE = l_itIt->second;
            if (!l_oE.m_bProvided && l_fnInEnding(l_oE.m_iStamp) && l_oE.m_spRes.use_count() == 1) {
                l_nFreed++;
                l_itIt = m_mapCache.erase(l_itIt);
                l_bAgain = true;
            } else {
                ++l_itIt;
            }
        }
    }
    for (auto& [l_strK, l_oE] : m_mapCache)
        if (l_fnInEnding(l_oE.m_iStamp)) { l_oE.m_iStamp = l_iOuter; l_nKept++; }
    if (l_nFreed || l_nKept) std::fprintf(stderr, "[res] stamp %d ended: %zu freed, %zu still held\n", l_iStamp, l_nFreed, l_nKept);
}

void ResourceManager::f_Keep(const std::shared_ptr<Resource>& l_spR) {
    for (auto& [l_strK, l_oE] : m_mapCache)
        if (l_oE.m_spRes == l_spR) l_oE.m_iStamp = 0;
}

// ---- reloading ---------------------------------------------------------------------------------------------------

ResourceManager::FileStamp ResourceManager::f_StatFile(const std::string& l_strPath) {
    FileStamp l_oS;
#if !defined(__ANDROID__)   // APK entries never change while the game runs
    if (l_strPath.empty() || l_strPath[0] == '@') return l_oS;
    std::error_code l_oEc;
    const std::filesystem::path l_oP = toms::f_VfsPath(l_strPath);
    const auto l_oT = std::filesystem::last_write_time(l_oP, l_oEc);
    if (l_oEc) return l_oS;
    const auto l_u64N = std::filesystem::file_size(l_oP, l_oEc);
    if (l_oEc) return l_oS;
    l_oS.m_i64Time = (long long)l_oT.time_since_epoch().count();
    l_oS.m_i64Size = (long long)l_u64N;
#else
    (void)l_strPath;
#endif
    return l_oS;
}

// Only the resource is used after load(): a load that get<>s a new dependency can rehash the map and move `e`.
bool ResourceManager::f_Reload(Entry& l_oE) {
    std::string l_strErr;
    Resource& l_oR = *l_oE.m_spRes;
    if (!l_oR.f_Load(*this, l_strErr)) {   // the old contents stay
        std::fprintf(stderr, "[res] reload %s: %s (kept the old contents)\n", l_oR.m_strPath.c_str(), l_strErr.empty() ? "cannot read" : l_strErr.c_str());
        return false;
    }
    l_oR.m_eState = Resource::State::Ready;
    l_oR.m_strError.clear();
    l_oR.m_iVersion++;
    std::fprintf(stderr, "[res] reloaded %s\n", l_oR.m_strPath.c_str());
    return true;
}

int ResourceManager::f_CheckFiles() {
    int l_iN = 0;
    std::vector<std::string> l_vKeys;   // reloading may add entries (a new atlas page): walk a copy of the keys
    l_vKeys.reserve(m_mapCache.size());
    for (const auto& [l_strK, l_oE] : m_mapCache) l_vKeys.push_back(l_strK);
    for (const std::string& l_strK : l_vKeys) {
        auto l_itIt = m_mapCache.find(l_strK);
        if (l_itIt == m_mapCache.end() || l_itIt->second.m_bProvided) continue;
        const FileStamp l_oNow = f_StatFile(l_itIt->second.m_spRes->m_strPath);
        if (l_oNow.m_i64Size < 0 || l_oNow == l_itIt->second.m_oFile) continue;
        l_itIt->second.m_oFile = l_oNow;
        if (f_Reload(l_itIt->second)) l_iN++;
    }
    return l_iN;
}

bool ResourceManager::f_Refresh(const std::shared_ptr<Resource>& l_spR) {
    for (auto& [l_strK, l_oE] : m_mapCache) {
        if (l_oE.m_spRes != l_spR || l_oE.m_bProvided) continue;
        const FileStamp l_oNow = f_StatFile(l_spR->m_strPath);
        if (l_oNow.m_i64Size < 0 || l_oNow == l_oE.m_oFile) return false;
        l_oE.m_oFile = l_oNow;
        return f_Reload(l_oE);
    }
    return false;
}

std::vector<ResourceManager::Info> ResourceManager::f_Report() const {
    std::vector<Info> l_vOut;
    for (const auto& [l_strK, l_oE] : m_mapCache) {
        Info l_oI;
        l_oI.m_strType = l_oE.m_spRes->f_Type();
        l_oI.m_strPath = l_oE.m_spRes->f_Path();
        l_oI.m_strError = l_oE.m_spRes->f_Error();
        l_oI.m_nBytes = l_oE.m_spRes->f_Bytes();
        l_oI.m_iUsers = (long)l_oE.m_spRes.use_count() - 1;
        l_oI.m_iStamp = l_oE.m_iStamp;
        l_oI.m_iVersion = l_oE.m_spRes->f_Version();
        l_oI.m_bOk = l_oE.m_spRes->f_Ok();
        l_oI.m_bProvided = l_oE.m_bProvided;
        l_vOut.push_back(std::move(l_oI));
    }
    std::sort(l_vOut.begin(), l_vOut.end(), [](const Info& l_oA, const Info& l_oB) { return l_oA.m_strPath != l_oB.m_strPath ? l_oA.m_strPath < l_oB.m_strPath : l_oA.m_strType < l_oB.m_strType; });
    return l_vOut;
}

}  // namespace toms::res
