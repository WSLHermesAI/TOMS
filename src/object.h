// object.h — lowest-level base for every TOMS game object.
//
// Ports the intent of FM79979's NamedTypedObject (Core/Common/NamedTypedObject.h):
// a universal base that knows its TYPE, its NAME, and a globally unique ID, with
// RTTI-lite helpers (IsType<T>, As<T>). Adapted to modern C++ and designed to be
// owned by std::shared_ptr so game objects auto-release.
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
#include <atomic>

class Object : public std::enable_shared_from_this<Object> {
public:
    using Ptr = std::shared_ptr<Object>;

    virtual ~Object() = default;

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

    // factory: build a shared_ptr<Object>-compatible instance of T
    template <class T, class... Args>
    static std::shared_ptr<T> Make(Args&&... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

protected:
    Object() : m_uid(NextUID()) {}

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
