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

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); ++g_iFail; } } while(0)

// a concrete game object for the test
class Monster : public toms::Node {
    TOMS_OBJECT(Monster)
public:
    using Node::Node;   // inherit Node(const std::string& name)
    int m_iHp = 10;
};

int main() {
    // ---- fixtures scope: all test objects live and die here ----
    {
        // 1) Type() / f_StaticType() stable strings
        auto l_spM = Monster::f_Make<Monster>("goblin");
        CHECK(std::string(l_spM->f_Type()) == "Monster", "Type() returns class name");
        CHECK(std::string(Monster::f_StaticType()) == "Monster", "f_StaticType() returns class name");
        CHECK(l_spM->f_IsType<Monster>(), "IsType<Monster>() true for Monster");
        CHECK(!l_spM->f_IsType<toms::SpriteNode>(), "IsType<SpriteNode>() false for Monster");

        // 2) Name() / SetName()
        CHECK(l_spM->f_Name() == "goblin", "Name() returns ctor name");
        l_spM->f_SetName("orc");
        CHECK(l_spM->f_Name() == "orc", "SetName() updates name");

        // 3) UniqueID is non-zero and distinct per object
        auto l_spA = Monster::f_Make<Monster>("a");
        auto l_spB = Monster::f_Make<Monster>("b");
        CHECK(l_spA->f_UniqueID() != 0 && l_spB->f_UniqueID() != 0, "UniqueID non-zero");
        CHECK(l_spA->f_UniqueID() != l_spB->f_UniqueID(), "UniqueID distinct across objects");

        // 4) As<T>() downcast works through shared_ptr
        Object::Ptr l_spBase = l_spM;                       // upcast to Object
        auto l_spBack = l_spBase->f_As<Monster>();
        CHECK(l_spBack != nullptr, "As<Monster>() recovers derived shared_ptr");
        CHECK(l_spBack->m_iHp == 10, "As<Monster>() object is the same instance (hp intact)");
        auto l_spNotSprite = l_spBase->f_As<toms::SpriteNode>();
        CHECK(l_spNotSprite == nullptr, "As<SpriteNode>() returns nullptr for a Monster");

        // 6) Node base now answers Object API (type/name/uid) for free
        toms::GameObject l_oPanel("inventoryPanel");
        CHECK(std::string(l_oPanel.f_Type()) == "GameObject", "GameObject reports its type");
        CHECK(l_oPanel.f_Name() == "inventoryPanel", "GameObject reports its name");
        CHECK(l_oPanel.f_UniqueID() != 0, "GameObject has a unique id");

        // 7) scene-graph nodes are typed too
        toms::SpriteNode l_oIcon; l_oIcon.f_SetName("potion");
        CHECK(std::string(l_oIcon.f_Type()) == "SpriteNode", "SpriteNode reports its type");
        CHECK(l_oIcon.f_Name() == "potion", "SpriteNode reports its name");
    }   // <-- m, a, b, panel, icon all destroyed -> registry back to 0

    // ---- 5/8) live-count + leak detection on a CLEAN registry ----
    auto& l_oReg = ObjectRegistry::f_Instance();
    CHECK(l_oReg.f_LiveCount() == 0, "registry clean after fixtures destroyed");

    {
        auto l_spTmp = Monster::f_Make<Monster>("tmpObj");
        CHECK(l_oReg.f_LiveCount() == 1, "LiveCount == 1 while a shared object is alive");
    }
    CHECK(l_oReg.f_LiveCount() == 0, "LiveCount returns to 0 after auto-release");

    // an intentionally leaked object (not owned by any shared_ptr)
    Monster* l_pLeak = new Monster("leakedGoblin");
    CHECK(l_oReg.f_LiveCount() == 1, "leaked (raw) object is counted while alive");
    printf("[object_test] expected leak dump:\n");
    Object::f_DumpLeaks();                            // should print type+name of the leak
    delete l_pLeak;                                    // simulate cleanup
    CHECK(l_oReg.f_LiveCount() == 0, "after delete, LiveCount returns to 0");

    if (g_iFail == 0) { printf("object_test: ALL PASS (8 groups)\n"); return 0; }
    printf("object_test: %d FAILED\n", g_iFail);
    return 1;
}
