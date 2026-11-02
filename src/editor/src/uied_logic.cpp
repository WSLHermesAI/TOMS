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

QStringList f_UiFiles(const QString& l_oDir) {
    const QDir l_oD(l_oDir);
    QStringList l_oOut = l_oD.entryList({QStringLiteral("*.rml")}, QDir::Files, QDir::Name);
    l_oOut += l_oD.entryList({QStringLiteral("*.rcss")}, QDir::Files, QDir::Name);
    return l_oOut;
}

namespace {

QJsonObject f_MergeOnto(QJsonObject l_oBase, const QJsonObject& l_oTop) {   // objects key by key, the rest replaced
    for (auto l_oIt = l_oTop.begin(); l_oIt != l_oTop.end(); ++l_oIt) {
        if (l_oIt.value().isObject() && l_oBase.value(l_oIt.key()).isObject())
            l_oBase[l_oIt.key()] = f_MergeOnto(l_oBase.value(l_oIt.key()).toObject(), l_oIt.value().toObject());
        else
            l_oBase[l_oIt.key()] = l_oIt.value();
    }
    return l_oBase;
}

QJsonObject f_ReadScenarioAt(const QString& l_oFile, int l_iDepth) {
    QFile l_oF(l_oFile);
    if (l_iDepth > 8 || !l_oF.open(QIODevice::ReadOnly)) return {};
    const QJsonObject l_oOwn = QJsonDocument::fromJson(l_oF.readAll()).object();
    QJsonObject l_oOut;
    for (const QJsonValue& l_oInc : l_oOwn.value(QStringLiteral("include")).toArray())
        l_oOut = f_MergeOnto(l_oOut, f_ReadScenarioAt(QFileInfo(l_oFile).dir().filePath(l_oInc.toString()), l_iDepth + 1));
    return f_MergeOnto(l_oOut, l_oOwn);
}

}  // namespace

QJsonObject f_ReadScenario(const QString& l_oFile) { return f_ReadScenarioAt(l_oFile, 0); }

QString f_PreviewLabel(const QJsonObject& l_oSc, const QString& l_oFolder, int* l_pOrder) {
    struct Screen { const char* m_pszScene; const char* m_pszName; int m_iOrder; };
    static const Screen l_arrScreens[] = {
        {"title", "Title screen", 0},           {"stage", "Map and HUD", 10},        {"menu", "Player menu", 20},
        {"saveLoad", "Save / Load", 30},        {"stageSelect", "Stage select", 31}, {"stairsConfirm", "Stairs: go up / down?", 32},
        {"notice", "Notice", 33},               {"dialogue", "Dialogue", 40},        {"talk", "Talking to an NPC", 41},
        {"hub", "Village hub", 42},             {"store", "Store", 50},              {"battle", "Battle", 60},
        {"battleWon", "Victory", 61},           {"arena", "Weapon arena", 62},       {"weaponTrial", "Weapon trial (start story)", 63},
        {"miniGame", "Mini-game", 70},          {"ending", "Ending", 80},
    };
    static const char* l_arrTabs[][2] = {{"status", "Status"}, {"gear", "Equipment"}, {"items", "Items"},
                                     {"events", "Events"}, {"cards", "Cards"},    {"system", "System"}};
    const QString l_oScene = l_oSc.value(QStringLiteral("scene")).toString();
    QString l_oName = l_oScene.isEmpty() ? QStringLiteral("Game") : l_oScene;
    int l_iO = 90;
    for (const Screen& l_oS : l_arrScreens)
        if (l_oScene == QLatin1String(l_oS.m_pszScene)) { l_oName = QString::fromLatin1(l_oS.m_pszName); l_iO = l_oS.m_iOrder; }
    if (l_oScene == QStringLiteral("menu")) {   // one entry per tab, in the tabs' order
        const QString l_oTab = l_oSc.value(QStringLiteral("menu")).toObject().value(QStringLiteral("tab")).toString();
        for (int l_iI = 0; l_iI < 6; l_iI++)
            if (l_oTab == QLatin1String(l_arrTabs[l_iI][0])) { l_oName += QStringLiteral(": ") + QString::fromLatin1(l_arrTabs[l_iI][1]); l_iO += l_iI; }
    } else if (l_oScene == QStringLiteral("saveLoad")) {
        const QString l_oMode = l_oSc.value(QStringLiteral("saveLoad")).toObject().value(QStringLiteral("mode")).toString();
        if (!l_oMode.isEmpty()) l_oName += QStringLiteral(" (") + l_oMode + QLatin1Char(')');
    }
    if (l_pOrder) *l_pOrder = l_iO;
    return l_oName + QStringLiteral("  (") + l_oFolder + QLatin1Char(')');
}

QStringList f_SelectorsAtRml(const QString& l_oRml, int l_iPos) {
    l_iPos = std::clamp(l_iPos, 0, (int)l_oRml.size());
    // the start tag around the cursor: the last "<" before it whose ">" is at or after it
    const int l_iOpen = l_oRml.lastIndexOf(QLatin1Char('<'), l_iPos > 0 && l_iPos == l_oRml.size() ? l_iPos - 1 : l_iPos);
    if (l_iOpen < 0) return {};
    const int l_iClose = l_oRml.indexOf(QLatin1Char('>'), l_iOpen);
    if (l_iClose < 0 || l_iClose < l_iPos - 1) return {};   // the cursor is between tags (text, or after a ">")
    const QString l_oTag = l_oRml.mid(l_iOpen, l_iClose - l_iOpen + 1);
    static const QRegularExpression l_oName(QStringLiteral(R"(^<([A-Za-z][\w-]*))"));
    const QRegularExpressionMatch l_oN = l_oName.match(l_oTag);
    if (!l_oN.hasMatch()) return {};   // an end tag, a comment, <?...>
    const QString l_oT = l_oN.captured(1).toLower();
    static const QStringList l_oFrame = {QStringLiteral("rml"), QStringLiteral("head"), QStringLiteral("body"),
                                      QStringLiteral("title"), QStringLiteral("link"), QStringLiteral("style"),
                                      QStringLiteral("script"), QStringLiteral("template")};
    if (l_oFrame.contains(l_oT)) return {};
    static const QRegularExpression l_oId(QStringLiteral(R"re(\bid\s*=\s*"([^"{}]+)")re"));
    const QRegularExpressionMatch l_oIm = l_oId.match(l_oTag);
    if (l_oIm.hasMatch()) return {QLatin1Char('#') + l_oIm.captured(1).trimmed()};
    static const QRegularExpression l_oCls(QStringLiteral(R"re((?<![\w-])class\s*=\s*"([^"]*)")re"));
    QString l_oSel = l_oT;
    const QRegularExpressionMatch l_oCm = l_oCls.match(l_oTag);
    if (l_oCm.hasMatch())
        for (const QString& l_oC : l_oCm.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            if (!l_oC.contains(QLatin1Char('{'))) l_oSel += QLatin1Char('.') + l_oC;
    return {l_oSel};
}

QStringList f_SelectorsAtRcss(const QString& l_oCss0, int l_iPos) {
    // comments blanked out (same length, so offsets stay)
    QString l_oCss = l_oCss0;
    for (int l_iA = l_oCss.indexOf(QStringLiteral("/*")); l_iA >= 0; l_iA = l_oCss.indexOf(QStringLiteral("/*"), l_iA)) {
        int l_iB = l_oCss.indexOf(QStringLiteral("*/"), l_iA + 2);
        l_iB = l_iB < 0 ? l_oCss.size() : l_iB + 2;
        for (int l_iI = l_iA; l_iI < l_iB; l_iI++) if (l_oCss[l_iI] != QLatin1Char('\n')) l_oCss[l_iI] = QLatin1Char(' ');
        l_iA = l_iB;
    }
    l_iPos = std::clamp(l_iPos, 0, (int)l_oCss.size());
    // walk the blocks: each "{" starts a rule (or an @media block) whose selector began after the last "{" / "}" / ";"
    struct Open { int m_iSelStart, m_iBrace; bool m_bAt; };
    std::vector<Open> l_vStack;
    int l_iSegStart = 0;
    for (int l_iI = 0; l_iI < l_oCss.size(); l_iI++) {
        const QChar l_oCh = l_oCss[l_iI];
        if (l_oCh == QLatin1Char('{')) {
            const QString l_oSel = l_oCss.mid(l_iSegStart, l_iI - l_iSegStart).trimmed();
            int l_iFirst = l_iSegStart;   // the rule starts at its selector's first character (not in a comment before it)
            while (l_iFirst < l_iI && l_oCss[l_iFirst].isSpace()) l_iFirst++;
            l_vStack.push_back({l_iFirst, l_iI, l_oSel.startsWith(QLatin1Char('@'))});
            l_iSegStart = l_iI + 1;
        } else if (l_oCh == QLatin1Char('}')) {
            if (!l_vStack.empty()) {
                const Open l_oO = l_vStack.back();
                l_vStack.pop_back();
                if (!l_oO.m_bAt && l_iPos >= l_oO.m_iSelStart && l_iPos <= l_iI) {   // the cursor is in this rule: its selector or its block
                    QStringList l_oOut;
                    for (QString l_oS : l_oCss.mid(l_oO.m_iSelStart, l_oO.m_iBrace - l_oO.m_iSelStart).split(QLatin1Char(','))) {
                        static const QRegularExpression l_oPseudo(QStringLiteral(R"(::?[A-Za-z-]+(\([^)]*\))?)"));
                        l_oS = l_oS.remove(l_oPseudo).simplified();
                        if (!l_oS.isEmpty()) l_oOut << l_oS;
                    }
                    return l_oOut;
                }
            }
            l_iSegStart = l_iI + 1;
        } else if (l_oCh == QLatin1Char(';') && l_vStack.empty()) {
            l_iSegStart = l_iI + 1;   // an @import / @charset line
        }
    }
    return {};
}

QStringList f_LinkedStyleSheets(const QString& l_oRmlText) {
    static const QRegularExpression l_oLink(QStringLiteral(R"(<link\b[^>]*\bhref\s*=\s*"([^"]+\.rcss)\")"),
                                         QRegularExpression::CaseInsensitiveOption);
    QStringList l_oOut;
    for (auto l_oIt = l_oLink.globalMatch(l_oRmlText); l_oIt.hasNext();) l_oOut << l_oIt.next().captured(1);
    return l_oOut;
}

int f_LineOfId(const QString& l_oRmlText, const QString& l_oId) {
    if (l_oId.isEmpty()) return 0;
    const QRegularExpression l_oRe(QStringLiteral(R"(\bid\s*=\s*"%1")").arg(QRegularExpression::escape(l_oId)));
    const QStringList l_oLines = l_oRmlText.split(QLatin1Char('\n'));
    for (int l_iI = 0; l_iI < l_oLines.size(); l_iI++)
        if (l_oRe.match(l_oLines[l_iI]).hasMatch()) return l_iI + 1;
    return 0;
}

int f_LineOfTagWithClasses(const QString& l_oRmlText, const QString& l_oTag, const QStringList& l_oClasses) {
    if (l_oTag.isEmpty()) return 0;
    const QRegularExpression l_oOpen(QStringLiteral(R"(<%1\b([^>]*))").arg(QRegularExpression::escape(l_oTag)));
    static const QRegularExpression l_oCls(QStringLiteral(R"(\bclass\s*=\s*"([^"]*)\")"));
    const QStringList l_oLines = l_oRmlText.split(QLatin1Char('\n'));
    for (int l_iI = 0; l_iI < l_oLines.size(); l_iI++) {
        for (auto l_oIt = l_oOpen.globalMatch(l_oLines[l_iI]); l_oIt.hasNext();) {
            const QString l_oAttrs = l_oIt.next().captured(1);
            const QStringList l_oHas = l_oCls.match(l_oAttrs).captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            bool l_bAll = true;
            for (const QString& l_oC : l_oClasses) l_bAll = l_bAll && l_oHas.contains(l_oC);
            if (l_bAll) return l_iI + 1;
        }
    }
    return 0;
}

QList<RuleHit> f_RulesFor(const QString& l_oFile, const QString& l_oRcssText, const QString& l_oId, const QStringList& l_oClasses,
                        const QString& l_oTag) {
    QList<RuleHit> l_oOut;
    QStringList l_oPatterns;   // a selector token: #id, .class, or the tag as a whole word not after # . -
    if (!l_oId.isEmpty()) l_oPatterns << QStringLiteral("#%1(?![\\w-])").arg(QRegularExpression::escape(l_oId));
    for (const QString& l_oC : l_oClasses) l_oPatterns << QStringLiteral("\\.%1(?![\\w-])").arg(QRegularExpression::escape(l_oC));
    if (!l_oTag.isEmpty()) l_oPatterns << QStringLiteral("(?<![\\w#.:-])%1(?![\\w-])").arg(QRegularExpression::escape(l_oTag));
    if (l_oPatterns.isEmpty()) return l_oOut;
    const QRegularExpression l_oToken(l_oPatterns.join(QLatin1Char('|')));
    // Selectors are what comes before '{', outside comments; a rule may start on one line and open on the next.
    const QStringList l_oLines = l_oRcssText.split(QLatin1Char('\n'));
    bool l_bInComment = false;
    QString l_oSelector;
    int l_iSelectorLine = 0;
    for (int l_iI = 0; l_iI < l_oLines.size(); l_iI++) {
        QString l_oL = l_oLines[l_iI];
        QString l_oCode;   // the line without comments
        for (int l_iK = 0; l_iK < l_oL.size(); l_iK++) {
            if (l_bInComment) {
                if (l_oL.mid(l_iK, 2) == QLatin1String("*/")) { l_bInComment = false; l_iK++; }
                continue;
            }
            if (l_oL.mid(l_iK, 2) == QLatin1String("/*")) { l_bInComment = true; l_iK++; continue; }
            l_oCode += l_oL[l_iK];
        }
        int l_iDepth = 0;
        for (int l_iK = 0; l_iK < l_oCode.size(); l_iK++) {
            const QChar l_oCh = l_oCode[l_iK];
            if (l_oCh == QLatin1Char('{')) {
                if (l_iDepth == 0 && l_oToken.match(l_oSelector).hasMatch())
                    l_oOut.append({l_oFile, l_iSelectorLine ? l_iSelectorLine : l_iI + 1, l_oLines[l_iSelectorLine ? l_iSelectorLine - 1 : l_iI].trimmed()});
                l_iDepth++;
                l_oSelector.clear();
                l_iSelectorLine = 0;
            } else if (l_oCh == QLatin1Char('}')) {
                l_iDepth = l_iDepth > 0 ? l_iDepth - 1 : 0;
                l_oSelector.clear();
                l_iSelectorLine = 0;
            } else if (l_iDepth == 0) {
                if (!l_oCh.isSpace() && l_iSelectorLine == 0) l_iSelectorLine = l_iI + 1;
                l_oSelector += l_oCh;
            }
        }
        if (l_iDepth == 0) l_oSelector += QLatin1Char(' ');
    }
    return l_oOut;
}

Problem f_ParseProblem(int l_iLevel, const QString& l_oMessage) {
    Problem l_oP;
    l_oP.m_iLevel = l_iLevel;
    l_oP.m_oText = l_oMessage.trimmed();
    static const QRegularExpression l_oFile(QStringLiteral(R"(([\w\-]+\.(?:rml|rcss))\b)"));
    static const QRegularExpression l_oLineNo(QStringLiteral(R"((?:\bline\s*|\.(?:rml|rcss):\s*)(\d+))"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch l_oF = l_oFile.match(l_oMessage);
    if (l_oF.hasMatch()) l_oP.m_oFile = l_oF.captured(1);
    const QRegularExpressionMatch l_oN = l_oLineNo.match(l_oMessage);
    if (l_oN.hasMatch()) l_oP.m_iLine = l_oN.captured(1).toInt();
    return l_oP;
}

}  // namespace uied
