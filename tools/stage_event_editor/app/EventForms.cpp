// EventForms.cpp -- the event editor's inspector: an event, a floor, a missing id; the condition rows and action rows
// every editor shares (events, NPC chat lines, dialogue choices and start rows). As event_editor.html's renderForm,
// renderFloor, renderMissing, condHtml / bindCond, logicHtml / bindLogic.
#include "EventEditor.h"

#include "EventWidgets.h"
#include "FlowLayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using stage::Json;
using namespace EvUi;

namespace {
bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
std::string kindOf(const std::string& s) { return s.substr(0, s.find(':')); }
std::string nameOf(const std::string& s) { const size_t c = s.find(':'); return c == std::string::npos ? std::string() : s.substr(c + 1); }

void addHeader(QComboBox* c, const QString& text) {
    c->addItem(text, QStringLiteral("\x01"));
    if (auto* m = qobject_cast<QStandardItemModel*>(c->model())) {
        QStandardItem* it = m->item(c->count() - 1);
        it->setEnabled(false);
        QFont f = it->font();
        f.setBold(true);
        it->setFont(f);
    }
}

QHBoxLayout* row(QWidget* lbl = nullptr) {
    auto* h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    if (lbl) { lbl->setMinimumWidth(130); h->addWidget(lbl); }
    return h;
}

QSpinBox* spin(int v, int lo = -999999, int hi = 999999) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(v);
    s->setKeyboardTracking(false);
    return s;
}
}  // namespace

QString EventEditor::catOf(const std::string& ref) {
    const Json* r = m_p.recOf(ref);
    if (r && r->contains("category") && (*r)["category"].is_string() && !(*r)["category"].get<std::string>().empty()) return q((*r)["category"].get<std::string>());
    const std::string k = kindOf(ref);
    if (k == "flag") {
        const std::string setBy = r ? r->value("setBy", std::string()) : std::string();
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("^(ch_\\d+)")).match(q(setBy));
        return m.hasMatch() ? tr("Chapter %1").arg(m.captured(1)) : tr("Uncategorized");
    }
    return k == "counter" ? tr("Story counters") : tr("Uncategorized");
}

QStringList EventEditor::actionTargets(const std::string& type) const {
    QStringList out;
    const Json& S = m_p.S;
    auto keys = [&](const char* k) { for (auto& [n, v] : S[k].items()) { (void)v; out << q(n); } };
    if (type == "addVar" || type == "setVar") keys("vars");
    else if (type == "addCounter") keys("counters");
    else if (type == "setFlag") keys("flags");
    else if (type == "addStat") for (const std::string& s : m_p.stats) out << q(s);
    else for (const std::string& i : m_p.items) out << q(i);
    return out;
}

QComboBox* EventEditor::subjectCombo(const std::string& cur) {
    auto* c = new QComboBox;
    c->setMaxVisibleItems(24);
    auto add = [&](const std::string& v, const QString& label) { c->addItem(label, q(v)); if (v == cur) c->setCurrentIndex(c->count() - 1); };
    std::map<QString, std::vector<std::string>> vg;
    for (auto& [v, r] : m_p.S["vars"].items()) { (void)r; vg[catOf("var:" + v)].push_back(v); }
    for (const auto& [cat, list] : vg) { addHeader(c, tr("Event variables") + QStringLiteral(" · ") + cat); for (const std::string& v : list) add("var:" + v, q(v)); }
    auto group = [&](const QString& title, const std::vector<std::string>& ids, const char* pre) {
        if (ids.empty()) return;
        addHeader(c, title);
        for (const std::string& id : ids) add(pre + id, q(id));
    };
    std::vector<std::string> counters, flags, events, floors, talkers;
    for (auto& [k, v] : m_p.S["counters"].items()) { (void)v; counters.push_back(k); }
    for (auto& [k, v] : m_p.S["flags"].items()) { (void)v; flags.push_back(k); }
    for (const Json& e : m_p.S["events"]) {
        events.push_back(e["id"].get<std::string>());
        if (e["trigger"].value("on", std::string()) == "talk" && !e["dialogue"].get<std::string>().empty()) talkers.push_back(e["id"].get<std::string>());
    }
    for (const Json& f : m_p.S["floors"]) floors.push_back(f["id"].get<std::string>());
    group(tr("Counters"), counters, "counter:");
    group(tr("Flags"), flags, "flag:");
    group(tr("Events done"), events, "event:");
    group(tr("Talks with (count)"), m_p.npcIds(), "talks:");
    group(tr("Status"), m_p.stats, "stat:");
    group(tr("Floor cleared"), floors, "floor:");
    group(tr("Item held"), m_p.items, "item:");
    group(tr("Current stage"), floors, "stage:");
    group(tr("Choice made in"), talkers, "choice:");
    if (!cur.empty() && c->findData(q(cur)) < 0) { c->addItem(QStringLiteral("⛔ ") + q(cur), q(cur)); c->setCurrentIndex(c->count() - 1); }
    return c;
}

// ---- the condition editor ------------------------------------------------------------------------------------------

QWidget* EventEditor::condBox(const QString& key, const std::function<Json()>& get, const std::function<void(const Json&)>& set,
                              const QString& label, const QString& none) {
    auto* box = new QWidget;
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);
    const Json cond = get();
    const stage::CondRows cr = stage::condToRows(cond);
    const std::string k = key.toStdString();
    if (cr.raw || m_condJson.count(k)) {   // nested logic, or the user's choice: JSON
        if (cr.raw) v->addWidget(muted(tr("A condition the editor cannot show as rows (kept as written):")));
        auto* h = row();
        auto* line = new CommitLine(cond.is_null() ? QString() : q(cond.dump()));
        line->setPlaceholderText(tr("JSON condition, e.g. {\"type\": \"talkCountAtLeast\", \"npc\": \"villager\", \"value\": 8} (empty = always)"));
        line->onCommit = [this, set, label](const QString& s) {
            if (s.trimmed().isEmpty()) { edit(label, [set] { set(Json()); }); return; }
            const Json j = Json::parse(s.toStdString(), nullptr, false);
            if (j.is_discarded()) { toast(tr("Not valid JSON: the field was not changed."), true); return; }
            edit(label, [set, j] { set(j); });
        };
        h->addWidget(line, 1);
        if (!cr.raw) {
            auto* back = new QToolButton;
            back->setText(tr("back to rows"));
            connect(back, &QToolButton::clicked, this, [this, k] { m_condJson.erase(k); scheduleRender(); });
            h->addWidget(back);
        }
        v->addLayout(h);
        return box;
    }
    const std::vector<Json> rows = cr.rows;
    const std::string mode = cr.mode;
    auto write = [this, set, label](const std::string& m, const std::vector<Json>& rs) { edit(label, [set, m, rs] { set(stage::rowsToCond(m, rs)); }); };
    for (size_t i = 0; i < rows.size(); i++) {
        const stage::CondRow r = stage::leafToRow(rows[i]);
        const std::string kk = kindOf(r.subj);
        auto* h = row();
        QComboBox* subj = subjectCombo(r.subj);
        auto* op = new QComboBox;
        for (const auto& [o, lb] : stage::opsFor(kk)) {
            static const std::map<std::string, const char*> opNames = {
                {"opSet", QT_TR_NOOP("is set")}, {"opNotSet", QT_TR_NOOP("is not set")}, {"opDone", QT_TR_NOOP("is done")}, {"opNotDone", QT_TR_NOOP("is not done")},
                {"opCleared", QT_TR_NOOP("is cleared")}, {"opNotCleared", QT_TR_NOOP("is not cleared")}, {"opOnStage", QT_TR_NOOP("is the current stage")},
                {"opNotOnStage", QT_TR_NOOP("is not the current stage")}, {"opChose", QT_TR_NOOP("chose")}};
            auto it = opNames.find(lb);
            op->addItem(it != opNames.end() ? tr(it->second) : q(lb), q(o));
        }
        op->setCurrentIndex(std::max(0, op->findData(q(r.op))));
        h->addWidget(subj, 2);
        h->addWidget(op);
        QSpinBox* val = nullptr;
        QComboBox* choice = nullptr;
        if (r.op != "set" && r.op != "not") {
            if (kk == "choice") {
                choice = new QComboBox;
                choice->setEditable(true);
                for (const std::string& c : m_p.choiceIdsOf(nameOf(r.subj))) choice->addItem(q(c));
                choice->setEditText(r.val.is_string() ? q(r.val.get<std::string>()) : QString());
                h->addWidget(choice);
            } else {
                val = spin(r.val.is_number() ? (int)r.val.get<double>() : 0);
                h->addWidget(val);
            }
        }
        auto* del = new QToolButton;
        del->setText(QStringLiteral("✕"));
        h->addWidget(del);
        auto upd = [=]() {
            const std::string s = subj->currentData().toString().toStdString();
            if (s == "\x01") return;
            std::string o = op->currentData().toString().toStdString();
            const auto ops = stage::opsFor(kindOf(s));
            if (std::none_of(ops.begin(), ops.end(), [&](const auto& x) { return x.first == o; })) o = ops[0].first;
            Json value = val ? Json(val->value()) : choice ? Json(choice->currentText().toStdString()) : Json(kindOf(s) == "item" ? 1 : 0);
            std::vector<Json> rs = rows;
            rs[i] = stage::rowToLeaf(s, o, value);
            write(mode, rs);
        };
        connect(subj, &QComboBox::activated, this, upd);
        connect(op, &QComboBox::activated, this, upd);
        if (val) connect(val, &QSpinBox::valueChanged, this, upd);
        if (choice) connect(choice->lineEdit(), &QLineEdit::editingFinished, this, upd);
        connect(del, &QToolButton::clicked, this, [=] { std::vector<Json> rs = rows; rs.erase(rs.begin() + (long)i); write(mode, rs); });
        v->addLayout(h);
    }
    if (rows.empty()) v->addWidget(muted(none.isEmpty() ? tr("Always.") : none));
    auto* h = row();
    if (rows.size() > 1) {
        auto* m = new QComboBox;
        m->addItem(tr("all of these"), QStringLiteral("all"));
        m->addItem(tr("any of these"), QStringLiteral("any"));
        m->setCurrentIndex(mode == "any" ? 1 : 0);
        connect(m, &QComboBox::activated, this, [=](int ix) { write(ix == 1 ? "any" : "all", rows); });
        h->addWidget(m);
    }
    auto* addB = new QPushButton(tr("+ Add condition"));
    connect(addB, &QPushButton::clicked, this, [=] {
        std::vector<Json> rs = rows;
        const Json& vars = m_p.S["vars"];
        if (!vars.empty()) rs.push_back(Json{{"type", "varAtLeast"}, {"var", vars.begin().key()}, {"value", 1}});
        else rs.push_back(Json{{"type", "counterAtLeast"}, {"counter", m_p.S["counters"].empty() ? std::string() : m_p.S["counters"].begin().key()}, {"min", 1}});
        write(mode, rs);
    });
    auto* js = new QToolButton;
    js->setText(QStringLiteral("{ }"));
    js->setToolTip(tr("Edit as JSON (for nested all / any / not)"));
    connect(js, &QToolButton::clicked, this, [this, k] { m_condJson.insert(k); scheduleRender(); });
    h->addWidget(addB);
    h->addWidget(js);
    h->addStretch(1);
    v->addLayout(h);
    return box;
}

// ---- action rows -------------------------------------------------------------------------------------------------

QWidget* EventEditor::actsBox(const QString& key, const std::function<Json()>& get, const std::function<Json*()>& list, const QString& label) {
    (void)key;
    auto* box = new QWidget;
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);
    const Json g0 = get();
    const Json acts = g0.is_array() ? g0 : g0.is_object() ? Json::array({g0}) : Json::array();
    static const std::vector<std::pair<std::string, const char*>> kinds = {
        {"addVar", QT_TR_NOOP("add to variable")}, {"setVar", QT_TR_NOOP("set variable")}, {"addCounter", QT_TR_NOOP("add to counter")},
        {"setFlag", QT_TR_NOOP("set flag")}, {"give", QT_TR_NOOP("give item")}, {"take", QT_TR_NOOP("take item")}, {"addStat", QT_TR_NOOP("add to status")}};
    for (size_t i = 0; i < acts.size(); i++) {
        const Json a = acts[i];
        const std::string type = a.value("type", std::string());
        auto* h = row();
        const bool known = std::any_of(kinds.begin(), kinds.end(), [&](const auto& x) { return x.first == type; });
        if (known) {
            auto* tc = new QComboBox;
            for (const auto& [t, lb] : kinds) tc->addItem(tr(lb), q(t));
            tc->setCurrentIndex(tc->findData(q(type)));
            auto* tg = new QComboBox;
            const QStringList targets = actionTargets(type);
            tg->addItems(targets);
            const QString cur = q(stage::actTarget(a));
            if (!cur.isEmpty() && !targets.contains(cur)) tg->addItem(cur);
            tg->setCurrentText(cur);
            h->addWidget(tc);
            h->addWidget(tg, 1);
            QSpinBox* val = nullptr;
            if (type == "addVar" || type == "setVar" || type == "addCounter" || type == "addStat") {
                const Json av = stage::actValue(a);
                val = spin(av.is_number() ? (int)av.get<double>() : av.is_boolean() ? (av.get<bool>() ? 1 : 0) : 0);
                h->addWidget(val);
            }
            connect(tc, &QComboBox::activated, this, [=](int) {
                const std::string nt = tc->currentData().toString().toStdString();
                const QStringList ts = actionTargets(nt);
                Json old = stage::actValue(a);
                if (!old.is_number() || old.get<double>() == 0) old = 1;
                edit(label, [=] { Json* l = list(); (*l)[i] = stage::makeAction(nt, ts.isEmpty() ? std::string() : ts[0].toStdString(), old); });
            });
            auto upd = [=] {
                const std::string t = tc->currentData().toString().toStdString(), target = tg->currentText().toStdString();
                const Json value = val ? Json(val->value()) : Json(1);
                edit(label, [=] { Json* l = list(); (*l)[i] = stage::makeAction(t, target, value); });
            };
            connect(tg, &QComboBox::activated, this, upd);
            if (val) connect(val, &QSpinBox::valueChanged, this, upd);
        } else {   // an action the rows do not know: kept as JSON
            auto* c = chip(q(type.empty() ? "?" : type) + QStringLiteral(" ") + q(a.dump()).left(60));
            c->setToolTip(q(a.dump()));
            h->addWidget(c, 1);
        }
        auto* del = new QToolButton;
        del->setText(QStringLiteral("✕"));
        connect(del, &QToolButton::clicked, this, [=] { edit(label, [=] { Json* l = list(); l->erase(i); }); });
        h->addWidget(del);
        v->addLayout(h);
    }
    if (acts.empty()) v->addWidget(muted(tr("Nothing else.")));
    auto* add = new QPushButton(tr("+ Add action"));
    connect(add, &QPushButton::clicked, this, [=] {
        edit(label, [=] {
            Json* l = list();
            const Json& vars = m_p.S["vars"];
            if (!vars.empty()) l->push_back(stage::makeAction("addVar", vars.begin().key(), 1));
            else l->push_back(stage::makeAction("addCounter", m_p.S["counters"].empty() ? std::string() : m_p.S["counters"].begin().key(), 1));
        });
    });
    auto* h = row();
    h->addWidget(add);
    h->addStretch(1);
    v->addLayout(h);
    return box;
}

// ---- an event ------------------------------------------------------------------------------------------------------

QWidget* EventEditor::logicBox(Json& e) {
    const std::string id = e["id"].get<std::string>();
    auto* g = new QGroupBox(tr("Logic"));
    auto* v = new QVBoxLayout(g);
    auto E = [this, id](const QString& label, const std::function<void(Json&)>& fn) {
        edit(label, [this, id, fn] { if (Json* x = m_p.ev(id)) fn(*x); });
    };
    const QString hTrig = tr("Trigger of %1").arg(q(id));
    // Fires when
    {
        auto* h = row(new QLabel(tr("Fires when")));
        auto* on = new QComboBox;
        on->addItem(tr("the player steps on its tile"), QStringLiteral("step"));
        on->addItem(tr("the player talks to"), QStringLiteral("talk"));
        on->addItem(tr("this variable changes"), QStringLiteral("varChanged"));
        on->addItem(tr("another event links to it (next)"), QStringLiteral("chain"));
        const std::string trOn = e["trigger"].value("on", std::string("step"));
        on->setCurrentIndex(std::max(0, on->findData(q(trOn))));
        connect(on, &QComboBox::activated, this, [=](int) {
            const std::string o = on->currentData().toString().toStdString();
            E(hTrig, [this, o](Json& x) {
                if (o == "talk") x["trigger"] = Json{{"on", o}, {"npc", m_p.npcPlaces.empty() ? std::string() : m_p.npcPlaces.begin()->first}};
                else if (o == "varChanged") x["trigger"] = Json{{"on", o}, {"var", m_p.S["vars"].empty() ? std::string() : m_p.S["vars"].begin().key()}};
                else x["trigger"] = Json{{"on", o}};
            });
        });
        h->addWidget(on);
        if (trOn == "talk" || trOn == "varChanged") {
            auto* arg = new QComboBox;
            const std::string cur = e["trigger"].value(trOn == "talk" ? "npc" : "var", std::string());
            if (trOn == "talk") {
                std::vector<std::string> ids;
                for (const auto& [n, f] : m_p.npcPlaces) { (void)f; ids.push_back(n); }
                if (!cur.empty() && std::find(ids.begin(), ids.end(), cur) == ids.end()) ids.push_back(cur);
                for (const std::string& n : ids) {
                    auto it = m_p.npcPlaces.find(n);
                    QStringList fl;
                    if (it != m_p.npcPlaces.end()) for (const std::string& f : it->second) fl << q(f);
                    arg->addItem(it != m_p.npcPlaces.end() ? QStringLiteral("%1 (%2)").arg(q(n), fl.join(QStringLiteral(", "))) : tr("%1 (not placed)").arg(q(n)), q(n));
                }
            } else {
                for (auto& [k, r] : m_p.S["vars"].items()) { (void)r; arg->addItem(q(k), q(k)); }
                if (!cur.empty() && arg->findData(q(cur)) < 0) arg->addItem(q(cur), q(cur));
            }
            arg->setCurrentIndex(std::max(0, arg->findData(q(cur))));
            connect(arg, &QComboBox::activated, this, [=](int) {
                const std::string val = arg->currentData().toString().toStdString();
                E(hTrig, [val](Json& x) { if (x["trigger"]["on"] == "talk") x["trigger"]["npc"] = val; else x["trigger"]["var"] = val; });
            });
            h->addWidget(arg, 1);
        }
        h->addStretch(1);
        v->addLayout(h);
        // a talk event: its dialogue, what follows it, where, its menu order (08_NPC_DIALOGUE.md)
        if (trOn == "talk") {
            auto dlgCombo = [&](const std::string& cur, const QString& none) {
                auto* c = new QComboBox;
                c->addItem(none, QString());
                std::vector<std::string> ds = m_p.dialogueList();
                if (!cur.empty() && std::find(ds.begin(), ds.end(), cur) == ds.end()) ds.push_back(cur);
                for (const std::string& d : ds) {
                    const auto dl = m_p.dialogueList();
                    c->addItem(q(d) + (std::find(dl.begin(), dl.end(), d) == dl.end() ? QStringLiteral(" ⛔") : QString()), q(d));
                }
                c->setCurrentIndex(std::max(0, c->findData(q(cur))));
                return c;
            };
            auto* h1 = row(new QLabel(tr("Dialogue")));
            auto* dlg = dlgCombo(e["dialogue"].get<std::string>(), tr("(none: the event text as one page)"));
            connect(dlg, &QComboBox::activated, this, [=](int) { const std::string d = dlg->currentData().toString().toStdString(); E(hTrig, [d](Json& x) { x["dialogue"] = d; }); });
            h1->addWidget(dlg, 1);
            v->addLayout(h1);
            auto* h2 = row(new QLabel(tr("After finishing")));
            auto* after = dlgCombo(e["afterDialogue"].get<std::string>(), tr("(hide the topic)"));
            connect(after, &QComboBox::activated, this, [=](int) { const std::string d = after->currentData().toString().toStdString(); E(hTrig, [d](Json& x) { x["afterDialogue"] = d; }); });
            h2->addWidget(after, 1);
            v->addLayout(h2);
            auto* h3 = row(new QLabel(tr("Talked to on")));
            bool cond = false;
            const std::string cur = m_p.talkFloorOf(e, &cond);
            if (cond) h3->addWidget(muted(tr("set by its conditions (any of / JSON)")));
            else {
                const std::string npc = e["trigger"].value("npc", std::string());
                auto it = m_p.npcPlaces.find(npc);
                std::vector<std::string> fl = it != m_p.npcPlaces.end() ? it->second : std::vector<std::string>();
                auto* fc = new QComboBox;
                fc->addItem(tr("any floor it stands on (%1)").arg(fl.size()), QString());
                if (!cur.empty() && std::find(fl.begin(), fl.end(), cur) == fl.end()) fc->addItem(q(cur) + QStringLiteral(" ⚠"), q(cur));
                for (const std::string& f : fl) fc->addItem(q(f), q(f));
                fc->setCurrentIndex(std::max(0, fc->findData(q(cur))));
                connect(fc, &QComboBox::activated, this, [=](int) {
                    const std::string f = fc->currentData().toString().toStdString();
                    E(hTrig, [this, f](Json& x) { m_p.setTalkFloor(x, f); });
                });
                h3->addWidget(fc, 1);
            }
            v->addLayout(h3);
            auto* h4 = row(new QLabel(tr("Menu order")));
            auto* order = new CommitLine(e["topicOrder"].is_number() ? QString::number(e["topicOrder"].get<int>()) : QString());
            order->setPlaceholderText(tr("auto"));
            order->setMaximumWidth(80);
            order->onCommit = [=](const QString& s) {
                bool ok = false;
                const int n = s.trimmed().toInt(&ok);
                E(hTrig, [ok, n](Json& x) { x["topicOrder"] = ok ? Json(n) : Json(); });
            };
            h4->addWidget(order);
            h4->addStretch(1);
            v->addLayout(h4);
        }
    }
    // Repeat
    {
        auto* h = row(new QLabel(tr("Repeat")));
        auto* rep = new QCheckBox(tr("repeatable (runs again after finishing)"));
        rep->setChecked(e["once"] == false);
        connect(rep, &QCheckBox::toggled, this, [=](bool on) { E(hTrig, [on](Json& x) { x["once"] = !on; if (!on) x["maxTimes"] = 0; }); });
        h->addWidget(rep);
        if (e["once"] == false) {
            h->addWidget(muted(tr("at most")));
            auto* mx = spin(e["maxTimes"].get<int>(), 0, 9999);
            connect(mx, &QSpinBox::valueChanged, this, [=](int n) { E(hTrig, [n](Json& x) { x["maxTimes"] = n; }); });
            h->addWidget(mx);
            h->addWidget(muted(tr("times (0 = no limit); its \"Then fire\" links wake only the first time")), 1);
        }
        h->addStretch(1);
        v->addLayout(h);
    }
    // Active only when
    v->addWidget(heading(tr("Active only when")));
    v->addWidget(condBox(QStringLiteral("ev"), [this, id] { const Json* x = m_p.ev(id); return x ? m_p.reqJson(*x) : Json(); },
                         [this, id](const Json& c) { if (Json* x = m_p.ev(id)) m_p.setEventReq(*x, c); }, tr("Conditions of %1").arg(q(id)), tr("Always active.")));
    // Then do
    v->addWidget(heading(tr("Then do")));
    v->addWidget(actsBox(QStringLiteral("ev"), [this, id] { const Json* x = m_p.ev(id); return x ? (*x)["actions"] : Json::array(); },
                         [this, id] { Json* x = m_p.ev(id); return x ? &(*x)["actions"] : nullptr; }, tr("Actions of %1").arg(q(id))));
    // Then fire (direct link)
    v->addWidget(heading(tr("Then fire (direct link)")));
    {
        auto* chips = new QWidget;
        auto* flow = new FlowLayout(chips, 4);
        for (const Json& n : e["next"]) {
            const std::string nx = n.get<std::string>();
            auto* c = chip(q(nx) + QStringLiteral("  ✕"), m_p.ev(nx) ? QColor() : QColor("#ff5d5d"), tr("Click: open it · ✕: remove the link (click its right edge)"));
            auto* menuOpen = new QAction(tr("Open"), c);
            auto* menuDel = new QAction(tr("Remove link"), c);
            c->addAction(menuOpen);
            c->addAction(menuDel);
            c->setPopupMode(QToolButton::InstantPopup);
            connect(menuOpen, &QAction::triggered, this, [this, nx] { if (m_p.ev(nx)) select(nx); });
            connect(menuDel, &QAction::triggered, this, [=] { E(tr("Links of %1").arg(q(id)), [nx](Json& x) { Json& l = x["next"]; l.erase(std::remove(l.begin(), l.end(), Json(nx)), l.end()); }); });
            flow->addWidget(c);
        }
        auto* add = new QComboBox;
        add->addItem(tr("+ link an event…"), QString());
        for (const Json& o : m_p.S["events"])
            if (o["id"] != id && std::find(e["next"].begin(), e["next"].end(), o["id"]) == e["next"].end()) add->addItem(q(o["id"].get<std::string>()), q(o["id"].get<std::string>()));
        connect(add, &QComboBox::activated, this, [=](int) {
            const std::string nx = add->currentData().toString().toStdString();
            if (!nx.empty()) E(tr("Links of %1").arg(q(id)), [nx](Json& x) { x["next"].push_back(nx); });
        });
        flow->addWidget(add);
        v->addWidget(chips);
    }
    // Fired by
    {
        auto* h = row(new QLabel(tr("Fired by")));
        auto* chips = new QWidget;
        auto* flow = new FlowLayout(chips, 4);
        int n = 0;
        for (const Json& o : m_p.S["events"])
            if (std::find(o["next"].begin(), o["next"].end(), Json(id)) != o["next"].end()) {
                const std::string oid = o["id"].get<std::string>();
                auto* c = chip(QStringLiteral("→ ") + q(oid));
                connect(c, &QToolButton::clicked, this, [this, oid] { select(oid); });
                flow->addWidget(c);
                n++;
            }
        const std::string on = e["trigger"].value("on", std::string());
        if (on == "talk") { flow->addWidget(chip(QStringLiteral("💬 ") + q(e["trigger"].value("npc", std::string())))); n++; }
        if (on == "varChanged") { flow->addWidget(chip(QStringLiteral("◆ ") + q(e["trigger"].value("var", std::string())))); n++; }
        if (!n) flow->addWidget(muted(QStringLiteral("—")));
        h->addWidget(chips, 1);
        v->addLayout(h);
    }
    return g;
}

QWidget* EventEditor::eventForm(Json& e) {
    const std::string id = e["id"].get<std::string>();
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(10, 8, 10, 10);
    const auto fl = m_p.floorsOf(id);
    const std::string on = e["trigger"].value("on", std::string("step"));
    auto E = [this, id](const QString& label, const std::function<void(Json&)>& fn) {
        edit(label, [this, id, fn] { if (Json* x = m_p.ev(id)) fn(*x); });
    };
    if (fl.empty() && on == "step") {
        auto* h = row();
        h->addWidget(banner(tr("⚠ <b>Orphan:</b> no floor can roll this event, so it never appears in game. Fix: <b>+ Add floor…</b> below, or"), false), 1);
        auto* del = new QPushButton(tr("Delete event"));
        connect(del, &QPushButton::clicked, this, [this, id] {
            edit(tr("Delete %1").arg(q(id)), [this, id] {
                Json& evs = m_p.S["events"];
                for (size_t i = 0; i < evs.size(); i++) if (evs[i]["id"] == id) { evs.erase(i); break; }
                m_p.selected.clear();
            });
        });
        h->addWidget(del);
        v->addLayout(h);
    }
    // id, kind, pool, text key
    {
        auto* grid = new QGridLayout;
        grid->addWidget(heading(QStringLiteral("eventId")), 0, 0);
        grid->addWidget(heading(tr("kind")), 0, 1);
        grid->addWidget(heading(tr("pool")), 0, 2);
        grid->addWidget(heading(tr("text key")), 0, 3);
        auto* idEd = new CommitLine(q(id));
        auto* idErr = new QLabel;
        idErr->setStyleSheet(QStringLiteral("color:#ff5d5d"));
        idErr->setWordWrap(true);
        auto errText = [this](const std::string& err) {
            if (err.empty()) return QString();
            if (err == "format") return tr("Any name works, Chinese too (e.g. ev_village_scarecrow or 稻草人), except empty, spaces at the ends, or \" ' < > & | \\ /");
            return tr("%1 already exists in %2").arg(QString(), q(err.substr(4))).trimmed();
        };
        connect(idEd, &QLineEdit::textEdited, this, [=](const QString& s) {
            const std::string nu = s.trimmed().toStdString();
            idErr->setText(nu == id ? QString() : errText(m_p.idError(nu, m_p.ev(id))));
        });
        idEd->onCommit = [=](const QString& s) {
            const std::string nu = s.trimmed().toStdString();
            if (nu == id) return;
            const std::string err = m_p.idError(nu, m_p.ev(id));
            if (!err.empty()) { idErr->setText(errText(err)); return; }
            edit(tr("Rename %1 → %2").arg(q(id), q(nu)), [this, id, nu] { if (Json* x = m_p.ev(id)) m_p.renameEvent(*x, nu); });
        };
        grid->addWidget(idEd, 1, 0);
        grid->addWidget(idErr, 2, 0, 1, 4);
        auto* kind = new QComboBox;
        const std::string k = e["kind"].get<std::string>();
        if (k.empty()) kind->addItem(tr("(choose a kind)"), QString());
        const auto kl = m_p.kindsList();
        for (const std::string& kk : kl) {
            const QString n = q(m_p.kindName(kk, lang().toStdString()));
            kind->addItem(q(kk) + (n.isEmpty() ? QString() : QStringLiteral(" — ") + n), q(kk));
        }
        if (!k.empty() && std::find(kl.begin(), kl.end(), k) == kl.end()) kind->addItem(QStringLiteral("⚠ ") + q(k), q(k));
        kind->addItem(tr("+ New kind…"), QStringLiteral("__new"));
        kind->setCurrentIndex(std::max(0, kind->findData(q(k))));
        connect(kind, &QComboBox::activated, this, [=](int) {
            const QString nk = kind->currentData().toString();
            if (nk == QStringLiteral("__new")) { setTab(QStringLiteral("kinds")); return; }
            E(tr("Kind of %1 → %2").arg(q(id), nk), [nk](Json& x) { x["kind"] = nk.toStdString(); });
        });
        grid->addWidget(kind, 1, 1);
        auto* pool = new QComboBox;
        for (const std::string& p : m_p.pools()) pool->addItem(q(p));
        pool->setCurrentText(q(e["pool"].get<std::string>()));
        connect(pool, &QComboBox::activated, this, [=](int) {
            const std::string p = pool->currentText().toStdString();
            edit(tr("Move %1 to %2").arg(q(id), q(p)), [this, id, p] {
                Json& evs = m_p.S["events"];
                for (size_t i = 0; i < evs.size(); i++)
                    if (evs[i]["id"] == id) { Json x = evs[i]; evs.erase(i); x["pool"] = p; evs.push_back(x); break; }
            });
        });
        grid->addWidget(pool, 1, 2);
        auto* key = new QLineEdit(q(e["text"].get<std::string>()));
        key->setEnabled(false);
        grid->addWidget(key, 1, 3);
        v->addLayout(grid);
    }
    // floors that can roll it
    v->addWidget(heading(tr("Floors that can roll it (%1)").arg(fl.size())));
    {
        auto* chips = new QWidget;
        auto* flow = new FlowLayout(chips, 4);
        for (const Json* f : fl) {
            const std::string fid = (*f)["id"].get<std::string>();
            auto* box = new QWidget;
            auto* h = new QHBoxLayout(box);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(0);
            auto* c = chip(q(fid) + QStringLiteral(" ") + q((*f)["act"].get<std::string>()));
            connect(c, &QToolButton::clicked, this, [this, fid] { select(fid, true); });
            auto* pin = new QToolButton;
            pin->setText(QStringLiteral("📍"));
            pin->setAutoRaise(true);
            pin->setToolTip(tr("Place on %1's map (stage editor)").arg(q(fid)));
            const QString kindQ = q(e["kind"].get<std::string>());
            connect(pin, &QToolButton::clicked, this, [this, id, kindQ, fid] { Q_EMIT placeOnStage(q(id), kindQ, {q(fid)}); });
            auto* rm = new QToolButton;
            rm->setText(QStringLiteral("✕"));
            rm->setAutoRaise(true);
            rm->setToolTip(tr("Disconnect"));
            connect(rm, &QToolButton::clicked, this, [this, id, fid] {
                edit(tr("Disconnect %1 from %2").arg(q(id), q(fid)), [this, id, fid] {
                    if (Json* f2 = m_p.flo(fid)) { Json& l = (*f2)["events"]; auto it = std::find(l.begin(), l.end(), Json(id)); if (it != l.end()) l.erase(it); }
                });
            });
            h->addWidget(c);
            h->addWidget(pin);
            h->addWidget(rm);
            flow->addWidget(box);
        }
        auto* add = new QPushButton(tr("+ Add floor…"));
        connect(add, &QPushButton::clicked, this, [this, id] { addFloorsDialog(id); });
        flow->addWidget(add);
        v->addWidget(chips);
    }
    v->addWidget(logicBox(e));
    // text in every language
    const std::string textKey = e["text"].get<std::string>();
    const Json tx = m_p.text(textKey);
    const bool ph = m_p.isPlaceholder(textKey);
    v->addWidget(rich(QStringLiteral("<span style='color:#8b929c;font-weight:600'>%1</span> %2 <span style='color:#8b929c;font-size:11px'>%3</span>")
                          .arg(tr("Text"), ph ? QStringLiteral("<span style='color:#ff9f43'>%1</span>").arg(tr("⚠ placeholder: write the real line")) : QString(),
                               esc(tr("Enter = new line (saved as \\n)")))));
    QLabel* preview = nullptr;
    CommitText* zhBox = nullptr;
    const QString kindLbl = e["kind"].get<std::string>().empty() ? tr("kind?") : q(e["kind"].get<std::string>());
    auto previewHtml = [kindLbl](const QString& zh) {
        return QStringLiteral("<small style='color:#c9a85a'>%1</small><br>%2").arg(esc(kindLbl),
                                                                                   zh.isEmpty() ? QStringLiteral("<span style='color:#8b929c'>…</span>") : esc(zh).replace(QStringLiteral("\n"), QStringLiteral("<br>")));
    };
    for (const char* l : stage::kLangs) {
        auto* h = row(new QLabel(QString::fromLatin1(l)));
        h->itemAt(0)->widget()->setMinimumWidth(50);
        auto* t = new CommitText(tx.contains(l) && tx[l].is_string() ? q(tx[l].get<std::string>()) : QString());
        t->setPlaceholderText(std::string(l) == "zh_TW" ? tr("The line the player reads") : tr("untranslated"));
        const std::string lang = l;
        t->onCommit = [=](const QString& s) {
            edit(tr("Text %1 [%2]").arg(q(id), q(lang)), [this, textKey, lang, s] {
                Json& tt = m_p.S["texts"][textKey];
                if (!tt.is_object()) tt = Json::object();
                tt[lang] = s.toStdString();
                if (lang == "zh_TW") tt.erase("_todo");
            });
        };
        if (lang == "zh_TW") zhBox = t;
        h->addWidget(t, 1);
        v->addLayout(h);
    }
    // title and description (the player's event log)
    auto setKeyText = [this, id](const char* field, const std::string& lang, const QString& s, const QString& label) {
        const std::string f = field;
        edit(label, [this, id, f, lang, s] {
            Json* x = m_p.ev(id);
            if (!x) return;
            if ((*x)[f].get<std::string>().empty()) (*x)[f] = id + "." + f;
            Json& tt = m_p.S["texts"][(*x)[f].get<std::string>()];
            if (!tt.is_object()) tt = Json::object();
            tt[lang] = s.toStdString();
        });
    };
    v->addWidget(heading(tr("Title (the player's event log)")));
    const Json ti = m_p.text(e["title"].get<std::string>()), de = m_p.text(e["desc"].get<std::string>());
    for (const char* l : stage::kLangs) {
        auto* h = row(new QLabel(QString::fromLatin1(l)));
        h->itemAt(0)->widget()->setMinimumWidth(50);
        auto* t = new CommitLine(ti.contains(l) && ti[l].is_string() ? q(ti[l].get<std::string>()) : QString());
        const std::string lang = l;
        t->onCommit = [=](const QString& s) { setKeyText("title", lang, s, tr("Title %1 [%2]").arg(q(id), q(lang))); };
        h->addWidget(t, 1);
        v->addLayout(h);
    }
    v->addWidget(heading(tr("Description (the player's event log)")));
    for (const char* l : stage::kLangs) {
        auto* h = row(new QLabel(QString::fromLatin1(l)));
        h->itemAt(0)->widget()->setMinimumWidth(50);
        auto* t = new CommitText(de.contains(l) && de[l].is_string() ? q(de[l].get<std::string>()) : QString());
        const std::string lang = l;
        t->onCommit = [=](const QString& s) { setKeyText("desc", lang, s, tr("Description %1 [%2]").arg(q(id), q(lang))); };
        h->addWidget(t, 1);
        v->addLayout(h);
    }
    {
        auto* h = row(new QLabel(tr("In the player's event log")));
        auto* c = new QComboBox;
        const Json* kr = m_p.S["kinds"].contains(e["kind"].get<std::string>()) ? &m_p.S["kinds"][e["kind"].get<std::string>()] : nullptr;
        const bool kindNo = kr && kr->contains("inLog") && (*kr)["inLog"] == false;
        c->addItem(tr("as its kind (%1)").arg(kindNo ? tr("no") : tr("yes")), QString());
        c->addItem(tr("yes"), QStringLiteral("1"));
        c->addItem(tr("no"), QStringLiteral("0"));
        c->setCurrentIndex(!e.contains("inLog") ? 0 : e["inLog"] == true ? 1 : 2);
        connect(c, &QComboBox::activated, this, [=](int) {
            const QString val = c->currentData().toString();
            E(tr("Event log %1").arg(q(id)), [val](Json& x) { if (val.isEmpty()) x.erase("inLog"); else x["inLog"] = val == QStringLiteral("1"); });
        });
        h->addWidget(c);
        h->addStretch(1);
        v->addLayout(h);
    }
    v->addWidget(heading(tr("Preview (game dialog)")));
    preview = rich(previewHtml(tx.contains("zh_TW") && tx["zh_TW"].is_string() ? q(tx["zh_TW"].get<std::string>()) : QString()));
    preview->setStyleSheet(QStringLiteral("background:#0d0f12; border:1px solid #6b5a3a; border-radius:4px; padding:8px; color:#f3e6c8"));
    v->addWidget(preview);
    // the preview label lives as long as the form: the zh_TW box updates it while typing
    if (zhBox) {
        QLabel* pv = preview;
        zhBox->onEdit = [pv, previewHtml](const QString& s) { pv->setText(previewHtml(s)); };
    }
    v->addStretch(1);
    return w;
}

void EventEditor::addFloorsDialog(const std::string& evId) {
    const Json* e = m_p.ev(evId);
    if (!e) return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Floors that can roll %1").arg(q(evId)));
    dlg.resize(560, 520);
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(rich(tr("Floors that can roll <b>%1</b>").arg(esc(q(evId)))));
    auto* area = new QScrollArea;
    area->setWidgetResizable(true);
    auto* inner = new QWidget;
    auto* iv = new QVBoxLayout(inner);
    std::vector<QCheckBox*> boxes;
    std::vector<std::string> acts;
    for (const Json& f : m_p.S["floors"]) if (std::find(acts.begin(), acts.end(), f["act"].get<std::string>()) == acts.end()) acts.push_back(f["act"].get<std::string>());
    const auto have = m_p.floorsOf(evId);
    for (const std::string& a : acts) {
        iv->addWidget(heading(q(a)));
        auto* gw = new QWidget;
        auto* flow = new FlowLayout(gw, 6);
        for (const Json& f : m_p.S["floors"]) {
            if (f["act"] != a) continue;
            auto* c = new QCheckBox(q(f["id"].get<std::string>()));
            const bool on = std::any_of(have.begin(), have.end(), [&](const Json* x) { return (*x)["id"] == f["id"]; });
            c->setChecked(on);
            c->setEnabled(!on);
            flow->addWidget(c);
            boxes.push_back(c);
        }
        iv->addWidget(gw);
    }
    iv->addStretch(1);
    area->setWidget(inner);
    v->addWidget(area, 1);
    auto* openStageC = new QCheckBox(tr("Then open the stage editor to place it on the map"));
    openStageC->setChecked(true);
    v->addWidget(openStageC);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Cancel);
    bb->addButton(tr("Connect"), QDialogButtonBox::AcceptRole);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    std::vector<std::string> ids;
    QStringList qids;
    for (QCheckBox* c : boxes) if (c->isEnabled() && c->isChecked()) { ids.push_back(c->text().toStdString()); qids << c->text(); }
    if (ids.empty()) return;
    const QString kind = q((*e)["kind"].get<std::string>());
    edit(tr("Connect %1 to %2").arg(q(evId), qids.join(QStringLiteral(", "))), [this, ids, evId] {
        for (const std::string& f : ids) if (Json* fl = m_p.flo(f)) (*fl)["events"].push_back(evId);
    });
    if (openStageC->isChecked()) Q_EMIT placeOnStage(q(evId), kind, qids);
}

void EventEditor::createEvent(const std::string& poolIn) {
    std::string pool = poolIn;
    if (pool.empty()) { const Json* s = m_p.ev(m_p.selected); pool = s ? (*s)["pool"].get<std::string>() : std::string("pool_act01"); }
    m_collapsed.erase(pool);
    const std::string id = m_p.newEventId();
    edit(tr("New event %1 in %2").arg(q(id), q(pool)), [this, id, pool] {
        if (std::find(m_p.S["pools"].begin(), m_p.S["pools"].end(), Json(pool)) == m_p.S["pools"].end()) m_p.S["pools"].push_back(pool);
        m_p.S["events"].push_back(m_p.blankEvent(id, pool));
        for (const char* k : {".text", ".title", ".desc"}) {
            Json t = Json::object();
            for (const char* l : stage::kLangs) t[l] = "";
            m_p.S["texts"][id + k] = t;
        }
        m_p.selected = id;
    });
}

void EventEditor::createFloor(const std::string& id, const std::string& fromId) {
    const Json* from = m_p.flo(fromId);
    if (!from || m_p.flo(id)) return;
    const std::string act = (*from)["act"].get<std::string>();
    edit(tr("Create floor %1 (%2)").arg(q(id), q(act)), [this, id, act, fromId] {
        m_p.S["floors"].push_back(Json{{"id", id}, {"act", act}, {"role", "normal"}, {"events", Json::array()}, {"next", nullptr}, {"derived", false},
                                       {"stairs", Json::array({Json{{"dir", "down"}, {"to", fromId}}})}});
    });
}

void EventEditor::createChapter(const std::string& act) {
    std::vector<std::string> ids;
    for (const auto& p : m_p.problems()) if (p.rule == "missing_event" && p.act == act) ids.push_back(p.id);
    if (ids.empty()) return;
    edit(tr("Create %1 missing events for %2").arg(ids.size()).arg(q(act)), [this, ids, act] { for (const std::string& id : ids) m_p.makeRecord(id, act); });
    toast(tr("Created %1 events for %2. Common variants went to pool_common, the rest to %3. They still need kinds and text: see the Problems tab.")
              .arg(ids.size()).arg(q(act), QStringLiteral("pool_act") + q(act.size() > 3 ? act.substr(3) : act)));
}

// ---- a floor -------------------------------------------------------------------------------------------------------

QWidget* EventEditor::floorForm(Json& f) {
    const std::string fid = f["id"].get<std::string>();
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(10, 8, 10, 10);
    auto EF = [this, fid](const QString& label, const std::function<void(Json&)>& fn) {   // a touched floor writes its own stairs list
        edit(label, [this, fid, fn] { if (Json* x = m_p.flo(fid)) { fn(*x); (*x)["derived"] = false; } });
    };
    v->addWidget(rich(QStringLiteral("<span style='font-size:16px;font-weight:600'>%1</span> <span style='color:#8b929c'>%2 · %3</span>%4")
                          .arg(esc(q(fid)), esc(q(f["act"].get<std::string>())), esc(q(f["role"].get<std::string>())),
                               m_p.isNewFloor(fid) ? QStringLiteral(" <b style='color:#4cd18a'>%1</b>").arg(tr("NEW")) : QString())));
    const auto reach = m_p.reachable();
    if (!reach.count(fid)) v->addWidget(banner(tr("⚠ <b>Unreachable:</b> no path of open or unlockable stairs leads here from F01."), false));
    {
        auto* h = row(new QLabel(tr("chapter")));
        auto* act = new QComboBox;
        std::set<std::string> acts;
        for (const Json& x : m_p.S["floors"]) acts.insert(x["act"].get<std::string>());
        for (const std::string& a : acts) act->addItem(q(a));
        act->setCurrentText(q(f["act"].get<std::string>()));
        connect(act, &QComboBox::activated, this, [=](int) {
            const std::string a = act->currentText().toStdString();
            EF(tr("%1 chapter → %2").arg(q(fid), q(a)), [a](Json& x) { x["act"] = a; });
        });
        h->addWidget(act);
        h->addWidget(new QLabel(tr("role")));
        auto* role = new QLineEdit(q(f["role"].get<std::string>()));
        role->setEnabled(false);
        h->addWidget(role);
        h->addStretch(1);
        v->addLayout(h);
    }
    // stairs
    v->addWidget(heading(tr("Stairs (%1)").arg(f["stairs"].size())));
    const Json stairs = f["stairs"];
    std::vector<std::string> here, defined;
    for (const Json& x : f["events"]) if (m_p.ev(x.get<std::string>())) here.push_back(x.get<std::string>());
    for (const Json& e : m_p.S["events"]) defined.push_back(e["id"].get<std::string>());
    if (stairs.empty()) v->addWidget(muted(tr("No stairs.")));
    for (size_t i = 0; i < stairs.size(); i++) {
        const Json st = stairs[i];
        const std::string to = st.value("to", std::string());
        const std::vector<std::string> locks = stage::stairUnlockEvents(st);   // W38: the stair opens when all are done
        const std::string lock = locks.empty() ? std::string() : locks[0];
        auto* h = row();
        auto* dir = new QComboBox;
        dir->addItem(tr("↑ up"), QStringLiteral("up"));
        dir->addItem(tr("↓ down"), QStringLiteral("down"));
        dir->setCurrentIndex(st.value("dir", std::string("up")) == "down" ? 1 : 0);
        connect(dir, &QComboBox::activated, this, [=](int) {
            const std::string d = dir->currentData().toString().toStdString();
            EF(tr("Stair %1 of %2: direction").arg(i + 1).arg(q(fid)), [i, d](Json& x) { x["stairs"][i]["dir"] = d; });
        });
        auto* toC = new QComboBox;
        toC->setEditable(true);
        for (const Json& x : m_p.S["floors"]) toC->addItem(q(x["id"].get<std::string>()));
        toC->setEditText(q(to));
        toC->lineEdit()->setPlaceholderText(tr("floor id"));
        connect(toC->lineEdit(), &QLineEdit::editingFinished, this, [=] {
            const std::string nv = toC->currentText().trimmed().toStdString();
            if (nv == to) return;
            if (!nv.empty() && !stage::nameOk(nv, true)) { toast(tr("A floor name is also its file name: any name works, Chinese too, except \" ' < > & | \\ / : * ?"), true); return; }
            EF(tr("Stair of %1 → %2").arg(q(fid), q(nv)), [i, nv](Json& x) { x["stairs"][i]["to"] = nv; });
        });
        connect(toC, &QComboBox::activated, this, [=](int) { toC->lineEdit()->editingFinished(); });
        // the event list of a combo: this floor's events first, then the others ("\x01": a heading, not choosable)
        auto fillEvents = [&](QComboBox* c, const QString& none, const std::string& current) {
            c->addItem(none, QString());
            if (!current.empty() && !m_p.ev(current)) c->addItem(tr("⛔ %1 (not defined)").arg(q(current)), q(current));
            auto hdr = [c](const QString& t) {
                c->addItem(t, QStringLiteral("\x01"));
                if (auto* m = qobject_cast<QStandardItemModel*>(c->model())) m->item(c->count() - 1)->setEnabled(false);
            };
            hdr(tr("events this floor can roll"));
            for (const std::string& x : here) c->addItem(q(x), q(x));
            hdr(tr("other events"));
            for (const std::string& x : defined) if (std::find(here.begin(), here.end(), x) == here.end()) c->addItem(q(x), q(x));
            c->setCurrentIndex(std::max(0, c->findData(q(current))));
        };
        auto* un = new QComboBox;
        fillEvents(un, tr("(always open)"), lock);
        connect(un, &QComboBox::activated, this, [=](int) {
            const std::string ev2 = un->currentData().toString().toStdString();
            if (ev2 == "\x01") return;
            std::vector<std::string> nl = locks;   // the first event changes; the others stay (none: the stair opens)
            if (ev2.empty()) nl.clear(); else if (nl.empty()) nl.push_back(ev2); else nl[0] = ev2;
            EF(ev2.empty() ? tr("Unlock %1 → %2").arg(q(fid), q(to)) : tr("Lock %1 → %2 until %3").arg(q(fid), q(to), q(ev2)),
               [i, nl](Json& x) { stage::setStairUnlockEvents(x["stairs"][i], nl); });
        });
        auto* del = new QToolButton;
        del->setText(QStringLiteral("✕"));
        connect(del, &QToolButton::clicked, this, [=] { EF(tr("Remove stair %1 → %2").arg(q(fid), q(to)), [i](Json& x) { x["stairs"].erase(i); }); });
        h->addWidget(dir);
        h->addWidget(toC, 1);
        h->addWidget(muted(tr("unlock when done:")));
        h->addWidget(un, 2);
        h->addWidget(del);
        v->addLayout(h);
        if (!locks.empty()) {   // W38: more events the stair waits for (all must be done), each with ✕, and "+ another"
            auto* eh = row();
            eh->addWidget(muted(tr("and when done:")));
            for (size_t k = 1; k < locks.size(); k++) {
                auto* c = chip(QStringLiteral("🔒 ") + q(locks[k]) + (m_p.ev(locks[k]) ? QString() : QStringLiteral(" ⛔")) + QStringLiteral("  ✕"));
                c->setToolTip(tr("Remove this event from the lock"));
                connect(c, &QToolButton::clicked, this, [=] {
                    std::vector<std::string> nl = locks;
                    nl.erase(nl.begin() + (long)k);
                    EF(tr("Lock %1 → %2: without %3").arg(q(fid), q(to), q(locks[k])), [i, nl](Json& x) { stage::setStairUnlockEvents(x["stairs"][i], nl); });
                });
                eh->addWidget(c);
            }
            auto* more = new QComboBox;
            fillEvents(more, tr("+ another event"), std::string());
            connect(more, &QComboBox::activated, this, [=](int) {
                const std::string ev2 = more->currentData().toString().toStdString();
                if (ev2.empty() || ev2 == "\x01") return;
                std::vector<std::string> nl = locks;
                nl.push_back(ev2);
                EF(tr("Lock %1 → %2 also until %3").arg(q(fid), q(to), q(ev2)), [i, nl](Json& x) { stage::setStairUnlockEvents(x["stairs"][i], nl); });
            });
            eh->addWidget(more, 1);
            eh->addStretch(1);
            v->addLayout(eh);
        }
        if (!to.empty() && !m_p.flo(to)) {
            auto* eh = row();
            eh->addWidget(rich(QStringLiteral("<span style='color:#ff5d5d'>%1</span>").arg(tr("⛔ %1 does not exist yet.").arg(esc(q(to))))));
            auto* mk = new QPushButton(tr("Create floor %1").arg(q(to)));
            connect(mk, &QPushButton::clicked, this, [this, to, fid] { createFloor(to, fid); });
            eh->addWidget(mk);
            eh->addStretch(1);
            v->addLayout(eh);
        }
    }
    {
        auto* add = new QPushButton(tr("+ Add stair"));
        connect(add, &QPushButton::clicked, this, [=] { EF(tr("Add stair to %1").arg(q(fid)), [](Json& x) { x["stairs"].push_back(Json{{"dir", "up"}, {"to", ""}}); }); });
        auto* h = row();
        h->addWidget(add);
        h->addStretch(1);
        v->addLayout(h);
    }
    // reached from, events
    v->addWidget(heading(tr("Reached from")));
    {
        auto* chips = new QWidget;
        auto* flow = new FlowLayout(chips, 4);
        int n = 0;
        for (const Json& o : m_p.S["floors"])
            for (const Json& st : o["stairs"])
                if (st.value("to", std::string()) == fid) {
                    const std::string oid = o["id"].get<std::string>();
                    QString lock;
                    for (const std::string& e : stage::stairUnlockEvents(st)) lock += (lock.isEmpty() ? QStringLiteral(" 🔒 ") : QStringLiteral(" + ")) + q(e);
                    auto* c = chip((st.value("dir", std::string("up")) == "down" ? QStringLiteral("↓ ") : QStringLiteral("↑ ")) + q(oid) + lock);
                    connect(c, &QToolButton::clicked, this, [this, oid] { select(oid); });
                    flow->addWidget(c);
                    n++;
                    break;
                }
        if (!n) flow->addWidget(muted(QStringLiteral("—")));
        v->addWidget(chips);
    }
    v->addWidget(heading(tr("Events it can roll (%1)").arg(f["events"].size())));
    {
        auto* chips = new QWidget;
        auto* flow = new FlowLayout(chips, 4);
        for (const Json& x : f["events"]) {
            const std::string id = x.get<std::string>();
            auto* c = chip(q(id), m_p.ev(id) ? QColor() : QColor("#ff5d5d"));
            connect(c, &QToolButton::clicked, this, [this, id] { select(id); });
            flow->addWidget(c);
        }
        if (f["events"].empty()) flow->addWidget(muted(QStringLiteral("—")));
        v->addWidget(chips);
    }
    v->addStretch(1);
    return w;
}

// ---- a missing id --------------------------------------------------------------------------------------------------

QWidget* EventEditor::missingForm(const std::string& id) {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(10, 8, 10, 10);
    const auto fl = m_p.floorsOf(id);
    const std::string act = (*fl[0])["act"].get<std::string>();
    const std::string pool = startsWith(id, "ev_common_") ? "pool_common" : "pool_act" + (act.size() > 3 ? act.substr(3) : act);
    v->addWidget(banner(QStringLiteral("<b style='color:#ff5d5d'>%1</b><br>%2").arg(tr("⛔ Missing event id: %1").arg(esc(q(id))),
                        tr("%1 floor(s) list this id, but no pool defines it. When one of these floors rolls it, there is no event to show.").arg(fl.size())), true));
    v->addWidget(heading(tr("Listed by")));
    auto* chips = new QWidget;
    auto* flow = new FlowLayout(chips, 4);
    for (const Json* f : fl) {
        const std::string fid = (*f)["id"].get<std::string>();
        auto* c = chip(q(fid) + QStringLiteral(" ") + q((*f)["act"].get<std::string>()));
        connect(c, &QToolButton::clicked, this, [this, fid] { select(fid); });
        flow->addWidget(c);
    }
    v->addWidget(chips);
    v->addWidget(heading(tr("Text")));
    const Json tx = m_p.S["texts"].contains(id) ? m_p.S["texts"][id] : Json();
    if (tx.is_object())
        v->addWidget(rich(tr("A placeholder exists in text.json under <code>%1</code>%2: “%3”").arg(esc(q(id)),
                          tx.contains("_todo") ? tr(" (marked _todo)") : QString(), esc(q(tx.value("zh_TW", std::string()))))));
    else v->addWidget(muted(tr("No string in text.json yet.")));
    v->addWidget(heading(tr("Fix it")));
    {
        auto* h = row();
        auto* mk = new QPushButton(tr("Create in %1").arg(q(pool)));
        mk->setDefault(true);
        connect(mk, &QPushButton::clicked, this, [this, id, act, pool] {
            edit(tr("Create %1 in %2").arg(q(id), q(pool)), [this, id, act] { m_p.makeRecord(id, act); m_p.selected = id; });
        });
        h->addWidget(mk);
        const std::string guess = m_p.guessKind(id);
        h->addWidget(muted((std::find(m_p.S["pools"].begin(), m_p.S["pools"].end(), Json(pool)) == m_p.S["pools"].end()
                                ? tr("%1.json does not exist yet; it will be created. ").arg(q(pool)) : QString())
                           + tr("Kind guess:") + QStringLiteral(" ") + (guess.empty() ? tr("none, choose one") : q(guess))), 1);
        v->addLayout(h);
    }
    {
        int n = 0;
        for (const auto& p : m_p.problems()) if (p.rule == "missing_event" && p.act == act) n++;
        auto* ch = new QPushButton(tr("Create all %1 missing ids of %2").arg(n).arg(q(act)));
        connect(ch, &QPushButton::clicked, this, [this, act] { createChapter(act); });
        auto* h = row();
        h->addWidget(ch);
        h->addStretch(1);
        v->addLayout(h);
    }
    {
        auto* rm = new QPushButton(tr("Remove from floors"));
        connect(rm, &QPushButton::clicked, this, [this, id] {
            edit(tr("Remove %1 from floors").arg(q(id)), [this, id] {
                for (Json& f : m_p.S["floors"]) { Json& l = f["events"]; l.erase(std::remove(l.begin(), l.end(), Json(id)), l.end()); }
                m_p.selected.clear();
            });
        });
        auto* h = row();
        h->addWidget(rm);
        h->addWidget(muted(tr("if the id is a typo or the event was dropped")), 1);
        v->addLayout(h);
    }
    v->addStretch(1);
    return w;
}
