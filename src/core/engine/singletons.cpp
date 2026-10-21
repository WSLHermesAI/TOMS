// singletons.cpp -- see singletons.h.
#include "singletons.h"

#include <mutex>
#include <utility>
#include <vector>

namespace toms {
namespace {

struct Entry { const char* name; std::function<void()> release; };

std::mutex& lock() { static std::mutex m; return m; }
std::vector<Entry>& entries() { static std::vector<Entry> e; return e; }

} // namespace

void onReleaseSingletons(const char* name, std::function<void()> release) {
    std::lock_guard<std::mutex> g(lock());
    entries().push_back({name, std::move(release)});
}

void releaseSingletons() {
    std::vector<Entry> list;
    {
        std::lock_guard<std::mutex> g(lock());
        list.swap(entries());           // a release that registers again goes to a fresh list
    }
    for (auto it = list.rbegin(); it != list.rend(); ++it)   // newest first
        if (it->release) it->release();
    std::vector<Entry>().swap(list);    // and the list's own memory
    std::lock_guard<std::mutex> g(lock());
    std::vector<Entry>().swap(entries());
}

} // namespace toms
