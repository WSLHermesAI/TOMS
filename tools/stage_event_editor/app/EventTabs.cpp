// EventTabs.cpp -- the event editor's Floor map, Variables, value tabs and Kinds (event_editor.html's renderFloorMap,
// renderVars, renderValueTab, renderKinds).
#include "EventEditor.h"

#include "EventWidgets.h"
#include "FlowLayout.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QTimer>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

using stage::Json;
using namespace EvUi;

namespace {
bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
std::string kindOf(const std::string& s) { return s.substr(0, s.find(':')); }
std::string nameOf(const std::string& s) { const size_t c = s.find(':'); return c == std::string::npos ? std::string() : s.substr(c + 1); }
int fseq(const std::string& id) { try { return std::stoi(id.substr(1)); } catch (...) { return 0; } }
QString icon(const std::string& k) { return k == "var" ? QStringLiteral("◆") : k == "counter" ? QStringLiteral("▲") : QStringLiteral("⚑"); }
QString cleanCat(const QString& c) {
    QString s = c.trimmed();
    s.replace(QRegularExpression(QStringLiteral("\\s*/\\s*")), QStringLiteral("/"));
    while (s.startsWith(QLatin1Char('/'))) s.remove(0, 1);
    while (s.endsWith(QLatin1Char('/'))) s.chop(1);
    return s;
}
bool catNameOk(const QString& c) { return !c.isEmpty() && !c.contains(QRegularExpression(QStringLiteral("[\"<>&|\\\\]"))); }
QString howWrite(const Json& e, const std::string& ref, const Json& flags) {
    QStringList out;
    for (const Json& a : e["actions"]) {
        if (stage::actRef(a) != ref) continue;
        const std::string t = a.value("type", std::string());
        if (t == "setVar") out << QStringLiteral("= ") + QString::fromStdString(a.value("value", Json(0)).dump());
        else if (t == "setFlag") out << QStringLiteral("set");
        else { const double d = a.value("delta", Json(0)).is_number() ? a["delta"].get<double>() : 0; out << (d >= 0 ? QStringLiteral("+") : QString()) + QString::number(d); }
    }
    if (startsWith(ref, "flag:") && flags.contains(ref.substr(5)) && flags[ref.substr(5)].value("setBy", std::string()) == e["id"].get<std::string>()) out << QStringLiteral("setBy");
    return out.join(QStringLiteral(", "));
}
QString howRead(const Json& e, const std::string& ref) {
    QStringList out;
    for (const Json& l : e["req"]) {
        if (stage::leafRef(l) != ref) continue;
        const std::string t = l.value("type", std::string());
        out << (t == "varEquals" ? QStringLiteral("= ") + QString::fromStdString(l.value("value", Json(0)).dump())
              : t == "varAtLeast" ? QStringLiteral("≥ ") + QString::fromStdString(l.value("value", Json(0)).dump())
              : t == "counterAtLeast" ? QStringLiteral("≥ ") + QString::fromStdString(l.value("min", Json(0)).dump()) : QStringLiteral("set"));
    }
    return out.join(QStringLiteral(", "));
}
}  // namespace

// ---- the floor map -------------------------------------------------------------------------------------------------

QWidget* EventEditor::floorMapPane() {
    const double FW = 62, FH = 24, GX = 30, GY = 30, X0 = 56, Y0 = 26;
    std::vector<std::string> acts;
    for (const Json& f : m_p.S["floors"]) if (std::find(acts.begin(), acts.end(), f["act"].get<std::string>()) == acts.end()) acts.push_back(f["act"].get<std::string>());
    std::sort(acts.begin(), acts.end());
    std::vector<FloorMapView::Node> nodes;
    std::map<std::string, size_t> pos;
    std::vector<std::pair<QString, double>> rows;
    const auto reach = m_p.reachable();
    for (size_t r = 0; r < acts.size(); r++) {
        rows.push_back({q(acts[r]), Y0 + r * (FH + GY)});
        std::vector<const Json*> fl;
        for (const Json& f : m_p.S["floors"]) if (f["act"] == acts[r]) fl.push_back(&f);
        std::stable_sort(fl.begin(), fl.end(), [](const Json* a, const Json* b) { return fseq((*a)["id"].get<std::string>()) < fseq((*b)["id"].get<std::string>()); });
        for (size_t c = 0; c < fl.size(); c++) {
            FloorMapView::Node n;
            n.id = q((*fl[c])["id"].get<std::string>());
            n.act = q(acts[r]);
            n.boss = (*fl[c])["role"] == "boss-stage";
            n.warn = !reach.count((*fl[c])["id"].get<std::string>());
            n.isNew = m_p.isNewFloor((*fl[c])["id"].get<std::string>());
            n.r = QRectF(X0 + c * (FW + GX), Y0 + r * (FH + GY), FW, FH);
            pos[(*fl[c])["id"].get<std::string>()] = nodes.size();
            nodes.push_back(n);
        }
    }
    // stairs to floors that do not exist yet: a red stand-in at the end of the source's row
    for (const Json& f : m_p.S["floors"])
        for (const Json& st : f["stairs"]) {
            const std::string to = st.value("to", std::string());
            if (to.empty() || m_p.flo(to) || pos.count(to)) continue;
            const FloorMapView::Node& src = nodes[pos[f["id"].get<std::string>()]];
            int col = 0;
            for (const auto& n : nodes) if (std::abs(n.r.y() - src.r.y()) < 1) col++;
            FloorMapView::Node g;
            g.id = q(to);
            g.ghost = true;
            g.r = QRectF(X0 + col * (FW + GX), src.r.y(), FW + 10, FH);
            pos[to] = nodes.size();
            nodes.push_back(g);
        }
    std::vector<FloorMapView::Edge> edges;
    int nStairs = 0, nLocked = 0;
    for (const Json& f : m_p.S["floors"])
        for (const Json& st : f["stairs"]) {
            nStairs++;
            const std::string to = st.value("to", std::string()), lock = st.contains("unlock") ? st["unlock"].value("eventDone", std::string()) : std::string();
            if (!lock.empty()) nLocked++;
            if (to.empty() || !pos.count(to)) continue;
            if (st.value("dir", std::string("up")) == "down")
                if (const Json* other = m_p.flo(to)) {   // drawn once, as the up stair
                    bool dup = false;
                    for (const Json& x : (*other)["stairs"]) if (x.value("to", std::string()) == f["id"].get<std::string>() && x.value("dir", std::string("up")) == "up") dup = true;
                    if (dup) continue;
                }
            edges.push_back({q(f["id"].get<std::string>()), q(to), q(lock), !m_p.flo(to)});
        }
    auto* view = new FloorMapView;
    const QString sel = m_p.flo(m_p.selected) || pos.count(m_p.selected) ? q(m_p.selected) : QString();
    view->setGraph(nodes, edges, rows, sel);
    connect(view, &FloorMapView::clicked, this, [this](const QString& id) {
        const std::string s = id.toStdString();
        if (m_p.flo(s)) { select(s); return; }
        for (const Json& f : m_p.S["floors"])
            for (const Json& st : f["stairs"]) if (st.value("to", std::string()) == s) { select(f["id"].get<std::string>()); return; }
    });
    m_info->setText(tr("%1 floors · %2 stairs · %3 locked · click a floor").arg(m_p.S["floors"].size()).arg(nStairs).arg(nLocked));
    return view;
}

// ---- variables -----------------------------------------------------------------------------------------------------

QWidget* EventEditor::varsPane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    // the filter: text, category, kinds of value
    std::vector<std::string> refs = m_p.valueRefs();
    std::set<QString> cats;
    for (const std::string& r : refs) cats.insert(catOf(r));
    for (const Json& c : m_p.S["cats"]) cats.insert(q(c.get<std::string>()));
    {
        auto* h = new QHBoxLayout;
        auto* qe = new QLineEdit(m_vq);
        qe->setPlaceholderText(tr("Find: name, category, description, label…"));
        qe->setClearButtonEnabled(true);
        connect(qe, &QLineEdit::textChanged, this, [this](const QString& s) { m_vq = s; scheduleRender(); });
        h->addWidget(qe, 2);
        auto* cat = new QComboBox;
        cat->addItem(tr("All categories"), QString());
        for (const QString& c : cats) cat->addItem(c, c);
        cat->setCurrentIndex(std::max(0, cat->findData(m_vcat)));
        connect(cat, &QComboBox::activated, this, [this, cat](int) { m_vcat = cat->currentData().toString(); scheduleRender(); });
        h->addWidget(cat, 1);
        for (const char* k : {"var", "counter", "flag"}) {
            auto* c = new QToolButton;
            c->setCheckable(true);
            c->setChecked(m_vtypes.count(k) > 0);
            c->setText(icon(k) + QStringLiteral(" ") + (std::string(k) == "var" ? tr("event variable") : std::string(k) == "counter" ? tr("counter") : tr("flag")));
            const std::string kk = k;
            connect(c, &QToolButton::clicked, this, [this, kk] { if (m_vtypes.count(kk)) m_vtypes.erase(kk); else m_vtypes.insert(kk); scheduleRender(); });
            h->addWidget(c);
        }
        v->addLayout(h);
        // keep the caret in the search box after the re-render
        if (!m_vq.isEmpty()) QTimer::singleShot(0, qe, [qe] { qe->setFocus(); qe->setCursorPosition(qe->text().size()); });
    }
    // a new variable
    {
        auto* h = new QHBoxLayout;
        auto* name = new QLineEdit;
        name->setPlaceholderText(tr("name, e.g. well_clues"));
        auto* cat = new QComboBox;
        cat->setEditable(true);
        for (const QString& c : cats) cat->addItem(c);
        cat->setEditText(m_vcat);
        cat->lineEdit()->setPlaceholderText(tr("e.g. village/clues"));
        auto* def = new QSpinBox;
        def->setRange(-999999, 999999);
        auto* add = new QPushButton(tr("Add variable"));
        add->setDefault(true);
        auto* err = new QLabel;
        err->setStyleSheet(QStringLiteral("color:#ff5d5d"));
        auto doAdd = [=] {
            const std::string n = name->text().trimmed().toStdString();
            if (!stage::nameOk(n)) { err->setText(tr("Any name works, Chinese too, except empty, spaces at the ends, or \" ' < > & | \\ /")); return; }
            if (m_p.S["vars"].contains(n) || m_p.S["counters"].contains(n) || m_p.S["flags"].contains(n)) { err->setText(tr("%1 already exists").arg(q(n))); return; }
            const std::string c = cat->currentText().trimmed().toStdString();
            const int d = def->value();
            edit(tr("New variable %1").arg(q(n)), [this, n, c, d] {
                m_p.S["vars"][n] = Json{{"type", "int"}, {"default", d}, {"scope", "run"}};
                if (!c.empty()) m_p.S["vars"][n]["category"] = c;
            });
        };
        connect(add, &QPushButton::clicked, this, doAdd);
        connect(name, &QLineEdit::returnPressed, this, doAdd);
        h->addWidget(name, 2);
        h->addWidget(new QLabel(tr("Category")));
        h->addWidget(cat, 2);
        h->addWidget(new QLabel(tr("Default")));
        h->addWidget(def);
        h->addWidget(add);
        h->addWidget(err, 2);
        v->addLayout(h);
    }
    // categories: create, rename, delete
    {
        auto* h = new QHBoxLayout;
        auto* ce = new QComboBox;
        ce->setEditable(true);
        for (const QString& c : cats) ce->addItem(c);
        ce->setEditText(m_vcat);
        ce->lineEdit()->setPlaceholderText(tr("category, e.g. village/clues"));
        auto* create = new QPushButton(tr("+ Create category"));
        auto* rename = new QPushButton(tr("✎ Rename category"));
        auto* del = new QPushButton(tr("🗑 Delete category"));
        connect(create, &QPushButton::clicked, this, [=] {
            const QString c = cleanCat(ce->currentText());
            if (!catNameOk(c)) { toast(tr("Type a name (any characters, Chinese too, except \" < > & | \\)."), true); return; }
            if (cats.count(c)) { toast(tr("%1 already exists").arg(c), true); return; }
            edit(tr("New category %1").arg(c), [this, c] {
                m_p.S["cats"].push_back(c.toStdString());
                std::vector<std::string> l = m_p.S["cats"].get<std::vector<std::string>>();
                std::sort(l.begin(), l.end());
                m_p.S["cats"] = l;
            });
            m_vcat = c;
        });
        // the values in a category and its sub-categories
        auto members = [this](const QString& c) {
            std::vector<std::string> users;
            for (const std::string& r : m_p.valueRefs()) {
                const Json* rec = m_p.recOf(r);
                const QString own = rec && rec->contains("category") ? q((*rec)["category"].get<std::string>()) : QString();
                if (!own.isEmpty() && (own == c || own.startsWith(c + QLatin1Char('/')))) users.push_back(r);
            }
            return users;
        };
        connect(rename, &QPushButton::clicked, this, [=] {
            const QString c = cleanCat(ce->currentText());
            if (c.isEmpty()) { toast(tr("Type or pick a category to delete."), true); return; }
            bool ok = false;
            const QString nu = cleanCat(QInputDialog::getText(this, tr("Rename the category \"%1\"").arg(c),
                tr("Every variable, counter and flag in it (and in its sub-categories) moves to the new name. Use / for a sub-category, e.g. village/clues. A name that already exists merges the two."),
                QLineEdit::Normal, c, &ok));
            if (!ok || nu.isEmpty() || nu == c) return;
            if (!catNameOk(nu)) { toast(tr("Type a name (any characters, Chinese too, except \" < > & | \\)."), true); return; }
            const auto users = members(c);
            edit(tr("Rename category %1 → %2").arg(c, nu), [this, users, c, nu] {
                for (const std::string& r : users) {
                    Json* rec = m_p.recOf(r);
                    const QString own = q((*rec)["category"].get<std::string>());
                    (*rec)["category"] = (nu + own.mid(c.size())).toStdString();
                }
                std::set<std::string> nc;
                for (const Json& x : m_p.S["cats"]) {
                    const QString s = q(x.get<std::string>());
                    nc.insert((s == c || s.startsWith(c + QLatin1Char('/')) ? nu + s.mid(c.size()) : s).toStdString());
                }
                m_p.S["cats"] = std::vector<std::string>(nc.begin(), nc.end());
            });
            if (m_vcat == c) m_vcat = nu;
        });
        connect(del, &QPushButton::clicked, this, [=] {
            const QString c = cleanCat(ce->currentText());
            if (c.isEmpty()) { toast(tr("Type or pick a category to delete."), true); return; }
            const auto users = members(c);
            std::vector<std::string> created;
            for (const Json& x : m_p.S["cats"]) { const QString s = q(x.get<std::string>()); if (s == c || s.startsWith(c + QLatin1Char('/'))) created.push_back(x.get<std::string>()); }
            if (users.empty() && created.empty()) { toast(cats.count(c) ? tr("%1 is an automatic group (from the data); it cannot be deleted.").arg(c) : tr("There is no category %1.").arg(c), true); return; }
            std::vector<std::string> vars;
            for (const std::string& r : users) if (kindOf(r) == "var") vars.push_back(r);
            QMessageBox box(this);
            box.setWindowTitle(tr("Delete the category \"%1\"").arg(c));
            box.setText(users.empty() ? tr("Nothing uses this category. Delete it?")
                                      : tr("\"%1\" and its sub-categories hold %2 event variable(s) and %3 counter(s) / flag(s).").arg(c).arg(vars.size()).arg(users.size() - vars.size()));
            QPushButton* delVars = vars.empty() ? nullptr : box.addButton(tr("Delete %1 variable(s)").arg(vars.size()), QMessageBox::DestructiveRole);
            QPushButton* uncat = box.addButton(users.empty() ? tr("🗑 Delete category") : tr("Make uncategorized"), QMessageBox::AcceptRole);
            box.addButton(QMessageBox::Cancel);
            box.exec();
            const bool killVars = delVars && box.clickedButton() == delVars;
            if (!killVars && box.clickedButton() != uncat) return;
            edit(killVars ? tr("Delete category %1 and %2 variable(s)").arg(c).arg(vars.size()) : tr("Delete category %1").arg(c), [this, users, vars, created, killVars] {
                for (const std::string& r : users) {
                    if (killVars && std::find(vars.begin(), vars.end(), r) != vars.end()) m_p.S["vars"].erase(nameOf(r));
                    else if (Json* rec = m_p.recOf(r)) rec->erase("category");
                }
                Json keep = Json::array();
                for (const Json& x : m_p.S["cats"]) if (std::find(created.begin(), created.end(), x.get<std::string>()) == created.end()) keep.push_back(x);
                m_p.S["cats"] = keep;
            });
            if (m_vcat == c || m_vcat.startsWith(c + QLatin1Char('/'))) m_vcat.clear();
        });
        h->addWidget(new QLabel(QStringLiteral("📁")));
        h->addWidget(ce, 2);
        h->addWidget(create);
        h->addWidget(rename);
        h->addWidget(del);
        h->addStretch(1);
        v->addLayout(h);
    }
    // the list
    const QString qq = m_vq.trimmed().toLower();
    auto matches = [&](const std::string& r) {
        if (!m_vtypes.count(kindOf(r))) return false;
        const QString c = catOf(r);
        if (!m_vcat.isEmpty() && c != m_vcat && !c.startsWith(m_vcat + QLatin1Char('/'))) return false;
        if (qq.isEmpty()) return true;
        const Json* rec = m_p.recOf(r);
        QStringList hay{q(nameOf(r)), c};
        if (rec) for (const char* k : {"desc", "label"}) if (rec->contains(k)) { const Json& x = (*rec)[k]; if (x.is_object()) { hay << q(x.value("zh_TW", std::string())) << q(x.value("en", std::string())); } else if (x.is_string()) hay << q(x.get<std::string>()); }
        if (rec && rec->contains("setBy")) hay << q((*rec)["setBy"].get<std::string>());
        for (const QString& s : hay) if (s.toLower().contains(qq)) return true;
        return false;
    };
    std::vector<std::string> shown;
    for (const std::string& r : refs) if (matches(r)) shown.push_back(r);
    static const std::map<std::string, int> tord = {{"var", 0}, {"counter", 1}, {"flag", 2}};
    std::stable_sort(shown.begin(), shown.end(), [&](const std::string& a, const std::string& b) {
        if (!m_vcat.isEmpty() && catOf(a) != catOf(b)) return catOf(a) < catOf(b);
        if (tord.at(kindOf(a)) != tord.at(kindOf(b))) return tord.at(kindOf(a)) < tord.at(kindOf(b));
        return nameOf(a) < nameOf(b);
    });
    for (auto it = m_varSel.begin(); it != m_varSel.end();) it = std::find(refs.begin(), refs.end(), *it) == refs.end() ? m_varSel.erase(it) : std::next(it);
    if (!m_varSel.empty()) {   // several values into one category at once
        auto* h = new QHBoxLayout;
        h->addWidget(rich(QStringLiteral("<b>%1</b>").arg(tr("%1 selected").arg(m_varSel.size()))));
        h->addWidget(new QLabel(tr("Move to category")));
        auto* bc = new QComboBox;
        bc->setEditable(true);
        for (const QString& c : cats) bc->addItem(c);
        bc->setEditText(m_vcat);
        auto apply = [=](const QString& cat) {
            const QString c = cleanCat(cat);
            if (!c.isEmpty() && !catNameOk(c)) { toast(tr("Type a name (any characters, Chinese too, except \" < > & | \\)."), true); return; }
            const std::vector<std::string> sel(m_varSel.begin(), m_varSel.end());
            edit(c.isEmpty() ? tr("Take the category off %1 value(s)").arg(sel.size()) : tr("Move %1 value(s) to category %2").arg(sel.size()).arg(c), [this, sel, c] {
                for (const std::string& r : sel) if (Json* rec = m_p.recOf(r)) { if (c.isEmpty()) rec->erase("category"); else (*rec)["category"] = c.toStdString(); }
            });
        };
        auto* ap = new QPushButton(tr("Apply"));
        connect(ap, &QPushButton::clicked, this, [=] { apply(bc->currentText()); });
        auto* un = new QPushButton(tr("Make uncategorized"));
        connect(un, &QPushButton::clicked, this, [=] { apply(QString()); });
        auto* cl = new QPushButton(tr("Clear selection"));
        connect(cl, &QPushButton::clicked, this, [this] { m_varSel.clear(); scheduleRender(); });
        h->addWidget(bc, 1);
        h->addWidget(ap);
        h->addWidget(un);
        h->addWidget(cl);
        v->addLayout(h);
    }
    auto* table = new QTableWidget((int)shown.size(), 8);
    table->setHorizontalHeaderLabels({QStringLiteral("✓"), tr("Name"), tr("Type"), tr("Category"), QString(), tr("Changed by"), tr("Read by"), QString()});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    auto chipsText = [](const std::vector<const Json*>& l) {
        QStringList s;
        for (const Json* e : l) s << QString::fromStdString((*e)["id"].get<std::string>());
        return s.isEmpty() ? QStringLiteral("—") : s.join(QStringLiteral(", "));
    };
    for (size_t i = 0; i < shown.size(); i++) {
        const std::string ref = shown[i], k = kindOf(ref), n = nameOf(ref);
        const Json rec = m_p.recOf(ref) ? *m_p.recOf(ref) : Json::object();
        auto* pick = new QCheckBox;
        pick->setChecked(m_varSel.count(ref) > 0);
        connect(pick, &QCheckBox::toggled, this, [this, ref](bool on) { if (on) m_varSel.insert(ref); else m_varSel.erase(ref); scheduleRender(); });
        table->setCellWidget((int)i, 0, pick);
        auto* nb = new QToolButton;
        nb->setText(icon(k) + QStringLiteral(" ") + q(n));
        nb->setAutoRaise(true);
        nb->setToolTip(tr("Click to open it in a tab."));
        connect(nb, &QToolButton::clicked, this, [this, ref] { openValue(ref); });
        table->setCellWidget((int)i, 1, nb);
        table->setItem((int)i, 2, new QTableWidgetItem(k == "var" ? tr("event variable") : k == "counter" ? tr("counter") : tr("flag")));
        auto* ce = new CommitLine(rec.contains("category") ? q(rec["category"].get<std::string>()) : QString());
        ce->setPlaceholderText(catOf(ref));
        ce->onCommit = [this, ref, n](const QString& s) {
            const std::string c = s.trimmed().toStdString();
            edit(tr("Category of %1").arg(q(n)), [this, ref, c] { if (Json* r = m_p.recOf(ref)) { if (c.empty()) r->erase("category"); else (*r)["category"] = c; } });
        };
        table->setCellWidget((int)i, 3, ce);
        if (k == "var") {
            auto* cell = new QWidget;
            auto* h = new QHBoxLayout(cell);
            h->setContentsMargins(2, 0, 2, 0);
            auto* d = new QSpinBox;
            d->setRange(-999999, 999999);
            d->setValue(rec.value("default", Json(0)).is_number() ? rec["default"].get<int>() : rec.value("default", false) == true ? 1 : 0);
            d->setKeyboardTracking(false);
            connect(d, &QSpinBox::valueChanged, this, [this, n](int x) { edit(tr("Variable %1").arg(q(n)), [this, n, x] { m_p.S["vars"][n]["default"] = x; }); });
            auto* sc = new QComboBox;
            sc->addItems({QStringLiteral("run"), QStringLiteral("meta")});
            sc->setCurrentText(rec.value("scope", std::string("run")) == "meta" ? QStringLiteral("meta") : QStringLiteral("run"));
            connect(sc, &QComboBox::activated, this, [this, n, sc](int) { const std::string s = sc->currentText().toStdString(); edit(tr("Variable %1").arg(q(n)), [this, n, s] { m_p.S["vars"][n]["scope"] = s; }); });
            h->addWidget(d);
            h->addWidget(sc);
            table->setCellWidget((int)i, 4, cell);
            auto* del = new QToolButton;
            del->setText(tr("Delete"));
            connect(del, &QToolButton::clicked, this, [this, n] { edit(tr("Delete variable %1").arg(q(n)), [this, n] { m_p.S["vars"].erase(n); }); });
            table->setCellWidget((int)i, 7, del);
        } else if (k == "counter") {
            QStringList cl;
            if (rec.contains("clamp")) for (const Json& x : rec["clamp"]) cl << q(x.dump());
            table->setItem((int)i, 4, new QTableWidgetItem(L(rec.value("label", Json())) + QStringLiteral(" · ") + cl.join(QStringLiteral(" … "))));
        } else {
            table->setItem((int)i, 4, new QTableWidgetItem(q(rec.value("scope", std::string())) + QStringLiteral(" · ") + q(rec.value("setBy", std::string()))));
        }
        table->setItem((int)i, 5, new QTableWidgetItem(chipsText(m_p.changers(ref))));
        table->setItem((int)i, 6, new QTableWidgetItem(chipsText(m_p.readers(ref))));
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    table->setMinimumHeight(300);
    if (shown.empty()) v->addWidget(muted(tr("No value matches the filter.")));
    v->addWidget(table, 1);
    m_info->setText(tr("%1 of %2 values").arg(shown.size()).arg(refs.size()));
    return w;
}

// ---- a value tab ---------------------------------------------------------------------------------------------------

QWidget* EventEditor::valuePane(const std::string& ref) {
    const std::string k = kindOf(ref), n = nameOf(ref);
    Json* recP = m_p.recOf(ref);
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    if (!recP) { v->addWidget(muted(tr("This value no longer exists."))); return w; }
    const Json rec = *recP;
    const QString file = k == "var" ? QStringLiteral("story/vars.json") : k == "counter" ? QStringLiteral("story/counters.json") : QStringLiteral("story/flags.json");
    const QString typeName = k == "var" ? tr("event variable") : k == "counter" ? tr("counter") : tr("flag");
    auto* top = new QHBoxLayout;
    auto* formW = new QWidget;
    auto* form = new QFormLayout(formW);
    const QString hEdit = tr("Edit %1").arg(q(n));
    auto upd = [this, ref, hEdit](const std::function<void(Json&)>& fn) { edit(hEdit, [this, ref, fn] { if (Json* r = m_p.recOf(ref)) fn(*r); }); };
    form->addRow(rich(QStringLiteral("<span style='font-size:15px;font-weight:600'>%1 %2</span> <span style='color:#8b929c'>%3</span>").arg(icon(k), esc(q(n)), typeName)));
    if (k == "var") {
        auto* name = new CommitLine(q(n));
        auto* err = new QLabel;
        err->setStyleSheet(QStringLiteral("color:#ff5d5d"));
        name->onCommit = [=](const QString& s) {
            const std::string nu = s.trimmed().toStdString();
            if (nu == n) return;
            if (!stage::nameOk(nu) || m_p.S["vars"].contains(nu) || m_p.S["counters"].contains(nu) || m_p.S["flags"].contains(nu)) {
                err->setText(tr("Use a name not in use; any characters work, Chinese too, except \" ' < > & | \\ / and spaces at the ends"));
                return;
            }
            edit(tr("Rename variable %1 → %2").arg(q(n), q(nu)), [this, n, nu] {
                Json vars = Json::object();
                for (auto& [kk, vv] : m_p.S["vars"].items()) vars[kk == n ? nu : kk] = vv;
                m_p.S["vars"] = vars;
                for (Json& e : m_p.S["events"]) {
                    for (Json& a : e["actions"]) if (a.value("var", std::string()) == n) a["var"] = nu;
                    for (Json& l : e["req"]) if (l.value("var", std::string()) == n) l["var"] = nu;
                    if (e["trigger"].value("on", std::string()) == "varChanged" && e["trigger"].value("var", std::string()) == n) e["trigger"]["var"] = nu;
                }
            });
            m_openVals.replaceInStrings(QStringLiteral("var:") + q(n), QStringLiteral("var:") + q(nu));
            m_tab = QStringLiteral("val:var:") + q(nu);
        };
        form->addRow(tr("Name"), name);
        form->addRow(QString(), err);
    } else {
        form->addRow(tr("Name"), rich(QStringLiteral("<b>%1</b><br><span style='color:#8b929c;font-size:11px'>%2</span>").arg(esc(q(n)),
            tr("Counters and flags are named in chapter and dialogue files and in the game code, so they are renamed there, not here."))));
    }
    auto* cat = new CommitLine(rec.contains("category") ? q(rec["category"].get<std::string>()) : QString());
    cat->setPlaceholderText(catOf(ref));
    cat->onCommit = [=](const QString& s) { const std::string c = s.trimmed().toStdString(); upd([c](Json& r) { if (c.empty()) r.erase("category"); else r["category"] = c; }); };
    form->addRow(tr("Category"), cat);
    auto loc = [](const Json& o, const char* l) { return o.is_object() && o.contains(l) && o[l].is_string() ? QString::fromStdString(o[l].get<std::string>()) : QString(); };
    auto scopeBox = [&] {
        auto* sc = new QComboBox;
        sc->addItems({QStringLiteral("run"), QStringLiteral("meta")});
        sc->setCurrentText(rec.value("scope", std::string("run")) == "meta" ? QStringLiteral("meta") : QStringLiteral("run"));
        connect(sc, &QComboBox::activated, this, [=](int) { const std::string s = sc->currentText().toStdString(); upd([s](Json& r) { r["scope"] = s; }); });
        return sc;
    };
    if (k == "var") {
        auto* type = new QComboBox;
        type->addItems({QStringLiteral("int"), QStringLiteral("bool")});
        type->setCurrentText(rec.value("type", std::string("int")) == "bool" ? QStringLiteral("bool") : QStringLiteral("int"));
        connect(type, &QComboBox::activated, this, [=](int) { const std::string t = type->currentText().toStdString(); upd([t](Json& r) { r["type"] = t; }); });
        form->addRow(tr("Type"), type);
        auto* d = new QSpinBox;
        d->setRange(-999999, 999999);
        d->setKeyboardTracking(false);
        d->setValue(rec.value("default", Json(0)).is_number() ? rec["default"].get<int>() : 0);
        connect(d, &QSpinBox::valueChanged, this, [=](int x) { upd([x](Json& r) { r["default"] = x; }); });
        form->addRow(tr("Default"), d);
        form->addRow(tr("Scope"), scopeBox());
        for (const char* l : {"zh_TW", "en"}) {
            auto* t = new CommitLine(loc(rec.value("desc", Json()), l));
            const std::string lang = l;
            t->onCommit = [=](const QString& s) {
                upd([lang, s](Json& r) {
                    Json dd = r.contains("desc") && r["desc"].is_object() ? r["desc"] : Json::object();
                    dd[lang] = s.toStdString();
                    if (dd.value("zh_TW", std::string()).empty() && dd.value("en", std::string()).empty()) r.erase("desc"); else r["desc"] = dd;
                });
            };
            form->addRow(tr("Description") + QStringLiteral(" · ") + QString::fromLatin1(l), t);
        }
    }
    if (k == "counter") {
        for (const char* l : {"zh_TW", "en"}) {
            auto* t = new CommitLine(loc(rec.value("label", Json()), l));
            const std::string lang = l;
            t->onCommit = [=](const QString& s) {
                upd([lang, s](Json& r) { if (!r.contains("label") || !r["label"].is_object()) r["label"] = Json::object(); r["label"][lang] = s.toStdString(); });
            };
            form->addRow(tr("HUD label") + QStringLiteral(" · ") + QString::fromLatin1(l), t);
        }
        auto* disp = new CommitLine(rec.contains("displayAt") ? q(rec["displayAt"].dump()) : QString());
        disp->onCommit = [=](const QString& s) { bool ok; const int x = s.toInt(&ok); upd([ok, x](Json& r) { if (ok) r["displayAt"] = x; else r.erase("displayAt"); }); };
        form->addRow(tr("Show on HUD at"), disp);
        auto* clampRow = new QWidget;
        auto* ch = new QHBoxLayout(clampRow);
        ch->setContentsMargins(0, 0, 0, 0);
        const Json cl = rec.value("clamp", Json::array());
        auto* mn = new CommitLine(cl.size() > 0 && !cl[0].is_null() ? q(cl[0].dump()) : QString());
        auto* mx = new CommitLine(cl.size() > 1 && !cl[1].is_null() ? q(cl[1].dump()) : QString());
        auto setClamp = [=](const QString&) {
            bool a, b;
            const int x = mn->text().toInt(&a), y = mx->text().toInt(&b);
            upd([=](Json& r) { r["clamp"] = Json::array({a ? Json(x) : Json(), b ? Json(y) : Json()}); });
        };
        mn->onCommit = setClamp;
        mx->onCommit = setClamp;
        ch->addWidget(mn);
        ch->addWidget(new QLabel(QStringLiteral("…")));
        ch->addWidget(mx);
        form->addRow(tr("Clamp (min … max)"), clampRow);
    }
    if (k == "flag") {
        form->addRow(tr("Scope"), scopeBox());
        auto* sb = new CommitLine(q(rec.value("setBy", std::string())));
        sb->onCommit = [=](const QString& s) { const std::string x = s.toStdString(); upd([x](Json& r) { r["setBy"] = x; }); };
        form->addRow(tr("Set by (story data)"), sb);
    }
    form->addRow(muted(tr("Saved in %1").arg(file)));
    auto* btns = new QHBoxLayout;
    auto* sv = new QPushButton(tr("Save…"));
    connect(sv, &QPushButton::clicked, this, [this] { save(); });
    btns->addWidget(sv);
    if (k == "var") {
        auto* del = new QPushButton(tr("Delete variable"));
        connect(del, &QPushButton::clicked, this, [this, n, ref] { edit(tr("Delete variable %1").arg(q(n)), [this, n] { m_p.S["vars"].erase(n); }); closeValue(ref); });
        btns->addWidget(del);
    }
    auto* cl = new QPushButton(tr("Close tab"));
    connect(cl, &QPushButton::clicked, this, [this, ref] { closeValue(ref); });
    btns->addWidget(cl);
    btns->addStretch(1);
    form->addRow(btns);
    top->addWidget(formW, 1);
    // who changes, reads and watches it
    auto* right = new QWidget;
    auto* rv = new QVBoxLayout(right);
    auto list = [&](const QString& title, const std::vector<const Json*>& evs, const std::function<QString(const Json&)>& how) {
        rv->addWidget(heading(title + QStringLiteral(" (%1)").arg(evs.size())));
        if (evs.empty()) rv->addWidget(muted(QStringLiteral("—")));
        for (const Json* e : evs) {
            auto* h = new QHBoxLayout;
            const std::string id = (*e)["id"].get<std::string>();
            auto* c = chip(q(id));
            connect(c, &QToolButton::clicked, this, [this, id] { select(id); });
            h->addWidget(c);
            h->addWidget(muted(how(*e)), 1);
            rv->addLayout(h);
        }
    };
    list(tr("Changed by"), m_p.changers(ref), [&](const Json& e) { return howWrite(e, ref, m_p.S["flags"]); });
    list(tr("Read by"), m_p.readersOnly(ref), [&](const Json& e) { return howRead(e, ref); });
    if (k == "var") list(tr("Fires on change"), m_p.watchers(ref), [this](const Json&) { return tr("on change"); });
    const auto linked = m_p.linkedValues(ref);
    rv->addWidget(heading(tr("Linked values (through an event)") + QStringLiteral(" (%1)").arg(linked.size())));
    auto* chips = new QWidget;
    auto* flow = new FlowLayout(chips, 4);
    for (const std::string& r : linked) {
        auto* c = chip(icon(kindOf(r)) + QStringLiteral(" ") + q(nameOf(r)));
        connect(c, &QToolButton::clicked, this, [this, r] { openValue(r); });
        flow->addWidget(c);
    }
    if (linked.empty()) flow->addWidget(muted(QStringLiteral("—")));
    rv->addWidget(chips);
    rv->addStretch(1);
    top->addWidget(right, 1);
    v->addLayout(top);
    m_info->setText(QStringLiteral("%1 · ✎ %2 · 👁 %3 · ⇄ %4").arg(typeName).arg(m_p.changers(ref).size()).arg(m_p.readersOnly(ref).size() + m_p.watchers(ref).size()).arg(linked.size()));
    return w;
}

// ---- kinds ---------------------------------------------------------------------------------------------------------

QWidget* EventEditor::kindsPane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->addWidget(muted(tr("Every event has a kind. This list is saved in assets/data/events/kinds.json; the kind drop-down, the filter and the colours in every view come from it.")));
    std::vector<std::string> kinds = m_p.kindsList();
    std::vector<std::string> unknown;
    for (const Json& e : m_p.S["events"]) {
        const std::string k = e["kind"].get<std::string>();
        if (!k.empty() && !m_p.S["kinds"].contains(k) && std::find(unknown.begin(), unknown.end(), k) == unknown.end()) unknown.push_back(k);
    }
    std::vector<std::string> rows = kinds;
    rows.insert(rows.end(), unknown.begin(), unknown.end());
    auto* table = new QTableWidget((int)rows.size() + 1, 8);
    table->setHorizontalHeaderLabels({QString(), tr("Kind id"), tr("Name") + QStringLiteral(" · zh_TW"), QStringLiteral("en"), tr("Description"), tr("In log"), tr("Events"), QString()});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    auto users = [this](const std::string& k) {
        std::vector<std::string> out;
        for (const Json& e : m_p.S["events"]) if (e["kind"] == k) out.push_back(e["id"].get<std::string>());
        return out;
    };
    for (size_t i = 0; i < rows.size(); i++) {
        const std::string k = rows[i];
        const bool known = i < kinds.size();
        const std::vector<std::string> u = users(k);
        std::set<std::string>& sel = m_kindSel[k];
        for (auto it = sel.begin(); it != sel.end();) it = std::find(u.begin(), u.end(), *it) == u.end() ? sel.erase(it) : std::next(it);
        auto setRec = [this, k](const QString& label, const std::function<void(Json&)>& fn) { edit(label, [this, k, fn] { fn(m_p.S["kinds"][k]); }); };
        if (known) {
            const Json r = m_p.S["kinds"][k];
            auto* col = new QToolButton;
            const QColor c(q(r.value("color", std::string("#8b929c"))));
            col->setStyleSheet(QStringLiteral("background:%1; min-width:22px").arg(c.name()));
            col->setToolTip(tr("Colour in the editors"));
            connect(col, &QToolButton::clicked, this, [=] {
                const QColor nc = QColorDialog::getColor(c, this, tr("Colour in the editors"));
                if (nc.isValid()) setRec(tr("Edit kind %1").arg(q(k)), [nc](Json& x) { x["color"] = nc.name().toStdString(); });
            });
            table->setCellWidget((int)i, 0, col);
            auto* idE = new CommitLine(q(k));
            idE->onCommit = [=](const QString& s) {
                const std::string nu = s.trimmed().toStdString();
                if (nu == k) return;
                if (!stage::nameOk(nu)) { toast(tr("Any name works, Chinese too, except empty, spaces at the ends, or \" ' < > & | \\ /"), true); return; }
                if (m_p.S["kinds"].contains(nu)) { toast(tr("%1 already exists").arg(q(nu)), true); return; }
                edit(tr("Rename kind %1 → %2").arg(q(k), q(nu)), [this, k, nu] {
                    Json ks = Json::object();
                    for (auto& [kk, vv] : m_p.S["kinds"].items()) ks[kk == k ? nu : kk] = vv;
                    m_p.S["kinds"] = ks;
                    for (Json& e : m_p.S["events"]) if (e["kind"] == k) e["kind"] = nu;
                });
                if (m_kind == q(k)) m_kind = q(nu);
                if (m_kindSel.count(k)) { m_kindSel[nu] = m_kindSel[k]; m_kindSel.erase(k); }
            };
            table->setCellWidget((int)i, 1, idE);
            for (int li = 0; li < 2; li++) {
                const char* l = li ? "en" : "zh_TW";
                auto* t = new CommitLine(r.contains("name") ? q(r["name"].value(l, std::string())) : QString());
                const std::string lang = l;
                t->onCommit = [=](const QString& s) { setRec(tr("Edit kind %1").arg(q(k)), [lang, s](Json& x) { if (!x.contains("name") || !x["name"].is_object()) x["name"] = Json::object(); x["name"][lang] = s.trimmed().toStdString(); }); };
                table->setCellWidget((int)i, 2 + li, t);
            }
            auto* desc = new CommitLine(L(r.value("desc", Json())));
            desc->setPlaceholderText(tr("what this kind of event does"));
            const std::string lng = lang().toStdString();
            desc->onCommit = [=](const QString& s) {
                setRec(tr("Edit kind %1").arg(q(k)), [lng, s](Json& x) {
                    const std::string d = s.trimmed().toStdString();
                    if (d.empty()) x.erase("desc");
                    else { if (!x.contains("desc") || !x["desc"].is_object()) x["desc"] = Json::object(); x["desc"][lng] = d; }
                });
            };
            table->setCellWidget((int)i, 4, desc);
            auto* log = new QCheckBox;
            log->setChecked(!(r.contains("inLog") && r["inLog"] == false));
            log->setToolTip(tr("Events of this kind are listed in the player's event log"));
            connect(log, &QCheckBox::toggled, this, [=](bool on) { setRec(tr("Event log of kind %1").arg(q(k)), [on](Json& x) { if (on) x.erase("inLog"); else x["inLog"] = false; }); });
            table->setCellWidget((int)i, 5, log);
        } else {
            auto* add = new QPushButton(QStringLiteral("⚠ ") + q(k) + QStringLiteral(" · ") + tr("Add to the list"));
            add->setToolTip(tr("used by %1 event(s) but not in this list").arg(u.size()));
            connect(add, &QPushButton::clicked, this, [this, k] {
                edit(tr("New kind %1").arg(q(k)), [this, k] { m_p.S["kinds"][k] = Json{{"name", Json{{"zh_TW", k}, {"en", k}}}, {"color", "#8b929c"}}; });
            });
            table->setCellWidget((int)i, 1, add);
        }
        // its events: tick some, move them to another kind
        auto* ev = new QWidget;
        auto* eh = new QHBoxLayout(ev);
        eh->setContentsMargins(2, 0, 2, 0);
        auto* cnt = new QToolButton;
        cnt->setText(QString::number(u.size()));
        cnt->setToolTip(tr("Show these events in the Events tree"));
        connect(cnt, &QToolButton::clicked, this, [this, k] { m_kind = q(k); scheduleRender(); });
        auto* pick = new QComboBox;
        pick->addItem(tr("%1 selected").arg(sel.size()), QString());
        auto* model = new QStandardItemModel(pick);
        auto* head = new QStandardItem(tr("%1 selected").arg(sel.size()));
        head->setEnabled(false);
        model->appendRow(head);
        for (const std::string& id : u) {
            auto* it = new QStandardItem(q(id));
            it->setCheckable(true);
            it->setCheckState(sel.count(id) ? Qt::Checked : Qt::Unchecked);
            it->setData(q(id));
            model->appendRow(it);
        }
        pick->setModel(model);
        connect(model, &QStandardItemModel::itemChanged, this, [this, k](QStandardItem* it) {
            const std::string id = it->data().toString().toStdString();
            if (id.empty()) return;
            if (it->checkState() == Qt::Checked) m_kindSel[k].insert(id); else m_kindSel[k].erase(id);
        });
        auto* all = new QToolButton;
        all->setText(sel.size() == u.size() && !u.empty() ? tr("Select none") : tr("Select all (%1)").arg(u.size()));
        connect(all, &QToolButton::clicked, this, [this, k, u] {
            auto& s = m_kindSel[k];
            if (s.size() == u.size()) s.clear(); else s = std::set<std::string>(u.begin(), u.end());
            scheduleRender();
        });
        eh->addWidget(cnt);
        eh->addWidget(pick, 1);
        eh->addWidget(all);
        table->setCellWidget((int)i, 6, ev);
        if (!u.empty()) {
            auto* mv = new QComboBox;
            mv->addItem(tr("Move selected to…"), QString());
            for (const std::string& x : kinds) if (x != k) mv->addItem(q(x), q(x));
            connect(mv, &QComboBox::activated, this, [this, mv, k](int) {
                const std::string to = mv->currentData().toString().toStdString();
                const std::set<std::string> ids = m_kindSel[k];
                if (to.empty() || ids.empty()) { toast(tr("Tick events to move them")); return; }
                m_kindSel[k].clear();
                m_kindSel[to].insert(ids.begin(), ids.end());
                edit(tr("Move %1 event(s) from %2 to %3").arg(ids.size()).arg(q(k), q(to)), [this, ids, to] { for (Json& e : m_p.S["events"]) if (ids.count(e["id"].get<std::string>())) e["kind"] = to; });
            });
            table->setCellWidget((int)i, 7, mv);
        } else if (known) {
            auto* del = new QToolButton;
            del->setText(tr("Delete"));
            connect(del, &QToolButton::clicked, this, [this, k] { edit(tr("Delete kind %1").arg(q(k)), [this, k] { m_p.S["kinds"].erase(k); }); });
            table->setCellWidget((int)i, 7, del);
        }
    }
    // a new kind
    {
        const int r = (int)rows.size();
        auto* col = new QToolButton;
        auto color = std::make_shared<QColor>(QColor("#8b929c"));
        col->setStyleSheet(QStringLiteral("background:#8b929c; min-width:22px"));
        connect(col, &QToolButton::clicked, this, [this, col, color] {
            const QColor nc = QColorDialog::getColor(*color, this);
            if (nc.isValid()) { *color = nc; col->setStyleSheet(QStringLiteral("background:%1; min-width:22px").arg(nc.name())); }
        });
        auto* id = new QLineEdit;
        id->setPlaceholderText(tr("new kind id, e.g. secret"));
        auto* zh = new QLineEdit;
        zh->setPlaceholderText(QStringLiteral("zh_TW"));
        auto* en = new QLineEdit;
        en->setPlaceholderText(QStringLiteral("en"));
        auto* add = new QPushButton(tr("+ Add kind"));
        auto doAdd = [=] {
            const std::string k = id->text().trimmed().toStdString();
            if (!stage::nameOk(k)) { toast(tr("Any name works, Chinese too, except empty, spaces at the ends, or \" ' < > & | \\ /"), true); return; }
            if (m_p.S["kinds"].contains(k)) { toast(tr("%1 already exists").arg(q(k)), true); return; }
            const std::string z = zh->text().trimmed().toStdString(), e = en->text().trimmed().toStdString(), c = color->name().toStdString();
            edit(tr("New kind %1").arg(q(k)), [this, k, z, e, c] { m_p.S["kinds"][k] = Json{{"name", Json{{"zh_TW", z.empty() ? k : z}, {"en", e.empty() ? k : e}}}, {"color", c}}; });
        };
        connect(add, &QPushButton::clicked, this, doAdd);
        connect(id, &QLineEdit::returnPressed, this, doAdd);
        table->setCellWidget(r, 0, col);
        table->setCellWidget(r, 1, id);
        table->setCellWidget(r, 2, zh);
        table->setCellWidget(r, 3, en);
        table->setCellWidget(r, 4, add);
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    table->setMinimumHeight(260);
    v->addWidget(table, 1);
    v->addWidget(muted(tr("Renaming a kind updates every event of that kind. The floors' meta.eventKinds are notes left by the floor generator and are not changed. The game does not read kinds yet: trap and cache effects still come from words in the event id (06_EVENT_LOGIC.md).")));
    m_info->setText(tr("%1 kinds · %2 events").arg(kinds.size()).arg(m_p.S["events"].size()));
    return w;
}
