// uied_logic.cpp -- see uied_logic.h.
#include "uied_logic.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <vector>

namespace uied {

QStringList uiFiles(const QString& dir) {
    const QDir d(dir);
    QStringList out = d.entryList({QStringLiteral("*.rml")}, QDir::Files, QDir::Name);
    out += d.entryList({QStringLiteral("*.rcss")}, QDir::Files, QDir::Name);
    return out;
}

namespace {

QJsonObject mergeOnto(QJsonObject base, const QJsonObject& top) {   // objects key by key, the rest replaced
    for (auto it = top.begin(); it != top.end(); ++it) {
        if (it.value().isObject() && base.value(it.key()).isObject())
            base[it.key()] = mergeOnto(base.value(it.key()).toObject(), it.value().toObject());
        else
            base[it.key()] = it.value();
    }
    return base;
}

QJsonObject readScenarioAt(const QString& file, int depth) {
    QFile f(file);
    if (depth > 8 || !f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject own = QJsonDocument::fromJson(f.readAll()).object();
    QJsonObject out;
    for (const QJsonValue& inc : own.value(QStringLiteral("include")).toArray())
        out = mergeOnto(out, readScenarioAt(QFileInfo(file).dir().filePath(inc.toString()), depth + 1));
    return mergeOnto(out, own);
}

}  // namespace

QJsonObject readScenario(const QString& file) { return readScenarioAt(file, 0); }

QString previewLabel(const QJsonObject& sc, const QString& folder, int* order) {
    struct Screen { const char* scene; const char* name; int order; };
    static const Screen kScreens[] = {
        {"title", "Title screen", 0},           {"stage", "Map and HUD", 10},        {"menu", "Player menu", 20},
        {"saveLoad", "Save / Load", 30},        {"stageSelect", "Stage select", 31}, {"stairsConfirm", "Stairs: go up / down?", 32},
        {"notice", "Notice", 33},               {"dialogue", "Dialogue", 40},        {"talk", "Talking to an NPC", 41},
        {"hub", "Village hub", 42},             {"store", "Store", 50},              {"battle", "Battle", 60},
        {"battleWon", "Victory", 61},           {"arena", "Weapon arena", 62},       {"weaponTrial", "Weapon trial (start story)", 63},
        {"miniGame", "Mini-game", 70},          {"ending", "Ending", 80},
    };
    static const char* kTabs[][2] = {{"status", "Status"}, {"gear", "Equipment"}, {"items", "Items"},
                                     {"events", "Events"}, {"cards", "Cards"},    {"system", "System"}};
    const QString scene = sc.value(QStringLiteral("scene")).toString();
    QString name = scene.isEmpty() ? QStringLiteral("Game") : scene;
    int o = 90;
    for (const Screen& s : kScreens)
        if (scene == QLatin1String(s.scene)) { name = QString::fromLatin1(s.name); o = s.order; }
    if (scene == QStringLiteral("menu")) {   // one entry per tab, in the tabs' order
        const QString tab = sc.value(QStringLiteral("menu")).toObject().value(QStringLiteral("tab")).toString();
        for (int i = 0; i < 6; i++)
            if (tab == QLatin1String(kTabs[i][0])) { name += QStringLiteral(": ") + QString::fromLatin1(kTabs[i][1]); o += i; }
    } else if (scene == QStringLiteral("saveLoad")) {
        const QString mode = sc.value(QStringLiteral("saveLoad")).toObject().value(QStringLiteral("mode")).toString();
        if (!mode.isEmpty()) name += QStringLiteral(" (") + mode + QLatin1Char(')');
    }
    if (order) *order = o;
    return name + QStringLiteral("  (") + folder + QLatin1Char(')');
}

QStringList selectorsAtRml(const QString& rml, int pos) {
    pos = std::clamp(pos, 0, (int)rml.size());
    // the start tag around the cursor: the last "<" before it whose ">" is at or after it
    const int open = rml.lastIndexOf(QLatin1Char('<'), pos > 0 && pos == rml.size() ? pos - 1 : pos);
    if (open < 0) return {};
    const int close = rml.indexOf(QLatin1Char('>'), open);
    if (close < 0 || close < pos - 1) return {};   // the cursor is between tags (text, or after a ">")
    const QString tag = rml.mid(open, close - open + 1);
    static const QRegularExpression name(QStringLiteral(R"(^<([A-Za-z][\w-]*))"));
    const QRegularExpressionMatch n = name.match(tag);
    if (!n.hasMatch()) return {};   // an end tag, a comment, <?...>
    const QString t = n.captured(1).toLower();
    static const QStringList frame = {QStringLiteral("rml"), QStringLiteral("head"), QStringLiteral("body"),
                                      QStringLiteral("title"), QStringLiteral("link"), QStringLiteral("style"),
                                      QStringLiteral("script"), QStringLiteral("template")};
    if (frame.contains(t)) return {};
    static const QRegularExpression id(QStringLiteral(R"re(\bid\s*=\s*"([^"{}]+)")re"));
    const QRegularExpressionMatch im = id.match(tag);
    if (im.hasMatch()) return {QLatin1Char('#') + im.captured(1).trimmed()};
    static const QRegularExpression cls(QStringLiteral(R"re((?<![\w-])class\s*=\s*"([^"]*)")re"));
    QString sel = t;
    const QRegularExpressionMatch cm = cls.match(tag);
    if (cm.hasMatch())
        for (const QString& c : cm.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            if (!c.contains(QLatin1Char('{'))) sel += QLatin1Char('.') + c;
    return {sel};
}

QStringList selectorsAtRcss(const QString& css0, int pos) {
    // comments blanked out (same length, so offsets stay)
    QString css = css0;
    for (int a = css.indexOf(QStringLiteral("/*")); a >= 0; a = css.indexOf(QStringLiteral("/*"), a)) {
        int b = css.indexOf(QStringLiteral("*/"), a + 2);
        b = b < 0 ? css.size() : b + 2;
        for (int i = a; i < b; i++) if (css[i] != QLatin1Char('\n')) css[i] = QLatin1Char(' ');
        a = b;
    }
    pos = std::clamp(pos, 0, (int)css.size());
    // walk the blocks: each "{" starts a rule (or an @media block) whose selector began after the last "{" / "}" / ";"
    struct Open { int selStart, brace; bool at; };
    std::vector<Open> stack;
    int segStart = 0;
    for (int i = 0; i < css.size(); i++) {
        const QChar ch = css[i];
        if (ch == QLatin1Char('{')) {
            const QString sel = css.mid(segStart, i - segStart).trimmed();
            int first = segStart;   // the rule starts at its selector's first character (not in a comment before it)
            while (first < i && css[first].isSpace()) first++;
            stack.push_back({first, i, sel.startsWith(QLatin1Char('@'))});
            segStart = i + 1;
        } else if (ch == QLatin1Char('}')) {
            if (!stack.empty()) {
                const Open o = stack.back();
                stack.pop_back();
                if (!o.at && pos >= o.selStart && pos <= i) {   // the cursor is in this rule: its selector or its block
                    QStringList out;
                    for (QString s : css.mid(o.selStart, o.brace - o.selStart).split(QLatin1Char(','))) {
                        static const QRegularExpression pseudo(QStringLiteral(R"(::?[A-Za-z-]+(\([^)]*\))?)"));
                        s = s.remove(pseudo).simplified();
                        if (!s.isEmpty()) out << s;
                    }
                    return out;
                }
            }
            segStart = i + 1;
        } else if (ch == QLatin1Char(';') && stack.empty()) {
            segStart = i + 1;   // an @import / @charset line
        }
    }
    return {};
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
