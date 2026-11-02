// ObjectWindow.h -- the stage editor's detail windows (docs/gameplay/02_STAGE_EDITOR.md section 2c), as the HTML editor's:
//
//   an object (✎ Edit)   one placed object's own settings, the stage file's "objects" keyed "x,y":
//                         Look (sprite, animation, tint over its type's look) | Events (linked: fired when the player
//                         defeats / picks up / opens / talks to / steps on it)
//   a type (palette)      its record in enemies.json / items.json / npcs/<id>.json: Look (the default for every object
//                         of the type) | Data (name, stats, item effect, NPC greeting); delete when no floor places it.
//                         Opened by a click on the palette and it follows the palette (the same window shows the next
//                         type clicked); it does not block the editor, so painting goes on with it open. Apply keeps it
//                         open; changes not applied are asked about before it shows another type.
//   a new type (+)        a copy of an existing one under a new id (a new NPC also gets a one-page dialogue)
//
// Changes go into the working copies; Save writes them (names go into text.json).
#pragma once

#include "stage_project.h"

#include <QDialog>

#include <map>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QTabWidget;
class QVBoxLayout;
class StageEditor;

class ObjectWindow : public QDialog {
    Q_OBJECT
public:
    enum Kind { Object, Type, New };
    static void f_OpenObject(StageEditor* l_pEd, int l_iX, int l_iY);
    static void f_OpenType(StageEditor* l_pEd, const std::string& l_strSem);
    // brush: what the new type is painted as once created (the palette group whose + made it: a door from Doors is its
    // gate "gate:<id>", from Keys its key "item:<id>"); "" = "<cat>:<id>" ("gate:<id>" for a door and its key).
    // group: one of the palette's own categories the new type goes into ("" = its usual group).
    static void f_OpenNew(StageEditor* l_pEd, const std::string& l_strCat, const std::string& l_strBrush = std::string(),
                        const std::string& l_strGroup = std::string());
    static ObjectWindow* f_Current();   // the open one (the self test)
    Kind f_Kind() const { return m_eKind; }
    const std::string& f_Sem() const { return m_strSem; }
    void f_Apply();                     // Apply / Create
    void f_SetLookSprite(const std::string& l_strSprite);   // as a click in the sprite grid
    void f_LinkEvent(const std::string& l_strId);           // as "+ Link"
    void f_SetNewId(const std::string& l_strId, const std::string& l_strFrom);

private:
    ObjectWindow(StageEditor* l_pEd, Kind l_eKind);
    void f_Render();
    void f_LoadType(const std::string& l_strSem);   // a type's record into the working copy (also: the window follows the palette)
    bool f_Dirty() const;                      // the working copy differs from what was loaded / applied
    bool f_ConfirmLeave();                     // changes not applied: Apply / Discard / Cancel (false: stay)
    QWidget* f_LookTab();
    QWidget* f_EventsTab();
    QWidget* f_DataTab();
    QWidget* f_NewForm();
    void f_DoDelete();
    // the look being edited: an object's own, or a type's ("sprite" in its record, the rest in its "look")
    stage::Json f_Look() const;
    void f_SetLook(const std::string& l_strKey, const stage::Json& l_jV);   // null = remove

    StageEditor* m_pEd;
    Kind m_eKind;
    int m_iX = 0, m_iY = 0;
    std::string m_strSem, m_strCat, m_strId, m_strFrom;
    std::string m_strBrushPrefix;                       // New: the brush once created ("gate:", "item:", ...)
    std::string m_strGroup;                             // New: the palette category it goes into
    stage::Json m_jW;                                 // working copy: { look, events } or the type's record
    std::map<std::string, stage::Json> m_mapTexts;      // name / desc / greeting: text.json values
    stage::Json m_jOrig;                              // as loaded or last applied (dirty())
    std::map<std::string, stage::Json> m_mapOrigTexts;
    QString m_oSpriteFilter;
    int m_iTab = 0;

    QLabel* m_pHead = nullptr;
    QLabel* m_pNote = nullptr;
    QTabWidget* m_pTabs = nullptr;
    QVBoxLayout* m_pBody = nullptr;
    QWidget* m_pContent = nullptr;
    QPushButton *m_pDel = nullptr, *m_pApply = nullptr;
};
