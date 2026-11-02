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
    static ObjectRegistry& f_Instance() { static ObjectRegistry l_oR; return l_oR; }

    void f_Add(Trackable* l_pO) {
        std::lock_guard<std::mutex> l_oLk(m_oMutex);
        m_setLive.insert(l_pO);
    }
    void f_Remove(Trackable* l_pO) {
        std::lock_guard<std::mutex> l_oLk(m_oMutex);
        m_setLive.erase(l_pO);   // idempotent: erasing a missing pointer is a no-op
    }
    long f_LiveCount() const {
        std::lock_guard<std::mutex> l_oLk(m_oMutex);
        return (long)m_setLive.size();
    }
    void f_DumpLeaks();   // defined after Trackable is complete (needs f_Type()/f_Name())

private:
    std::unordered_set<Trackable*> m_setLive;
    mutable std::mutex m_oMutex;
};

// ---------------------------------------------------------------------------
// Trackable — COPYABLE base (type + name + unique id + registry).
// Safe to copy: each copy is a distinct live instance (registered separately),
// matching value-semantics expectations (e.g. passing an EnemyInst by value).
// ---------------------------------------------------------------------------
class Trackable {
public:
    Trackable() : m_u64Uid(f_NextUID()) { ObjectRegistry::f_Instance().f_Add(this); }
    Trackable(const Trackable&) : m_u64Uid(f_NextUID()) { ObjectRegistry::f_Instance().f_Add(this); }
    Trackable& operator=(const Trackable&) { return *this; }   // keep our own uid; don't double-register
    virtual ~Trackable() { ObjectRegistry::f_Instance().f_Remove(this); }

    virtual const char* f_Type() const { return "Trackable"; }
    template<class T> bool f_IsType() const { return f_Type() == T::f_StaticType(); }

    void f_SetName(const std::string& l_strN) { m_strName = l_strN; }
    const std::string& f_Name() const { return m_strName; }

    uint64_t f_UniqueID() const { return m_u64Uid; }

    // factory: make a shared_ptr<Object> subclass
    template<class T, class... A> static std::shared_ptr<T> f_Make(A&&... l_oA) {
        return std::make_shared<T>(std::forward<A>(l_oA)...);
    }

protected:
    std::string m_strName;
    uint64_t m_u64Uid;
    static uint64_t f_NextUID() { static std::atomic<uint64_t> l_oC{1}; return l_oC.fetch_add(1); }
};

// Per-class type key (mirrors FM79979 DEFINE_TYPE_INFO / TYPDE_DEFINE_MARCO).
// Put TOMS_OBJECT(ClassName) in the class body.
#define TOMS_OBJECT(T) \
public: \
    static const char* f_StaticType() { return #T; } \
    const char* f_Type() const override { return #T; } \
private:

// ---- ObjectRegistry::DumpLeaks defined here: Trackable is complete, so
//      Type()/Name() are callable on each still-alive object. ----
inline void ObjectRegistry::f_DumpLeaks() {
    std::lock_guard<std::mutex> l_oLk(m_oMutex);
    if (m_setLive.empty()) {
        TOMS_LOG_INFO("Object lifecycle: live objects = 0, no leaks");
        return;
    }
    TOMS_LOG_ERROR("Object lifecycle: LEAK {} object(s) still alive:", (int)m_setLive.size());
    for (Trackable* l_pO : m_setLive)
        TOMS_LOG_ERROR("  - type={} name=\"{}\"", l_pO->f_Type(), l_pO->f_Name());
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
    template<class T> std::shared_ptr<T> f_As() { return std::dynamic_pointer_cast<T>(shared_from_this()); }
    template<class T> std::shared_ptr<const T> f_As() const { return std::dynamic_pointer_cast<const T>(shared_from_this()); }

    // After teardown: dump any remaining live Objects by type + name.
    static void f_DumpLeaks() { ObjectRegistry::f_Instance().f_DumpLeaks(); }
};
