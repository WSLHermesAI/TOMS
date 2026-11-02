// EventGraph.cpp -- the logic graph (EventGraph.h): the HTML event editor's node graph, native.
#include "EventGraph.h"

#include <QAction>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>

using Json = stage::Json;

namespace {
QString f_Q(const std::string& l_strS) { return QString::fromStdString(l_strS); }
std::string f_Str(const Json& l_jJ, const char* l_pszK) {
    return l_jJ.is_object() && l_jJ.contains(l_pszK) && l_jJ[l_pszK].is_string() ? l_jJ[l_pszK].get<std::string>() : std::string();
}
bool f_StartsWith(const std::string& l_strS, const char* l_pszP) { return l_strS.rfind(l_pszP, 0) == 0; }
std::string f_RefType(const std::string& l_strR) { const size_t l_nC = l_strR.find(':'); return l_nC == std::string::npos ? "any" : l_strR.substr(0, l_nC); }
std::string f_RefName(const std::string& l_strR) { const size_t l_nC = l_strR.find(':'); return l_nC == std::string::npos ? l_strR : l_strR.substr(l_nC + 1); }
QString f_NumText(const Json& l_jV) {
    if (l_jV.is_number_integer()) return QString::number(l_jV.get<long long>());
    if (l_jV.is_number()) return QString::number(l_jV.get<double>());
    if (l_jV.is_boolean()) return l_jV.get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
    if (l_jV.is_string()) return f_Q(l_jV.get<std::string>());
    return f_Q(l_jV.dump());
}

// the HTML editor's pin colours (PINCOL) and sizes (.nn / .pin / .nh)
QColor f_PinColor(const std::string& l_strKind) {
    static const std::map<std::string, QColor> l_mapC = {{"flow", QColor("#ffffff")}, {"var", QColor("#a98bff")}, {"counter", QColor("#3fc2b5")},
                                                    {"flag", QColor("#ff6b6b")}, {"event", QColor("#e0b44c")}, {"item", QColor("#7bd86a")},
                                                    {"any", QColor("#8b929c")}};
    auto l_itIt = l_mapC.find(l_strKind);
    return l_itIt == l_mapC.end() ? QColor("#8b929c") : l_itIt->second;
}
constexpr double g_dHeadH = 40, g_dRowH = 21, g_dBodyPad = 7, g_dMinW = 190, g_dDot = 10;
QFont f_TitleFont() { QFont l_oF; l_oF.setPixelSize(12); l_oF.setBold(true); return l_oF; }
QFont f_SubFont() { QFont l_oF; l_oF.setPixelSize(11); return l_oF; }
QFont f_PinFont(bool l_bGhost) { QFont l_oF; l_oF.setPixelSize(12); l_oF.setItalic(l_bGhost); return l_oF; }
constexpr const char* g_pszPosKey = "eventEditor/logicGraphPositions";
}  // namespace

LogicGraph::LogicGraph(stage::EventProject& l_oP, LogicGraphState& l_oSt, std::function<void(const QString&, std::function<void()>)> l_fnEdit,
                       std::function<QString(const Json&)> l_fnTextOf, QWidget* l_pParent)
    : QWidget(l_pParent), m_oP(l_oP), m_oSt(l_oSt), m_fnEdit(std::move(l_fnEdit)), m_fnTextOf(std::move(l_fnTextOf)) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    if (!m_oSt.m_bLoaded) {   // node positions from earlier runs
        m_oSt.m_bLoaded = true;
        const QJsonObject l_oO = QJsonDocument::fromJson(QSettings().value(g_pszPosKey).toByteArray()).object();
        for (auto l_oIt = l_oO.begin(); l_oIt != l_oO.end(); ++l_oIt) {
            const QJsonObject l_oP2 = l_oIt.value().toObject();
            m_oSt.m_mapPos[l_oIt.key().toStdString()] = QPointF(l_oP2.value(QStringLiteral("x")).toDouble(), l_oP2.value(QStringLiteral("y")).toDouble());
        }
        m_bFitPending = true;   // the first time: show the whole graph
    }
    m_pFlowTimer = new QTimer(this);
    m_pFlowTimer->setInterval(33);
    connect(m_pFlowTimer, &QTimer::timeout, this, [this] { m_dFlowOffset -= 1.0; update(); });
    f_Rebuild();
}

// ---- the model -------------------------------------------------------------------------------------------------------

QString LogicGraph::f_CondLabel(const Json& l_jL) const {
    const std::string l_strT = f_Str(l_jL, "type");
    if (l_strT == "varAtLeast") return f_Q(f_Str(l_jL, "var")) + QStringLiteral(" ≥ ") + f_NumText(l_jL.value("value", Json(0)));
    if (l_strT == "varEquals") return f_Q(f_Str(l_jL, "var")) + QStringLiteral(" = ") + f_NumText(l_jL.value("value", Json(0)));
    if (l_strT == "counterAtLeast") return f_Q(f_Str(l_jL, "counter")) + QStringLiteral(" ≥ ") + f_NumText(l_jL.value("min", Json(0)));
    if (l_strT == "runFlagSet") {
        const std::string l_strF = f_Str(l_jL, "flag");
        return f_StartsWith(l_strF, "event_") ? f_Q(l_strF.substr(6)) + QLatin1Char(' ') + tr("done") : QStringLiteral("⚑ ") + f_Q(l_strF);
    }
    return f_Q(l_jL.dump());
}

QString LogicGraph::f_ActLabel(const Json& l_jA) const {
    const std::string l_strT = f_Str(l_jA, "type");
    const auto l_fnSignedNum = [](const Json& l_jV) { const QString l_oS = f_NumText(l_jV); return (l_jV.is_number() && l_jV.get<double>() >= 0) ? QStringLiteral("+") + l_oS : l_oS; };
    if (l_strT == "addVar") return l_fnSignedNum(l_jA.value("delta", Json(0))) + QStringLiteral(" → ") + f_Q(f_Str(l_jA, "var"));
    if (l_strT == "setVar") return QStringLiteral("= ") + f_NumText(l_jA.value("value", Json(0))) + QStringLiteral(" → ") + f_Q(f_Str(l_jA, "var"));
    if (l_strT == "addCounter") return l_fnSignedNum(l_jA.value("delta", Json(0))) + QStringLiteral(" → ") + f_Q(f_Str(l_jA, "counter"));
    if (l_strT == "setFlag") return QStringLiteral("⚑ ") + f_Q(f_Str(l_jA, "flag"));
    if (l_strT == "give") return QStringLiteral("🎁 ") + f_Q(f_Str(l_jA, "itemId"));
    return f_Q(l_strT);
}

QString LogicGraph::f_TrigSub(const Json& l_jE) const {
    const Json& l_jTg = l_jE["trigger"];
    const std::string l_strOn = l_jTg.value("on", std::string("step"));
    if (l_strOn == "talk") return QStringLiteral("💬 ") + f_Q(l_jTg.value("npc", std::string()));
    if (l_strOn == "varChanged") return QStringLiteral("◆ ") + f_Q(l_jTg.value("var", std::string()));
    if (l_strOn == "chain") return tr("from a link");
    QString l_oS = tr("on step");
    QStringList l_oFl;
    for (const Json* l_pF : m_oP.f_FloorsOf(f_Str(l_jE, "id"))) l_oFl << f_Q(f_Str(*l_pF, "id"));
    if (!l_oFl.isEmpty()) l_oS += QStringLiteral(" · ") + l_oFl.join(QLatin1Char(' '));
    return l_oS;
}

bool LogicGraph::f_MakeNode(const std::string& l_strId, Node& l_oN) const {
    const std::string l_strK = f_RefType(l_strId), l_strName = f_RefName(l_strId);
    l_oN = Node();
    l_oN.m_strId = l_strId;
    l_oN.m_strType = l_strK;
    l_oN.m_strName = l_strName;
    if (l_strK == "ev") {
        const Json* l_pE = m_oP.f_Ev(l_strName);
        l_oN.m_vIns.push_back({"fire", "flow", tr("Fire")});
        l_oN.m_vOuts.push_back({"then", "flow", tr("Then")});
        l_oN.m_vOuts.push_back({"done", "event", tr("done")});
        if (l_pE) {
            const Json& l_jReq = (*l_pE)["req"];
            for (size_t l_nI = 0; l_nI < l_jReq.size(); l_nI++) l_oN.m_vIns.push_back({"req:" + std::to_string(l_nI), f_RefType(stage::f_LeafRef(l_jReq[l_nI])), f_CondLabel(l_jReq[l_nI])});
            const Json& l_jActs = (*l_pE)["actions"];
            for (size_t l_nI = 0; l_nI < l_jActs.size(); l_nI++) l_oN.m_vOuts.push_back({"act:" + std::to_string(l_nI), f_RefType(stage::f_ActRef(l_jActs[l_nI])), f_ActLabel(l_jActs[l_nI])});
        }
        l_oN.m_vIns.push_back({"+req", "any", tr("+ condition"), true});
        l_oN.m_vOuts.push_back({"+act", "any", tr("+ action"), true});
        l_oN.m_oTitle = f_Q(l_strName);
        l_oN.m_oSub = l_pE ? f_TrigSub(*l_pE) : tr("not defined");
        l_oN.m_oColor = l_pE ? QColor(f_Q(m_oP.f_KindColor(f_Str(*l_pE, "kind")))) : QColor("#ff5d5d");
        if (!l_oN.m_oColor.isValid()) l_oN.m_oColor = QColor("#8b929c");
        l_oN.m_bMissing = !l_pE;
    } else if (l_strK == "var") {
        const Json& l_jV = m_oP.m_jS["vars"];
        const bool l_bHas = l_jV.contains(l_strName);
        l_oN.m_oTitle = QStringLiteral("◆ ") + f_Q(l_strName);
        l_oN.m_oSub = l_bHas ? QStringLiteral("%1 · %2 · %3").arg(f_Q(l_jV[l_strName].value("type", std::string("int"))), f_NumText(l_jV[l_strName].value("default", Json(0))),
                                                          f_Q(l_jV[l_strName].value("scope", std::string())))
                    : tr("not defined");
        l_oN.m_oColor = f_PinColor("var");
        l_oN.m_vIns.push_back({"set", "var", tr("change")});
        l_oN.m_vOuts.push_back({"value", "var", tr("value")});
        l_oN.m_vOuts.push_back({"changed", "flow", tr("on change")});
        l_oN.m_bMissing = !l_bHas;
    } else if (l_strK == "counter") {
        const Json& l_jC = m_oP.m_jS["counters"];
        const bool l_bHas = l_jC.contains(l_strName);
        l_oN.m_oTitle = QStringLiteral("▲ ") + f_Q(l_strName);
        l_oN.m_oSub = l_bHas ? m_fnTextOf(l_jC[l_strName].value("label", Json())) : tr("not defined");
        l_oN.m_oColor = f_PinColor("counter");
        l_oN.m_vIns.push_back({"set", "counter", tr("change")});
        l_oN.m_vOuts.push_back({"value", "counter", tr("value")});
        l_oN.m_bMissing = !l_bHas;
    } else if (l_strK == "flag") {
        const Json& l_jF = m_oP.m_jS["flags"];
        const bool l_bHas = l_jF.contains(l_strName);
        l_oN.m_oTitle = QStringLiteral("⚑ ") + f_Q(l_strName);
        l_oN.m_oSub = l_bHas ? f_Q(l_jF[l_strName].value("scope", std::string())) : tr("not defined");
        l_oN.m_oColor = f_PinColor("flag");
        l_oN.m_vIns.push_back({"set", "flag", tr("change")});
        l_oN.m_vOuts.push_back({"value", "flag", tr("value")});
        l_oN.m_bMissing = !l_bHas;
    } else if (l_strK == "npc") {
        auto l_itIt = m_oP.m_mapNpcPlaces.find(l_strName);
        QStringList l_oFl;
        if (l_itIt != m_oP.m_mapNpcPlaces.end()) for (const std::string& l_strF : l_itIt->second) l_oFl << f_Q(l_strF);
        l_oN.m_oTitle = QStringLiteral("💬 ") + f_Q(l_strName);
        l_oN.m_oSub = l_oFl.isEmpty() ? tr("not placed") : l_oFl.join(QStringLiteral(", "));
        l_oN.m_oColor = QColor("#ff9f43");
        l_oN.m_vOuts.push_back({"talk", "flow", tr("on talk")});
        l_oN.m_bMissing = l_itIt == m_oP.m_mapNpcPlaces.end() && !m_oP.m_jS["npcs"].contains(l_strName);
    } else {
        return false;
    }
    f_Measure(l_oN);
    return true;
}

void LogicGraph::f_Measure(Node& l_oN) const {
    const QFontMetrics l_oTf(f_TitleFont()), l_oSf(f_SubFont());
    double l_dInW = 0, l_dOutW = 0;
    for (const Pin& l_oP : l_oN.m_vIns) l_dInW = std::max(l_dInW, (double)QFontMetrics(f_PinFont(l_oP.m_bGhost)).horizontalAdvance(l_oP.m_oLabel));
    for (const Pin& l_oP : l_oN.m_vOuts) l_dOutW = std::max(l_dOutW, (double)QFontMetrics(f_PinFont(l_oP.m_bGhost)).horizontalAdvance(l_oP.m_oLabel));
    const double l_dPins = (l_dInW > 0 ? 9 + g_dDot + 6 + l_dInW : 0) + 18 + (l_dOutW > 0 ? l_dOutW + 6 + g_dDot + 9 : 0);
    l_oN.m_dW = std::max({g_dMinW, l_dPins, (double)l_oTf.horizontalAdvance(l_oN.m_oTitle) + 22, (double)l_oSf.horizontalAdvance(l_oN.m_oSub) + 22});
    l_oN.m_dH = g_dHeadH + g_dBodyPad + g_dRowH * (double)std::max(l_oN.m_vIns.size(), l_oN.m_vOuts.size()) + g_dBodyPad;
}

void LogicGraph::f_Rebuild() {
    m_mapNodes.clear();
    m_vOrder.clear();
    m_vLinks.clear();
    m_setUsed.clear();
    const auto l_fnAdd = [this](const std::string& l_strId) -> Node* {
        auto l_itIt = m_mapNodes.find(l_strId);
        if (l_itIt != m_mapNodes.end()) return &l_itIt->second;
        Node l_oN;
        if (!f_MakeNode(l_strId, l_oN)) return nullptr;
        m_vOrder.push_back(l_strId);
        return &(m_mapNodes[l_strId] = l_oN);
    };
    // the events with logic, or that another event links to or waits for
    const Json& l_jEvents = m_oP.m_jS["events"];
    for (const Json& l_jE : l_jEvents) {
        const std::string l_strId = f_Str(l_jE, "id");
        bool l_bLinked = m_oP.f_HasLogic(l_jE);
        for (const Json& l_jO : l_jEvents) {
            if (l_bLinked) break;
            for (const Json& l_jX : l_jO["next"]) if (l_jX.is_string() && l_jX.get<std::string>() == l_strId) l_bLinked = true;
            for (const Json& l_jL : l_jO["req"]) if (stage::f_LeafRef(l_jL) == "event:" + l_strId) l_bLinked = true;
        }
        if (l_bLinked) l_fnAdd("ev:" + l_strId);
    }
    for (const std::string& l_strX : m_oSt.m_setExtra) l_fnAdd(l_strX);

    std::vector<std::string> l_vEvNodes;
    for (const std::string& l_strId : m_vOrder) if (f_StartsWith(l_strId, "ev:") && m_oP.f_Ev(l_strId.substr(3))) l_vEvNodes.push_back(l_strId);
    for (const std::string& l_strNid : l_vEvNodes) {
        const std::string l_strEid = l_strNid.substr(3);
        const Json& l_jE = *m_oP.f_Ev(l_strEid);
        const Json& l_jTg = l_jE["trigger"];
        const std::string l_strOn = l_jTg.value("on", std::string("step"));
        if (l_strOn == "talk" && !l_jTg.value("npc", std::string()).empty()) {
            const std::string l_strNpc = l_jTg["npc"].get<std::string>();
            l_fnAdd("npc:" + l_strNpc);
            m_vLinks.push_back({"npc:" + l_strNpc + "|talk", l_strNid + "|fire", "flow", l_strEid,
                               [this, l_strEid] { if (Json* l_pEv = m_oP.f_Ev(l_strEid)) (*l_pEv)["trigger"] = Json{{"on", "step"}}; }});
        }
        if (l_strOn == "varChanged" && !l_jTg.value("var", std::string()).empty()) {
            const std::string l_strVar = l_jTg["var"].get<std::string>();
            l_fnAdd("var:" + l_strVar);
            m_vLinks.push_back({"var:" + l_strVar + "|changed", l_strNid + "|fire", "flow", l_strEid,
                               [this, l_strEid] { if (Json* l_pEv = m_oP.f_Ev(l_strEid)) (*l_pEv)["trigger"] = Json{{"on", "step"}}; }});
        }
        for (const Json& l_jX : l_jE["next"]) {
            if (!l_jX.is_string()) continue;
            const std::string l_strTo = l_jX.get<std::string>();
            l_fnAdd("ev:" + l_strTo);
            m_vLinks.push_back({l_strNid + "|then", "ev:" + l_strTo + "|fire", "flow", l_strEid, [this, l_strEid, l_strTo] {
                                   Json* l_pEv = m_oP.f_Ev(l_strEid);
                                   if (!l_pEv) return;
                                   Json& l_jNx = (*l_pEv)["next"];
                                   for (size_t l_nI = 0; l_nI < l_jNx.size(); l_nI++) if (l_jNx[l_nI] == l_strTo) { l_jNx.erase(l_nI); break; }
                               }});
        }
        const Json& l_jReq = l_jE["req"];
        for (size_t l_nI = 0; l_nI < l_jReq.size(); l_nI++) {
            const std::string l_strR = stage::f_LeafRef(l_jReq[l_nI]);
            if (l_strR.empty()) continue;
            const std::string l_strSrc = f_StartsWith(l_strR, "event:") ? "ev:" + f_RefName(l_strR) + "|done" : l_strR + "|value";
            l_fnAdd(l_strSrc.substr(0, l_strSrc.find('|')));
            m_vLinks.push_back({l_strSrc, l_strNid + "|req:" + std::to_string(l_nI), f_RefType(l_strR), l_strEid,
                               [this, l_strEid, l_nI] { if (Json* l_pEv = m_oP.f_Ev(l_strEid)) if (l_nI < (*l_pEv)["req"].size()) (*l_pEv)["req"].erase(l_nI); }});
        }
        const Json& l_jActs = l_jE["actions"];
        for (size_t l_nI = 0; l_nI < l_jActs.size(); l_nI++) {
            const std::string l_strR = stage::f_ActRef(l_jActs[l_nI]);
            if (l_strR.empty() || f_StartsWith(l_strR, "item:")) continue;
            l_fnAdd(l_strR);
            m_vLinks.push_back({l_strNid + "|act:" + std::to_string(l_nI), l_strR + "|set", f_RefType(l_strR), l_strEid,
                               [this, l_strEid, l_nI] { if (Json* l_pEv = m_oP.f_Ev(l_strEid)) if (l_nI < (*l_pEv)["actions"].size()) (*l_pEv)["actions"].erase(l_nI); }});
        }
    }
    for (const Link& l_oL : m_vLinks) { m_setUsed.insert(l_oL.m_strFrom); m_setUsed.insert(l_oL.m_strTo); }
    // the editor's selected event is the graph's selection when nothing else is
    const std::string l_strSel = m_oP.m_strSelected;
    if (!l_strSel.empty() && m_oP.f_Ev(l_strSel) && m_mapNodes.count("ev:" + l_strSel) && m_eDrag == Drag::None &&
        (m_oSt.m_setSel.empty() || (m_oSt.m_setSel.size() == 1 && !m_oSt.m_setSel.count("ev:" + l_strSel) && f_StartsWith(*m_oSt.m_setSel.begin(), "ev:"))))
        m_oSt.m_setSel = {"ev:" + l_strSel};
    for (auto l_itIt = m_oSt.m_setSel.begin(); l_itIt != m_oSt.m_setSel.end();) l_itIt = m_mapNodes.count(*l_itIt) ? std::next(l_itIt) : m_oSt.m_setSel.erase(l_itIt);
    if (m_oSt.m_iSelLink >= (int)m_vLinks.size()) m_oSt.m_iSelLink = -1;
    f_AutoLayout();
    f_UpdateFlow();
}

void LogicGraph::f_AutoLayout() {
    // a node's column is the longest chain of links before it; nodes without a position go below that column's last one
    std::map<std::string, int> l_mapRank;
    for (const std::string& l_strId : m_vOrder) l_mapRank[l_strId] = 0;
    const int l_iN = (int)m_vOrder.size();
    for (int l_iPass = 0; l_iPass < l_iN; l_iPass++) {
        bool l_bChanged = false;
        for (const Link& l_oL : m_vLinks) {
            const std::string l_strA = l_oL.m_strFrom.substr(0, l_oL.m_strFrom.find('|')), l_strB = l_oL.m_strTo.substr(0, l_oL.m_strTo.find('|'));
            if (l_mapRank[l_strB] < l_mapRank[l_strA] + 1 && l_mapRank[l_strA] < l_iN) { l_mapRank[l_strB] = l_mapRank[l_strA] + 1; l_bChanged = true; }
        }
        if (!l_bChanged) break;
    }
    std::map<int, double> l_mapColY;
    for (const std::string& l_strId : m_vOrder)
        if (m_oSt.m_mapPos.count(l_strId)) l_mapColY[l_mapRank[l_strId]] = std::max(l_mapColY[l_mapRank[l_strId]], m_oSt.m_mapPos[l_strId].y() + m_mapNodes[l_strId].m_dH + 28);
    for (const std::string& l_strId : m_vOrder) {
        if (m_oSt.m_mapPos.count(l_strId)) continue;
        const int l_iC = l_mapRank[l_strId];
        m_oSt.m_mapPos[l_strId] = QPointF(l_iC * 300.0, l_mapColY[l_iC]);
        l_mapColY[l_iC] += m_mapNodes[l_strId].m_dH + 28;
    }
}

LogicGraph::PinRef LogicGraph::f_PinOf(const std::string& l_strPid) const {
    const size_t l_nBar = l_strPid.find('|');
    if (l_nBar == std::string::npos) return {};
    auto l_itIt = m_mapNodes.find(l_strPid.substr(0, l_nBar));
    if (l_itIt == m_mapNodes.end()) return {};
    const std::string l_strP = l_strPid.substr(l_nBar + 1);
    for (const Pin& l_oX : l_itIt->second.m_vOuts) if (l_oX.m_strId == l_strP) return {&l_itIt->second, &l_oX, true};
    for (const Pin& l_oX : l_itIt->second.m_vIns) if (l_oX.m_strId == l_strP) return {&l_itIt->second, &l_oX, false};
    return {};
}

// what connecting an output pin to an input pin means (the HTML editor's linkAction), or false when they do not fit
bool LogicGraph::f_LinkAction(const Node& l_oOutN, const Pin& l_oOutP, const Node& l_oInN, const Pin& l_oInP, Action& l_pairAct) const {
    const Json* l_pE = l_oInN.m_strType == "ev" ? m_oP.f_Ev(l_oInN.m_strName) : nullptr;
    const std::string l_strInName = l_oInN.m_strName, l_strOutName = l_oOutN.m_strName;
    if (l_oOutP.m_strKind == "flow" && l_oInP.m_strId == "fire") {
        if (!l_pE) return false;
        if (l_oOutN.m_strType == "npc") {
            if ((*l_pE)["trigger"].value("on", std::string()) == "talk" && (*l_pE)["trigger"].value("npc", std::string()) == l_strOutName) return false;
            l_pairAct = {tr("Trigger: %1").arg(f_Q(l_strInName)), [this, l_strInName, l_strOutName] { if (Json* l_pX = m_oP.f_Ev(l_strInName)) (*l_pX)["trigger"] = Json{{"on", "talk"}, {"npc", l_strOutName}}; }};
            return true;
        }
        if (l_oOutN.m_strType == "var" && l_oOutP.m_strId == "changed") {
            l_pairAct = {tr("Trigger: %1").arg(f_Q(l_strInName)), [this, l_strInName, l_strOutName] { if (Json* l_pX = m_oP.f_Ev(l_strInName)) (*l_pX)["trigger"] = Json{{"on", "varChanged"}, {"var", l_strOutName}}; }};
            return true;
        }
        if (l_oOutN.m_strType == "ev" && l_oOutP.m_strId == "then") {
            const Json* l_pS = m_oP.f_Ev(l_strOutName);
            if (!l_pS || l_strOutName == l_strInName) return false;
            for (const Json& l_jX : (*l_pS)["next"]) if (l_jX == l_strInName) return false;
            l_pairAct = {tr("Link: %1").arg(f_Q(l_strOutName)), [this, l_strInName, l_strOutName] { if (Json* l_pX = m_oP.f_Ev(l_strOutName)) (*l_pX)["next"].push_back(l_strInName); }};
            return true;
        }
        return false;
    }
    if (l_pE && (l_oInP.m_strId == "+req" || f_StartsWith(l_oInP.m_strId, "req:"))) {
        Json l_jLeaf;
        if (l_oOutP.m_strId == "value" && l_oOutN.m_strType == "var") l_jLeaf = Json{{"type", "varAtLeast"}, {"var", l_strOutName}, {"value", 1}};
        else if (l_oOutP.m_strId == "value" && l_oOutN.m_strType == "counter") l_jLeaf = Json{{"type", "counterAtLeast"}, {"counter", l_strOutName}, {"min", 1}};
        else if (l_oOutP.m_strId == "value" && l_oOutN.m_strType == "flag") l_jLeaf = Json{{"type", "runFlagSet"}, {"flag", l_strOutName}};
        else if (l_oOutP.m_strId == "done" && l_oOutN.m_strType == "ev" && l_strOutName != l_strInName) l_jLeaf = Json{{"type", "runFlagSet"}, {"flag", "event_" + l_strOutName}};
        if (l_jLeaf.is_null()) return false;
        if (l_oInP.m_strId == "+req") {
            l_pairAct = {tr("Condition: %1").arg(f_Q(l_strInName)), [this, l_strInName, l_jLeaf] { if (Json* l_pX = m_oP.f_Ev(l_strInName)) (*l_pX)["req"].push_back(l_jLeaf); }};
            return true;
        }
        const size_t l_nI = (size_t)std::stoi(l_oInP.m_strId.substr(4));
        const Json& l_jReq = (*l_pE)["req"];
        if (l_nI < l_jReq.size()) {   // retargeting keeps the old comparison when it still applies
            const Json& l_jOld = l_jReq[l_nI];
            const std::string l_strOt = f_Str(l_jOld, "type");
            if ((l_strOt == "varAtLeast" || l_strOt == "varEquals") && l_jLeaf.contains("var")) { l_jLeaf["type"] = l_strOt; l_jLeaf["value"] = l_jOld.value("value", Json(1)); }
            if (l_strOt == "counterAtLeast" && l_jLeaf.contains("counter")) l_jLeaf["min"] = l_jOld.value("min", Json(1));
        }
        l_pairAct = {tr("Condition: %1").arg(f_Q(l_strInName)), [this, l_strInName, l_jLeaf, l_nI] {
                   if (Json* l_pX = m_oP.f_Ev(l_strInName)) if (l_nI < (*l_pX)["req"].size()) (*l_pX)["req"][l_nI] = l_jLeaf;
               }};
        return true;
    }
    if (l_oOutN.m_strType == "ev" && (l_oOutP.m_strId == "+act" || f_StartsWith(l_oOutP.m_strId, "act:")) && l_oInP.m_strId == "set") {
        const Json* l_pS = m_oP.f_Ev(l_strOutName);
        if (!l_pS) return false;
        const int l_iI = l_oOutP.m_strId == "+act" ? -1 : std::stoi(l_oOutP.m_strId.substr(4));
        const Json l_jOld = l_iI >= 0 && l_iI < (int)(*l_pS)["actions"].size() ? (*l_pS)["actions"][(size_t)l_iI] : Json();
        Json l_jNval = 1;
        if (l_jOld.is_object()) l_jNval = l_jOld.contains("delta") ? l_jOld["delta"] : l_jOld.contains("value") ? l_jOld["value"] : Json(1);
        Json l_jA;
        if (l_oInN.m_strType == "var") l_jA = f_Str(l_jOld, "type") == "setVar" ? Json{{"type", "setVar"}, {"var", l_strInName}, {"value", l_jNval}} : Json{{"type", "addVar"}, {"var", l_strInName}, {"delta", l_jNval}};
        else if (l_oInN.m_strType == "counter") l_jA = Json{{"type", "addCounter"}, {"counter", l_strInName}, {"delta", l_jNval}};
        else if (l_oInN.m_strType == "flag") l_jA = Json{{"type", "setFlag"}, {"flag", l_strInName}};
        if (l_jA.is_null()) return false;
        l_pairAct = {tr("Action: %1").arg(f_Q(l_strOutName)), [this, l_strOutName, l_jA, l_iI] {
                   Json* l_pX = m_oP.f_Ev(l_strOutName);
                   if (!l_pX) return;
                   if (l_iI < 0) (*l_pX)["actions"].push_back(l_jA);
                   else if (l_iI < (int)(*l_pX)["actions"].size()) (*l_pX)["actions"][(size_t)l_iI] = l_jA;
               }};
        return true;
    }
    return false;
}

bool LogicGraph::f_ActionFor(const PinRef& l_oA, const PinRef& l_oB, Action* l_pAct) const {
    if (!l_oA.m_pN || !l_oB.m_pN || l_oA.m_bOut == l_oB.m_bOut || l_oA.m_pN->m_strId == l_oB.m_pN->m_strId) return false;
    const PinRef& l_oO = l_oA.m_bOut ? l_oA : l_oB;
    const PinRef& l_oI = l_oA.m_bOut ? l_oB : l_oA;
    Action l_pairTmp;
    const bool l_bOk = f_LinkAction(*l_oO.m_pN, *l_oO.m_pP, *l_oI.m_pN, *l_oI.m_pP, l_pairTmp);
    if (l_bOk && l_pAct) *l_pAct = l_pairTmp;
    return l_bOk;
}

bool LogicGraph::f_ConnectPins(const std::string& l_strFrom, const std::string& l_strTo) {
    Action l_pairAct;
    if (!f_ActionFor(f_PinOf(l_strFrom), f_PinOf(l_strTo), &l_pairAct)) return false;
    m_fnEdit(l_pairAct.first, l_pairAct.second);
    return true;
}

int LogicGraph::f_EventNodes() const {
    int l_iN = 0;
    for (const auto& [l_strId, l_oNode] : m_mapNodes) l_iN += l_oNode.m_strType == "ev";
    return l_iN;
}
int LogicGraph::f_ValueNodes() const {
    int l_iN = 0;
    for (const auto& [l_strId, l_oNode] : m_mapNodes) l_iN += l_oNode.m_strType == "var" || l_oNode.m_strType == "counter" || l_oNode.m_strType == "flag";
    return l_iN;
}

// ---- geometry ----------------------------------------------------------------------------------------------------------

QRectF LogicGraph::f_NodeRect(const Node& l_oN) const {
    const QPointF l_oP = m_oSt.m_mapPos.count(l_oN.m_strId) ? m_oSt.m_mapPos.at(l_oN.m_strId) : QPointF();
    return QRectF(l_oP, QSizeF(l_oN.m_dW, l_oN.m_dH));
}

QPointF LogicGraph::f_PinPos(const std::string& l_strPid) const {
    const PinRef l_oR = f_PinOf(l_strPid);
    if (!l_oR.m_pN) return {};
    const QRectF l_oNr = f_NodeRect(*l_oR.m_pN);
    const auto& l_vList = l_oR.m_bOut ? l_oR.m_pN->m_vOuts : l_oR.m_pN->m_vIns;
    size_t l_nRow = 0;
    for (size_t l_nI = 0; l_nI < l_vList.size(); l_nI++) if (&l_vList[l_nI] == l_oR.m_pP) l_nRow = l_nI;
    const double l_dY = l_oNr.top() + g_dHeadH + g_dBodyPad + g_dRowH * (double)l_nRow + g_dRowH / 2;
    return QPointF(l_oR.m_bOut ? l_oNr.right() - 9 - g_dDot / 2 : l_oNr.left() + 9 + g_dDot / 2, l_dY);
}

std::string LogicGraph::f_PinAt(const QPointF& l_oG) const {
    for (auto l_itIt = m_vOrder.rbegin(); l_itIt != m_vOrder.rend(); ++l_itIt) {
        const Node& l_oN = m_mapNodes.at(*l_itIt);
        const QRectF l_oNr = f_NodeRect(l_oN);
        if (!l_oNr.adjusted(-8, 0, 8, 0).contains(l_oG)) continue;
        for (int l_iSide = 0; l_iSide < 2; l_iSide++) {
            const auto& l_vList = l_iSide ? l_oN.m_vOuts : l_oN.m_vIns;
            for (const Pin& l_oP : l_vList) {
                const std::string l_strPid = l_oN.m_strId + "|" + l_oP.m_strId;
                const QPointF l_oC = f_PinPos(l_strPid);
                // the dot, or the pin's half of its row
                const bool l_bOnRow = std::abs(l_oG.y() - l_oC.y()) <= g_dRowH / 2 && (l_iSide ? l_oG.x() >= l_oNr.center().x() : l_oG.x() <= l_oNr.center().x());
                if (QLineF(l_oG, l_oC).length() <= 9 || l_bOnRow) return l_strPid;
            }
        }
    }
    return {};
}

std::string LogicGraph::f_NodeAt(const QPointF& l_oG) const {
    for (auto l_itIt = m_vOrder.rbegin(); l_itIt != m_vOrder.rend(); ++l_itIt)
        if (f_NodeRect(m_mapNodes.at(*l_itIt)).contains(l_oG)) return *l_itIt;
    return {};
}

QPainterPath LogicGraph::f_Curve(const QPointF& l_oA, const QPointF& l_oB) const {
    const double l_dDx = std::max(50.0, std::abs(l_oB.x() - l_oA.x()) * 0.5);
    QPainterPath l_oPath(l_oA);
    l_oPath.cubicTo(l_oA + QPointF(l_dDx, 0), l_oB - QPointF(l_dDx, 0), l_oB);
    return l_oPath;
}

int LogicGraph::f_LinkAt(const QPointF& l_oG) const {
    QPainterPathStroker l_oS;
    l_oS.setWidth(14 / std::max(0.3, m_oSt.m_dZoom));
    for (int l_iI = (int)m_vLinks.size() - 1; l_iI >= 0; l_iI--)
        if (l_oS.createStroke(f_Curve(f_PinPos(m_vLinks[(size_t)l_iI].m_strFrom), f_PinPos(m_vLinks[(size_t)l_iI].m_strTo))).contains(l_oG)) return l_iI;
    return -1;
}

void LogicGraph::f_Fit() {
    if (m_mapNodes.empty() || width() < 50 || height() < 50) return;
    QRectF l_oB;
    for (const auto& [l_strId, l_oN] : m_mapNodes) l_oB = l_oB.isNull() ? f_NodeRect(l_oN) : l_oB.united(f_NodeRect(l_oN));
    const double l_dZ = std::clamp(std::min((width() - 60) / l_oB.width(), (height() - 60) / l_oB.height()), 0.3, 1.4);
    m_oSt.m_dZoom = l_dZ;
    m_oSt.m_oPan = QPointF((width() - l_oB.width() * l_dZ) / 2 - l_oB.left() * l_dZ, (height() - l_oB.height() * l_dZ) / 2 - l_oB.top() * l_dZ);
    update();
}

void LogicGraph::f_SavePositions() {
    QJsonObject l_oO;
    for (const auto& [l_strId, l_oP] : m_oSt.m_mapPos) l_oO.insert(f_Q(l_strId), QJsonObject{{QStringLiteral("x"), l_oP.x()}, {QStringLiteral("y"), l_oP.y()}});
    QSettings().setValue(g_pszPosKey, QJsonDocument(l_oO).toJson(QJsonDocument::Compact));
}

void LogicGraph::f_UpdateFlow() {
    // the selected nodes' links flow (animated dashes)
    bool l_bFlowing = false;
    for (const Link& l_oL : m_vLinks)
        if (m_oSt.m_setSel.count(l_oL.m_strFrom.substr(0, l_oL.m_strFrom.find('|'))) || m_oSt.m_setSel.count(l_oL.m_strTo.substr(0, l_oL.m_strTo.find('|')))) l_bFlowing = true;
    if (l_bFlowing && !m_pFlowTimer->isActive()) m_pFlowTimer->start();
    if (!l_bFlowing) m_pFlowTimer->stop();
}

// ---- painting ----------------------------------------------------------------------------------------------------------

void LogicGraph::paintEvent(QPaintEvent*) {
    QPainter l_oG(this);
    l_oG.setRenderHint(QPainter::Antialiasing);
    l_oG.fillRect(rect(), QColor("#1a1c20"));
    // the grid: fine lines every 16, strong every 128 (graph units)
    for (const auto& [l_dStep, l_oCol] : {std::pair<double, QColor>{16, QColor("#222429")}, {128, QColor("#2b2e35")}}) {
        const double l_dS = l_dStep * m_oSt.m_dZoom;
        if (l_dS < 6) continue;
        l_oG.setPen(QPen(l_oCol, 1));
        for (double l_dX = std::fmod(m_oSt.m_oPan.x(), l_dS); l_dX < width(); l_dX += l_dS) l_oG.drawLine(QPointF(l_dX, 0), QPointF(l_dX, height()));
        for (double l_dY = std::fmod(m_oSt.m_oPan.y(), l_dS); l_dY < height(); l_dY += l_dS) l_oG.drawLine(QPointF(0, l_dY), QPointF(width(), l_dY));
    }

    l_oG.save();
    l_oG.translate(m_oSt.m_oPan);
    l_oG.scale(m_oSt.m_dZoom, m_oSt.m_dZoom);

    // links under the nodes
    for (size_t l_nI = 0; l_nI < m_vLinks.size(); l_nI++) {
        const Link& l_oL = m_vLinks[l_nI];
        const bool l_bFlow = l_oL.m_strKind == "flow";
        const bool l_bFlowing = m_oSt.m_setSel.count(l_oL.m_strFrom.substr(0, l_oL.m_strFrom.find('|'))) || m_oSt.m_setSel.count(l_oL.m_strTo.substr(0, l_oL.m_strTo.find('|')));
        QPen l_oPen(l_bFlow ? QColor("#e8eaee") : f_PinColor(l_oL.m_strKind), l_bFlow ? 3.0 : 2.2);
        if ((int)l_nI == m_oSt.m_iSelLink) { l_oPen.setColor(QColor("#ffd24c")); l_oPen.setWidthF(3.5); }
        if (l_bFlowing) { l_oPen.setDashPattern({10.0 / l_oPen.widthF(), 7.0 / l_oPen.widthF()}); l_oPen.setDashOffset(m_dFlowOffset * 17.0 / 16.0 / l_oPen.widthF()); }
        l_oG.setPen(l_oPen);
        l_oG.setBrush(Qt::NoBrush);
        l_oG.drawPath(f_Curve(f_PinPos(l_oL.m_strFrom), f_PinPos(l_oL.m_strTo)));
    }
    if (m_eDrag == Drag::Link) {   // the link being dragged
        const PinRef l_oA = f_PinOf(m_strLinkFrom);
        if (l_oA.m_pN) {
            QPen l_oPen(f_PinColor(l_oA.m_pP->m_strKind), 2.2, Qt::DashLine);
            l_oG.setPen(l_oPen);
            const QPointF l_oP = f_PinPos(m_strLinkFrom);
            l_oG.drawPath(l_oA.m_bOut ? f_Curve(l_oP, m_oLinkTo) : f_Curve(m_oLinkTo, l_oP));
        }
    }

    // nodes
    const QFont l_oTf = f_TitleFont(), l_oSf = f_SubFont();
    for (const std::string& l_strId : m_vOrder) {
        const Node& l_oN = m_mapNodes.at(l_strId);
        const QRectF l_oR = f_NodeRect(l_oN);
        const bool l_bSel = m_oSt.m_setSel.count(l_strId) > 0;
        // shadow, body, border
        l_oG.setPen(Qt::NoPen);
        l_oG.setBrush(QColor(0, 0, 0, 110));
        l_oG.drawRoundedRect(l_oR.translated(0, 4), 9, 9);
        if (l_bSel) { l_oG.setBrush(QColor(255, 210, 76, 90)); l_oG.drawRoundedRect(l_oR.adjusted(-2, -2, 2, 2), 11, 11); }
        l_oG.setBrush(QColor(30, 32, 38, 242));
        l_oG.setPen(QPen(l_oN.m_bMissing ? QColor("#ff5d5d") : l_bSel ? QColor("#ffd24c") : QColor("#4a505a"), 1));
        l_oG.drawRoundedRect(l_oR, 9, 9);
        // header: the colour fading to the right
        QPainterPath l_oHead;
        l_oHead.addRoundedRect(QRectF(l_oR.left() + 1, l_oR.top() + 1, l_oR.width() - 2, g_dHeadH), 8, 8);
        QPainterPath l_oCut;
        l_oCut.addRect(QRectF(l_oR.left(), l_oR.top() + g_dHeadH - 8, l_oR.width(), 9));
        l_oHead = l_oHead.united(l_oCut.intersected(l_oHead));
        QLinearGradient l_oGrad(l_oR.topLeft(), l_oR.topRight());
        QColor l_oC0 = l_oN.m_oColor, l_oC1 = l_oN.m_oColor;
        l_oC0.setAlphaF(0.8f);
        l_oC1.setAlphaF(0.25f);
        l_oGrad.setColorAt(0, l_oC0);
        l_oGrad.setColorAt(1, l_oC1);
        l_oG.setPen(Qt::NoPen);
        l_oG.setBrush(l_oGrad);
        l_oG.drawPath(l_oHead);
        l_oG.setFont(l_oTf);
        l_oG.setPen(QColor(0, 0, 0, 160));
        l_oG.drawText(QPointF(l_oR.left() + 10, l_oR.top() + 18), l_oN.m_oTitle);
        l_oG.setPen(Qt::white);
        l_oG.drawText(QPointF(l_oR.left() + 10, l_oR.top() + 17), l_oN.m_oTitle);
        l_oG.setFont(l_oSf);
        l_oG.setPen(QColor(255, 255, 255, 215));
        l_oG.drawText(QPointF(l_oR.left() + 10, l_oR.top() + 33), l_oN.m_oSub);
        // pins
        for (int l_iSide = 0; l_iSide < 2; l_iSide++) {
            const auto& l_vList = l_iSide ? l_oN.m_vOuts : l_oN.m_vIns;
            for (const Pin& l_oP : l_vList) {
                const std::string l_strPid = l_strId + "|" + l_oP.m_strId;
                const QPointF l_oC = f_PinPos(l_strPid);
                const bool l_bOn = m_setUsed.count(l_strPid) > 0;
                const bool l_bDragging = m_eDrag == Drag::Link && l_strPid != m_strLinkFrom;
                const bool l_bOk = l_bDragging && m_setOk.count(l_strPid);
                const double l_dAlpha = l_bDragging && !l_bOk ? 0.22 : l_oP.m_bGhost ? 0.55 : 1.0;
                l_oG.setOpacity(l_dAlpha);
                if (l_bOk) { l_oG.setPen(Qt::NoPen); l_oG.setBrush(QColor(76, 209, 138, 136)); l_oG.drawEllipse(l_oC, 9, 9); }
                const QColor l_oPc = f_PinColor(l_oP.m_strKind);
                if (l_oP.m_strKind == "flow") {   // a white arrow
                    QPolygonF l_oArrow;
                    l_oArrow << l_oC + QPointF(-5.5, -6) << l_oC + QPointF(0.5, -6) << l_oC + QPointF(5.5, 0) << l_oC + QPointF(0.5, 6) << l_oC + QPointF(-5.5, 6);
                    l_oG.setPen(Qt::NoPen);
                    l_oG.setBrush(l_bOn ? QColor(Qt::white) : QColor("#8b929c"));
                    l_oG.drawPolygon(l_oArrow);
                } else {
                    l_oG.setPen(QPen(l_oPc, 2));
                    l_oG.setBrush(l_bOn ? QBrush(l_oPc) : Qt::NoBrush);
                    l_oG.drawEllipse(l_oC, g_dDot / 2 - 1, g_dDot / 2 - 1);
                }
                l_oG.setFont(f_PinFont(l_oP.m_bGhost));
                l_oG.setPen(l_bOk ? QColor("#7ee2a8") : QColor("#d7dbe0"));
                const QFontMetrics l_oFm(l_oG.font());
                const double l_dTy = l_oC.y() + l_oFm.ascent() / 2.0 - 1;
                if (l_iSide) l_oG.drawText(QPointF(l_oC.x() - g_dDot / 2 - 6 - l_oFm.horizontalAdvance(l_oP.m_oLabel), l_dTy), l_oP.m_oLabel);
                else l_oG.drawText(QPointF(l_oC.x() + g_dDot / 2 + 6, l_dTy), l_oP.m_oLabel);
                l_oG.setOpacity(1.0);
            }
        }
    }
    l_oG.restore();

    if (m_eDrag == Drag::Box) {
        l_oG.setPen(QPen(QColor("#4c9eff"), 1));
        l_oG.setBrush(QColor(76, 158, 255, 34));
        l_oG.drawRect(m_oBox);
    }
    // the empty note and the hint
    QFont l_oHf;
    l_oHf.setPixelSize(11);
    l_oG.setFont(l_oHf);
    if (m_mapNodes.empty()) {
        l_oG.setPen(QColor("#8b929c"));
        l_oG.drawText(rect().adjusted(40, 0, -40, -40), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("No event has logic yet. Right-click to add a node, or give an event a trigger, condition, action or link in the inspector."));
    }
    const QString l_oHint = tr("Drag pin → pin to connect · right-drag: pan · wheel: zoom · right-click: add node · click a link, then Delete · F: fit");
    const QFontMetrics l_oFm(l_oHf);
    const QRect l_oHr(8, height() - l_oFm.height() - 10, l_oFm.horizontalAdvance(l_oHint) + 14, l_oFm.height() + 4);
    l_oG.setPen(Qt::NoPen);
    l_oG.setBrush(QColor(0, 0, 0, 170));
    l_oG.drawRoundedRect(l_oHr, 3, 3);
    l_oG.setPen(QColor("#8b929c"));
    l_oG.drawText(l_oHr, Qt::AlignCenter, l_oHint);
}

void LogicGraph::resizeEvent(QResizeEvent*) {
    if (!m_bFitPending || width() < 50 || height() < 50) return;
    m_bFitPending = false;
    f_Fit();
    // a graph too big to read when fitted opens at a readable zoom on the selected event instead (F shows it all)
    if (m_oSt.m_dZoom < 0.75 && !m_oSt.m_setSel.empty() && m_mapNodes.count(*m_oSt.m_setSel.begin())) {
        const QRectF l_oR = f_NodeRect(m_mapNodes.at(*m_oSt.m_setSel.begin()));
        m_oSt.m_dZoom = 0.9;
        m_oSt.m_oPan = QPointF(width() / 2.0 - l_oR.center().x() * m_oSt.m_dZoom, height() / 2.0 - l_oR.center().y() * m_oSt.m_dZoom);
        update();
    }
}

// ---- input -------------------------------------------------------------------------------------------------------------

void LogicGraph::mousePressEvent(QMouseEvent* l_pE) {
    setFocus();
    m_oPress = l_pE->position().toPoint();
    m_bMoved = false;
    m_eBtn = l_pE->button();
    const QPointF l_oGpt = f_ToGraph(l_pE->position());
    if (l_pE->button() == Qt::MiddleButton || l_pE->button() == Qt::RightButton) {
        m_eDrag = Drag::Pan;
        m_oPanStart = m_oSt.m_oPan;
        return;
    }
    if (l_pE->button() != Qt::LeftButton) return;
    const std::string l_strPin = f_PinAt(l_oGpt);
    if (!l_strPin.empty()) {
        m_eDrag = Drag::Link;
        m_strLinkFrom = l_strPin;
        m_oLinkTo = l_oGpt;
        m_setOk.clear();
        const PinRef l_oA = f_PinOf(l_strPin);
        for (const auto& [l_strId, l_oN] : m_mapNodes)
            for (const auto* l_pList : {&l_oN.m_vIns, &l_oN.m_vOuts})
                for (const Pin& l_oP : *l_pList) {
                    const std::string l_strPid = l_strId + "|" + l_oP.m_strId;
                    if (l_strPid != l_strPin && f_ActionFor(l_oA, f_PinOf(l_strPid))) m_setOk.insert(l_strPid);
                }
        update();
        return;
    }
    const int l_iLink = f_LinkAt(l_oGpt);
    const std::string l_strNode = f_NodeAt(l_oGpt);
    if (l_strNode.empty() && l_iLink >= 0) { m_oSt.m_iSelLink = l_iLink; update(); return; }
    m_oSt.m_iSelLink = -1;
    if (!l_strNode.empty()) {
        if (l_pE->modifiers() & Qt::ShiftModifier) { if (!m_oSt.m_setSel.erase(l_strNode)) m_oSt.m_setSel.insert(l_strNode); }
        else if (!m_oSt.m_setSel.count(l_strNode)) m_oSt.m_setSel = {l_strNode};
        m_eDrag = Drag::Node;
        m_strDragNode = l_strNode;
        m_mapOrigPos.clear();
        for (const std::string& l_strId : m_oSt.m_setSel) m_mapOrigPos[l_strId] = m_oSt.m_mapPos[l_strId];
        f_UpdateFlow();
        update();
        return;
    }
    if (!(l_pE->modifiers() & Qt::ShiftModifier)) m_oSt.m_setSel.clear();
    m_eDrag = Drag::Box;
    m_oBox = QRect(m_oPress, m_oPress);
    f_UpdateFlow();
    update();
}

void LogicGraph::mouseMoveEvent(QMouseEvent* l_pE) {
    const QPoint l_oPos = l_pE->position().toPoint();
    if ((l_oPos - m_oPress).manhattanLength() > 3) m_bMoved = true;
    switch (m_eDrag) {
    case Drag::Pan:
        m_oSt.m_oPan = m_oPanStart + (l_oPos - m_oPress);
        if (m_bMoved) setCursor(Qt::ClosedHandCursor);
        update();
        break;
    case Drag::Node: {
        const QPointF l_oD = QPointF(l_oPos - m_oPress) / m_oSt.m_dZoom;
        for (const auto& [l_strId, l_oP] : m_mapOrigPos) m_oSt.m_mapPos[l_strId] = QPointF(std::round(l_oP.x() + l_oD.x()), std::round(l_oP.y() + l_oD.y()));
        update();
        break;
    }
    case Drag::Link:
        m_oLinkTo = f_ToGraph(l_pE->position());
        update();
        break;
    case Drag::Box: {
        m_oBox = QRect(m_oPress, l_oPos).normalized();
        const QRectF l_oGb(f_ToGraph(m_oBox.topLeft()), f_ToGraph(m_oBox.bottomRight()));
        for (const auto& [l_strId, l_oN] : m_mapNodes) {
            if (f_NodeRect(l_oN).intersects(l_oGb)) m_oSt.m_setSel.insert(l_strId);
            else if (!(l_pE->modifiers() & Qt::ShiftModifier)) m_oSt.m_setSel.erase(l_strId);
        }
        f_UpdateFlow();
        update();
        break;
    }
    case Drag::None: {
        const QPointF l_oGpt = f_ToGraph(l_pE->position());
        setCursor(!f_PinAt(l_oGpt).empty() ? Qt::CrossCursor : !f_NodeAt(l_oGpt).empty() ? Qt::SizeAllCursor : f_LinkAt(l_oGpt) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        break;
    }
    }
}

void LogicGraph::mouseReleaseEvent(QMouseEvent* l_pE) {
    const Drag l_eD = m_eDrag;
    m_eDrag = Drag::None;
    unsetCursor();
    if (l_eD == Drag::Pan) {
        if (!m_bMoved && m_eBtn == Qt::RightButton) f_AddMenu(l_pE->position().toPoint(), std::string());
        return;
    }
    if (l_eD == Drag::Box) { update(); return; }
    if (l_eD == Drag::Node) {
        f_SavePositions();
        if (!m_bMoved && f_StartsWith(m_strDragNode, "ev:") && m_oP.f_Ev(m_strDragNode.substr(3)) && m_fnOnSelectEvent) m_fnOnSelectEvent(m_strDragNode.substr(3));
        update();
        return;
    }
    if (l_eD == Drag::Link) {
        const QPointF l_oGpt = f_ToGraph(l_pE->position());
        const std::string l_strPin = f_PinAt(l_oGpt);
        m_setOk.clear();
        if (!l_strPin.empty()) {
            Action l_pairAct;
            if (f_ActionFor(f_PinOf(m_strLinkFrom), f_PinOf(l_strPin), &l_pairAct)) { m_fnEdit(l_pairAct.first, l_pairAct.second); return; }
            update();
            return;
        }
        if (f_NodeAt(l_oGpt).empty()) { f_AddMenu(l_pE->position().toPoint(), m_strLinkFrom); return; }
        update();
    }
}

void LogicGraph::mouseDoubleClickEvent(QMouseEvent* l_pE) {
    const std::string l_strNode = f_NodeAt(f_ToGraph(l_pE->position()));
    const std::string l_strT = f_RefType(l_strNode);
    if ((l_strT == "var" || l_strT == "counter" || l_strT == "flag") && m_fnOnOpenValue) m_fnOnOpenValue(l_strNode);
}

void LogicGraph::wheelEvent(QWheelEvent* l_pE) {
    const QPointF l_oM = l_pE->position();
    const double l_dZ0 = m_oSt.m_dZoom, l_dZ = std::clamp(l_dZ0 * (l_pE->angleDelta().y() > 0 ? 1.12 : 1 / 1.12), 0.3, 2.0);
    m_oSt.m_oPan = l_oM - (l_oM - m_oSt.m_oPan) * (l_dZ / l_dZ0);
    m_oSt.m_dZoom = l_dZ;
    update();
}

void LogicGraph::keyPressEvent(QKeyEvent* l_pE) {
    if ((l_pE->key() == Qt::Key_Delete || l_pE->key() == Qt::Key_Backspace) && m_oSt.m_iSelLink >= 0 && m_oSt.m_iSelLink < (int)m_vLinks.size()) {
        const Link l_oL = m_vLinks[(size_t)m_oSt.m_iSelLink];
        m_oSt.m_iSelLink = -1;
        m_fnEdit(tr("Unlink"), l_oL.m_fnDel);
        return;
    }
    if (l_pE->key() == Qt::Key_F) { f_Fit(); return; }
    QWidget::keyPressEvent(l_pE);
}

void LogicGraph::f_AddMenu(const QPoint& l_oAt, const std::string& l_strPending) {
    const QPointF l_oGAt = f_ToGraph(l_oAt);
    const PinRef l_oA = l_strPending.empty() ? PinRef() : f_PinOf(l_strPending);
    // a node fits when one of its pins can take the pending link
    const auto l_fnFits = [&](const std::string& l_strId) {
        if (l_strPending.empty()) return true;
        Node l_oN;
        if (!f_MakeNode(l_strId, l_oN)) return false;
        Node* l_pKeep = &l_oN;
        for (int l_iSide = 0; l_iSide < 2; l_iSide++)
            for (const Pin& l_oP : l_iSide ? l_pKeep->m_vOuts : l_pKeep->m_vIns)
                if (f_ActionFor(l_oA, PinRef{l_pKeep, &l_oP, l_iSide == 1})) return true;
        return false;
    };
    std::vector<std::pair<QString, std::vector<std::string>>> l_vGroups(5);
    l_vGroups[0].first = tr("Events");
    for (const Json& l_jE : m_oP.m_jS["events"]) l_vGroups[0].second.push_back("ev:" + f_Str(l_jE, "id"));
    l_vGroups[1].first = tr("Event variables");
    for (auto& [l_strK, l_jV] : m_oP.m_jS["vars"].items()) { (void)l_jV; l_vGroups[1].second.push_back("var:" + l_strK); }
    l_vGroups[2].first = tr("Counters");
    for (auto& [l_strK, l_jV] : m_oP.m_jS["counters"].items()) { (void)l_jV; l_vGroups[2].second.push_back("counter:" + l_strK); }
    l_vGroups[3].first = tr("Flags");
    for (auto& [l_strK, l_jV] : m_oP.m_jS["flags"].items()) { (void)l_jV; l_vGroups[3].second.push_back("flag:" + l_strK); }
    l_vGroups[4].first = tr("NPCs");
    for (const std::string& l_strN : m_oP.f_NpcIds()) l_vGroups[4].second.push_back("npc:" + l_strN);

    QMenu l_oMenu(this);
    auto* l_pSearch = new QLineEdit;
    l_pSearch->setPlaceholderText(tr("Search nodes…"));
    l_pSearch->setClearButtonEnabled(true);
    auto* l_pWa = new QWidgetAction(&l_oMenu);
    l_pWa->setDefaultWidget(l_pSearch);
    l_oMenu.addAction(l_pWa);
    if (l_strPending.empty()) l_oMenu.addAction(tr("Fit view (F)"), this, [this] { f_Fit(); });
    std::vector<std::pair<QAction*, std::string>> l_vItems;
    for (const auto& [l_oTitle, l_vIds] : l_vGroups) {
        std::vector<std::string> l_vShown;
        for (const std::string& l_strId : l_vIds) if (!m_mapNodes.count(l_strId) && l_fnFits(l_strId)) l_vShown.push_back(l_strId);
        if (l_vShown.empty()) continue;
        l_oMenu.addSection(l_oTitle);
        for (const std::string& l_strId : l_vShown) l_vItems.push_back({l_oMenu.addAction(f_Q(f_RefName(l_strId))), l_strId});
    }
    connect(l_pSearch, &QLineEdit::textChanged, &l_oMenu, [&l_vItems](const QString& l_oT) {
        for (auto& [l_pAct, l_strId] : l_vItems) l_pAct->setVisible(l_oT.isEmpty() || f_Q(l_strId).contains(l_oT, Qt::CaseInsensitive));
    });
    QTimer::singleShot(0, l_pSearch, [l_pSearch] { l_pSearch->setFocus(); });
    QAction* l_pChosen = l_oMenu.exec(mapToGlobal(l_oAt));
    if (!l_pChosen) { update(); return; }
    std::string l_strId;
    for (auto& [l_pAct, l_strNid] : l_vItems) if (l_pAct == l_pChosen) l_strId = l_strNid;
    if (l_strId.empty()) return;   // Fit view
    m_oSt.m_setExtra.insert(l_strId);
    m_oSt.m_mapPos[l_strId] = l_oGAt - QPointF(0, 14);
    f_SavePositions();
    f_Rebuild();
    if (!l_strPending.empty()) {   // connect the pending link to the new node's first pin that fits
        const PinRef l_oFrom = f_PinOf(l_strPending);
        const Node& l_oN = m_mapNodes.at(l_strId);
        for (int l_iSide = 0; l_iSide < 2; l_iSide++)
            for (const Pin& l_oP : l_iSide ? l_oN.m_vOuts : l_oN.m_vIns) {
                Action l_pairAct;
                if (f_ActionFor(l_oFrom, PinRef{&l_oN, &l_oP, l_iSide == 1}, &l_pairAct)) { m_fnEdit(l_pairAct.first, l_pairAct.second); return; }
            }
    }
    update();
}
