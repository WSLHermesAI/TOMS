// uied_logic.h -- the UI editor's text work, without widgets (docs/gameplay/01_RMLUI.md "The UI editor"): which files a
// screen is made of, where an element is written, which style rules reach it, and where an RmlUi message points.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace uied {

// The UI folder's documents: every .rml, then every .rcss, by name.
QStringList f_UiFiles(const QString& l_oDir);

// The style sheets an .rml links (<link type="text/rcss" href="...">), as written, in order.
QStringList f_LinkedStyleSheets(const QString& l_oRmlText);

// The 1-based line where the .rml gives an element `id="<id>"`, 0 = none.
int f_LineOfId(const QString& l_oRmlText, const QString& l_oId);
// The 1-based line of the first element with this tag and every one of these classes, 0 = none (an element
// without an id: the inspector's next best guess).
int f_LineOfTagWithClasses(const QString& l_oRmlText, const QString& l_oTag, const QStringList& l_oClasses);

// A style rule whose selector names an element's id, one of its classes or its tag.
struct RuleHit {
    QString m_oFile;   // the style sheet's file name
    int m_iLine = 0;   // 1-based, the line of the selector
    QString m_oText;   // the rule's selector line, trimmed
};
QList<RuleHit> f_RulesFor(const QString& l_oFile, const QString& l_oRcssText, const QString& l_oId, const QStringList& l_oClasses,
                        const QString& l_oTag);

// What the text cursor is on, as RmlUi selectors for the elements to glow in the game (the editor's hint):
//  - in an .rml: the element whose start tag the cursor is in (from its "<" to its ">"): "#id", else "tag.class..."
//    (the classes written in class="..."; data-class-* ones change at run time and are left out). The document's
//    frame (rml, head, body, ...) gives nothing.
//  - in an .rcss: the rule the cursor is in (its selector or its block), one selector per comma, pseudo-classes
//    (:hover, :active, ...) dropped; an @media block's own line gives nothing.
// `pos` is a character offset in the text. Empty = nothing to glow.
QStringList f_SelectorsAtRml(const QString& l_oRmlText, int l_iPos);
QStringList f_SelectorsAtRcss(const QString& l_oRcssText, int l_iPos);

// The Preview list (the test scenarios, tests/scenarios/<folder>/scenario.json):
//  - readScenario: the file with what it includes ("include": [...], relative to it) under it, as the game reads it:
//    objects merged key by key, the file's own fields on top (enough for its scene and the scene's fields);
//  - previewLabel: what the list shows, by screen, e.g. "Player menu: Status  (menu_status)", "Battle  (battle_bow)";
//    `order` sorts the list: the screens in the game's order, the player menu's tabs in their own.
QJsonObject f_ReadScenario(const QString& l_oFile);
QString f_PreviewLabel(const QJsonObject& l_oScenario, const QString& l_oFolder, int* l_pOrder = nullptr);

// What an RmlUi log message points at: a .rml / .rcss file name and a line, when it says them.
struct Problem {
    int m_iLevel = 0;       // 0 info, 1 warning, 2 error
    QString m_oFile;        // "battle.rcss", or empty
    int m_iLine = 0;        // 1-based, 0 = not said
    QString m_oText;
};
Problem f_ParseProblem(int l_iLevel, const QString& l_oMessage);

}  // namespace uied
