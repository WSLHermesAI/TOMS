// uied_logic.h -- the UI editor's text work, without widgets (docs/gameplay/01_RMLUI.md "The UI editor"): which files a
// screen is made of, where an element is written, which style rules reach it, and where an RmlUi message points.
#pragma once

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

// What an RmlUi log message points at: a .rml / .rcss file name and a line, when it says them.
struct Problem {
    int level = 0;       // 0 info, 1 warning, 2 error
    QString file;        // "battle.rcss", or empty
    int line = 0;        // 1-based, 0 = not said
    QString text;
};
Problem parseProblem(int level, const QString& message);

}  // namespace uied
