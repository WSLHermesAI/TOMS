// object.h — lowest-level base for every TOMS game object.
//
// Ports the intent of FM79979's NamedTypedObject (Core/Common/NamedTypedObject.h):
// a universal base that knows its TYPE, its NAME, a UNIQUE ID, and (via a global
// registry) whether it was ever leaked.
//
// Design note — two layers:
//   * Trackable  : COPYABLE base carrying type/name/uid + registry hooks. Gameplay
//                  structs that are copied by value (EnemyInst, CombatState, Player,
//                  Stage, ...) derive from this so the leak detector covers them too,
//                  without breaking value semantics.
//   * Object     : NON-COPYABLE base (adds shared_ptr / enable_shared_from_this) for
//                  heap-managed game objects. The scene graph (Node/GameObject) uses
//                  this. Derives from Trackable.

#pragma once
#include <string>
#include <cstdint>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <memory>
#include <iostream>
#include "log.h"

class Trackable;   // forward declaration for the registry

// Registry of currently-alive Trackables (one global instance, thread-safe).
class ObjectRegistry {
public:
    static ObjectRegistry& instance() { static ObjectRegistry r; return r; }

    void Add(Trackable* o) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_live.insert(o);
    }
    void Remove(Trackable* o) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_live.erase(o);   // idempotent: erasing a missing pointer is a no-op
    }
    long LiveCount() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return (long)m_live.size();
    }
    void DumpLeaks();   // defined after Trackable is complete (needs Type()/Name())

private:
    std::unordered_set<Trackable*> m_live;
    mutable std::mutex m_mutex;
};

// ---------------------------------------------------------------------------
// Trackable — COPYABLE base (type + name + unique id + registry).
// Safe to copy: each copy is a distinct live instance (registered separately),
// matching value-semantics expectations (e.g. passing an EnemyInst by value).
// ---------------------------------------------------------------------------
class Trackable {
public:
    Trackable() : uid_(NextUID()) { ObjectRegistry::instance().Add(this); }
    Trackable(const Trackable&) : uid_(NextUID()) { ObjectRegistry::instance().Add(this); }
    Trackable& operator=(const Trackable&) { return *this; }   // keep our own uid; don't double-register
    virtual ~Trackable() { ObjectRegistry::instance().Remove(this); }

    virtual const char* Type() const { return "Trackable"; }
    template<class T> bool IsType() const { return Type() == T::StaticType(); }

    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }

    uint64_t UniqueID() const { return uid_; }

    // factory: make a shared_ptr<Object> subclass
    template<class T, class... A> static std::shared_ptr<T> Make(A&&... a) {
        return std::make_shared<T>(std::forward<A>(a)...);
    }

protected:
    std::string name_;
    uint64_t uid_;
    static uint64_t NextUID() { static std::atomic<uint64_t> c{1}; return c.fetch_add(1); }
};

// Per-class type key (mirrors FM79979 DEFINE_TYPE_INFO / TYPDE_DEFINE_MARCO).
// Put TOMS_OBJECT(ClassName) in the class body.
#define TOMS_OBJECT(T) \
public: \
    static const char* StaticType() { return #T; } \
    const char* Type() const override { return #T; } \
private:

// ---- ObjectRegistry::DumpLeaks defined here: Trackable is complete, so
//      Type()/Name() are callable on each still-alive object. ----
inline void ObjectRegistry::DumpLeaks() {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_live.empty()) {
        TOMS_LOG_INFO("Object lifecycle: live objects = 0, no leaks");
        return;
    }
    TOMS_LOG_ERROR("Object lifecycle: LEAK {} object(s) still alive:", (int)m_live.size());
    for (Trackable* o : m_live)
        TOMS_LOG_ERROR("  - type={} name=\"{}\"", o->Type(), o->Name());
}

// ---------------------------------------------------------------------------
// Object — NON-COPYABLE base for heap-managed game objects (adds shared_ptr).
// The scene graph (Node -> GameObject) derives from this.
// ---------------------------------------------------------------------------
class Object : public Trackable, public std::enable_shared_from_this<Object> {
public:
    using Ptr = std::shared_ptr<Object>;
    virtual ~Object() = default;

    // safe downcast to shared_ptr<T>
    template<class T> std::shared_ptr<T> As() { return std::dynamic_pointer_cast<T>(shared_from_this()); }
    template<class T> std::shared_ptr<const T> As() const { return std::dynamic_pointer_cast<const T>(shared_from_this()); }

    // After teardown: dump any remaining live Objects by type + name.
    static void DumpLeaks() { ObjectRegistry::instance().DumpLeaks(); }
};
