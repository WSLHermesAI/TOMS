// object.h — lowest-level base for every TOMS game object.
//
// Ports the intent of FM79979's NamedTypedObject (Core/Common/NamedTypedObject.h):
// a universal base that knows its TYPE, its NAME, and a globally unique ID, with
// RTTI-lite helpers (IsType<T>, As<T>). Adapted to modern C++ and designed to be
// owned by std::shared_ptr so game objects auto-release.
//
// LEAK DETECTION (inspired by FM79979 FMLog's object-lifecycle bookkeeping):
// every Object is registered on construction and unregistered on destruction, so
// we keep a running count of live objects. After all game resources are destroyed,
// call Object::DumpLeaks(): if the live count is not zero it prints each leaked
// object's NAME and TYPE. This catches forgotten shared_ptr owners / leaks.
//
// Why shared_ptr:
//   * an object held by shared_ptr releases itself when the last reference drops,
//     so the scene graph / inventories don't have to manually delete anything;
//   * Object derives from enable_shared_from_this so any object can hand out a
//     std::shared_ptr<Derived> to itself via As<T>() / shared_from_this().
//
// Every Node (and therefore every GameObject / SpriteNode / TextNode) already
// inherits this, so all game objects answer Type()/Name()/UniqueID() for free.
#pragma once
#include <memory>
#include <string>
#include <cstdint>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <iostream>

class Object;   // forward declaration so ObjectRegistry can hold Object* before the
                // full Object definition below

// Registry of currently-alive Objects (one global instance, thread-safe).
// Implemented as a Meyers singleton inside an inline function so it is a single
// instance even when object.h is included by several translation units.
class ObjectRegistry {
public:
    static ObjectRegistry& instance() { static ObjectRegistry r; return r; }

    void Add(Object* o) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_live.insert(o);
    }
    // Idempotent: erasing a pointer not currently tracked is a no-op, so an
    // edge-case double-destroy can never make the live count go negative.
    void Remove(Object* o) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_live.erase(o);
    }
    long LiveCount() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return (long)m_live.size();
    }
    void DumpLeaks();

private:
    ObjectRegistry() = default;
    std::unordered_set<Object*> m_live;
    mutable std::mutex m_mutex;
};

class Object : public std::enable_shared_from_this<Object> {
public:
    using Ptr = std::shared_ptr<Object>;

    Object() : m_uid(NextUID()) { ObjectRegistry::instance().Add(this); }
    virtual ~Object() { ObjectRegistry::instance().Remove(this); }

    // ---- type (RTTI-lite, mirrors NamedTypedObject::Type()/TypeID) ----
    // Override to return a stable string key (the class name). Use TOMS_OBJECT(T)
    // in the class body to generate this + a static StaticType().
    virtual const char* Type() const { return "Object"; }

    // true if this object's dynamic type is exactly T (or derived, via dynamic_cast)
    template <class T>
    bool IsType() const { return dynamic_cast<const T*>(this) != nullptr; }

    // safe downcast to a shared_ptr<T> (returns nullptr if not a T)
    template <class T>
    std::shared_ptr<T> As() { return std::dynamic_pointer_cast<T>(shared_from_this()); }
    template <class T>
    std::shared_ptr<const T> As() const { return std::dynamic_pointer_cast<const T>(shared_from_this()); }

    // ---- name (mirrors NamedTypedObject name) ----
    void SetName(const std::string& n) { m_name = n; }
    const std::string& Name() const { return m_name; }

    // ---- unique id (mirrors NamedTypedObject::GetUniqueID) ----
    uint64_t UniqueID() const { return m_uid; }

    // ---- leak diagnostics ----
    static long LiveCount() { return ObjectRegistry::instance().LiveCount(); }
    static void DumpLeaks() { ObjectRegistry::instance().DumpLeaks(); }

    // factory: build a shared_ptr<Object>-compatible instance of T
    template <class T, class... Args>
    static std::shared_ptr<T> Make(Args&&... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

protected:
    std::string m_name;
    uint64_t    m_uid = 0;

private:
    static uint64_t NextUID() {
        static std::atomic<uint64_t> s_counter{1};
        return s_counter.fetch_add(1, std::memory_order_relaxed);
    }
};

// Per-class type key. Put `TOMS_OBJECT(MyClass)` in the class body (after the
// base-class list). Generates StaticType() (a stable string) + Type() override.
//   class Monster : public Object { TOMS_OBJECT(Monster) ... };
// Then:  obj->IsType<Monster>();  Monster::StaticType() == "Monster";
#define TOMS_OBJECT(T)                                            \
public:                                                          \
    static const char* StaticType() { return #T; }               \
    const char* Type() const override { return #T; }            \
private:

// ---- ObjectRegistry method definitions (need complete Object for Type()/Name()) ----
inline void ObjectRegistry::DumpLeaks() {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_live.empty()) {
        std::cout << "[Object] live objects = " << m_live.size() << " (no leaks)\n";
        return;
    }
    std::cout << "[Object] LEAK: " << m_live.size()
              << " object(s) still alive (count=" << m_live.size() << "):\n";
    for (Object* o : m_live)
        std::cout << "  - type=" << o->Type() << "  name=\"" << o->Name() << "\"\n";
}
