// uied_logic.h -- the UI editor's text work, without widgets (docs/gameplay/01_RMLUI.md "The UI editor"): which files a
// screen is made of, where an element is written, which style rules reach it, and where an RmlUi message points.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace uied {

// The UI folder's documents: every .rml, then every .rcss, by name.
QStringList uiFiles(const QString& dir);

// The style sheets an .rml links (<link type="text/rcss" href="...">), as written, in order.
QStringList linkedStyleSheets(const QString& rmlText);

// The 1-based line where the .rml gives an element `id="<id>"`, 0 = none.
int lineOfId(const QString& rmlText, const QString& id);
// The 1-based line of the first element with this tag and every one of these classes, 0 = none (an element
// without an id: the inspector's next best guess).
int lineOfTagWithClasses(const QString& rmlText, const QString& tag, const QStringList& classes);

// A style rule whose selector names an element's id, one of its classes or its tag.
struct RuleHit {
    QString file;   // the style sheet's file name
    int line = 0;   // 1-based, the line of the selector
    QString text;   // the rule's selector line, trimmed
};
QList<RuleHit> rulesFor(const QString& file, const QString& rcssText, const QString& id, const QStringList& classes,
                        const QString& tag);

// What the text cursor is on, as RmlUi selectors for the elements to glow in the game (the editor's hint):
//  - in an .rml: the element whose start tag the cursor is in (from its "<" to its ">"): "#id", else "tag.class..."
//    (the classes written in class="..."; data-class-* ones change at run time and are left out). The document's
//    frame (rml, head, body, ...) gives nothing.
//  - in an .rcss: the rule the cursor is in (its selector or its block), one selector per comma, pseudo-classes
//    (:hover, :active, ...) dropped; an @media block's own line gives nothing.
// `pos` is a character offset in the text. Empty = nothing to glow.
QStringList selectorsAtRml(const QString& rmlText, int pos);
QStringList selectorsAtRcss(const QString& rcssText, int pos);

// The Preview list (the test scenarios, tests/scenarios/<folder>/scenario.json):
//  - readScenario: the file with what it includes ("include": [...], relative to it) under it, as the game reads it:
//    objects merged key by key, the file's own fields on top (enough for its scene and the scene's fields);
//  - previewLabel: what the list shows, by screen, e.g. "Player menu: Status  (menu_status)", "Battle  (battle_bow)";
//    `order` sorts the list: the screens in the game's order, the player menu's tabs in their own.
QJsonObject readScenario(const QString& file);
QString previewLabel(const QJsonObject& scenario, const QString& folder, int* order = nullptr);

// What an RmlUi log message points at: a .rml / .rcss file name and a line, when it says them.
struct Problem {
    int level = 0;       // 0 info, 1 warning, 2 error
    QString file;        // "battle.rcss", or empty
    int line = 0;        // 1-based, 0 = not said
    QString text;
};
Problem parseProblem(int level, const QString& message);

}  // namespace uied
