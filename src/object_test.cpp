// object_test.cpp — headless verification of the Object base + shared_ptr model
// and the FMLog-style created/destroyed object counter + leak dump.
// Builds objects via shared_ptr, checks Type()/Name()/UniqueID(), IsType<T>(),
// As<T>() downcast, and proves auto-release + leak reporting.
// Exits 0 on success, 1 on any failed check.
#include "object.h"
#include "node.h"
#include "scene.h"
#include <cstdio>
#include <vector>

static int s_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); ++s_fail; } } while(0)

// a concrete game object for the test
class Monster : public toms::Node {
    TOMS_OBJECT(Monster)
public:
    using Node::Node;   // inherit Node(const std::string& name)
    int hp = 10;
};

int main() {
    // ---- fixtures scope: all test objects live and die here ----
    {
        // 1) Type() / StaticType() stable strings
        auto m = Monster::Make<Monster>("goblin");
        CHECK(std::string(m->Type()) == "Monster", "Type() returns class name");
        CHECK(std::string(Monster::StaticType()) == "Monster", "StaticType() returns class name");
        CHECK(m->IsType<Monster>(), "IsType<Monster>() true for Monster");
        CHECK(!m->IsType<toms::SpriteNode>(), "IsType<SpriteNode>() false for Monster");

        // 2) Name() / SetName()
        CHECK(m->Name() == "goblin", "Name() returns ctor name");
        m->SetName("orc");
        CHECK(m->Name() == "orc", "SetName() updates name");

        // 3) UniqueID is non-zero and distinct per object
        auto a = Monster::Make<Monster>("a");
        auto b = Monster::Make<Monster>("b");
        CHECK(a->UniqueID() != 0 && b->UniqueID() != 0, "UniqueID non-zero");
        CHECK(a->UniqueID() != b->UniqueID(), "UniqueID distinct across objects");

        // 4) As<T>() downcast works through shared_ptr
        Object::Ptr base = m;                       // upcast to Object
        auto back = base->As<Monster>();
        CHECK(back != nullptr, "As<Monster>() recovers derived shared_ptr");
        CHECK(back->hp == 10, "As<Monster>() object is the same instance (hp intact)");
        auto notSprite = base->As<toms::SpriteNode>();
        CHECK(notSprite == nullptr, "As<SpriteNode>() returns nullptr for a Monster");

        // 6) Node base now answers Object API (type/name/uid) for free
        toms::GameObject panel("inventoryPanel");
        CHECK(std::string(panel.Type()) == "GameObject", "GameObject reports its type");
        CHECK(panel.Name() == "inventoryPanel", "GameObject reports its name");
        CHECK(panel.UniqueID() != 0, "GameObject has a unique id");

        // 7) scene-graph nodes are typed too
        toms::SpriteNode icon; icon.SetName("potion");
        CHECK(std::string(icon.Type()) == "SpriteNode", "SpriteNode reports its type");
        CHECK(icon.Name() == "potion", "SpriteNode reports its name");
    }   // <-- m, a, b, panel, icon all destroyed -> registry back to 0

    // ---- 5/8) live-count + leak detection on a CLEAN registry ----
    CHECK(Object::LiveCount() == 0, "registry clean after fixtures destroyed");

    {
        auto tmp = Monster::Make<Monster>("tmpObj");
        CHECK(Object::LiveCount() == 1, "LiveCount == 1 while a shared object is alive");
    }
    CHECK(Object::LiveCount() == 0, "LiveCount returns to 0 after auto-release");

    // an intentionally leaked object (not owned by any shared_ptr)
    Monster* leak = new Monster("leakedGoblin");
    CHECK(Object::LiveCount() == 1, "leaked (raw) object is counted while alive");
    printf("[object_test] expected leak dump:\n");
    Object::DumpLeaks();                            // should print type+name of the leak
    delete leak;                                    // simulate cleanup
    CHECK(Object::LiveCount() == 0, "after delete, LiveCount returns to 0");

    if (s_fail == 0) { printf("object_test: ALL PASS (8 groups)\n"); return 0; }
    printf("object_test: %d FAILED\n", s_fail);
    return 1;
}
