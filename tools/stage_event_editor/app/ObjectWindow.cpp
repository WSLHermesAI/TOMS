// ObjectWindow.cpp -- see ObjectWindow.h.
#include "ObjectWindow.h"

#include "SpriteBank.h"
#include "StageDraw.h"
#include "StageEditor.h"
#include "StageText.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QVBoxLayout>

using stage::Json;
using StageText::q;

namespace {
QPointer<ObjectWindow> g_open;

// A new type's id: lowercase letters, Chinese (or any other script's) characters, digits and _ -- it becomes a JSON key,
// part of "monster:<id>" and text keys ("item.<id>.name"), and an NPC's file name (npcs/<id>.json), so no spaces and none
// of : # . / , that those are joined with. Every file read and write here takes UTF-8 paths.
bool validTypeId(const std::string& id) {
    static const QRegularExpression re(QStringLiteral("^[\\p{Ll}\\p{Lo}\\p{Nd}_]+$"), QRegularExpression::UseUnicodePropertiesOption);
    return re.match(QString::fromStdString(id)).hasMatch();
}

std::string catOf(const std::string& sem) { return sem.substr(0, sem.find(':')); }
std::string idOf(const std::string& sem) { const size_t c = sem.find(':'); return c == std::string::npos ? std::string() : sem.substr(c + 1); }

QString verb(const std::string& cat) {
    if (cat == "monster") return ObjectWindow::tr("defeats this monster");
    if (cat == "item" || cat == "key") return ObjectWindow::tr("picks this up");
    if (cat == "door" || cat == "gate") return ObjectWindow::tr("opens this");
    if (cat == "npc") return ObjectWindow::tr("talks to this NPC");
    return ObjectWindow::tr("steps here");
}

QString catName(const std::string& cat) {
    if (cat == "monster") return ObjectWindow::tr("monster");
    if (cat == "item") return ObjectWindow::tr("item");
    if (cat == "story") return ObjectWindow::tr("door and key");
    return ObjectWindow::tr("NPC");
}

std::vector<std::string> copyChoices(stage::Project& p, const std::string& cat) {
    if (cat == "monster") return p.enemyIds();
    if (cat == "npc") { std::vector<std::string> out; for (const auto& [id, n] : p.npcs) { (void)n; out.push_back(id); } return out; }
    // "story": a key item that opens its own gate (the palette's doors, keys and gates)
    const std::string type = cat == "story" ? "story" : "consumable";
    std::vector<std::string> out;
    for (const std::string& id : p.itemIds()) if (p.itemType(id) == type) out.push_back(id);
    return out;
}
}  // namespace

ObjectWindow* ObjectWindow::current() { return g_open; }

ObjectWindow::ObjectWindow(StageEditor* ed, Kind kind) : QDialog(ed), m_ed(ed), m_kind(kind) {
    setAttribute(Qt::WA_DeleteOnClose);
    // A type's window follows the palette and leaves the editor usable (a tool window over it); an object's or a new
    // type's is answered first.
    if (kind == Type) {
        setWindowFlag(Qt::Tool);
        setWindowModality(Qt::NonModal);
    } else {
        setWindowModality(Qt::WindowModal);
    }
    resize(760, 640);
    auto* lay = new QVBoxLayout(this);
    m_head = new QLabel(this);
    m_head->setTextFormat(Qt::RichText);
    lay->addWidget(m_head);
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    lay->addWidget(m_tabs, 1);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) { if (i >= 0 && m_tabs->isVisible()) { m_tab = i; } });
    auto* foot = new QHBoxLayout();
    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color: #ff9f43"));
    foot->addWidget(m_note, 1);
    m_del = new QPushButton(this);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    m_apply = new QPushButton(this);
    m_apply->setDefault(true);
    foot->addWidget(m_del);
    foot->addWidget(cancel);
    foot->addWidget(m_apply);
    lay->addLayout(foot);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, &ObjectWindow::apply);
    connect(m_del, &QPushButton::clicked, this, &ObjectWindow::doDelete);
}

void ObjectWindow::openObject(StageEditor* ed, int x, int y) {
    stage::StageDoc* d = ed->curDoc();
    if (!d) return;
    const std::string sem = ed->project().semAt(*d, x, y);
    if (!stage::Project::isEntity(sem)) return;
    if (g_open) g_open->close();
    auto* w = new ObjectWindow(ed, Object);
    w->m_x = x;
    w->m_y = y;
    w->m_sem = sem;
    const Json* o = stage::Project::objOf(*d, x, y);
    w->m_w = Json{{"look", o && o->contains("look") ? (*o)["look"] : Json::object()},
                  {"events", o && o->contains("events") ? (*o)["events"] : Json::array()}};
    g_open = w;
    w->render();
    w->show();
}

void ObjectWindow::openType(StageEditor* ed, const std::string& sem) {
    if (!ed->project().typeRec(sem)) { ed->toast(tr("%1 has no record to edit.").arg(q(sem))); return; }
    if (g_open && g_open->m_kind == Type) {   // the open type window shows this one, where it is
        if (g_open->m_sem == sem) { g_open->raise(); return; }
        if (!g_open->confirmLeave()) return;
        g_open->loadType(sem);
        g_open->render();
        g_open->raise();
        return;
    }
    if (g_open) g_open->close();
    auto* w = new ObjectWindow(ed, Type);
    w->loadType(sem);
    g_open = w;
    w->render();
    w->show();
}

void ObjectWindow::loadType(const std::string& sem) {
    const Json* r = m_ed->project().typeRec(sem);
    m_sem = sem;
    m_cat = catOf(sem);
    m_id = idOf(sem);
    m_w = r ? *r : Json::object();
    m_texts.clear();
    if (r)
        for (const char* k : {"name", "desc", "greeting"})
            if (r->contains(k) && (*r)[k].is_string()) m_texts[k] = m_ed->project().textOf((*r)[k].get<std::string>());
    m_orig = m_w;
    m_origTexts = m_texts;
}

bool ObjectWindow::dirty() const { return m_kind == Type && (m_w != m_orig || m_texts != m_origTexts); }

bool ObjectWindow::confirmLeave() {
    if (!dirty()) return true;
    QMessageBox box(QMessageBox::Question, tr("Changes not applied"),
                    tr("%1 has changes that are not applied yet.").arg(q(m_ed->project().nameOf(m_sem))), QMessageBox::NoButton, this);
    QPushButton* applyB = box.addButton(tr("Apply"), QMessageBox::AcceptRole);
    QPushButton* discardB = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(applyB);
    box.exec();
    if (box.clickedButton() == applyB) { apply(); return true; }
    return box.clickedButton() == discardB;
}

void ObjectWindow::openNew(StageEditor* ed, const std::string& cat, const std::string& brush, const std::string& group) {
    if (g_open) g_open->close();
    auto* w = new ObjectWindow(ed, New);
    w->m_cat = cat;
    w->m_group = group;
    w->m_brushPrefix = !brush.empty() ? brush : cat == "story" ? std::string("gate:") : cat + ":";
    const std::vector<std::string> ids = copyChoices(ed->project(), cat);
    w->m_from = ids.empty() ? std::string() : ids[0];
    g_open = w;
    w->render();
    w->show();
}

// ---- the look being edited -------------------------------------------------------------------------------------------

Json ObjectWindow::look() const {
    if (m_kind == Object) return m_w["look"];
    Json l = m_w.contains("look") && m_w["look"].is_object() ? m_w["look"] : Json::object();
    if (m_w.contains("sprite") && m_w["sprite"].is_string()) l["sprite"] = m_w["sprite"];
    return l;
}

void ObjectWindow::setLook(const std::string& key, const Json& v) {
    if (m_kind == Object) {
        if (v.is_null()) m_w["look"].erase(key);
        else m_w["look"][key] = v;
        return;
    }
    // a type: its sprite is the record's "sprite" ("slime.png" for monsters and items); animation and tint go in "look"
    if (key == "sprite") {
        if (v.is_null()) m_w.erase("sprite");
        else m_w["sprite"] = m_cat == "npc" ? v : Json(v.get<std::string>() + ".png");
        return;
    }
    if (v.is_null()) {
        if (m_w.contains("look")) { m_w["look"].erase(key); if (m_w["look"].empty()) m_w.erase("look"); }
    } else {
        if (!m_w.contains("look") || !m_w["look"].is_object()) m_w["look"] = Json::object();
        m_w["look"][key] = v;
    }
}

void ObjectWindow::setLookSprite(const std::string& sprite) {
    setLook("sprite", sprite.empty() ? Json() : Json(sprite));
    render();
}

void ObjectWindow::linkEvent(const std::string& id) {
    if (m_kind != Object || id.empty()) return;
    if (!m_ed->project().event(id)) { m_ed->toast(tr("No event %1.").arg(q(id)), true); return; }
    Json& evs = m_w["events"];
    if (std::find(evs.begin(), evs.end(), Json(id)) == evs.end()) evs.push_back(id);
    render();
}

void ObjectWindow::setNewId(const std::string& id, const std::string& from) {
    m_id = id;
    if (!from.empty()) m_from = from;
}

// ---- rendering -----------------------------------------------------------------------------------------------------

void ObjectWindow::render() {
    stage::Project& p = m_ed->project();
    const int keepTab = m_tab;
    m_tabs->blockSignals(true);
    while (m_tabs->count()) { QWidget* w = m_tabs->widget(0); m_tabs->removeTab(0); w->deleteLater(); }
    m_note->clear();
    m_del->setEnabled(true);
    m_del->setToolTip(QString());
    if (m_kind == Object) {
        setWindowTitle(tr("Object settings"));
        m_head->setText(QStringLiteral("<b>%1</b> <span style='color:#8b929c'>%2 · %3 · %4</span>")
                            .arg(q(p.nameOf(m_sem)).toHtmlEscaped(), q(m_sem).toHtmlEscaped(), tr("Cell %1, %2").arg(m_x).arg(m_y), q(m_ed->curId())));
        m_del->setText(tr("Reset (type look, no events)"));
        m_apply->setText(tr("Apply"));
        m_tabs->addTab(lookTab(), tr("Look"));
        m_tabs->addTab(eventsTab(), tr("Events"));
    } else if (m_kind == Type) {
        setWindowTitle(tr("Type details"));
        const std::string file = m_cat == "monster" ? stage::Project::enemiesPath() : m_cat == "item" ? stage::Project::itemsPath() : stage::Project::npcPath(m_id);
        m_head->setText(QStringLiteral("<b>%1</b> <span style='color:#8b929c'>%2 · %3</span>")
                            .arg(q(p.nameOf(m_sem)).toHtmlEscaped(), q(m_sem).toHtmlEscaped(), q(file)));
        m_del->setText(tr("Delete this type"));
        m_apply->setText(tr("Apply"));
        const std::vector<std::string> uses = p.typeUses(m_sem);
        if (!uses.empty()) {
            QStringList f;
            for (const std::string& u : uses) f << q(u);
            const QString msg = tr("Placed on %1: remove it from those floors first.").arg(f.join(QStringLiteral(", ")));
            m_del->setEnabled(false);
            m_del->setToolTip(msg);
            m_note->setText(msg);
        }
        m_tabs->addTab(lookTab(), tr("Look"));
        m_tabs->addTab(dataTab(), tr("Data"));
    } else {
        setWindowTitle(tr("New %1 type").arg(catName(m_cat)));
        m_head->setText(QStringLiteral("<b>%1</b>").arg(tr("New %1 type").arg(catName(m_cat))));
        m_del->setVisible(false);
        m_apply->setText(tr("Create"));
        m_tabs->addTab(newForm(), tr("New"));
    }
    m_tabs->tabBar()->setVisible(m_kind != New);
    m_tab = std::min(keepTab, m_tabs->count() - 1);
    m_tabs->setCurrentIndex(m_tab);
    m_tabs->blockSignals(false);
}

QWidget* ObjectWindow::lookTab() {
    stage::Project& p = m_ed->project();
    SpriteBank& bank = m_ed->bank();
    const Json lk = look();
    const Json base = m_kind == Object ? p.typeLook(m_sem) : Json();
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);

    // the preview, as the game draws it: its type's look, then its own
    auto* top = new QHBoxLayout();
    QImage prev(96, 96, QImage::Format_ARGB32_Premultiplied);
    prev.fill(QColor(0x2b, 0x2b, 0x33));
    {
        QPainter g(&prev);
        StageDraw::drawSem(g, p, bank, "floor", QRectF(0, 0, 96, 96));
        Json merged = base.is_object() ? base : Json::object();
        for (auto& [k, val] : lk.items()) merged[k] = val;
        StageDraw::drawSem(g, p, bank, m_sem, QRectF(0, 0, 96, 96), merged);
    }
    auto* pv = new QLabel;
    pv->setPixmap(QPixmap::fromImage(prev));
    pv->setFrameShape(QFrame::Box);
    top->addWidget(pv);
    auto* note = new QLabel((m_kind == Object ? tr("This one object only: empty fields use its type's look.")
                                              : tr("Every object of this type, unless an object sets its own (✎ Edit)."))
                            + QStringLiteral("\n") + tr("The tint multiplies the sprite's colours (white = unchanged)."));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #8b929c"));
    top->addWidget(note, 1);
    v->addLayout(top);

    // sprite
    const QString sp = lk.contains("sprite") ? StageDraw::spriteName(lk["sprite"]) : QString();
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Picture")));
    QString shown = sp;
    if (shown.isEmpty()) {
        const QString def = base.is_object() && base.contains("sprite") ? StageDraw::spriteName(base["sprite"]) : q(p.spriteOf(m_sem));
        shown = tr("type's: %1").arg(def.isEmpty() ? QStringLiteral("—") : def);
    }
    auto* cur = new QLabel(QStringLiteral("<b>%1</b>").arg(shown.toHtmlEscaped()));
    row->addWidget(cur);
    if (!sp.isEmpty()) {
        auto* clear = new QPushButton(m_kind == Object ? tr("Use the type's") : tr("None"));
        connect(clear, &QPushButton::clicked, this, [this] { setLookSprite(std::string()); });
        row->addWidget(clear);
    }
    row->addStretch(1);
    auto* find = new QLineEdit(m_spriteFilter);
    find->setPlaceholderText(tr("find a sprite"));
    find->setClearButtonEnabled(true);
    find->setMaximumWidth(180);
    row->addWidget(find);
    v->addLayout(row);
    auto* pick = new QLabel(tr("The game's pictures: click one to draw this with it."));
    pick->setStyleSheet(QStringLiteral("color: #8b929c"));
    v->addWidget(pick);
    auto* grid = new QListWidget;
    grid->setViewMode(QListView::IconMode);
    grid->setIconSize(QSize(32, 32));
    grid->setGridSize(QSize(72, 56));
    grid->setResizeMode(QListView::Adjust);
    grid->setMovement(QListView::Static);
    grid->setMinimumHeight(180);
    for (const std::string& n : p.sprites) {
        const QString name = q(n);
        auto* it = new QListWidgetItem(QIcon(QPixmap::fromImage(bank.image(name).scaled(32, 32))), name);
        it->setData(Qt::UserRole, name);
        it->setToolTip(name);
        grid->addItem(it);
        if (name == sp) it->setSelected(true);
    }
    auto filter = [grid](const QString& t) {
        for (int i = 0; i < grid->count(); i++) grid->item(i)->setHidden(!t.isEmpty() && !grid->item(i)->text().contains(t, Qt::CaseInsensitive));
    };
    filter(m_spriteFilter);
    connect(find, &QLineEdit::textChanged, this, [this, filter](const QString& t) { m_spriteFilter = t.trimmed(); filter(m_spriteFilter); });
    connect(grid, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { setLookSprite(it->data(Qt::UserRole).toString().toStdString()); });
    v->addWidget(grid, 1);

    // animation: "anim/<file>.anim#<clip>"
    auto* arow = new QHBoxLayout();
    arow->addWidget(new QLabel(tr("Animation")));
    auto* anim = new QComboBox;
    const std::string curAnim = lk.contains("anim") && lk["anim"].is_string() ? lk["anim"].get<std::string>() : std::string();
    anim->addItem(tr("(none: the sprite)"), QString());
    bool found = curAnim.empty();
    for (const auto& [file, clips] : p.anims) {
        anim->addItem(q(file), QStringLiteral("#header"));
        if (auto* model = qobject_cast<QStandardItemModel*>(anim->model())) model->item(anim->count() - 1)->setEnabled(false);
        for (const std::string& c : clips) {
            const std::string ref = file + "#" + c;
            anim->addItem(QStringLiteral("    ") + q(c), q(ref));
            if (ref == curAnim) { anim->setCurrentIndex(anim->count() - 1); found = true; }
        }
    }
    if (!found) { anim->addItem(QStringLiteral("⛔ ") + q(curAnim), q(curAnim)); anim->setCurrentIndex(anim->count() - 1); }
    connect(anim, &QComboBox::currentIndexChanged, this, [this, anim](int i) {
        const QString ref = anim->itemData(i).toString();
        if (ref == QLatin1String("#header")) return;
        setLook("anim", ref.isEmpty() ? Json() : Json(ref.toStdString()));
        render();
    });
    arow->addWidget(anim, 1);
    if (base.is_object() && base.contains("anim") && curAnim.empty())
        arow->addWidget(new QLabel(tr("type's: %1").arg(q(base["anim"].get<std::string>()))));
    v->addLayout(arow);

    // tint
    float t[4];
    const bool tinted = StageDraw::lookTint(lk, t);
    auto* trow = new QHBoxLayout();
    trow->addWidget(new QLabel(tr("Colour")));
    auto* use = new QCheckBox(tr("tint"));
    use->setChecked(tinted);
    connect(use, &QCheckBox::toggled, this, [this](bool on) {
        setLook("tint", on ? Json::array({1, 0.6, 0.6, 1}) : Json());
        render();
    });
    trow->addWidget(use);
    if (tinted) {
        const QColor c = QColor::fromRgbF(t[0], t[1], t[2]);
        auto* pick = new QPushButton(c.name());
        pick->setStyleSheet(QStringLiteral("background:%1; color:%2").arg(c.name(), c.lightness() > 128 ? "#000" : "#fff"));
        connect(pick, &QPushButton::clicked, this, [this, c, t] {
            const QColor n = QColorDialog::getColor(c, this, tr("Colour"));
            if (!n.isValid()) return;
            auto r3 = [](double x) { return std::round(x * 1000) / 1000; };
            setLook("tint", Json::array({r3(n.redF()), r3(n.greenF()), r3(n.blueF()), t[3]}));
            render();
        });
        trow->addWidget(pick);
        trow->addWidget(new QLabel(tr("opacity")));
        auto* alpha = new QSlider(Qt::Horizontal);
        alpha->setRange(0, 20);
        alpha->setValue(int(std::lround(t[3] * 20)));
        alpha->setMaximumWidth(140);
        auto* av = new QLabel(QStringLiteral("%1%").arg(std::lround(t[3] * 100)));
        connect(alpha, &QSlider::valueChanged, this, [this, av](int val) {
            Json tint = look()["tint"];
            tint[3] = val / 20.0;
            setLook("tint", tint);
            av->setText(QStringLiteral("%1%").arg(val * 5));
        });
        connect(alpha, &QSlider::sliderReleased, this, [this] { render(); });
        trow->addWidget(alpha);
        trow->addWidget(av);
    }
    trow->addStretch(1);
    v->addLayout(trow);
    return w;
}

QWidget* ObjectWindow::eventsTab() {
    stage::Project& p = m_ed->project();
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* intro = new QLabel(tr("Linked events fire when the player %1 (a talk event becomes a topic).").arg(verb(catOf(m_sem))));
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color: #8b929c"));
    v->addWidget(intro);
    Json& evs = m_w["events"];
    if (evs.empty()) v->addWidget(new QLabel(tr("No linked events.")));
    for (const Json& e : evs) {
        const std::string id = e.get<std::string>();
        const stage::Event* ev = p.event(id);
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel(ev ? QStringLiteral("%1 · %2").arg(q(id), q(ev->kind)) : QStringLiteral("%1 ⛔").arg(q(id)));
        if (!ev) lbl->setStyleSheet(QStringLiteral("color: #ff9b9b"));
        row->addWidget(lbl, 1);
        auto* go = new QPushButton(QStringLiteral("↗"));
        go->setToolTip(tr("Open in the event editor"));
        connect(go, &QPushButton::clicked, this, [this, id] { Q_EMIT m_ed->openEventEditor(q(id)); });
        auto* un = new QPushButton(QStringLiteral("✕"));
        un->setToolTip(tr("Unlink"));
        connect(un, &QPushButton::clicked, this, [this, id] {
            Json& l = m_w["events"];
            l.erase(std::remove(l.begin(), l.end(), Json(id)), l.end());
            render();
        });
        row->addWidget(go);
        row->addWidget(un);
        v->addLayout(row);
    }
    auto* add = new QHBoxLayout();
    add->addWidget(new QLabel(tr("Link an event")));
    auto* box = new QComboBox;
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);
    for (const stage::Event& e : p.events) {
        if (std::find(evs.begin(), evs.end(), Json(e.id)) != evs.end()) continue;
        const QString title = q(p.localized(Json(e.title)));
        box->addItem(q(e.id) + (title.isEmpty() || title == q(e.title) ? QString() : QStringLiteral("  —  ") + title), q(e.id));
    }
    box->setCurrentIndex(-1);
    box->lineEdit()->setPlaceholderText(tr("event id"));
    box->completer()->setFilterMode(Qt::MatchContains);
    box->completer()->setCompletionMode(QCompleter::PopupCompletion);
    auto* btn = new QPushButton(tr("+ Link"));
    auto doAdd = [this, box] {
        QString t = box->currentData().toString();
        if (t.isEmpty() || box->currentText() != box->itemText(box->currentIndex())) t = box->currentText().section(QStringLiteral("  —  "), 0, 0).trimmed();
        linkEvent(t.toStdString());
    };
    connect(btn, &QPushButton::clicked, this, doAdd);
    add->addWidget(box, 1);
    add->addWidget(btn);
    v->addLayout(add);
    v->addStretch(1);
    return w;
}

QWidget* ObjectWindow::dataTab() {
    stage::Project& p = m_ed->project();
    auto* w = new QWidget;
    auto* form = new QFormLayout(w);
    auto num = [&](const char* key, const QString& label) {
        auto* sb = new QSpinBox;
        sb->setRange(-999999, 999999);
        sb->setValue(m_w.contains(key) && m_w[key].is_number() ? m_w[key].get<int>() : 0);
        const std::string k = key;
        connect(sb, &QSpinBox::valueChanged, this, [this, k](int val) { m_w[k] = val; });
        form->addRow(label, sb);
    };
    // a text: zh_TW and en; a monster's name is in its record, an item's / NPC's is a text.json key
    auto text = [&](const char* key, const QString& label) {
        const std::string k = key;
        const Json val = m_cat == "monster" && k == "name" ? (m_w.contains("name") && m_w["name"].is_object() ? m_w["name"] : Json::object())
                                                           : (m_texts.count(k) ? m_texts[k] : Json::object());
        auto* row = new QHBoxLayout();
        for (const char* l : {"zh_TW", "en"}) {
            auto* e = new QLineEdit(val.contains(l) && val[l].is_string() ? q(val[l].get<std::string>()) : QString());
            e->setPlaceholderText(QString::fromLatin1(l));
            const std::string lang = l;
            connect(e, &QLineEdit::textEdited, this, [this, k, lang](const QString& s) {
                if (m_cat == "monster" && k == "name") {
                    if (!m_w.contains("name") || !m_w["name"].is_object()) m_w["name"] = Json::object();
                    m_w["name"][lang] = s.toStdString();
                    return;
                }
                if (!m_w.contains(k) || !m_w[k].is_string()) m_w[k] = (m_cat == "npc" ? "npc." : "item.") + m_id + "." + k;
                m_texts[k][lang] = s.toStdString();
            });
            row->addWidget(e);
        }
        form->addRow(label, row);
    };
    if (m_cat == "monster") {
        text("name", tr("Name"));
        num("hp", tr("HP"));
        num("atk", tr("ATK"));
        num("def", tr("DEF"));
        num("exp", tr("EXP"));
        num("gold", tr("Gold"));
    }
    if (m_cat == "item") {
        text("name", tr("Name"));
        text("desc", tr("Description"));
        auto* type = new QComboBox;
        for (const char* t : {"consumable", "story", "key"}) type->addItem(QString::fromLatin1(t));
        type->setCurrentText(q(m_w.value("type", std::string("consumable"))));
        connect(type, &QComboBox::currentTextChanged, this, [this](const QString& t) { m_w["type"] = t.toStdString(); });
        form->addRow(tr("Type"), type);
        num("price", tr("Price"));
        auto* imp = new QCheckBox;
        imp->setChecked(m_w.value("important", false));
        connect(imp, &QCheckBox::toggled, this, [this](bool on) { m_w["important"] = on; });
        form->addRow(tr("Important"), imp);
        auto* stats = new QWidget;
        auto* sv = new QVBoxLayout(stats);
        sv->setContentsMargins(0, 0, 0, 0);
        if (!m_w.contains("stats") || !m_w["stats"].is_object()) m_w["stats"] = Json::object();
        std::vector<std::string> keys;
        for (auto& [k, val] : m_w["stats"].items()) { (void)val; keys.push_back(k); }
        for (const std::string& k : keys) {
            auto* row = new QHBoxLayout();
            auto* key = new QComboBox;
            std::vector<std::string> names = {"hp", "maxhp", "atk", "def", "gold", "exp", "key_yellow", "key_blue", "key_red"};
            if (std::find(names.begin(), names.end(), k) == names.end()) names.push_back(k);
            for (const std::string& n : names) key->addItem(q(n));
            key->setCurrentText(q(k));
            connect(key, &QComboBox::currentTextChanged, this, [this, k](const QString& nk) {
                const Json val = m_w["stats"][k];
                m_w["stats"].erase(k);
                m_w["stats"][nk.toStdString()] = val;
                render();
            });
            auto* val = new QSpinBox;
            val->setRange(-99999, 99999);
            val->setValue(m_w["stats"][k].is_number() ? m_w["stats"][k].get<int>() : 0);
            connect(val, &QSpinBox::valueChanged, this, [this, k](int x) { m_w["stats"][k] = x; });
            auto* del = new QPushButton(QStringLiteral("✕"));
            connect(del, &QPushButton::clicked, this, [this, k] { m_w["stats"].erase(k); render(); });
            row->addWidget(key);
            row->addWidget(val);
            row->addWidget(del);
            row->addStretch(1);
            sv->addLayout(row);
        }
        auto* add = new QPushButton(tr("+ stat"));
        connect(add, &QPushButton::clicked, this, [this] {
            std::string k = "hp";
            for (const char* c : {"hp", "atk", "def", "gold", "exp"}) if (!m_w["stats"].contains(c)) { k = c; break; }
            m_w["stats"][k] = 1;
            render();
        });
        sv->addWidget(add, 0, Qt::AlignLeft);
        form->addRow(tr("Effect"), stats);
    }
    if (m_cat == "npc") {
        text("name", tr("Name"));
        text("greeting", tr("Greeting"));
        auto* note = new QLabel(tr("Its chat lines, topics and dialogues are in the event editor's NPCs tab."));
        note->setWordWrap(true);
        form->addRow(note);
        auto* go = new QPushButton(tr("Edit in the event editor →"));
        connect(go, &QPushButton::clicked, this, [this] { Q_EMIT m_ed->openNpcInEventEditor(q(m_id)); });
        form->addRow(go);
    }
    (void)p;
    return w;
}

QWidget* ObjectWindow::newForm() {
    auto* w = new QWidget;
    auto* form = new QFormLayout(w);
    auto* id = new QLineEdit(q(m_id));
    id->setPlaceholderText(m_cat == "monster" ? QStringLiteral("slime_red") : m_cat == "item" ? QStringLiteral("potion_green")
                           : m_cat == "story" ? QStringLiteral("crypt_key") : QStringLiteral("guard"));
    connect(id, &QLineEdit::textEdited, this, [this](const QString& s) { m_id = s.trimmed().toStdString(); });
    form->addRow(tr("Id"), id);
    auto* from = new QComboBox;
    for (const std::string& k : copyChoices(m_ed->project(), m_cat)) from->addItem(q(k));
    from->setCurrentText(q(m_from));
    connect(from, &QComboBox::currentTextChanged, this, [this](const QString& s) { m_from = s.toStdString(); });
    form->addRow(tr("Copy from"), from);
    QString note;
    if (m_cat == "monster") note = tr("A new entry in enemies.json, copied from the one picked (stats, sprite); then set its name, stats and look.");
    else if (m_cat == "item") note = tr("A new entry in items.json, copied from the one picked (effect, sprite); its name goes to text.json.");
    else if (m_cat == "story") note = tr("A new key item in items.json (type \"story\"), copied from the one picked. Its gate -- a door only this key opens -- appears in the Gates group; put both on the map.");
    else note = tr("A new NPC file (npcs/<id>.json) with a one-page dialogue (dialogue/<id>.json); write its chat lines and topics in the event editor.");
    auto* n = new QLabel(note);
    n->setWordWrap(true);
    n->setStyleSheet(QStringLiteral("color: #8b929c"));
    form->addRow(n);
    return w;
}

// ---- apply / delete --------------------------------------------------------------------------------------------------

void ObjectWindow::apply() {
    stage::Project& p = m_ed->project();
    if (m_kind == Object) {
        Json o = Json::object();
        if (!m_w["look"].empty()) o["look"] = m_w["look"];
        if (!m_w["events"].empty()) o["events"] = m_w["events"];
        const std::string key = std::to_string(m_x) + "," + std::to_string(m_y);
        m_ed->edit(tr("Object settings: %1").arg(q(p.nameOf(m_sem))), [&](stage::StageDoc& d) {
            if (const Json* old = stage::Project::objOf(d, m_x, m_y); old && old->contains("uid"))
                o["uid"] = (*old)["uid"];   // its save id stays (docs/28_SAVE_SYSTEM.md)
            if (!d.st.contains("objects") || !d.st["objects"].is_object()) d.st["objects"] = Json::object();
            if (!o.empty()) d.st["objects"][key] = o;
            else d.st["objects"].erase(key);
            if (d.st["objects"].empty()) d.st.erase("objects");
        });
        close();
        return;
    }
    if (m_kind == New) {
        const std::string id = m_id;
        if (!validTypeId(id)) { m_ed->toast(tr("Use lowercase letters, Chinese characters, 0-9 and _ only (no spaces)."), true); return; }
        const bool story = m_cat == "story";   // a door and its key: an item of type "story"
        const std::string cat = story ? std::string("item") : m_cat;
        const std::string sem = cat + ":" + id;
        if (p.typeRec(sem) || (m_cat == "npc" && p.npcs.count(id))) { m_ed->toast(tr("%1 already exists.").arg(q(id)), true); return; }
        const Json* srcP = p.typeRec(cat + ":" + m_from);
        Json src = srcP ? *srcP : Json::object();
        src.erase("group");   // its category is the + that made it, not the copied one's
        const Json idText = Json{{"zh_TW", id}, {"en", id}};
        if (m_cat == "monster") {
            src["name"] = idText;
            p.dataFiles[stage::Project::enemiesPath()].obj[id] = src;
        } else if (cat == "item") {
            if (story) {   // a door and its key: the key opens its gate ("opens"), shown in Doors and Keys
                src["type"] = "story";
                src["opens"] = "gate:" + id;
            }
            src["name"] = "item." + id + ".name";
            src["desc"] = "item." + id + ".desc";
            p.textEdits["item." + id + ".name"] = idText;
            p.textEdits["item." + id + ".desc"] = Json{{"zh_TW", ""}, {"en", ""}};
            p.dataFiles[stage::Project::itemsPath()].obj[id] = src;
        } else {   // a new NPC: its file, and a first chat line (a one-page dialogue to rewrite in the event editor)
            Json npc = Json::object();
            npc["npcId"] = id;
            npc["name"] = "npc." + id + ".name";
            npc["chat"] = Json{{"lines", Json::array({Json{{"dialogue", id}}})}, {"order", "loop"}};
            npc["topics"] = Json{{"menu", "auto"}};
            if (src.contains("sprite")) npc["sprite"] = src["sprite"];
            if (src.contains("look")) npc["look"] = src["look"];
            p.textEdits["npc." + id + ".name"] = idText;
            stage::DataFile df;
            df.obj = npc;
            p.dataFiles[stage::Project::npcPath(id)] = df;
            const std::string dlg = stage::Project::dialoguePath(id);
            if (!p.dataFiles.count(dlg) && !QFileInfo::exists(q(p.abs(dlg)))) {   // never over a dialogue already there
                stage::DataFile dd;
                dd.obj = Json{{"npcId", id}, {"start", "root"},
                              {"nodes", Json{{"root", Json{{"text", Json{{"zh_TW", "……"}, {"en", "..."}}},
                                                           {"choices", Json::array({Json{{"label", Json{{"zh_TW", "……"}, {"en", "..."}}}, {"next", nullptr}}})}}}}}};
                p.dataFiles[dlg] = dd;
            }
        }
        // painted with right away, shown in the palette group whose + made it, its details open on Data
        if (!m_group.empty()) p.setGroup(sem, m_group);
        m_ed->typesChanged();
        m_ed->setBrush(m_brushPrefix + id);
        m_ed->revealBrush();
        m_ed->toast(tr("%1 created: set its data and look here, then Save.").arg(q(id)));
        close();
        openType(m_ed, sem);
        if (ObjectWindow* w = g_open; w && w->m_kind == Type) {
            w->m_tab = 1;   // Data
            w->render();
        }
        return;
    }
    // a type: the working copy replaces the record; changed names go to text.json
    const std::string path = m_cat == "monster" ? stage::Project::enemiesPath() : m_cat == "item" ? stage::Project::itemsPath() : stage::Project::npcPath(m_id);
    if (m_cat == "npc") p.dataFiles[path].obj = m_w;
    else p.dataFiles[path].obj[m_id] = m_w;
    for (const auto& [k, v] : m_texts) {
        if (!m_w.contains(k) || !m_w[k].is_string()) continue;
        Json merged = p.textOf(m_w[k].get<std::string>());
        for (auto& [l, t] : v.items()) merged[l] = t;
        p.textEdits[m_w[k].get<std::string>()] = merged;
    }
    m_orig = m_w;
    m_origTexts = m_texts;
    m_ed->typesChanged();
    m_ed->toast(tr("%1 changed (Save writes it).").arg(q(m_id)));
    render();   // stays open: the palette's details window
}

void ObjectWindow::doDelete() {
    stage::Project& p = m_ed->project();
    if (m_kind == Object) {   // "Reset": back to the type's look, no events
        m_w = Json{{"look", Json::object()}, {"events", Json::array()}};
        render();
        return;
    }
    if (m_kind != Type || !p.typeUses(m_sem).empty()) return;
    if (QMessageBox::question(this, tr("Delete this type"), tr("Delete the type %1?").arg(q(m_id))) != QMessageBox::Yes) return;
    if (m_cat == "monster") p.dataFiles[stage::Project::enemiesPath()].obj.erase(m_id);
    else if (m_cat == "item") p.dataFiles[stage::Project::itemsPath()].obj.erase(m_id);
    else p.dataFiles[stage::Project::npcPath(m_id)].deleted = true;
    if (m_ed->view().brush == m_sem) m_ed->setBrush("wall");
    m_ed->typesChanged();
    m_ed->toast(tr("%1 deleted (Save writes it).").arg(q(m_id)));
    close();
}
