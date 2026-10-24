// EventNpcs.cpp -- the event editor's NPCs tab (an NPC file, its chat lines and topics, the talk preview) and Dialogues
// tab (a dialogue file's nodes as cards). docs/gameplay/event_editor/08_NPC_DIALOGUE.md; event_editor.html's renderNpcs,
// previewHtml and renderDialogues.
#include "EventEditor.h"

#include "EventWidgets.h"
#include "FlowLayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

using stage::Json;
using namespace EvUi;

namespace {
QHBoxLayout* lrow(const QString& label) {
    auto* h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    auto* l = new QLabel(label);
    l->setMinimumWidth(130);
    l->setStyleSheet(QStringLiteral("color:#8b929c"));
    h->addWidget(l);
    return h;
}
Json field(const Json& v) { return v.is_object() ? v : Json{{"en", v.is_string() ? v.get<std::string>() : std::string()}}; }
std::string lineDialogue(const Json& l) { return l.is_string() ? l.get<std::string>() : l.value("dialogue", std::string()); }
}  // namespace

// ---- NPCs ----------------------------------------------------------------------------------------------------------

QWidget* EventEditor::npcsPane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    const std::vector<std::string> ids = m_p.npcIds();
    if (m_npcSel.empty() || std::find(ids.begin(), ids.end(), m_npcSel) == ids.end()) m_npcSel = ids.empty() ? std::string() : ids[0];
    // the NPCs, and a new one
    {
        auto* chips = new QWidget;
        auto* flow = new FlowLayout(chips, 4);
        for (const std::string& n : ids) {
            auto* c = chip(q(n) + (m_p.S["npcs"].contains(n) ? QString() : QStringLiteral(" ⚠")), n == m_npcSel ? QColor("#4c9eff") : QColor());
            connect(c, &QToolButton::clicked, this, [this, n] { m_npcSel = n; scheduleRender(); });
            flow->addWidget(c);
        }
        auto* nw = new QLineEdit;
        nw->setPlaceholderText(tr("new NPC id"));
        nw->setMaximumWidth(150);
        auto* nb = new QPushButton(tr("+ NPC"));
        auto create = [this](const std::string& id) {
            edit(tr("New NPC %1").arg(q(id)), [this, id] {
                const std::string d = m_p.S["dialogues"].contains(id) ? id : m_p.dialogueList().empty() ? id : m_p.dialogueList()[0];
                m_p.S["npcs"][id] = Json{{"npcId", id}, {"name", "npc." + id + ".name"}, {"chat", Json{{"lines", Json::array({Json{{"dialogue", d}}})}, {"order", "loop"}}},
                                         {"topics", Json{{"menu", "auto"}}}};
                if (!m_p.S["texts"].contains("npc." + id + ".name")) m_p.S["texts"]["npc." + id + ".name"] = Json{{"zh_TW", id}, {"en", id}};
            });
            m_npcSel = id;
        };
        connect(nb, &QPushButton::clicked, this, [=] {
            const std::string n = nw->text().trimmed().toStdString();
            if (!stage::idOk(n)) { toast(tr("Use letters, digits, _ and - only."), true); return; }
            if (m_p.S["npcs"].contains(n)) { toast(tr("%1 already exists.").arg(q(n)), true); return; }
            create(n);
        });
        flow->addWidget(nw);
        flow->addWidget(nb);
        v->addWidget(chips);
        if (m_npcSel.empty()) { v->addWidget(muted(tr("No NPCs yet."))); v->addStretch(1); return w; }
        if (!m_p.S["npcs"].contains(m_npcSel)) {
            v->addWidget(rich(tr("%1 has no NPC file: the game plays dialogue/%1.json as its only chat line.").arg(esc(q(m_npcSel)))));
            auto* mk = new QPushButton(tr("Create its NPC file"));
            const std::string id = m_npcSel;
            connect(mk, &QPushButton::clicked, this, [=] { create(id); });
            v->addWidget(mk, 0, Qt::AlignLeft);
            v->addStretch(1);
            return w;
        }
    }
    const std::string id = m_npcSel;
    const Json f = m_p.S["npcs"][id];
    const QString hNpc = tr("NPC %1").arg(q(id));
    auto F = [this, id, hNpc](const std::function<void(Json&)>& fn) { edit(hNpc, [this, id, fn] { fn(m_p.S["npcs"][id]); }); };
    // lines as objects (a plain string line is a dialogue id)
    auto linesOf = [](Json& npc) -> Json& {
        if (!npc.contains("chat") || !npc["chat"].is_object()) npc["chat"] = Json{{"lines", Json::array()}};
        Json& l = npc["chat"]["lines"];
        if (!l.is_array()) l = Json::array();
        for (Json& x : l) if (x.is_string()) x = Json{{"dialogue", x}};
        return l;
    };
    // the file: name, greeting, sprite, where, menu rule
    {
        auto* g = new QGroupBox(tr("NPC file · npcs/%1.json").arg(q(id)));
        auto* gv = new QVBoxLayout(g);
        for (const char* key : {"name", "greeting"}) {
            auto* h = lrow(std::string(key) == "name" ? tr("Name") : tr("Greeting (menu)"));
            const Json t = f.contains(key) ? m_p.text(f[key].get<std::string>()) : Json::object();
            for (const char* l : {"zh_TW", "en"}) {
                auto* e = new CommitLine(t.contains(l) && t[l].is_string() ? q(t[l].get<std::string>()) : QString());
                e->setPlaceholderText(QString::fromLatin1(l));
                const std::string k = key, lang = l;
                e->onCommit = [=](const QString& s) {
                    edit(hNpc, [this, id, k, lang, s] {
                        Json& n = m_p.S["npcs"][id];
                        if (!n.contains(k) || !n[k].is_string() || n[k].get<std::string>().empty()) n[k] = "npc." + id + "." + k;
                        Json& tt = m_p.S["texts"][n[k].get<std::string>()];
                        if (!tt.is_object()) tt = Json::object();
                        tt[lang] = s.toStdString();
                    });
                };
                h->addWidget(e, 1);
            }
            gv->addLayout(h);
        }
        {
            auto* h = lrow(tr("Map sprite"));
            auto* sp = new QComboBox;
            sp->setEditable(true);
            for (const std::string& s : m_p.sprites) sp->addItem(q(s));
            sp->setEditText(q(f.value("sprite", std::string())));
            sp->lineEdit()->setPlaceholderText(QStringLiteral("npc_") + q(id));
            connect(sp->lineEdit(), &QLineEdit::editingFinished, this, [=] {
                const std::string s = sp->currentText().trimmed().toStdString();
                if (s == f.value("sprite", std::string())) return;
                F([s](Json& n) { if (s.empty()) n.erase("sprite"); else n["sprite"] = s; });
            });
            h->addWidget(sp, 1);
            gv->addLayout(h);
        }
        {
            auto* h = lrow(tr("Placed on"));
            auto it = m_p.npcPlaces.find(id);
            QStringList fl;
            if (it != m_p.npcPlaces.end()) for (const std::string& x : it->second) fl << q(x);
            h->addWidget(fl.isEmpty() ? rich(QStringLiteral("<span style='color:#ff9f43'>⚠ %1</span>").arg(tr("not placed on any floor (place it in the stage editor)"))) : rich(fl.join(QStringLiteral(", "))), 1);
            gv->addLayout(h);
        }
        {
            auto* h = lrow(tr("Topic menu"));
            auto* m = new QComboBox;
            m->addItem(tr("auto: only when more than one topic"), QStringLiteral("auto"));
            m->addItem(tr("always"), QStringLiteral("always"));
            m->setCurrentIndex(f.contains("topics") && f["topics"].value("menu", std::string()) == "always" ? 1 : 0);
            connect(m, &QComboBox::activated, this, [=](int) {
                const std::string s = m->currentData().toString().toStdString();
                F([s](Json& n) { if (!n.contains("topics") || !n["topics"].is_object()) n["topics"] = Json::object(); n["topics"]["menu"] = s; });
            });
            h->addWidget(m);
            h->addStretch(1);
            gv->addLayout(h);
        }
        v->addWidget(g);
    }
    // chat lines
    {
        auto* g = new QGroupBox(tr("Chat lines (when no topic is offered)"));
        auto* gv = new QVBoxLayout(g);
        auto* h = lrow(tr("Order"));
        auto* order = new QComboBox;
        order->addItem(tr("loop: in order, then back to the first"), QStringLiteral("loop"));
        order->addItem(tr("stay: in order, then repeat the last"), QStringLiteral("stay"));
        order->addItem(tr("random (never twice in a row)"), QStringLiteral("random"));
        order->setCurrentIndex(std::max(0, order->findData(q(f.contains("chat") ? f["chat"].value("order", std::string("loop")) : std::string("loop")))));
        connect(order, &QComboBox::activated, this, [=](int) {
            const std::string o = order->currentData().toString().toStdString();
            F([o, linesOf](Json& n) { linesOf(n); n["chat"]["order"] = o; });
        });
        h->addWidget(order);
        h->addStretch(1);
        gv->addLayout(h);
        const Json lines = f.contains("chat") && f["chat"].contains("lines") && f["chat"]["lines"].is_array() ? f["chat"]["lines"] : Json::array();
        for (size_t i = 0; i < lines.size(); i++) {
            const Json line = lines[i].is_string() ? Json{{"dialogue", lines[i]}} : lines[i];
            auto* card = new QWidget;
            card->setStyleSheet(QStringLiteral("QWidget#card { border-top:1px solid #3a3f47; }"));
            card->setObjectName(QStringLiteral("card"));
            auto* cv = new QVBoxLayout(card);
            auto* r = lrow(QString::number(i + 1));
            auto* d = new QComboBox;
            std::vector<std::string> ds = m_p.dialogueList();
            const std::string cur = lineDialogue(line);
            if (!cur.empty() && std::find(ds.begin(), ds.end(), cur) == ds.end()) ds.push_back(cur);
            for (const std::string& x : ds) d->addItem(q(x));
            d->setCurrentText(q(cur));
            connect(d, &QComboBox::activated, this, [=](int) { const std::string s = d->currentText().toStdString(); F([i, s, linesOf](Json& n) { linesOf(n)[i]["dialogue"] = s; }); });
            auto* open = new QToolButton;
            open->setText(tr("open ↗"));
            connect(open, &QToolButton::clicked, this, [this, cur] { m_dlgSel = cur; setTab(QStringLiteral("dlg")); });
            auto mk = [&](const QString& t, int dir) {
                auto* b = new QToolButton;
                b->setText(t);
                connect(b, &QToolButton::clicked, this, [=] {
                    F([i, dir, linesOf](Json& n) {
                        Json& l = linesOf(n);
                        if (dir == 0) { l.erase(i); return; }
                        const long j = (long)i + dir;
                        if (j < 0 || j >= (long)l.size()) return;
                        std::swap(l[i], l[(size_t)j]);
                    });
                });
                return b;
            };
            r->addWidget(d, 1);
            r->addWidget(open);
            r->addWidget(mk(QStringLiteral("▲"), -1));
            r->addWidget(mk(QStringLiteral("▼"), 1));
            r->addWidget(mk(QStringLiteral("✕"), 0));
            cv->addLayout(r);
            auto* wr = lrow(tr("Only when"));
            wr->addWidget(condBox(QStringLiteral("line%1").arg(i), [this, id, i] {
                const Json& cs = m_p.S;
                const Json& n = cs["npcs"][id];
                const Json l = n.contains("chat") && n["chat"].contains("lines") ? n["chat"]["lines"] : Json::array();
                return i < l.size() && l[i].is_object() ? l[i].value("requires", Json()) : Json();
            }, [this, id, i, linesOf](const Json& c) {
                Json& l = linesOf(m_p.S["npcs"][id]);
                if (i >= l.size()) return;
                if (c.is_null()) l[i].erase("requires"); else l[i]["requires"] = c;
            }, hNpc), 1);
            cv->addLayout(wr);
            cv->addWidget(muted(tr("When it plays (repeats every time: no rewards)")));
            cv->addWidget(actsBox(QStringLiteral("line%1").arg(i), [line] { return line.value("actions", Json::array()); }, [this, id, i, linesOf]() -> Json* {
                Json& l = linesOf(m_p.S["npcs"][id]);
                if (i >= l.size()) return nullptr;
                if (!l[i].contains("actions") || !l[i]["actions"].is_array()) l[i]["actions"] = Json::array();
                return &l[i]["actions"];
            }, hNpc));
            bool miss = false;
            const std::string first = m_pv.firstText(m_p, cur, lang().toStdString(), &miss);
            cv->addWidget(muted(QStringLiteral("“%1”").arg(miss ? tr("(dialogue/%1.json is missing)").arg(q(cur)) : q(first).left(80))));
            gv->addWidget(card);
        }
        auto* add = new QPushButton(tr("+ Chat line"));
        connect(add, &QPushButton::clicked, this, [=] {
            const std::string d = m_p.dialogueList().empty() ? id : m_p.dialogueList()[0];
            F([d, linesOf](Json& n) { linesOf(n).push_back(Json{{"dialogue", d}}); });
        });
        gv->addWidget(add, 0, Qt::AlignLeft);
        v->addWidget(g);
    }
    // topics in menu order
    {
        std::vector<Json*> topics = m_p.topicsOf(id);
        auto* g = new QGroupBox(tr("Topics (%1) in menu order").arg(topics.size()));
        auto* gv = new QVBoxLayout(g);
        if (topics.empty()) gv->addWidget(muted(tr("No talk events yet: set an event's \"Fires when\" to \"the player talks to\" this NPC.")));
        else {
            auto* t = new QTableWidget((int)topics.size(), 7);
            t->setHorizontalHeaderLabels({QStringLiteral("#"), tr("Event"), tr("Talked to on"), tr("Active only when"), tr("After finishing"), tr("Repeat"), QString()});
            t->verticalHeader()->hide();
            t->setEditTriggers(QAbstractItemView::NoEditTriggers);
            std::vector<std::string> tids;
            for (Json* e : topics) tids.push_back((*e)["id"].get<std::string>());
            for (size_t i = 0; i < topics.size(); i++) {
                const Json& e = *topics[i];
                const std::string eid = e["id"].get<std::string>();
                t->setItem((int)i, 0, new QTableWidgetItem(QString::number(i + 1)));
                auto* c = chip(q(eid));
                c->setToolTip(L(m_p.text(e["title"].get<std::string>())));
                connect(c, &QToolButton::clicked, this, [this, eid] { select(eid); });
                t->setCellWidget((int)i, 1, c);
                bool cond = false;
                const std::string tf = m_p.talkFloorOf(e, &cond);
                t->setItem((int)i, 2, new QTableWidgetItem(cond ? QStringLiteral("?") : tf.empty() ? tr("every floor") : q(tf)));
                QStringList req;
                if (!e["reqRaw"].is_null()) req << q(e["reqRaw"].dump());
                for (const Json& l : e["req"]) req << q(l.dump());
                t->setItem((int)i, 3, new QTableWidgetItem((req.isEmpty() ? QStringLiteral("—") : req.join(QStringLiteral(" & "))) + (m_p.woken(eid) ? QStringLiteral(" · ") + tr("waits to be woken") : QString())));
                t->setItem((int)i, 4, new QTableWidgetItem(e["afterDialogue"].get<std::string>().empty() ? tr("(hide the topic)") : q(e["afterDialogue"].get<std::string>())));
                t->setItem((int)i, 5, new QTableWidgetItem(e["once"] == false ? (e["maxTimes"].get<int>() ? QStringLiteral("× %1").arg(e["maxTimes"].get<int>()) : QStringLiteral("∞")) : QStringLiteral("—")));
                auto* mv = new QWidget;
                auto* mh = new QHBoxLayout(mv);
                mh->setContentsMargins(0, 0, 0, 0);
                for (int d : {-1, 1}) {   // ▲ / ▼ renumber every topic of this NPC 1..n (topicOrder)
                    auto* b = new QToolButton;
                    b->setText(d < 0 ? QStringLiteral("▲") : QStringLiteral("▼"));
                    b->setEnabled(d < 0 ? i > 0 : i + 1 < topics.size());
                    connect(b, &QToolButton::clicked, this, [this, id, i, d] {
                        edit(tr("Topic order of %1").arg(q(id)), [this, id, i, d] {
                            std::vector<Json*> tp = m_p.topicsOf(id);
                            std::swap(tp[i], tp[(size_t)((long)i + d)]);
                            for (size_t k = 0; k < tp.size(); k++) (*tp[k])["topicOrder"] = (int)k + 1;
                        });
                    });
                    mh->addWidget(b);
                }
                t->setCellWidget((int)i, 6, mv);
            }
            t->resizeColumnsToContents();
            t->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
            t->setMinimumHeight(std::min(320, 34 + 30 * (int)topics.size()));
            gv->addWidget(t);
        }
        v->addWidget(g);
    }
    v->addWidget(previewBox(id));
    v->addStretch(1);
    return w;
}

// ---- the talk preview ----------------------------------------------------------------------------------------------

QWidget* EventEditor::previewBox(const std::string& npc) {
    if (m_pv.npc != npc) m_pv.reset(m_p, npc);
    auto* g = new QGroupBox(tr("Talk preview"));
    auto* v = new QVBoxLayout(g);
    v->addWidget(muted(tr("A pretend run with the game's rules: tick finished events, set values, press Talk.")));
    std::vector<Json*> topics = m_p.topicsOf(npc);
    auto name = [this](const std::string& id) { const Json* e = m_p.ev(id); const QString t = e ? L(m_p.text((*e)["title"].get<std::string>())) : QString(); return t.isEmpty() ? q(id) : t; };
    // events the topics wait for, and the topics themselves
    std::vector<std::string> refs;
    for (Json* e : topics) {
        for (const Json& l : (*e)["req"]) {
            const std::string f = l.value("flag", std::string());
            if (l.value("type", std::string()) == "runFlagSet" && f.rfind("event_", 0) == 0 && std::find(refs.begin(), refs.end(), f.substr(6)) == refs.end()) refs.push_back(f.substr(6));
        }
        const std::string id = (*e)["id"].get<std::string>();
        if (std::find(refs.begin(), refs.end(), id) == refs.end()) refs.push_back(id);
    }
    {
        auto* h = lrow(tr("Finished events"));
        auto* chips = new QWidget;
        auto* flow = new FlowLayout(chips, 4);
        for (const std::string& id : refs) {
            auto* c = new QCheckBox(q(id));
            c->setChecked(m_pv.finished.count(id) > 0);
            connect(c, &QCheckBox::toggled, this, [this, id](bool on) { m_pv.tick(m_p, id, on); scheduleRender(); });
            flow->addWidget(c);
        }
        h->addWidget(chips, 1);
        v->addLayout(h);
    }
    // the values its topics and chat lines read
    {
        const std::string npcJson = m_p.S["npcs"].contains(npc) ? m_p.S["npcs"][npc].dump() : std::string();
        auto used = [&](const std::string& name2) {
            const std::string needle = "\"" + name2 + "\"";
            if (npcJson.find(needle) != std::string::npos) return true;
            for (Json* e : topics) if (m_p.reqJson(*e).dump().find(needle) != std::string::npos) return true;
            return false;
        };
        auto* h = lrow(tr("Values"));
        int n = 0;
        for (auto& [vname, r] : m_p.S["vars"].items()) {
            (void)r;
            if (!used(vname)) continue;
            h->addWidget(muted(q(vname)));
            auto* s = new QSpinBox;
            s->setRange(-99999, 99999);
            s->setValue((int)m_pv.vars[vname]);
            const std::string vn = vname;
            connect(s, &QSpinBox::valueChanged, this, [this, vn](int x) { m_pv.vars[vn] = x; });
            h->addWidget(s);
            n++;
        }
        for (auto& [cname, r] : m_p.S["counters"].items()) {
            (void)r;
            bool hit = false;
            for (Json* e : topics) if (m_p.reqJson(*e).dump().find("\"" + cname + "\"") != std::string::npos) hit = true;
            if (!hit) continue;
            h->addWidget(muted(q(cname)));
            auto* s = new QSpinBox;
            s->setRange(-99999, 99999);
            s->setValue((int)m_pv.counters[cname]);
            const std::string cn = cname;
            connect(s, &QSpinBox::valueChanged, this, [this, cn](int x) { m_pv.counters[cn] = x; });
            h->addWidget(s);
            n++;
        }
        h->addStretch(1);
        if (n) v->addLayout(h);
        else delete h;
    }
    {
        auto* h = lrow(tr("Talk on floor"));
        auto* fc = new QComboBox;
        auto it = m_p.npcPlaces.find(npc);
        std::vector<std::string> fl = it != m_p.npcPlaces.end() ? it->second : std::vector<std::string>();
        if (!m_pv.stage.empty() && std::find(fl.begin(), fl.end(), m_pv.stage) == fl.end()) fl.push_back(m_pv.stage);
        for (const std::string& f : fl) fc->addItem(q(f));
        fc->setCurrentText(q(m_pv.stage));
        connect(fc, &QComboBox::activated, this, [this, fc](int) { m_pv.stage = fc->currentText().toStdString(); scheduleRender(); });
        h->addWidget(fc);
        h->addStretch(1);
        v->addLayout(h);
    }
    {
        auto* h = lrow(tr("Floors cleared"));
        QStringList cl;
        for (const std::string& c : m_pv.cleared) cl << q(c);
        auto* e = new CommitLine(cl.join(QStringLiteral(", ")));
        e->setPlaceholderText(QStringLiteral("F05, F10"));
        e->onCommit = [this](const QString& s) {
            m_pv.cleared.clear();
            for (const QString& x : s.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) m_pv.cleared.insert(x.toStdString());
        };
        h->addWidget(e, 1);
        v->addLayout(h);
    }
    {
        auto* h = new QHBoxLayout;
        auto* talk = new QPushButton(tr("💬 Talk"));
        talk->setDefault(true);
        connect(talk, &QPushButton::clicked, this, [this, npc] { m_pv.talk(m_p, npc, lang().toStdString()); scheduleRender(); });
        auto* reset = new QPushButton(tr("Reset"));
        connect(reset, &QPushButton::clicked, this, [this, npc] { m_pv.reset(m_p, npc); scheduleRender(); });
        h->addWidget(talk);
        h->addWidget(reset);
        const auto st = m_pv.talks.count(npc) ? m_pv.talks[npc] : stage::TalkPreview::TalkState();
        h->addWidget(muted(tr("talks so far: %1 · chat index: %2").arg(st.count).arg(st.index)), 1);
        v->addLayout(h);
    }
    for (size_t i = 0; i < m_pv.log.size(); i++) {
        const auto& x = m_pv.log[i];
        auto* rowW = new QWidget;
        if (i) { auto* eff = new QGraphicsOpacityEffect(rowW); eff->setOpacity(0.55); rowW->setGraphicsEffect(eff); }
        auto* h = new QHBoxLayout(rowW);
        h->setContentsMargins(0, 0, 0, 0);
        auto* l = new QLabel(tr("Talk %1").arg(x.n));
        l->setMinimumWidth(130);
        l->setStyleSheet(QStringLiteral("color:#8b929c"));
        h->addWidget(l);
        auto item = [&](const stage::TalkPreview::MenuItem& m) {
            auto* box = new QWidget;
            auto* bh = new QHBoxLayout(box);
            bh->setContentsMargins(0, 0, 0, 0);
            bh->setSpacing(2);
            bh->addWidget(chip((m.finished ? QStringLiteral("✓ ") : QString()) + name(m.id)));
            if (!m.finished && i == 0) {
                auto* fin = new QToolButton;
                fin->setText(tr("finish"));
                fin->setToolTip(tr("Pretend the player finished it (its next events wake, its variables change)"));
                const std::string mid = m.id;
                connect(fin, &QToolButton::clicked, this, [this, mid] { m_pv.finish(m_p, mid); scheduleRender(); });
                bh->addWidget(fin);
            }
            return box;
        };
        if (x.kind == "chat") {
            QString t;
            if (x.dialogue.empty()) t = tr("(nothing to say: no chat line is available)");
            else if (x.text == "\x01missing") t = QStringLiteral("💬 %1: ").arg(q(x.dialogue)) + tr("(dialogue/%1.json is missing)").arg(q(x.dialogue));
            else t = QStringLiteral("💬 %1: %2").arg(q(x.dialogue), q(x.text));
            h->addWidget(rich(esc(t)), 1);
        } else if (x.kind == "topic") {
            h->addWidget(muted(tr("plays at once:")));
            h->addWidget(item(x.menu[0]));
            h->addStretch(1);
        } else {
            h->addWidget(muted(tr("menu:")));
            auto* chips = new QWidget;
            auto* flow = new FlowLayout(chips, 4);
            for (const auto& m : x.menu) flow->addWidget(item(m));
            flow->addWidget(chip(tr("Just chat")));
            flow->addWidget(chip(tr("Leave")));
            h->addWidget(chips, 1);
        }
        v->addWidget(rowW);
    }
    return g;
}

// ---- dialogues -----------------------------------------------------------------------------------------------------

QWidget* EventEditor::dialoguesPane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    const std::vector<std::string> ids = m_p.dialogueList();
    if (m_dlgSel.empty() || !m_p.S["dialogues"].contains(m_dlgSel)) m_dlgSel = ids.empty() ? std::string() : ids[0];
    {
        auto* h = lrow(tr("Dialogue"));
        auto* pick = new QComboBox;
        for (const std::string& i : ids) pick->addItem(q(i));
        pick->setCurrentText(q(m_dlgSel));
        connect(pick, &QComboBox::activated, this, [this, pick](int) { m_dlgSel = pick->currentText().toStdString(); scheduleRender(); });
        auto* nw = new QLineEdit;
        nw->setPlaceholderText(tr("new dialogue id"));
        nw->setMaximumWidth(170);
        auto* nb = new QPushButton(tr("+ Dialogue"));
        connect(nb, &QPushButton::clicked, this, [=] {
            const std::string n = nw->text().trimmed().toStdString();
            if (!stage::idOk(n)) { toast(tr("Use letters, digits, _ and - only."), true); return; }
            if (m_p.S["dialogues"].contains(n)) { toast(tr("%1 already exists.").arg(q(n)), true); return; }
            edit(tr("New dialogue %1").arg(q(n)), [this, n] {
                m_p.S["dialogues"][n] = Json{{"start", "root"}, {"nodes", Json{{"root", Json{{"text", Json{{"zh_TW", ""}, {"en", ""}}},
                                                                                             {"choices", Json::array({Json{{"label", Json{{"zh_TW", "……"}, {"en", "..."}}}, {"next", nullptr}}})}}}}}};
            });
            m_dlgSel = n;
        });
        h->addWidget(pick, 1);
        h->addWidget(nw);
        h->addWidget(nb);
        v->addLayout(h);
    }
    if (m_dlgSel.empty()) { v->addStretch(1); return w; }
    const std::string id = m_dlgSel;
    const Json d = m_p.S["dialogues"][id];
    const Json nodes = d.value("nodes", Json::object());
    std::vector<std::string> nids;
    for (auto& [k, n] : nodes.items()) { (void)n; nids.push_back(k); }
    const bool condStart = d.contains("start") && d["start"].is_array();
    const QString hD = tr("Dialogue %1").arg(q(id));
    auto E = [this, id, hD](const std::function<void(Json&)>& fn) { edit(hD, [this, id, fn] { fn(m_p.S["dialogues"][id]); }); };
    auto nodeCombo = [&](const std::string& cur, bool close) {
        auto* c = new QComboBox;
        if (close) c->addItem(tr("(close the dialogue)"), QString());
        std::vector<std::string> l = nids;
        if (!cur.empty() && std::find(l.begin(), l.end(), cur) == l.end()) l.push_back(cur);
        for (const std::string& n : l) c->addItem(q(n) + (nodes.contains(n) ? QString() : QStringLiteral(" ⛔")), q(n));
        c->setCurrentIndex(std::max(0, c->findData(q(cur))));
        return c;
    };
    // the file: used by, where it starts
    {
        auto* g = new QGroupBox(q(id) + QStringLiteral(".json"));
        auto* gv = new QVBoxLayout(g);
        auto* h = lrow(tr("Used by"));
        auto* chips = new QWidget;
        auto* flow = new FlowLayout(chips, 4);
        int n = 0;
        for (const Json& e : m_p.S["events"]) {
            const std::string eid = e["id"].get<std::string>();
            for (const auto& [key, role] : {std::pair<const char*, QString>{"dialogue", tr("topic")}, {"afterDialogue", tr("after finishing")}})
                if (e[key] == id) {
                    auto* c = chip(q(eid) + QStringLiteral(" · ") + role);
                    connect(c, &QToolButton::clicked, this, [this, eid] { select(eid); });
                    flow->addWidget(c);
                    n++;
                }
        }
        for (auto& [nid, npc] : m_p.S["npcs"].items())
            if (npc.contains("chat") && npc["chat"].contains("lines"))
                for (const Json& l : npc["chat"]["lines"]) if (lineDialogue(l) == id) { flow->addWidget(chip(q(nid) + QStringLiteral(" · ") + tr("chat line"))); n++; break; }
        if (!n) flow->addWidget(muted(tr("not used yet")));
        h->addWidget(chips, 1);
        gv->addLayout(h);
        auto* sh = lrow(tr("Starts at"));
        if (condStart) sh->addWidget(muted(tr("the first row whose condition holds:")));
        else {
            auto* sc = nodeCombo(d.value("start", std::string()), false);
            connect(sc, &QComboBox::activated, this, [=](int) { const std::string s = sc->currentData().toString().toStdString(); E([s](Json& x) { x["start"] = s; }); });
            sh->addWidget(sc);
        }
        auto* cs = new QCheckBox(tr("by condition"));
        cs->setChecked(condStart);
        connect(cs, &QCheckBox::toggled, this, [=](bool on) {
            E([on, nids](Json& x) {
                if (on) x["start"] = Json::array({Json{{"node", x["start"].is_string() ? x["start"].get<std::string>() : nids.empty() ? std::string() : nids[0]}}});
                else x["start"] = x["start"].is_array() && !x["start"].empty() ? x["start"].back().value("node", nids.empty() ? std::string() : nids[0]) : (nids.empty() ? std::string() : nids[0]);
            });
        });
        auto* foc = new QCheckBox(tr("finish the event however it closes"));
        foc->setChecked(d.value("finishOnClose", false));
        connect(foc, &QCheckBox::toggled, this, [=](bool on) { E([on](Json& x) { if (on) x["finishOnClose"] = true; else x.erase("finishOnClose"); }); });
        sh->addWidget(cs);
        sh->addWidget(foc);
        sh->addStretch(1);
        gv->addLayout(sh);
        if (condStart) {
            const Json starts = d["start"];
            for (size_t i = 0; i < starts.size(); i++) {
                auto* r = lrow(QString::number(i + 1));
                auto* nc = nodeCombo(starts[i].value("node", std::string()), false);
                connect(nc, &QComboBox::activated, this, [=](int) { const std::string s = nc->currentData().toString().toStdString(); E([i, s](Json& x) { x["start"][i]["node"] = s; }); });
                auto* del = new QToolButton;
                del->setText(QStringLiteral("✕"));
                connect(del, &QToolButton::clicked, this, [=] { E([i, nids](Json& x) { x["start"].erase(i); if (x["start"].empty()) x["start"] = nids.empty() ? std::string() : nids[0]; }); });
                r->addWidget(nc);
                r->addWidget(condBox(QStringLiteral("st%1").arg(i), [this, id, i] {
                    const Json& cs = m_p.S;
                    const Json& st = cs["dialogues"][id]["start"];
                    return st.is_array() && i < st.size() ? st[i].value("requires", Json()) : Json();
                }, [this, id, i](const Json& c) {
                    Json& st = m_p.S["dialogues"][id]["start"];
                    if (!st.is_array() || i >= st.size()) return;
                    if (c.is_null()) st[i].erase("requires"); else st[i]["requires"] = c;
                }, hD, tr("Otherwise (no condition).")), 1);
                r->addWidget(del);
                gv->addLayout(r);
            }
            auto* add = new QPushButton(tr("+ start row"));
            connect(add, &QPushButton::clicked, this, [=] { E([nids](Json& x) { x["start"].push_back(Json{{"node", nids.empty() ? std::string() : nids[0]}}); }); });
            gv->addWidget(add, 0, Qt::AlignLeft);
        }
        v->addWidget(g);
    }
    // the nodes as cards
    for (const std::string& nid : nids) {
        const Json n = nodes[nid];
        auto* g = new QGroupBox;
        auto* gv = new QVBoxLayout(g);
        auto* head = new QHBoxLayout;
        head->addWidget(rich(QStringLiteral("<b>%1</b>").arg(esc(q(nid)))));
        auto* ren = new CommitLine(q(nid));
        ren->setMaximumWidth(140);
        ren->setToolTip(tr("Rename the node"));
        ren->onCommit = [=](const QString& s) {
            const std::string nu = s.trimmed().toStdString();
            if (nu == nid) return;
            if (!stage::idOk(nu)) { toast(tr("Use letters, digits, _ and - only."), true); return; }
            if (nodes.contains(nu)) { toast(tr("%1 already exists.").arg(q(nu)), true); return; }
            E([nid, nu](Json& x) {
                Json nn = Json::object();
                for (auto& [k, vv] : x["nodes"].items()) nn[k == nid ? nu : k] = vv;
                x["nodes"] = nn;
                for (auto& [k, vv] : x["nodes"].items()) { (void)k; if (vv.contains("choices")) for (Json& c : vv["choices"]) if (c.value("next", Json()) == nid) c["next"] = nu; }
                if (x["start"] == nid) x["start"] = nu;
                if (x["start"].is_array()) for (Json& st : x["start"]) if (st.value("node", std::string()) == nid) st["node"] = nu;
            });
        };
        head->addWidget(ren);
        const bool isStart = condStart ? std::any_of(d["start"].begin(), d["start"].end(), [&](const Json& s) { return s.value("node", std::string()) == nid; }) : d.value("start", std::string()) == nid;
        if (isStart) head->addWidget(chip(tr("start")));
        head->addStretch(1);
        auto* del = new QPushButton(tr("Delete node"));
        connect(del, &QPushButton::clicked, this, [=] {
            if (nids.size() < 2) { toast(tr("A dialogue needs one node."), true); return; }
            if (QMessageBox::question(this, tr("Delete node"), tr("Delete node %1?").arg(q(nid))) != QMessageBox::Yes) return;
            E([nid](Json& x) { x["nodes"].erase(nid); });
        });
        head->addWidget(del);
        gv->addLayout(head);
        const Json tx = field(n.value("text", Json()));
        for (const char* l : {"zh_TW", "en"}) {
            auto* r = lrow(QString::fromLatin1(l));
            auto* t = new CommitText(tx.contains(l) && tx[l].is_string() ? q(tx[l].get<std::string>()) : QString());
            const std::string lang = l;
            t->onCommit = [=](const QString& s) { E([nid, lang, s](Json& x) { Json& node = x["nodes"][nid]; Json tt = field(node.value("text", Json())); tt[lang] = s.toStdString(); node["text"] = tt; }); };
            r->addWidget(t, 1);
            gv->addLayout(r);
        }
        int other = 0;
        for (auto& [k, x] : tx.items()) { (void)x; if (k != "zh_TW" && k != "en") other++; }
        if (other) gv->addWidget(muted(tr("%1 other language(s) kept as they are").arg(other)));
        gv->addWidget(muted(tr("Choices (what the player can answer)")));
        const Json choices = n.value("choices", Json::array());
        for (size_t i = 0; i < choices.size(); i++) {
            const Json c = choices[i];
            auto* card = new QWidget;
            card->setObjectName(QStringLiteral("choice"));
            card->setStyleSheet(QStringLiteral("QWidget#choice { border-left:3px solid #3a3f47; }"));
            auto* cv = new QVBoxLayout(card);
            cv->setContentsMargins(8, 2, 0, 2);
            auto C = [=](const std::function<void(Json&)>& fn) { E([nid, i, fn](Json& x) { fn(x["nodes"][nid]["choices"][i]); }); };
            auto* r1 = lrow(QString::number(i + 1));
            const Json lb = field(c.value("label", Json()));
            for (const char* l : {"zh_TW", "en"}) {
                auto* e = new CommitLine(lb.contains(l) && lb[l].is_string() ? q(lb[l].get<std::string>()) : QString());
                e->setPlaceholderText(QString::fromLatin1(l));
                const std::string lang = l;
                e->onCommit = [=](const QString& s) { C([lang, s](Json& ch) { Json t = field(ch.value("label", Json())); t[lang] = s.toStdString(); ch["label"] = t; }); };
                r1->addWidget(e, 1);
            }
            for (int dir : {-1, 1, 0}) {
                auto* b = new QToolButton;
                b->setText(dir < 0 ? QStringLiteral("▲") : dir > 0 ? QStringLiteral("▼") : QStringLiteral("✕"));
                connect(b, &QToolButton::clicked, this, [=] {
                    E([nid, i, dir](Json& x) {
                        Json& l = x["nodes"][nid]["choices"];
                        if (dir == 0) { l.erase(i); return; }
                        const long j = (long)i + dir;
                        if (j >= 0 && j < (long)l.size()) std::swap(l[i], l[(size_t)j]);
                    });
                });
                r1->addWidget(b);
            }
            cv->addLayout(r1);
            auto* r2 = lrow(tr("Then"));
            auto* nx = nodeCombo(c.value("next", Json()).is_string() ? c["next"].get<std::string>() : std::string(), true);
            connect(nx, &QComboBox::activated, this, [=](int) { const std::string s = nx->currentData().toString().toStdString(); C([s](Json& ch) { ch["next"] = s.empty() ? Json() : Json(s); }); });
            auto* fin = new QCheckBox(tr("finishes the event"));
            fin->setChecked(c.value("finish", false));
            connect(fin, &QCheckBox::toggled, this, [=](bool on) { C([on](Json& ch) { if (on) ch["finish"] = true; else ch.erase("finish"); }); });
            auto* cid = new CommitLine(q(c.value("id", std::string())));
            cid->setMaximumWidth(100);
            cid->setToolTip(tr("Remembered as choiceMade {event, id}, for later conditions"));
            cid->onCommit = [=](const QString& s) { const std::string x = s.trimmed().toStdString(); C([x](Json& ch) { if (x.empty()) ch.erase("id"); else ch["id"] = x; }); };
            r2->addWidget(nx);
            r2->addWidget(fin);
            r2->addWidget(muted(QStringLiteral("id")));
            r2->addWidget(cid);
            r2->addStretch(1);
            cv->addLayout(r2);
            auto* r3 = lrow(tr("Only when"));
            r3->addWidget(condBox(QStringLiteral("ch:%1|%2").arg(q(nid)).arg(i), [this, id, nid, i] {
                const Json& cs = m_p.S;
                const Json& node = cs["dialogues"][id]["nodes"][nid];
                const Json l = node.value("choices", Json::array());
                return i < l.size() ? l[i].value("requires", Json()) : Json();
            }, [this, id, nid, i](const Json& cond) {
                Json& l = m_p.S["dialogues"][id]["nodes"][nid]["choices"];
                if (i >= l.size()) return;
                if (cond.is_null()) l[i].erase("requires"); else l[i]["requires"] = cond;
            }, hD), 1);
            cv->addLayout(r3);
            auto* r4 = lrow(tr("Then do"));
            auto* acts = new QWidget;
            auto* av = new QVBoxLayout(acts);
            av->setContentsMargins(0, 0, 0, 0);
            av->addWidget(actsBox(QStringLiteral("%1|%2").arg(q(nid)).arg(i), [c] { return c.value("actions", Json::array()); }, [this, id, nid, i]() -> Json* {
                Json& l = m_p.S["dialogues"][id]["nodes"][nid]["choices"];
                if (i >= l.size()) return nullptr;
                Json& ch = l[i];
                if (!ch.contains("actions") || ch["actions"].is_null()) ch["actions"] = Json::array();
                else if (!ch["actions"].is_array()) ch["actions"] = Json::array({ch["actions"]});
                return &ch["actions"];
            }, hD));
            if (c.contains("action")) av->addWidget(muted(tr("Older single action (kept):") + QStringLiteral(" ") + q(c["action"].dump()).left(80)));
            r4->addWidget(acts, 1);
            cv->addLayout(r4);
            auto* r5 = lrow(tr("Wakes"));
            auto* chips = new QWidget;
            auto* flow = new FlowLayout(chips, 4);
            if (c.contains("fire") && c["fire"].is_array())
                for (const Json& fx : c["fire"]) {
                    const std::string x = fx.get<std::string>();
                    auto* b = chip(q(x) + QStringLiteral("  ✕"), m_p.ev(x) ? QColor() : QColor("#ff5d5d"));
                    connect(b, &QToolButton::clicked, this, [=] {
                        C([x](Json& ch) { Json& l = ch["fire"]; l.erase(std::remove(l.begin(), l.end(), Json(x)), l.end()); if (l.empty()) ch.erase("fire"); });
                    });
                    flow->addWidget(b);
                }
            auto* fa = new QComboBox;
            fa->addItem(tr("+ link an event…"), QString());
            for (const Json& e : m_p.S["events"]) fa->addItem(q(e["id"].get<std::string>()), q(e["id"].get<std::string>()));
            connect(fa, &QComboBox::activated, this, [=](int) {
                const std::string x = fa->currentData().toString().toStdString();
                if (x.empty()) return;
                C([x](Json& ch) {
                    if (!ch.contains("fire") || !ch["fire"].is_array()) ch["fire"] = Json::array();
                    if (std::find(ch["fire"].begin(), ch["fire"].end(), Json(x)) == ch["fire"].end()) ch["fire"].push_back(x);
                });
            });
            flow->addWidget(fa);
            r5->addWidget(chips, 1);
            cv->addLayout(r5);
            gv->addWidget(card);
        }
        auto* addC = new QPushButton(tr("+ Choice"));
        connect(addC, &QPushButton::clicked, this, [=] {
            E([nid](Json& x) { Json& node = x["nodes"][nid]; if (!node.contains("choices")) node["choices"] = Json::array(); node["choices"].push_back(Json{{"label", Json{{"zh_TW", ""}, {"en", ""}}}, {"next", nullptr}}); });
        });
        gv->addWidget(addC, 0, Qt::AlignLeft);
        v->addWidget(g);
    }
    {
        auto* h = new QHBoxLayout;
        auto* nn = new QLineEdit;
        nn->setPlaceholderText(tr("new node id"));
        nn->setMaximumWidth(150);
        auto* nb = new QPushButton(tr("+ Node"));
        connect(nb, &QPushButton::clicked, this, [=] {
            const std::string n = nn->text().trimmed().toStdString();
            if (!stage::idOk(n)) { toast(tr("Use letters, digits, _ and - only."), true); return; }
            if (nodes.contains(n)) { toast(tr("%1 already exists.").arg(q(n)), true); return; }
            E([n](Json& x) { x["nodes"][n] = Json{{"text", Json{{"zh_TW", ""}, {"en", ""}}}, {"choices", Json::array({Json{{"label", Json{{"zh_TW", "……"}, {"en", "..."}}}, {"next", nullptr}}})}}; });
        });
        h->addWidget(nn);
        h->addWidget(nb);
        h->addStretch(1);
        v->addLayout(h);
    }
    v->addStretch(1);
    return w;
}
