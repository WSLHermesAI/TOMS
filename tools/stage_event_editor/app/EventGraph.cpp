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
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string str(const Json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string();
}
bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
std::string refType(const std::string& r) { const size_t c = r.find(':'); return c == std::string::npos ? "any" : r.substr(0, c); }
std::string refName(const std::string& r) { const size_t c = r.find(':'); return c == std::string::npos ? r : r.substr(c + 1); }
QString numText(const Json& v) {
    if (v.is_number_integer()) return QString::number(v.get<long long>());
    if (v.is_number()) return QString::number(v.get<double>());
    if (v.is_boolean()) return v.get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.is_string()) return q(v.get<std::string>());
    return q(v.dump());
}

// the HTML editor's pin colours (PINCOL) and sizes (.nn / .pin / .nh)
QColor pinColor(const std::string& kind) {
    static const std::map<std::string, QColor> c = {{"flow", QColor("#ffffff")}, {"var", QColor("#a98bff")}, {"counter", QColor("#3fc2b5")},
                                                    {"flag", QColor("#ff6b6b")}, {"event", QColor("#e0b44c")}, {"item", QColor("#7bd86a")},
                                                    {"any", QColor("#8b929c")}};
    auto it = c.find(kind);
    return it == c.end() ? QColor("#8b929c") : it->second;
}
constexpr double kHeadH = 40, kRowH = 21, kBodyPad = 7, kMinW = 190, kDot = 10;
QFont titleFont() { QFont f; f.setPixelSize(12); f.setBold(true); return f; }
QFont subFont() { QFont f; f.setPixelSize(11); return f; }
QFont pinFont(bool ghost) { QFont f; f.setPixelSize(12); f.setItalic(ghost); return f; }
constexpr const char* kPosKey = "eventEditor/logicGraphPositions";
}  // namespace

LogicGraph::LogicGraph(stage::EventProject& p, LogicGraphState& st, std::function<void(const QString&, std::function<void()>)> edit,
                       std::function<QString(const Json&)> textOf, QWidget* parent)
    : QWidget(parent), m_p(p), m_st(st), m_edit(std::move(edit)), m_textOf(std::move(textOf)) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    if (!m_st.loaded) {   // node positions from earlier runs
        m_st.loaded = true;
        const QJsonObject o = QJsonDocument::fromJson(QSettings().value(kPosKey).toByteArray()).object();
        for (auto it = o.begin(); it != o.end(); ++it) {
            const QJsonObject p2 = it.value().toObject();
            m_st.pos[it.key().toStdString()] = QPointF(p2.value(QStringLiteral("x")).toDouble(), p2.value(QStringLiteral("y")).toDouble());
        }
        m_fitPending = true;   // the first time: show the whole graph
    }
    m_flowTimer = new QTimer(this);
    m_flowTimer->setInterval(33);
    connect(m_flowTimer, &QTimer::timeout, this, [this] { m_flowOffset -= 1.0; update(); });
    rebuild();
}

// ---- the model -------------------------------------------------------------------------------------------------------

QString LogicGraph::condLabel(const Json& l) const {
    const std::string t = str(l, "type");
    if (t == "varAtLeast") return q(str(l, "var")) + QStringLiteral(" ≥ ") + numText(l.value("value", Json(0)));
    if (t == "varEquals") return q(str(l, "var")) + QStringLiteral(" = ") + numText(l.value("value", Json(0)));
    if (t == "counterAtLeast") return q(str(l, "counter")) + QStringLiteral(" ≥ ") + numText(l.value("min", Json(0)));
    if (t == "runFlagSet") {
        const std::string f = str(l, "flag");
        return startsWith(f, "event_") ? q(f.substr(6)) + QLatin1Char(' ') + tr("done") : QStringLiteral("⚑ ") + q(f);
    }
    return q(l.dump());
}

QString LogicGraph::actLabel(const Json& a) const {
    const std::string t = str(a, "type");
    const auto signedNum = [](const Json& v) { const QString s = numText(v); return (v.is_number() && v.get<double>() >= 0) ? QStringLiteral("+") + s : s; };
    if (t == "addVar") return signedNum(a.value("delta", Json(0))) + QStringLiteral(" → ") + q(str(a, "var"));
    if (t == "setVar") return QStringLiteral("= ") + numText(a.value("value", Json(0))) + QStringLiteral(" → ") + q(str(a, "var"));
    if (t == "addCounter") return signedNum(a.value("delta", Json(0))) + QStringLiteral(" → ") + q(str(a, "counter"));
    if (t == "setFlag") return QStringLiteral("⚑ ") + q(str(a, "flag"));
    if (t == "give") return QStringLiteral("🎁 ") + q(str(a, "itemId"));
    return q(t);
}

QString LogicGraph::trigSub(const Json& e) const {
    const Json& tg = e["trigger"];
    const std::string on = tg.value("on", std::string("step"));
    if (on == "talk") return QStringLiteral("💬 ") + q(tg.value("npc", std::string()));
    if (on == "varChanged") return QStringLiteral("◆ ") + q(tg.value("var", std::string()));
    if (on == "chain") return tr("from a link");
    QString s = tr("on step");
    QStringList fl;
    for (const Json* f : m_p.floorsOf(str(e, "id"))) fl << q(str(*f, "id"));
    if (!fl.isEmpty()) s += QStringLiteral(" · ") + fl.join(QLatin1Char(' '));
    return s;
}

bool LogicGraph::makeNode(const std::string& id, Node& n) const {
    const std::string k = refType(id), name = refName(id);
    n = Node();
    n.id = id;
    n.type = k;
    n.name = name;
    if (k == "ev") {
        const Json* e = m_p.ev(name);
        n.ins.push_back({"fire", "flow", tr("Fire")});
        n.outs.push_back({"then", "flow", tr("Then")});
        n.outs.push_back({"done", "event", tr("done")});
        if (e) {
            const Json& req = (*e)["req"];
            for (size_t i = 0; i < req.size(); i++) n.ins.push_back({"req:" + std::to_string(i), refType(stage::leafRef(req[i])), condLabel(req[i])});
            const Json& acts = (*e)["actions"];
            for (size_t i = 0; i < acts.size(); i++) n.outs.push_back({"act:" + std::to_string(i), refType(stage::actRef(acts[i])), actLabel(acts[i])});
        }
        n.ins.push_back({"+req", "any", tr("+ condition"), true});
        n.outs.push_back({"+act", "any", tr("+ action"), true});
        n.title = q(name);
        n.sub = e ? trigSub(*e) : tr("not defined");
        n.color = e ? QColor(q(m_p.kindColor(str(*e, "kind")))) : QColor("#ff5d5d");
        if (!n.color.isValid()) n.color = QColor("#8b929c");
        n.missing = !e;
    } else if (k == "var") {
        const Json& v = m_p.S["vars"];
        const bool has = v.contains(name);
        n.title = QStringLiteral("◆ ") + q(name);
        n.sub = has ? QStringLiteral("%1 · %2 · %3").arg(q(v[name].value("type", std::string("int"))), numText(v[name].value("default", Json(0))),
                                                          q(v[name].value("scope", std::string())))
                    : tr("not defined");
        n.color = pinColor("var");
        n.ins.push_back({"set", "var", tr("change")});
        n.outs.push_back({"value", "var", tr("value")});
        n.outs.push_back({"changed", "flow", tr("on change")});
        n.missing = !has;
    } else if (k == "counter") {
        const Json& c = m_p.S["counters"];
        const bool has = c.contains(name);
        n.title = QStringLiteral("▲ ") + q(name);
        n.sub = has ? m_textOf(c[name].value("label", Json())) : tr("not defined");
        n.color = pinColor("counter");
        n.ins.push_back({"set", "counter", tr("change")});
        n.outs.push_back({"value", "counter", tr("value")});
        n.missing = !has;
    } else if (k == "flag") {
        const Json& f = m_p.S["flags"];
        const bool has = f.contains(name);
        n.title = QStringLiteral("⚑ ") + q(name);
        n.sub = has ? q(f[name].value("scope", std::string())) : tr("not defined");
        n.color = pinColor("flag");
        n.ins.push_back({"set", "flag", tr("change")});
        n.outs.push_back({"value", "flag", tr("value")});
        n.missing = !has;
    } else if (k == "npc") {
        auto it = m_p.npcPlaces.find(name);
        QStringList fl;
        if (it != m_p.npcPlaces.end()) for (const std::string& f : it->second) fl << q(f);
        n.title = QStringLiteral("💬 ") + q(name);
        n.sub = fl.isEmpty() ? tr("not placed") : fl.join(QStringLiteral(", "));
        n.color = QColor("#ff9f43");
        n.outs.push_back({"talk", "flow", tr("on talk")});
        n.missing = it == m_p.npcPlaces.end() && !m_p.S["npcs"].contains(name);
    } else {
        return false;
    }
    measure(n);
    return true;
}

void LogicGraph::measure(Node& n) const {
    const QFontMetrics tf(titleFont()), sf(subFont());
    double inW = 0, outW = 0;
    for (const Pin& p : n.ins) inW = std::max(inW, (double)QFontMetrics(pinFont(p.ghost)).horizontalAdvance(p.label));
    for (const Pin& p : n.outs) outW = std::max(outW, (double)QFontMetrics(pinFont(p.ghost)).horizontalAdvance(p.label));
    const double pins = (inW > 0 ? 9 + kDot + 6 + inW : 0) + 18 + (outW > 0 ? outW + 6 + kDot + 9 : 0);
    n.w = std::max({kMinW, pins, (double)tf.horizontalAdvance(n.title) + 22, (double)sf.horizontalAdvance(n.sub) + 22});
    n.h = kHeadH + kBodyPad + kRowH * (double)std::max(n.ins.size(), n.outs.size()) + kBodyPad;
}

void LogicGraph::rebuild() {
    m_nodes.clear();
    m_order.clear();
    m_links.clear();
    m_used.clear();
    const auto add = [this](const std::string& id) -> Node* {
        auto it = m_nodes.find(id);
        if (it != m_nodes.end()) return &it->second;
        Node n;
        if (!makeNode(id, n)) return nullptr;
        m_order.push_back(id);
        return &(m_nodes[id] = n);
    };
    // the events with logic, or that another event links to or waits for
    const Json& events = m_p.S["events"];
    for (const Json& e : events) {
        const std::string id = str(e, "id");
        bool linked = m_p.hasLogic(e);
        for (const Json& o : events) {
            if (linked) break;
            for (const Json& x : o["next"]) if (x.is_string() && x.get<std::string>() == id) linked = true;
            for (const Json& l : o["req"]) if (stage::leafRef(l) == "event:" + id) linked = true;
        }
        if (linked) add("ev:" + id);
    }
    for (const std::string& x : m_st.extra) add(x);

    std::vector<std::string> evNodes;
    for (const std::string& id : m_order) if (startsWith(id, "ev:") && m_p.ev(id.substr(3))) evNodes.push_back(id);
    for (const std::string& nid : evNodes) {
        const std::string eid = nid.substr(3);
        const Json& e = *m_p.ev(eid);
        const Json& tg = e["trigger"];
        const std::string on = tg.value("on", std::string("step"));
        if (on == "talk" && !tg.value("npc", std::string()).empty()) {
            const std::string npc = tg["npc"].get<std::string>();
            add("npc:" + npc);
            m_links.push_back({"npc:" + npc + "|talk", nid + "|fire", "flow", eid,
                               [this, eid] { if (Json* ev = m_p.ev(eid)) (*ev)["trigger"] = Json{{"on", "step"}}; }});
        }
        if (on == "varChanged" && !tg.value("var", std::string()).empty()) {
            const std::string var = tg["var"].get<std::string>();
            add("var:" + var);
            m_links.push_back({"var:" + var + "|changed", nid + "|fire", "flow", eid,
                               [this, eid] { if (Json* ev = m_p.ev(eid)) (*ev)["trigger"] = Json{{"on", "step"}}; }});
        }
        for (const Json& x : e["next"]) {
            if (!x.is_string()) continue;
            const std::string to = x.get<std::string>();
            add("ev:" + to);
            m_links.push_back({nid + "|then", "ev:" + to + "|fire", "flow", eid, [this, eid, to] {
                                   Json* ev = m_p.ev(eid);
                                   if (!ev) return;
                                   Json& nx = (*ev)["next"];
                                   for (size_t i = 0; i < nx.size(); i++) if (nx[i] == to) { nx.erase(i); break; }
                               }});
        }
        const Json& req = e["req"];
        for (size_t i = 0; i < req.size(); i++) {
            const std::string r = stage::leafRef(req[i]);
            if (r.empty()) continue;
            const std::string src = startsWith(r, "event:") ? "ev:" + refName(r) + "|done" : r + "|value";
            add(src.substr(0, src.find('|')));
            m_links.push_back({src, nid + "|req:" + std::to_string(i), refType(r), eid,
                               [this, eid, i] { if (Json* ev = m_p.ev(eid)) if (i < (*ev)["req"].size()) (*ev)["req"].erase(i); }});
        }
        const Json& acts = e["actions"];
        for (size_t i = 0; i < acts.size(); i++) {
            const std::string r = stage::actRef(acts[i]);
            if (r.empty() || startsWith(r, "item:")) continue;
            add(r);
            m_links.push_back({nid + "|act:" + std::to_string(i), r + "|set", refType(r), eid,
                               [this, eid, i] { if (Json* ev = m_p.ev(eid)) if (i < (*ev)["actions"].size()) (*ev)["actions"].erase(i); }});
        }
    }
    for (const Link& l : m_links) { m_used.insert(l.from); m_used.insert(l.to); }
    // the editor's selected event is the graph's selection when nothing else is
    const std::string sel = m_p.selected;
    if (!sel.empty() && m_p.ev(sel) && m_nodes.count("ev:" + sel) && m_drag == Drag::None &&
        (m_st.sel.empty() || (m_st.sel.size() == 1 && !m_st.sel.count("ev:" + sel) && startsWith(*m_st.sel.begin(), "ev:"))))
        m_st.sel = {"ev:" + sel};
    for (auto it = m_st.sel.begin(); it != m_st.sel.end();) it = m_nodes.count(*it) ? std::next(it) : m_st.sel.erase(it);
    if (m_st.selLink >= (int)m_links.size()) m_st.selLink = -1;
    autoLayout();
    updateFlow();
}

void LogicGraph::autoLayout() {
    // a node's column is the longest chain of links before it; nodes without a position go below that column's last one
    std::map<std::string, int> rank;
    for (const std::string& id : m_order) rank[id] = 0;
    const int n = (int)m_order.size();
    for (int pass = 0; pass < n; pass++) {
        bool changed = false;
        for (const Link& l : m_links) {
            const std::string a = l.from.substr(0, l.from.find('|')), b = l.to.substr(0, l.to.find('|'));
            if (rank[b] < rank[a] + 1 && rank[a] < n) { rank[b] = rank[a] + 1; changed = true; }
        }
        if (!changed) break;
    }
    std::map<int, double> colY;
    for (const std::string& id : m_order)
        if (m_st.pos.count(id)) colY[rank[id]] = std::max(colY[rank[id]], m_st.pos[id].y() + m_nodes[id].h + 28);
    for (const std::string& id : m_order) {
        if (m_st.pos.count(id)) continue;
        const int c = rank[id];
        m_st.pos[id] = QPointF(c * 300.0, colY[c]);
        colY[c] += m_nodes[id].h + 28;
    }
}

LogicGraph::PinRef LogicGraph::pinOf(const std::string& pid) const {
    const size_t bar = pid.find('|');
    if (bar == std::string::npos) return {};
    auto it = m_nodes.find(pid.substr(0, bar));
    if (it == m_nodes.end()) return {};
    const std::string p = pid.substr(bar + 1);
    for (const Pin& x : it->second.outs) if (x.id == p) return {&it->second, &x, true};
    for (const Pin& x : it->second.ins) if (x.id == p) return {&it->second, &x, false};
    return {};
}

// what connecting an output pin to an input pin means (the HTML editor's linkAction), or false when they do not fit
bool LogicGraph::linkAction(const Node& outN, const Pin& outP, const Node& inN, const Pin& inP, Action& act) const {
    const Json* e = inN.type == "ev" ? m_p.ev(inN.name) : nullptr;
    const std::string inName = inN.name, outName = outN.name;
    if (outP.kind == "flow" && inP.id == "fire") {
        if (!e) return false;
        if (outN.type == "npc") {
            if ((*e)["trigger"].value("on", std::string()) == "talk" && (*e)["trigger"].value("npc", std::string()) == outName) return false;
            act = {tr("Trigger: %1").arg(q(inName)), [this, inName, outName] { if (Json* x = m_p.ev(inName)) (*x)["trigger"] = Json{{"on", "talk"}, {"npc", outName}}; }};
            return true;
        }
        if (outN.type == "var" && outP.id == "changed") {
            act = {tr("Trigger: %1").arg(q(inName)), [this, inName, outName] { if (Json* x = m_p.ev(inName)) (*x)["trigger"] = Json{{"on", "varChanged"}, {"var", outName}}; }};
            return true;
        }
        if (outN.type == "ev" && outP.id == "then") {
            const Json* s = m_p.ev(outName);
            if (!s || outName == inName) return false;
            for (const Json& x : (*s)["next"]) if (x == inName) return false;
            act = {tr("Link: %1").arg(q(outName)), [this, inName, outName] { if (Json* x = m_p.ev(outName)) (*x)["next"].push_back(inName); }};
            return true;
        }
        return false;
    }
    if (e && (inP.id == "+req" || startsWith(inP.id, "req:"))) {
        Json leaf;
        if (outP.id == "value" && outN.type == "var") leaf = Json{{"type", "varAtLeast"}, {"var", outName}, {"value", 1}};
        else if (outP.id == "value" && outN.type == "counter") leaf = Json{{"type", "counterAtLeast"}, {"counter", outName}, {"min", 1}};
        else if (outP.id == "value" && outN.type == "flag") leaf = Json{{"type", "runFlagSet"}, {"flag", outName}};
        else if (outP.id == "done" && outN.type == "ev" && outName != inName) leaf = Json{{"type", "runFlagSet"}, {"flag", "event_" + outName}};
        if (leaf.is_null()) return false;
        if (inP.id == "+req") {
            act = {tr("Condition: %1").arg(q(inName)), [this, inName, leaf] { if (Json* x = m_p.ev(inName)) (*x)["req"].push_back(leaf); }};
            return true;
        }
        const size_t i = (size_t)std::stoi(inP.id.substr(4));
        const Json& req = (*e)["req"];
        if (i < req.size()) {   // retargeting keeps the old comparison when it still applies
            const Json& old = req[i];
            const std::string ot = str(old, "type");
            if ((ot == "varAtLeast" || ot == "varEquals") && leaf.contains("var")) { leaf["type"] = ot; leaf["value"] = old.value("value", Json(1)); }
            if (ot == "counterAtLeast" && leaf.contains("counter")) leaf["min"] = old.value("min", Json(1));
        }
        act = {tr("Condition: %1").arg(q(inName)), [this, inName, leaf, i] {
                   if (Json* x = m_p.ev(inName)) if (i < (*x)["req"].size()) (*x)["req"][i] = leaf;
               }};
        return true;
    }
    if (outN.type == "ev" && (outP.id == "+act" || startsWith(outP.id, "act:")) && inP.id == "set") {
        const Json* s = m_p.ev(outName);
        if (!s) return false;
        const int i = outP.id == "+act" ? -1 : std::stoi(outP.id.substr(4));
        const Json old = i >= 0 && i < (int)(*s)["actions"].size() ? (*s)["actions"][(size_t)i] : Json();
        Json nval = 1;
        if (old.is_object()) nval = old.contains("delta") ? old["delta"] : old.contains("value") ? old["value"] : Json(1);
        Json a;
        if (inN.type == "var") a = str(old, "type") == "setVar" ? Json{{"type", "setVar"}, {"var", inName}, {"value", nval}} : Json{{"type", "addVar"}, {"var", inName}, {"delta", nval}};
        else if (inN.type == "counter") a = Json{{"type", "addCounter"}, {"counter", inName}, {"delta", nval}};
        else if (inN.type == "flag") a = Json{{"type", "setFlag"}, {"flag", inName}};
        if (a.is_null()) return false;
        act = {tr("Action: %1").arg(q(outName)), [this, outName, a, i] {
                   Json* x = m_p.ev(outName);
                   if (!x) return;
                   if (i < 0) (*x)["actions"].push_back(a);
                   else if (i < (int)(*x)["actions"].size()) (*x)["actions"][(size_t)i] = a;
               }};
        return true;
    }
    return false;
}

bool LogicGraph::actionFor(const PinRef& a, const PinRef& b, Action* act) const {
    if (!a.n || !b.n || a.out == b.out || a.n->id == b.n->id) return false;
    const PinRef& o = a.out ? a : b;
    const PinRef& i = a.out ? b : a;
    Action tmp;
    const bool ok = linkAction(*o.n, *o.p, *i.n, *i.p, tmp);
    if (ok && act) *act = tmp;
    return ok;
}

bool LogicGraph::connectPins(const std::string& from, const std::string& to) {
    Action act;
    if (!actionFor(pinOf(from), pinOf(to), &act)) return false;
    m_edit(act.first, act.second);
    return true;
}

int LogicGraph::eventNodes() const {
    int n = 0;
    for (const auto& [id, node] : m_nodes) n += node.type == "ev";
    return n;
}
int LogicGraph::valueNodes() const {
    int n = 0;
    for (const auto& [id, node] : m_nodes) n += node.type == "var" || node.type == "counter" || node.type == "flag";
    return n;
}

// ---- geometry ----------------------------------------------------------------------------------------------------------

QRectF LogicGraph::nodeRect(const Node& n) const {
    const QPointF p = m_st.pos.count(n.id) ? m_st.pos.at(n.id) : QPointF();
    return QRectF(p, QSizeF(n.w, n.h));
}

QPointF LogicGraph::pinPos(const std::string& pid) const {
    const PinRef r = pinOf(pid);
    if (!r.n) return {};
    const QRectF nr = nodeRect(*r.n);
    const auto& list = r.out ? r.n->outs : r.n->ins;
    size_t row = 0;
    for (size_t i = 0; i < list.size(); i++) if (&list[i] == r.p) row = i;
    const double y = nr.top() + kHeadH + kBodyPad + kRowH * (double)row + kRowH / 2;
    return QPointF(r.out ? nr.right() - 9 - kDot / 2 : nr.left() + 9 + kDot / 2, y);
}

std::string LogicGraph::pinAt(const QPointF& g) const {
    for (auto it = m_order.rbegin(); it != m_order.rend(); ++it) {
        const Node& n = m_nodes.at(*it);
        const QRectF nr = nodeRect(n);
        if (!nr.adjusted(-8, 0, 8, 0).contains(g)) continue;
        for (int side = 0; side < 2; side++) {
            const auto& list = side ? n.outs : n.ins;
            for (const Pin& p : list) {
                const std::string pid = n.id + "|" + p.id;
                const QPointF c = pinPos(pid);
                // the dot, or the pin's half of its row
                const bool onRow = std::abs(g.y() - c.y()) <= kRowH / 2 && (side ? g.x() >= nr.center().x() : g.x() <= nr.center().x());
                if (QLineF(g, c).length() <= 9 || onRow) return pid;
            }
        }
    }
    return {};
}

std::string LogicGraph::nodeAt(const QPointF& g) const {
    for (auto it = m_order.rbegin(); it != m_order.rend(); ++it)
        if (nodeRect(m_nodes.at(*it)).contains(g)) return *it;
    return {};
}

QPainterPath LogicGraph::curve(const QPointF& a, const QPointF& b) const {
    const double dx = std::max(50.0, std::abs(b.x() - a.x()) * 0.5);
    QPainterPath path(a);
    path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
    return path;
}

int LogicGraph::linkAt(const QPointF& g) const {
    QPainterPathStroker s;
    s.setWidth(14 / std::max(0.3, m_st.zoom));
    for (int i = (int)m_links.size() - 1; i >= 0; i--)
        if (s.createStroke(curve(pinPos(m_links[(size_t)i].from), pinPos(m_links[(size_t)i].to))).contains(g)) return i;
    return -1;
}

void LogicGraph::fit() {
    if (m_nodes.empty() || width() < 50 || height() < 50) return;
    QRectF b;
    for (const auto& [id, n] : m_nodes) b = b.isNull() ? nodeRect(n) : b.united(nodeRect(n));
    const double z = std::clamp(std::min((width() - 60) / b.width(), (height() - 60) / b.height()), 0.3, 1.4);
    m_st.zoom = z;
    m_st.pan = QPointF((width() - b.width() * z) / 2 - b.left() * z, (height() - b.height() * z) / 2 - b.top() * z);
    update();
}

void LogicGraph::savePositions() {
    QJsonObject o;
    for (const auto& [id, p] : m_st.pos) o.insert(q(id), QJsonObject{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}});
    QSettings().setValue(kPosKey, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void LogicGraph::updateFlow() {
    // the selected nodes' links flow (animated dashes)
    bool flowing = false;
    for (const Link& l : m_links)
        if (m_st.sel.count(l.from.substr(0, l.from.find('|'))) || m_st.sel.count(l.to.substr(0, l.to.find('|')))) flowing = true;
    if (flowing && !m_flowTimer->isActive()) m_flowTimer->start();
    if (!flowing) m_flowTimer->stop();
}

// ---- painting ----------------------------------------------------------------------------------------------------------

void LogicGraph::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.fillRect(rect(), QColor("#1a1c20"));
    // the grid: fine lines every 16, strong every 128 (graph units)
    for (const auto& [step, col] : {std::pair<double, QColor>{16, QColor("#222429")}, {128, QColor("#2b2e35")}}) {
        const double s = step * m_st.zoom;
        if (s < 6) continue;
        g.setPen(QPen(col, 1));
        for (double x = std::fmod(m_st.pan.x(), s); x < width(); x += s) g.drawLine(QPointF(x, 0), QPointF(x, height()));
        for (double y = std::fmod(m_st.pan.y(), s); y < height(); y += s) g.drawLine(QPointF(0, y), QPointF(width(), y));
    }

    g.save();
    g.translate(m_st.pan);
    g.scale(m_st.zoom, m_st.zoom);

    // links under the nodes
    for (size_t i = 0; i < m_links.size(); i++) {
        const Link& l = m_links[i];
        const bool flow = l.kind == "flow";
        const bool flowing = m_st.sel.count(l.from.substr(0, l.from.find('|'))) || m_st.sel.count(l.to.substr(0, l.to.find('|')));
        QPen pen(flow ? QColor("#e8eaee") : pinColor(l.kind), flow ? 3.0 : 2.2);
        if ((int)i == m_st.selLink) { pen.setColor(QColor("#ffd24c")); pen.setWidthF(3.5); }
        if (flowing) { pen.setDashPattern({10.0 / pen.widthF(), 7.0 / pen.widthF()}); pen.setDashOffset(m_flowOffset * 17.0 / 16.0 / pen.widthF()); }
        g.setPen(pen);
        g.setBrush(Qt::NoBrush);
        g.drawPath(curve(pinPos(l.from), pinPos(l.to)));
    }
    if (m_drag == Drag::Link) {   // the link being dragged
        const PinRef a = pinOf(m_linkFrom);
        if (a.n) {
            QPen pen(pinColor(a.p->kind), 2.2, Qt::DashLine);
            g.setPen(pen);
            const QPointF p = pinPos(m_linkFrom);
            g.drawPath(a.out ? curve(p, m_linkTo) : curve(m_linkTo, p));
        }
    }

    // nodes
    const QFont tf = titleFont(), sf = subFont();
    for (const std::string& id : m_order) {
        const Node& n = m_nodes.at(id);
        const QRectF r = nodeRect(n);
        const bool sel = m_st.sel.count(id) > 0;
        // shadow, body, border
        g.setPen(Qt::NoPen);
        g.setBrush(QColor(0, 0, 0, 110));
        g.drawRoundedRect(r.translated(0, 4), 9, 9);
        if (sel) { g.setBrush(QColor(255, 210, 76, 90)); g.drawRoundedRect(r.adjusted(-2, -2, 2, 2), 11, 11); }
        g.setBrush(QColor(30, 32, 38, 242));
        g.setPen(QPen(n.missing ? QColor("#ff5d5d") : sel ? QColor("#ffd24c") : QColor("#4a505a"), 1));
        g.drawRoundedRect(r, 9, 9);
        // header: the colour fading to the right
        QPainterPath head;
        head.addRoundedRect(QRectF(r.left() + 1, r.top() + 1, r.width() - 2, kHeadH), 8, 8);
        QPainterPath cut;
        cut.addRect(QRectF(r.left(), r.top() + kHeadH - 8, r.width(), 9));
        head = head.united(cut.intersected(head));
        QLinearGradient grad(r.topLeft(), r.topRight());
        QColor c0 = n.color, c1 = n.color;
        c0.setAlphaF(0.8f);
        c1.setAlphaF(0.25f);
        grad.setColorAt(0, c0);
        grad.setColorAt(1, c1);
        g.setPen(Qt::NoPen);
        g.setBrush(grad);
        g.drawPath(head);
        g.setFont(tf);
        g.setPen(QColor(0, 0, 0, 160));
        g.drawText(QPointF(r.left() + 10, r.top() + 18), n.title);
        g.setPen(Qt::white);
        g.drawText(QPointF(r.left() + 10, r.top() + 17), n.title);
        g.setFont(sf);
        g.setPen(QColor(255, 255, 255, 215));
        g.drawText(QPointF(r.left() + 10, r.top() + 33), n.sub);
        // pins
        for (int side = 0; side < 2; side++) {
            const auto& list = side ? n.outs : n.ins;
            for (const Pin& p : list) {
                const std::string pid = id + "|" + p.id;
                const QPointF c = pinPos(pid);
                const bool on = m_used.count(pid) > 0;
                const bool dragging = m_drag == Drag::Link && pid != m_linkFrom;
                const bool ok = dragging && m_ok.count(pid);
                const double alpha = dragging && !ok ? 0.22 : p.ghost ? 0.55 : 1.0;
                g.setOpacity(alpha);
                if (ok) { g.setPen(Qt::NoPen); g.setBrush(QColor(76, 209, 138, 136)); g.drawEllipse(c, 9, 9); }
                const QColor pc = pinColor(p.kind);
                if (p.kind == "flow") {   // a white arrow
                    QPolygonF arrow;
                    arrow << c + QPointF(-5.5, -6) << c + QPointF(0.5, -6) << c + QPointF(5.5, 0) << c + QPointF(0.5, 6) << c + QPointF(-5.5, 6);
                    g.setPen(Qt::NoPen);
                    g.setBrush(on ? QColor(Qt::white) : QColor("#8b929c"));
                    g.drawPolygon(arrow);
                } else {
                    g.setPen(QPen(pc, 2));
                    g.setBrush(on ? QBrush(pc) : Qt::NoBrush);
                    g.drawEllipse(c, kDot / 2 - 1, kDot / 2 - 1);
                }
                g.setFont(pinFont(p.ghost));
                g.setPen(ok ? QColor("#7ee2a8") : QColor("#d7dbe0"));
                const QFontMetrics fm(g.font());
                const double ty = c.y() + fm.ascent() / 2.0 - 1;
                if (side) g.drawText(QPointF(c.x() - kDot / 2 - 6 - fm.horizontalAdvance(p.label), ty), p.label);
                else g.drawText(QPointF(c.x() + kDot / 2 + 6, ty), p.label);
                g.setOpacity(1.0);
            }
        }
    }
    g.restore();

    if (m_drag == Drag::Box) {
        g.setPen(QPen(QColor("#4c9eff"), 1));
        g.setBrush(QColor(76, 158, 255, 34));
        g.drawRect(m_box);
    }
    // the empty note and the hint
    QFont hf;
    hf.setPixelSize(11);
    g.setFont(hf);
    if (m_nodes.empty()) {
        g.setPen(QColor("#8b929c"));
        g.drawText(rect().adjusted(40, 0, -40, -40), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("No event has logic yet. Right-click to add a node, or give an event a trigger, condition, action or link in the inspector."));
    }
    const QString hint = tr("Drag pin → pin to connect · right-drag: pan · wheel: zoom · right-click: add node · click a link, then Delete · F: fit");
    const QFontMetrics fm(hf);
    const QRect hr(8, height() - fm.height() - 10, fm.horizontalAdvance(hint) + 14, fm.height() + 4);
    g.setPen(Qt::NoPen);
    g.setBrush(QColor(0, 0, 0, 170));
    g.drawRoundedRect(hr, 3, 3);
    g.setPen(QColor("#8b929c"));
    g.drawText(hr, Qt::AlignCenter, hint);
}

void LogicGraph::resizeEvent(QResizeEvent*) {
    if (!m_fitPending || width() < 50 || height() < 50) return;
    m_fitPending = false;
    fit();
    // a graph too big to read when fitted opens at a readable zoom on the selected event instead (F shows it all)
    if (m_st.zoom < 0.75 && !m_st.sel.empty() && m_nodes.count(*m_st.sel.begin())) {
        const QRectF r = nodeRect(m_nodes.at(*m_st.sel.begin()));
        m_st.zoom = 0.9;
        m_st.pan = QPointF(width() / 2.0 - r.center().x() * m_st.zoom, height() / 2.0 - r.center().y() * m_st.zoom);
        update();
    }
}

// ---- input -------------------------------------------------------------------------------------------------------------

void LogicGraph::mousePressEvent(QMouseEvent* e) {
    setFocus();
    m_press = e->position().toPoint();
    m_moved = false;
    m_btn = e->button();
    const QPointF gpt = toGraph(e->position());
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        m_drag = Drag::Pan;
        m_panStart = m_st.pan;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const std::string pin = pinAt(gpt);
    if (!pin.empty()) {
        m_drag = Drag::Link;
        m_linkFrom = pin;
        m_linkTo = gpt;
        m_ok.clear();
        const PinRef a = pinOf(pin);
        for (const auto& [id, n] : m_nodes)
            for (const auto* list : {&n.ins, &n.outs})
                for (const Pin& p : *list) {
                    const std::string pid = id + "|" + p.id;
                    if (pid != pin && actionFor(a, pinOf(pid))) m_ok.insert(pid);
                }
        update();
        return;
    }
    const int link = linkAt(gpt);
    const std::string node = nodeAt(gpt);
    if (node.empty() && link >= 0) { m_st.selLink = link; update(); return; }
    m_st.selLink = -1;
    if (!node.empty()) {
        if (e->modifiers() & Qt::ShiftModifier) { if (!m_st.sel.erase(node)) m_st.sel.insert(node); }
        else if (!m_st.sel.count(node)) m_st.sel = {node};
        m_drag = Drag::Node;
        m_dragNode = node;
        m_origPos.clear();
        for (const std::string& id : m_st.sel) m_origPos[id] = m_st.pos[id];
        updateFlow();
        update();
        return;
    }
    if (!(e->modifiers() & Qt::ShiftModifier)) m_st.sel.clear();
    m_drag = Drag::Box;
    m_box = QRect(m_press, m_press);
    updateFlow();
    update();
}

void LogicGraph::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    if ((pos - m_press).manhattanLength() > 3) m_moved = true;
    switch (m_drag) {
    case Drag::Pan:
        m_st.pan = m_panStart + (pos - m_press);
        if (m_moved) setCursor(Qt::ClosedHandCursor);
        update();
        break;
    case Drag::Node: {
        const QPointF d = QPointF(pos - m_press) / m_st.zoom;
        for (const auto& [id, p] : m_origPos) m_st.pos[id] = QPointF(std::round(p.x() + d.x()), std::round(p.y() + d.y()));
        update();
        break;
    }
    case Drag::Link:
        m_linkTo = toGraph(e->position());
        update();
        break;
    case Drag::Box: {
        m_box = QRect(m_press, pos).normalized();
        const QRectF gb(toGraph(m_box.topLeft()), toGraph(m_box.bottomRight()));
        for (const auto& [id, n] : m_nodes) {
            if (nodeRect(n).intersects(gb)) m_st.sel.insert(id);
            else if (!(e->modifiers() & Qt::ShiftModifier)) m_st.sel.erase(id);
        }
        updateFlow();
        update();
        break;
    }
    case Drag::None: {
        const QPointF gpt = toGraph(e->position());
        setCursor(!pinAt(gpt).empty() ? Qt::CrossCursor : !nodeAt(gpt).empty() ? Qt::SizeAllCursor : linkAt(gpt) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        break;
    }
    }
}

void LogicGraph::mouseReleaseEvent(QMouseEvent* e) {
    const Drag d = m_drag;
    m_drag = Drag::None;
    unsetCursor();
    if (d == Drag::Pan) {
        if (!m_moved && m_btn == Qt::RightButton) addMenu(e->position().toPoint(), std::string());
        return;
    }
    if (d == Drag::Box) { update(); return; }
    if (d == Drag::Node) {
        savePositions();
        if (!m_moved && startsWith(m_dragNode, "ev:") && m_p.ev(m_dragNode.substr(3)) && onSelectEvent) onSelectEvent(m_dragNode.substr(3));
        update();
        return;
    }
    if (d == Drag::Link) {
        const QPointF gpt = toGraph(e->position());
        const std::string pin = pinAt(gpt);
        m_ok.clear();
        if (!pin.empty()) {
            Action act;
            if (actionFor(pinOf(m_linkFrom), pinOf(pin), &act)) { m_edit(act.first, act.second); return; }
            update();
            return;
        }
        if (nodeAt(gpt).empty()) { addMenu(e->position().toPoint(), m_linkFrom); return; }
        update();
    }
}

void LogicGraph::mouseDoubleClickEvent(QMouseEvent* e) {
    const std::string node = nodeAt(toGraph(e->position()));
    const std::string t = refType(node);
    if ((t == "var" || t == "counter" || t == "flag") && onOpenValue) onOpenValue(node);
}

void LogicGraph::wheelEvent(QWheelEvent* e) {
    const QPointF m = e->position();
    const double z0 = m_st.zoom, z = std::clamp(z0 * (e->angleDelta().y() > 0 ? 1.12 : 1 / 1.12), 0.3, 2.0);
    m_st.pan = m - (m - m_st.pan) * (z / z0);
    m_st.zoom = z;
    update();
}

void LogicGraph::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && m_st.selLink >= 0 && m_st.selLink < (int)m_links.size()) {
        const Link l = m_links[(size_t)m_st.selLink];
        m_st.selLink = -1;
        m_edit(tr("Unlink"), l.del);
        return;
    }
    if (e->key() == Qt::Key_F) { fit(); return; }
    QWidget::keyPressEvent(e);
}

void LogicGraph::addMenu(const QPoint& at, const std::string& pending) {
    const QPointF gAt = toGraph(at);
    const PinRef a = pending.empty() ? PinRef() : pinOf(pending);
    // a node fits when one of its pins can take the pending link
    const auto fits = [&](const std::string& id) {
        if (pending.empty()) return true;
        Node n;
        if (!makeNode(id, n)) return false;
        Node* keep = &n;
        for (int side = 0; side < 2; side++)
            for (const Pin& p : side ? keep->outs : keep->ins)
                if (actionFor(a, PinRef{keep, &p, side == 1})) return true;
        return false;
    };
    std::vector<std::pair<QString, std::vector<std::string>>> groups(5);
    groups[0].first = tr("Events");
    for (const Json& e : m_p.S["events"]) groups[0].second.push_back("ev:" + str(e, "id"));
    groups[1].first = tr("Event variables");
    for (auto& [k, v] : m_p.S["vars"].items()) { (void)v; groups[1].second.push_back("var:" + k); }
    groups[2].first = tr("Counters");
    for (auto& [k, v] : m_p.S["counters"].items()) { (void)v; groups[2].second.push_back("counter:" + k); }
    groups[3].first = tr("Flags");
    for (auto& [k, v] : m_p.S["flags"].items()) { (void)v; groups[3].second.push_back("flag:" + k); }
    groups[4].first = tr("NPCs");
    for (const std::string& n : m_p.npcIds()) groups[4].second.push_back("npc:" + n);

    QMenu menu(this);
    auto* search = new QLineEdit;
    search->setPlaceholderText(tr("Search nodes…"));
    search->setClearButtonEnabled(true);
    auto* wa = new QWidgetAction(&menu);
    wa->setDefaultWidget(search);
    menu.addAction(wa);
    if (pending.empty()) menu.addAction(tr("Fit view (F)"), this, [this] { fit(); });
    std::vector<std::pair<QAction*, std::string>> items;
    for (const auto& [title, ids] : groups) {
        std::vector<std::string> shown;
        for (const std::string& id : ids) if (!m_nodes.count(id) && fits(id)) shown.push_back(id);
        if (shown.empty()) continue;
        menu.addSection(title);
        for (const std::string& id : shown) items.push_back({menu.addAction(q(refName(id))), id});
    }
    connect(search, &QLineEdit::textChanged, &menu, [&items](const QString& t) {
        for (auto& [act, id] : items) act->setVisible(t.isEmpty() || q(id).contains(t, Qt::CaseInsensitive));
    });
    QTimer::singleShot(0, search, [search] { search->setFocus(); });
    QAction* chosen = menu.exec(mapToGlobal(at));
    if (!chosen) { update(); return; }
    std::string id;
    for (auto& [act, nid] : items) if (act == chosen) id = nid;
    if (id.empty()) return;   // Fit view
    m_st.extra.insert(id);
    m_st.pos[id] = gAt - QPointF(0, 14);
    savePositions();
    rebuild();
    if (!pending.empty()) {   // connect the pending link to the new node's first pin that fits
        const PinRef from = pinOf(pending);
        const Node& n = m_nodes.at(id);
        for (int side = 0; side < 2; side++)
            for (const Pin& p : side ? n.outs : n.ins) {
                Action act;
                if (actionFor(from, PinRef{&n, &p, side == 1}, &act)) { m_edit(act.first, act.second); return; }
            }
    }
    update();
}
