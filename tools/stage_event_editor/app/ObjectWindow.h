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
    static void openObject(StageEditor* ed, int x, int y);
    static void openType(StageEditor* ed, const std::string& sem);
    // brush: what the new type is painted as once created (the palette group whose + made it: a door from Doors is its
    // gate "gate:<id>", from Keys its key "item:<id>"); "" = "<cat>:<id>" ("gate:<id>" for a door and its key).
    // group: one of the palette's own categories the new type goes into ("" = its usual group).
    static void openNew(StageEditor* ed, const std::string& cat, const std::string& brush = std::string(),
                        const std::string& group = std::string());
    static ObjectWindow* current();   // the open one (the self test)
    Kind kind() const { return m_kind; }
    const std::string& sem() const { return m_sem; }
    void apply();                     // Apply / Create
    void setLookSprite(const std::string& sprite);   // as a click in the sprite grid
    void linkEvent(const std::string& id);           // as "+ Link"
    void setNewId(const std::string& id, const std::string& from);

private:
    ObjectWindow(StageEditor* ed, Kind kind);
    void render();
    void loadType(const std::string& sem);   // a type's record into the working copy (also: the window follows the palette)
    bool dirty() const;                      // the working copy differs from what was loaded / applied
    bool confirmLeave();                     // changes not applied: Apply / Discard / Cancel (false: stay)
    QWidget* lookTab();
    QWidget* eventsTab();
    QWidget* dataTab();
    QWidget* newForm();
    void doDelete();
    // the look being edited: an object's own, or a type's ("sprite" in its record, the rest in its "look")
    stage::Json look() const;
    void setLook(const std::string& key, const stage::Json& v);   // null = remove

    StageEditor* m_ed;
    Kind m_kind;
    int m_x = 0, m_y = 0;
    std::string m_sem, m_cat, m_id, m_from;
    std::string m_brushPrefix;                       // New: the brush once created ("gate:", "item:", ...)
    std::string m_group;                             // New: the palette category it goes into
    stage::Json m_w;                                 // working copy: { look, events } or the type's record
    std::map<std::string, stage::Json> m_texts;      // name / desc / greeting: text.json values
    stage::Json m_orig;                              // as loaded or last applied (dirty())
    std::map<std::string, stage::Json> m_origTexts;
    QString m_spriteFilter;
    int m_tab = 0;

    QLabel* m_head = nullptr;
    QLabel* m_note = nullptr;
    QTabWidget* m_tabs = nullptr;
    QVBoxLayout* m_body = nullptr;
    QWidget* m_content = nullptr;
    QPushButton *m_del = nullptr, *m_apply = nullptr;
};
