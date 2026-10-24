// uied_logic.cpp -- see uied_logic.h.
#include "uied_logic.h"

#include <QDir>
#include <QRegularExpression>

namespace uied {

QStringList uiFiles(const QString& dir) {
    const QDir d(dir);
    QStringList out = d.entryList({QStringLiteral("*.rml")}, QDir::Files, QDir::Name);
    out += d.entryList({QStringLiteral("*.rcss")}, QDir::Files, QDir::Name);
    return out;
}

QStringList linkedStyleSheets(const QString& rmlText) {
    static const QRegularExpression link(QStringLiteral(R"(<link\b[^>]*\bhref\s*=\s*"([^"]+\.rcss)\")"),
                                         QRegularExpression::CaseInsensitiveOption);
    QStringList out;
    for (auto it = link.globalMatch(rmlText); it.hasNext();) out << it.next().captured(1);
    return out;
}

int lineOfId(const QString& rmlText, const QString& id) {
    if (id.isEmpty()) return 0;
    const QRegularExpression re(QStringLiteral(R"(\bid\s*=\s*"%1")").arg(QRegularExpression::escape(id)));
    const QStringList lines = rmlText.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); i++)
        if (re.match(lines[i]).hasMatch()) return i + 1;
    return 0;
}

int lineOfTagWithClasses(const QString& rmlText, const QString& tag, const QStringList& classes) {
    if (tag.isEmpty()) return 0;
    const QRegularExpression open(QStringLiteral(R"(<%1\b([^>]*))").arg(QRegularExpression::escape(tag)));
    static const QRegularExpression cls(QStringLiteral(R"(\bclass\s*=\s*"([^"]*)\")"));
    const QStringList lines = rmlText.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); i++) {
        for (auto it = open.globalMatch(lines[i]); it.hasNext();) {
            const QString attrs = it.next().captured(1);
            const QStringList has = cls.match(attrs).captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            bool all = true;
            for (const QString& c : classes) all = all && has.contains(c);
            if (all) return i + 1;
        }
    }
    return 0;
}

QList<RuleHit> rulesFor(const QString& file, const QString& rcssText, const QString& id, const QStringList& classes,
                        const QString& tag) {
    QList<RuleHit> out;
    QStringList patterns;   // a selector token: #id, .class, or the tag as a whole word not after # . -
    if (!id.isEmpty()) patterns << QStringLiteral("#%1(?![\\w-])").arg(QRegularExpression::escape(id));
    for (const QString& c : classes) patterns << QStringLiteral("\\.%1(?![\\w-])").arg(QRegularExpression::escape(c));
    if (!tag.isEmpty()) patterns << QStringLiteral("(?<![\\w#.:-])%1(?![\\w-])").arg(QRegularExpression::escape(tag));
    if (patterns.isEmpty()) return out;
    const QRegularExpression token(patterns.join(QLatin1Char('|')));
    // Selectors are what comes before '{', outside comments; a rule may start on one line and open on the next.
    const QStringList lines = rcssText.split(QLatin1Char('\n'));
    bool inComment = false;
    QString selector;
    int selectorLine = 0;
    for (int i = 0; i < lines.size(); i++) {
        QString l = lines[i];
        QString code;   // the line without comments
        for (int k = 0; k < l.size(); k++) {
            if (inComment) {
                if (l.mid(k, 2) == QLatin1String("*/")) { inComment = false; k++; }
                continue;
            }
            if (l.mid(k, 2) == QLatin1String("/*")) { inComment = true; k++; continue; }
            code += l[k];
        }
        int depth = 0;
        for (int k = 0; k < code.size(); k++) {
            const QChar ch = code[k];
            if (ch == QLatin1Char('{')) {
                if (depth == 0 && token.match(selector).hasMatch())
                    out.append({file, selectorLine ? selectorLine : i + 1, lines[selectorLine ? selectorLine - 1 : i].trimmed()});
                depth++;
                selector.clear();
                selectorLine = 0;
            } else if (ch == QLatin1Char('}')) {
                depth = depth > 0 ? depth - 1 : 0;
                selector.clear();
                selectorLine = 0;
            } else if (depth == 0) {
                if (!ch.isSpace() && selectorLine == 0) selectorLine = i + 1;
                selector += ch;
            }
        }
        if (depth == 0) selector += QLatin1Char(' ');
    }
    return out;
}

Problem parseProblem(int level, const QString& message) {
    Problem p;
    p.level = level;
    p.text = message.trimmed();
    static const QRegularExpression file(QStringLiteral(R"(([\w\-]+\.(?:rml|rcss))\b)"));
    static const QRegularExpression lineNo(QStringLiteral(R"((?:\bline\s*|\.(?:rml|rcss):\s*)(\d+))"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch f = file.match(message);
    if (f.hasMatch()) p.file = f.captured(1);
    const QRegularExpressionMatch n = lineNo.match(message);
    if (n.hasMatch()) p.line = n.captured(1).toInt();
    return p;
}

}  // namespace uied
