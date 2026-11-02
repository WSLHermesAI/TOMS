// singletons.cpp -- see singletons.h.
#include "singletons.h"

#include <mutex>
#include <utility>
#include <vector>

namespace toms {
namespace {

struct Entry { const char* m_pszName; std::function<void()> m_fnRelease; };

std::mutex& f_Lock() { static std::mutex l_oM; return l_oM; }
std::vector<Entry>& f_Entries() { static std::vector<Entry> l_vE; return l_vE; }

} // namespace

void f_OnReleaseSingletons(const char* l_pszName, std::function<void()> l_fnRelease) {
    std::lock_guard<std::mutex> l_oG(f_Lock());
    f_Entries().push_back({l_pszName, std::move(l_fnRelease)});
}

void f_ReleaseSingletons() {
    std::vector<Entry> l_vList;
    {
        std::lock_guard<std::mutex> l_oG(f_Lock());
        l_vList.swap(f_Entries());           // a release that registers again goes to a fresh list
    }
    for (auto l_itIt = l_vList.rbegin(); l_itIt != l_vList.rend(); ++l_itIt)   // newest first
        if (l_itIt->m_fnRelease) l_itIt->m_fnRelease();
    std::vector<Entry>().swap(l_vList);    // and the list's own memory
    std::lock_guard<std::mutex> l_oG(f_Lock());
    std::vector<Entry>().swap(f_Entries());
}

} // namespace toms
