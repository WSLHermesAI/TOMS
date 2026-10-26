// bed_window.cpp -- see bed_window.h.
#include "bed_window.h"

#include "battle_party.h"
#include "bgfx_viewport.h"
#include "game.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

using bed::ojson;

// The game view at a chosen shape (landscape, portrait, a tall phone), centred in the space it has. The viewport is a
// fixed child (its native window never moves to another parent), only its size changes.
class AspectBox : public QWidget {
public:
    explicit AspectBox(QWidget* parent = nullptr) : QWidget(parent) {}
    void setChild(QWidget* w) { child_ = w; w->setParent(this); place(); }
    void setAspect(double wOverH) { aspect_ = wOverH; place(); }
protected:
    void resizeEvent(QResizeEvent*) override { place(); }
private:
    void place() {
        if (!child_) return;
        if (aspect_ <= 0.0) { child_->setGeometry(rect()); return; }   // fill
        int w = width(), h = (int)(w / aspect_);
        if (h > height()) { h = height(); w = (int)(h * aspect_); }
        child_->setGeometry((width() - w) / 2, (height() - h) / 2, w, h);
    }
    QWidget* child_ = nullptr;
    double aspect_ = 0.0;
};

namespace {

QString readText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool writeText(const QString& path, const QString& text) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray b = text.toUtf8();
    return f.write(b) == b.size();
}

bool parse(const QString& text, ojson& out) {
    out = ojson::parse(text.toStdString(), nullptr, false);
    return !out.is_discarded();
}

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }

QString nameOf(const ojson& m) {
    if (m.contains("name") && m["name"].is_object()) return qs(m["name"].value("en", m["name"].value("zh_TW", std::string())));
    return QString();
}

}  // namespace

BattleEditorWindow::BattleEditorWindow() {
    setWindowTitle(tr("TOMS Battle Editor"));
    resize(1600, 940);
    mNumbers_ = new QMap<QString, QSpinBox*>();
    wNumbers_ = new QMap<QString, QSpinBox*>();
    pAttrs_ = new QMap<QString, QSpinBox*>();

    view_ = new BgfxViewport();
    dataDir_ = QDir::cleanPath(QDir(view_->assetDir()).filePath(QStringLiteral("../data")));
    setupsDir_ = QDir::cleanPath(QDir(view_->assetDir()).filePath(QStringLiteral("../../tools/battle_editor/setups")));
    fxFile_ = QDir(view_->assetDir()).filePath(QStringLiteral("fx/battle.particle"));
    scratchDir_ = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("toms_battle_editor"));
    QDir().mkpath(QDir(scratchDir_).filePath(QStringLiteral("current")));
    QDir().mkpath(setupsDir_);
    loadData();

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(buildMonsterList());
    split->addWidget(buildCentre());
    tabs_ = new QTabWidget();
    tabs_->addTab(buildMonsterTab(), tr("Monster"));
    tabs_->addTab(buildWeaponTab(), tr("Weapon"));
    tabs_->addTab(buildPlayerTab(), tr("Player"));
    tabs_->addTab(buildEventsTab(), tr("Events"));
    tabs_->addTab(buildLogTab(), tr("Log"));
    split->addWidget(tabs_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({240, 860, 480});

    auto* central = new QWidget();
    auto* v = new QVBoxLayout(central);
    v->setContentsMargins(4, 4, 4, 4);
    v->addWidget(split, 1);
    v->addWidget(buildSetupsBar());
    setCentralWidget(central);
    statusBar()->showMessage(tr("Tick up to three monsters (+ Battle), then ▶ Battle."));
    connect(view_, &BgfxViewport::statusMessage, this, [this](const QString& t) { statusBar()->showMessage(t, 4000); });

    fillMonsterList();
    if (monsters_->count() > 0) {
        monsters_->setCurrentRow(0);
        setInBattle(monsters_->item(0)->data(Qt::UserRole).toString(), true);
    }
    if (weapons_->count() > 0) weapons_->setCurrentRow(0);
    fillSetups();
    logTimer_.setInterval(250);
    connect(&logTimer_, &QTimer::timeout, this, &BattleEditorWindow::refreshLog);
    logTimer_.start();
}

// ---------------------------------------------------------------- the data files

bool BattleEditorWindow::loadData() {
    bool ok = parse(readText(QDir(dataDir_).filePath(QStringLiteral("enemies.json"))), enemies_) && enemies_.is_object();
    if (!ok) { enemies_ = ojson::object(); warn(tr("enemies.json"), tr("data/enemies.json could not be read.")); }
    if (!parse(readText(QDir(dataDir_).filePath(QStringLiteral("weapon_play.json"))), weaponPlay_) || !weaponPlay_.is_object()) {
        weaponPlay_ = ojson::object();
        warn(tr("weapon_play.json"), tr("data/weapon_play.json could not be read."));
        ok = false;
    }
    ojson stats;
    attrIds_.clear();
    if (parse(readText(QDir(dataDir_).filePath(QStringLiteral("stats.json"))), stats) && stats.contains("attributes"))
        for (const auto& a : stats["attributes"])
            if (a.contains("id")) attrIds_ << qs(a["id"].get<std::string>());
    ojson fx;
    effects_.clear();
    if (parse(readText(fxFile_), fx) && fx.contains("effects"))
        for (const auto& e : fx["effects"])
            if (e.contains("name")) effects_ << qs(e["name"].get<std::string>());
    monstersDirty_ = weaponsDirty_ = false;
    return ok;
}

bool BattleEditorWindow::saveMonsters() {
    const QString path = QDir(dataDir_).filePath(QStringLiteral("enemies.json"));
    if (!writeText(path, qs(bed::dumpCompact(enemies_)))) { warn(tr("Save"), tr("Could not write %1").arg(path)); return false; }
    monstersDirty_ = false;
    say(tr("Saved data/enemies.json"));
    return true;
}

bool BattleEditorWindow::saveWeapons() {
    const QString path = QDir(dataDir_).filePath(QStringLiteral("weapon_play.json"));
    if (!writeText(path, qs(weaponPlay_.dump(2, ' ', false) + "\n"))) { warn(tr("Save"), tr("Could not write %1").arg(path)); return false; }
    weaponsDirty_ = false;
    say(tr("Saved data/weapon_play.json (the next battle uses it)"));
    return true;
}

bool BattleEditorWindow::saveGrowthTable() {
    const QString path = QDir(dataDir_).filePath(QStringLiteral("stats.json"));
    const QString text = readText(path);
    ojson stats;
    if (!parse(text, stats)) { warn(tr("Save"), tr("data/stats.json could not be read.")); return false; }
    const ojson old = stats.contains("levelUp") ? stats["levelUp"].value("points", ojson::array()) : ojson::array();
    const std::vector<int> per = calculate(cTotal_->value(), cLevel_->value(), cShape_->value());
    const ojson rows = bed::pointsRows(per, cLevel_->value(), old);
    std::string t = ss(text);
    if (!bed::replaceLevelPoints(t, rows)) { warn(tr("Save"), tr("data/stats.json has no levelUp \"points\" table.")); return false; }
    if (!ask(tr("Save the growth table"), tr("Replace data/stats.json's levelUp points with these %1 rows?\n\n%2")
                                                .arg(rows.size()).arg(qs(rows.dump()))))
        return false;
    if (!writeText(path, qs(t))) { warn(tr("Save"), tr("Could not write %1").arg(path)); return false; }
    say(tr("Saved data/stats.json levelUp points (%1 rows)").arg(rows.size()));
    return true;
}

// ---------------------------------------------------------------- the monster list

QWidget* BattleEditorWindow::buildMonsterList() {
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(tr("<b>MONSTERS</b>"));
    inBattleLabel_ = new QLabel();
    search_ = new QLineEdit();
    search_->setPlaceholderText(tr("search…"));
    monsters_ = new QListWidget();
    monsters_->setToolTip(tr("Tick + Battle for the monsters in the test battle (up to three, in the order ticked)."));
    v->addWidget(title);
    v->addWidget(inBattleLabel_);
    v->addWidget(search_);
    v->addWidget(monsters_, 1);
    auto* row = new QHBoxLayout();
    auto* add = new QPushButton(tr("New"));
    auto* dup = new QPushButton(tr("Duplicate"));
    auto* del = new QPushButton(tr("Delete"));
    row->addWidget(add);
    row->addWidget(dup);
    row->addWidget(del);
    v->addLayout(row);
    auto* again = new QPushButton(tr("+ Again"));
    again->setToolTip(tr("The selected monster once more in the battle (e.g. three bats)."));
    v->addWidget(again);
    connect(again, &QPushButton::clicked, this, [this] { if (!current_.isEmpty()) addToBattle(current_); });
    auto* row2 = new QHBoxLayout();
    auto* save = new QPushButton(tr("Save monsters"));
    auto* revert = new QPushButton(tr("Revert"));
    row2->addWidget(save);
    row2->addWidget(revert);
    v->addLayout(row2);

    connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) {
        for (int i = 0; i < monsters_->count(); i++) {
            QListWidgetItem* it = monsters_->item(i);
            it->setHidden(!t.isEmpty() && !it->text().contains(t, Qt::CaseInsensitive));
        }
    });
    connect(monsters_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it) {
        if (it) showMonster(it->data(Qt::UserRole).toString());
    });
    connect(monsters_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (filling_) return;
        const QString id = it->data(Qt::UserRole).toString();
        const bool on = it->checkState() == Qt::Checked;
        if (on != inBattle_.contains(id) && !setInBattle(id, on)) {
            filling_ = true;
            it->setCheckState(Qt::Unchecked);
            filling_ = false;
        }
    });
    connect(add, &QPushButton::clicked, this, [this] {
        const QString id = QInputDialog::getText(this, tr("New monster"), tr("Its id (enemies.json key, e.g. slime_red):")).trimmed();
        if (id.isEmpty()) return;
        if (enemies_.contains(ss(id))) { warn(tr("New monster"), tr("\"%1\" is already there.").arg(id)); return; }
        ojson m = ojson::object();
        ojson name = ojson::object();
        name["en"] = ss(id);
        m["name"] = name;
        m["sprite"] = ss(id) + ".png";
        m["hp"] = 20; m["atk"] = 8; m["def"] = 2; m["exp"] = 5; m["gold"] = 3;
        enemies_[ss(id)] = m;
        monstersDirty_ = true;
        fillMonsterList();
        selectMonster(id);
    });
    connect(dup, &QPushButton::clicked, this, [this] {
        if (current_.isEmpty()) return;
        const QString id = QInputDialog::getText(this, tr("Duplicate"), tr("The copy's id:"), QLineEdit::Normal,
                                                 current_ + QStringLiteral("_2")).trimmed();
        if (id.isEmpty() || enemies_.contains(ss(id))) return;
        enemies_[ss(id)] = enemies_[ss(current_)];
        monstersDirty_ = true;
        fillMonsterList();
        selectMonster(id);
    });
    connect(del, &QPushButton::clicked, this, [this] {
        if (current_.isEmpty()) return;
        // which floors place it (data/stages/*.json name "monster:<id>")
        QStringList floors;
        QDirIterator it(QDir(dataDir_).filePath(QStringLiteral("stages")), {QStringLiteral("*.json")}, QDir::Files);
        while (it.hasNext()) {
            const QString f = it.next();
            if (readText(f).contains(QStringLiteral("\"monster:%1\"").arg(current_))) floors << QFileInfo(f).baseName();
        }
        QString text = tr("Delete \"%1\" from enemies.json (when saved)?").arg(current_);
        if (!floors.isEmpty()) text += tr("\n\nThese floors place it: %1").arg(floors.join(QStringLiteral(", ")));
        if (!ask(tr("Delete"), text)) return;
        setInBattle(current_, false);
        enemies_.erase(ss(current_));
        monstersDirty_ = true;
        current_.clear();
        fillMonsterList();
    });
    connect(save, &QPushButton::clicked, this, [this] { saveMonsters(); });
    connect(revert, &QPushButton::clicked, this, [this] {
        if (monstersDirty_ && !ask(tr("Revert"), tr("Drop the monster changes not saved?"))) return;
        loadData();
        fillMonsterList();
        showMonster(current_);
    });
    return w;
}

void BattleEditorWindow::fillMonsterList() {
    filling_ = true;
    monsters_->clear();
    for (auto it = enemies_.begin(); it != enemies_.end(); ++it) {
        if (it.key().empty() || it.key()[0] == '_' || !it.value().is_object()) continue;
        const QString id = qs(it.key());
        QString label = id;
        const QString name = nameOf(it.value());
        if (!name.isEmpty() && name != id) label += QStringLiteral("  ·  ") + name;
        if (it.value().value("boss", false)) label = QStringLiteral("★ ") + label;
        if (it.value().value("draft", false)) label += tr("  (draft)");
        auto* item = new QListWidgetItem(label, monsters_);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(inBattle_.contains(id) ? Qt::Checked : Qt::Unchecked);
    }
    filling_ = false;
    for (int i = 0; i < monsters_->count(); i++)
        if (monsters_->item(i)->data(Qt::UserRole).toString() == current_) monsters_->setCurrentRow(i);
    updateInBattleLabel();
}

int BattleEditorWindow::monsterCount() const { return monsters_->count(); }

bool BattleEditorWindow::setInBattle(const QString& id, bool on) {
    if (on) {
        if (inBattle_.contains(id)) return true;
        return addToBattle(id);
    }
    inBattle_.removeAll(id);
    filling_ = true;
    for (int i = 0; i < monsters_->count(); i++) {
        QListWidgetItem* it = monsters_->item(i);
        if (it->data(Qt::UserRole).toString() == id) it->setCheckState(Qt::Unchecked);
    }
    filling_ = false;
    updateInBattleLabel();
    return true;
}

bool BattleEditorWindow::addToBattle(const QString& id) {
    if (inBattle_.size() >= toms::kMaxFoes) { say(tr("A battle has up to three monsters.")); return false; }
    if (!enemies_.contains(ss(id))) return false;
    inBattle_ << id;
    filling_ = true;
    for (int i = 0; i < monsters_->count(); i++) {
        QListWidgetItem* it = monsters_->item(i);
        if (it->data(Qt::UserRole).toString() == id) it->setCheckState(Qt::Checked);
    }
    filling_ = false;
    updateInBattleLabel();
    return true;
}

void BattleEditorWindow::updateInBattleLabel() {
    inBattleLabel_->setText(tr("IN BATTLE: %1  %2").arg(inBattle_.size()).arg(inBattle_.join(QStringLiteral(" + "))));
}

bool BattleEditorWindow::selectMonster(const QString& id) {
    for (int i = 0; i < monsters_->count(); i++)
        if (monsters_->item(i)->data(Qt::UserRole).toString() == id) { monsters_->setCurrentRow(i); return true; }
    return false;
}

// ---------------------------------------------------------------- the centre: battle bar + game

QWidget* BattleEditorWindow::buildCentre() {
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    auto* bar = new QHBoxLayout();
    auto* battle = new QPushButton(tr("▶ Battle"));
    battle->setToolTip(tr("Start the test battle with the ticked monsters and the test player (again: restart it)."));
    battle->setStyleSheet(QStringLiteral("font-weight: bold;"));
    pause_ = new QPushButton(tr("⏸ Pause"));
    pause_->setCheckable(true);
    speed_ = new QComboBox();
    speed_->addItem(QStringLiteral("×0.5"), 0.5);
    speed_->addItem(QStringLiteral("×1"), 1.0);
    speed_->addItem(QStringLiteral("×2"), 2.0);
    speed_->setCurrentIndex(1);
    pace_ = new QComboBox();
    pace_->addItem(tr("pace: from the data"), QString());
    pace_->addItem(tr("pace: cooldown"), QStringLiteral("cooldown"));
    pace_->addItem(tr("pace: stamina"), QStringLiteral("stamina"));
    pace_->setToolTip(tr("Cooldown: each bar waits after a tap. Stamina: no waits, attacks and guards cost stamina. "
                         "Switches the running battle too (docs/gameplay/13 §6.3)."));
    auto* win = new QPushButton(tr("Win now"));
    auto* lose = new QPushButton(tr("Lose now"));
    autoRestart_ = new QCheckBox(tr("auto restart"));
    autoRestart_->setChecked(true);
    autoRestart_->setToolTip(tr("When the battle ends (every monster down, Win now, or a loss), the same setup starts "
                                "again after %1 s.").arg(kAutoRestartMs / 1000.0));
    shape_ = new QComboBox();
    shape_->addItem(tr("fill"), 0.0);
    shape_->addItem(tr("4:3 landscape"), 4.0 / 3.0);
    shape_->addItem(tr("16:9 landscape"), 16.0 / 9.0);
    shape_->addItem(tr("3:4 portrait"), 3.0 / 4.0);
    shape_->addItem(tr("phone portrait"), 9.0 / 19.5);
    shape_->setToolTip(tr("The game view's shape: the battle screen's landscape and portrait layouts (docs/gameplay/12)."));
    bar->addWidget(battle);
    bar->addWidget(pause_);
    bar->addWidget(speed_);
    bar->addWidget(pace_);
    bar->addWidget(win);
    bar->addWidget(lose);
    bar->addWidget(autoRestart_);
    bar->addStretch(1);
    bar->addWidget(shape_);
    v->addLayout(bar);
    viewBox_ = new AspectBox();
    viewBox_->setChild(view_);
    v->addWidget(viewBox_, 1);

    connect(battle, &QPushButton::clicked, this, [this] { startBattle(); });
    connect(pause_, &QPushButton::toggled, this, [this](bool on) {
        view_->setTimeScale(on ? 0.0 : speed_->currentData().toDouble());
    });
    connect(speed_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (!pause_->isChecked()) view_->setTimeScale(speed_->currentData().toDouble());
    });
    connect(pace_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (Game* g = view_->session().game()) g->setBattlePace(ss(pace_->currentData().toString()));
    });
    connect(win, &QPushButton::clicked, this, [this] { if (Game* g = view_->session().game()) g->debugForceWin(); });
    connect(lose, &QPushButton::clicked, this, [this] { if (Game* g = view_->session().game()) g->debugForceLose(); });
    connect(shape_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        viewBox_->setAspect(shape_->currentData().toDouble());
    });
    return w;
}

void BattleEditorWindow::setPace(const QString& pace) {
    const int i = pace_->findData(pace);
    if (i >= 0) pace_->setCurrentIndex(i);
}

bed::Setup BattleEditorWindow::currentSetup() const {
    bed::Setup s;
    s.name = setups_ ? ss(setups_->currentText()) : std::string();
    s.pace = ss(pace_->currentData().toString());
    for (const QString& id : inBattle_) {
        bed::SetupFoe f;
        f.id = ss(id);
        // the monster as edited here, saved or not: the scenario's overrides (enemyFromData reads them)
        const ojson& m = enemies_[ss(id)];
        for (const char* k : {"hp", "atk", "def", "atk_interval_ms", "moves", "look", "break", "events"})
            if (m.contains(k)) f.overrides[k] = m[k];
        s.foes.push_back(f);
    }
    s.player.lv = pLv_->value();
    s.player.hp = pHp_->value();
    s.player.maxhp = pMaxHp_->value();
    s.player.weapon = ss(pWeapon_->currentData().toString());
    s.player.weaponWins = pWins_->value();
    s.player.attrPoints = pPoints_->value();
    for (auto it = pAttrs_->begin(); it != pAttrs_->end(); ++it)
        if (it.value()->value() > 0) s.player.attrs[ss(it.key())] = it.value()->value();
    return s;
}

bool BattleEditorWindow::startBattle() {
    if (inBattle_.isEmpty()) { warn(tr("Battle"), tr("Tick a monster (+ Battle) first.")); return false; }
    const QString dir = QDir(scratchDir_).filePath(QStringLiteral("current"));
    const QString file = QDir(dir).filePath(QStringLiteral("scenario.json"));
    if (!writeText(file, qs(bed::setupToScenario(currentSetup()).dump(2) + "\n"))) {
        warn(tr("Battle"), tr("Could not write %1").arg(file));
        return false;
    }
    QString err;
    if (!view_->startScenario(dir, err)) {
        warn(tr("Battle"), tr("The battle could not start:\n%1").arg(err));
        return false;
    }
    logShown_ = -1;
    if (weaponsDirty_) say(tr("Battle started -- the weapon changes are used once saved"));
    else say(tr("Battle: %1").arg(inBattle_.join(QStringLiteral(" + "))));
    view_->setFocus();
    return true;
}

// ---------------------------------------------------------------- Monster

QWidget* BattleEditorWindow::buildMonsterTab() {
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);
    mId_ = new QLabel();
    mNameEn_ = new QLineEdit();
    mNameZh_ = new QLineEdit();
    mSprite_ = new QLineEdit();
    mLook_ = new QLineEdit();
    mLook_->setToolTip(tr("Its status map in data/looks.json (empty: \"monster\")."));
    mMoves_ = new QLineEdit();
    mMoves_->setToolTip(tr("What it may do next (data/moves.json), e.g. \"strike, heavy:2, guard\" (id:weight). "
                           "Empty: a plain strike."));
    mParty_ = new QLineEdit();
    mParty_->setToolTip(tr("Monsters that fight beside it when met on a floor, e.g. \"bat, bat\" (up to two)."));
    mBoss_ = new QCheckBox(tr("boss"));
    f->addRow(tr("id"), mId_);
    f->addRow(tr("name (en)"), mNameEn_);
    f->addRow(tr("name (zh_TW)"), mNameZh_);
    f->addRow(tr("sprite"), mSprite_);
    struct N { const char* key; const char* label; int max; };
    for (const N& n : {N{"hp", "HP", 99999}, N{"atk", "ATK", 9999}, N{"def", "DEF", 9999},
                       N{"atk_interval_ms", "attack interval (ms)", 60000}, N{"break", "break points (0: battle.json)", 999},
                       N{"exp", "EXP", 99999}, N{"gold", "gold", 99999}}) {
        auto* sb = new QSpinBox();
        sb->setRange(0, n.max);
        (*mNumbers_)[QString::fromLatin1(n.key)] = sb;
        f->addRow(tr(n.label), sb);
        connect(sb, qOverload<int>(&QSpinBox::valueChanged), this, [this] { monsterEdited(); });
    }
    f->addRow(tr("moves"), mMoves_);
    f->addRow(tr("look"), mLook_);
    f->addRow(tr("party"), mParty_);
    f->addRow(QString(), mBoss_);
    readout_ = new QLabel();
    readout_->setWordWrap(true);
    readout_->setStyleSheet(QStringLiteral("color: #555;"));
    f->addRow(tr("last battle"), readout_);
    for (QLineEdit* e : {mNameEn_, mNameZh_, mSprite_, mLook_, mMoves_, mParty_})
        connect(e, &QLineEdit::textEdited, this, [this] { monsterEdited(); });
    connect(mBoss_, &QCheckBox::toggled, this, [this] { monsterEdited(); });
    return w;
}

void BattleEditorWindow::showMonster(const QString& id) {
    current_ = id;
    filling_ = true;
    const bool has = !id.isEmpty() && enemies_.contains(ss(id));
    const ojson m = has ? enemies_[ss(id)] : ojson::object();
    mId_->setText(id);
    mNameEn_->setText(has && m.contains("name") ? qs(m["name"].value("en", std::string())) : QString());
    mNameZh_->setText(has && m.contains("name") ? qs(m["name"].value("zh_TW", std::string())) : QString());
    mSprite_->setText(qs(m.value("sprite", std::string())));
    for (auto it = mNumbers_->begin(); it != mNumbers_->end(); ++it)
        it.value()->setValue(m.value(ss(it.key()), it.key() == QStringLiteral("atk_interval_ms") ? 4000 : 0));
    mMoves_->setText(qs(bed::movesToText(m.value("moves", ojson::array()))));
    mLook_->setText(qs(m.value("look", std::string())));
    mParty_->setText(qs(bed::idsToText(m.value("party", ojson::array()))));
    mBoss_->setChecked(m.value("boss", false));
    filling_ = false;
    showEvents();
}

void BattleEditorWindow::setMonsterNumber(const QString& key, int value) {
    if (QSpinBox* sb = mNumbers_->value(key)) sb->setValue(value);
}

void BattleEditorWindow::monsterEdited() {
    if (filling_ || current_.isEmpty() || !enemies_.contains(ss(current_))) return;
    ojson& m = enemies_[ss(current_)];
    if (!m.contains("name") || !m["name"].is_object()) m["name"] = ojson::object();
    m["name"]["en"] = ss(mNameEn_->text());
    if (!mNameZh_->text().isEmpty()) m["name"]["zh_TW"] = ss(mNameZh_->text());
    m["sprite"] = ss(mSprite_->text());
    for (auto it = mNumbers_->begin(); it != mNumbers_->end(); ++it) {
        const std::string k = ss(it.key());
        const int v = it.value()->value();
        const bool optional = k == "atk_interval_ms" || k == "break";   // left out when it is the default
        if (optional && (v == 0 || (k == "atk_interval_ms" && v == 4000) )) m.erase(k);
        else m[k] = v;
    }
    auto setOrErase = [&m](const char* k, const ojson& v) { if (v.empty()) m.erase(k); else m[k] = v; };
    setOrErase("moves", bed::movesFromText(ss(mMoves_->text())));
    setOrErase("party", bed::idsFromText(ss(mParty_->text())));
    if (mLook_->text().trimmed().isEmpty()) m.erase("look"); else m["look"] = ss(mLook_->text().trimmed());
    if (mBoss_->isChecked()) m["boss"] = true; else m.erase("boss");
    monstersDirty_ = true;
    setWindowTitle(tr("TOMS Battle Editor *"));
    applyToRunningBattle(current_);
}

// An edit reaches the battle that is running at once (every monster of that kind in it), not only the next ▶ Battle.
void BattleEditorWindow::applyToRunningBattle(const QString& id) {
    Game* g = view_->session().game();
    if (!g || g->battleFoeCount() == 0 || !enemies_.contains(ss(id))) return;
    g->debugUpdateMonster(ss(id), nlohmann::json::parse(enemies_[ss(id)].dump()));
}

// The last battle's numbers from its log: how hard the monsters are for this player (13 §4.1's readout).
void BattleEditorWindow::updateReadout() {
    Game* g = view_->session().game();
    if (!g || !readout_) return;
    const std::vector<toms::BattleEvent>& ev = g->battleEvents();
    int dealt = 0, dealtN = 0, taken = 0, takenN = 0, endMs = 0;
    for (const toms::BattleEvent& e : ev) {
        if (e.type == "hit" && e.who < 0) { dealt += e.value; dealtN++; }
        if (e.type == "hit" && e.who >= 0) { taken += e.value; takenN++; }
        endMs = std::max(endMs, e.ms);
    }
    if (ev.empty()) { readout_->setText(tr("(no battle yet)")); return; }
    readout_->setText(tr("you hit %1 times, %2 on average; the monsters hit %3 times, %4 on average; %5 s")
                          .arg(dealtN).arg(dealtN ? dealt / dealtN : 0).arg(takenN).arg(takenN ? taken / takenN : 0)
                          .arg(endMs / 1000.0, 0, 'f', 1));
}

// ---------------------------------------------------------------- Weapon

QWidget* BattleEditorWindow::buildWeaponTab() {
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    weapons_ = new QListWidget();
    weapons_->setMaximumHeight(130);
    for (auto it = weaponPlay_.begin(); it != weaponPlay_.end(); ++it)
        if (!it.key().empty() && it.key()[0] != '_') weapons_->addItem(qs(it.key()));
    v->addWidget(new QLabel(tr("<b>weapon_play.json</b>")));
    v->addWidget(weapons_);
    auto* use = new QPushButton(tr("Use this weapon in the battle"));
    v->addWidget(use);
    wLabel_ = new QLabel();
    v->addWidget(wLabel_);
    auto* form = new QFormLayout();
    for (const char* k : {"sweepMs", "chargeMs", "hitWindowMs", "lateMs"}) {
        auto* sb = new QSpinBox();
        sb->setRange(0, 10000);
        (*wNumbers_)[QString::fromLatin1(k)] = sb;
        form->addRow(QString::fromLatin1(k), sb);
        connect(sb, qOverload<int>(&QSpinBox::valueChanged), this, [this] { weaponEdited(); });
    }
    wPace_ = new QComboBox();
    wPace_->addItem(tr("(the battle's)"), QString());
    wPace_->addItem(QStringLiteral("cooldown"), QStringLiteral("cooldown"));
    wPace_->addItem(QStringLiteral("stamina"), QStringLiteral("stamina"));
    form->addRow(tr("pace"), wPace_);
    connect(wPace_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { weaponEdited(); });
    v->addLayout(form);
    v->addWidget(new QLabel(tr("<b>Results</b> by points matched (13 §4.2): the highest row reached applies; hits \"all\" = every point. "
                               "A particle not in battle.particle is red.")));
    results_ = new QTableWidget(0, 8);
    results_->setHorizontalHeaderLabels({tr("hits"), tr("name"), tr("damage"), tr("fx"), tr("sound"), tr("stunMs"),
                                         tr("pushMs"), tr("breakAdd")});
    results_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    v->addWidget(results_, 1);
    auto* row = new QHBoxLayout();
    auto* addRow = new QPushButton(tr("+ Row"));
    auto* delRow = new QPushButton(tr("− Row"));
    auto* save = new QPushButton(tr("Save weapons"));
    row->addWidget(addRow);
    row->addWidget(delRow);
    row->addStretch(1);
    row->addWidget(save);
    v->addLayout(row);

    connect(weapons_, &QListWidget::currentTextChanged, this, [this](const QString& k) { showWeapon(k); });
    connect(use, &QPushButton::clicked, this, [this] {
        const QString k = currentWeapon_;
        if (k == QStringLiteral("default")) { say(tr("\"default\" is the settings of weapons without their own.")); return; }
        const int i = pWeapon_->findData(k == QStringLiteral("unarmed") ? QString() : k);
        if (i >= 0) pWeapon_->setCurrentIndex(i);
        else { pWeapon_->addItem(k, k); pWeapon_->setCurrentIndex(pWeapon_->count() - 1); }
        say(tr("The test player uses %1 (▶ Battle to try it)").arg(k));
    });
    connect(results_, &QTableWidget::cellChanged, this, [this] { weaponEdited(); });
    connect(addRow, &QPushButton::clicked, this, [this] {
        filling_ = true;
        const int r = results_->rowCount();
        results_->insertRow(r);
        for (int c = 0; c < results_->columnCount(); c++) results_->setItem(r, c, new QTableWidgetItem());
        results_->item(r, 0)->setText(QStringLiteral("all"));
        results_->item(r, 2)->setText(QStringLiteral("1"));
        filling_ = false;
        weaponEdited();
    });
    connect(delRow, &QPushButton::clicked, this, [this] {
        if (results_->currentRow() >= 0) { results_->removeRow(results_->currentRow()); weaponEdited(); }
    });
    connect(save, &QPushButton::clicked, this, [this] { saveWeapons(); });
    return w;
}

void BattleEditorWindow::showWeapon(const QString& key) {
    currentWeapon_ = key;
    filling_ = true;
    const ojson w = weaponPlay_.value(ss(key), ojson::object());
    const ojson def = weaponPlay_.value("default", ojson::object());
    for (auto it = wNumbers_->begin(); it != wNumbers_->end(); ++it)
        it.value()->setValue(w.value(ss(it.key()), def.value(ss(it.key()), 0)));
    wPace_->setCurrentIndex(std::max(0, wPace_->findData(qs(w.value("pace", std::string())))));
    const bool own = w.contains("results");
    wLabel_->setText(own ? tr("Its own results:") : tr("No results of its own: the default's apply (adding a row gives it its own)."));
    results_->setRowCount(0);
    const ojson rows = own ? w["results"] : ojson::array();
    for (const auto& r : rows) {
        const int i = results_->rowCount();
        results_->insertRow(i);
        auto cell = [&](int c, const QString& t) { results_->setItem(i, c, new QTableWidgetItem(t)); };
        cell(0, r.contains("hits") ? (r["hits"].is_string() ? qs(r["hits"].get<std::string>()) : QString::number(r["hits"].get<int>())) : QString());
        cell(1, qs(r.value("name", std::string())));
        cell(2, QString::number(r.value("damage", 1.0)));
        cell(3, qs(r.value("fx", std::string())));
        cell(4, qs(r.value("sound", std::string())));
        cell(5, QString::number(r.value("stunMs", 0)));
        cell(6, QString::number(r.value("pushMs", 0)));
        cell(7, QString::number(r.value("breakAdd", 0.0)));
        const QString fx = qs(r.value("fx", std::string()));
        if (!fx.isEmpty() && !effects_.contains(fx)) results_->item(i, 3)->setBackground(QColor(255, 200, 200));
    }
    filling_ = false;
}

void BattleEditorWindow::weaponEdited() {
    if (filling_ || currentWeapon_.isEmpty()) return;
    ojson& w = weaponPlay_[ss(currentWeapon_)];
    if (!w.is_object()) w = ojson::object();
    for (auto it = wNumbers_->begin(); it != wNumbers_->end(); ++it) w[ss(it.key())] = it.value()->value();
    if (wPace_->currentData().toString().isEmpty()) w.erase("pace"); else w["pace"] = ss(wPace_->currentData().toString());
    ojson rows = ojson::array();
    for (int r = 0; r < results_->rowCount(); r++) {
        auto text = [&](int c) { QTableWidgetItem* it = results_->item(r, c); return it ? it->text().trimmed() : QString(); };
        ojson row = ojson::object();
        bool num = false;
        const int hits = text(0).toInt(&num);
        row["hits"] = num ? ojson(hits) : ojson("all");
        if (!text(1).isEmpty()) row["name"] = ss(text(1));
        row["damage"] = text(2).isEmpty() ? 1.0 : text(2).toDouble();
        if (!text(3).isEmpty()) row["fx"] = ss(text(3));
        if (!text(4).isEmpty()) row["sound"] = ss(text(4));
        if (text(5).toInt() > 0) row["stunMs"] = text(5).toInt();
        if (text(6).toInt() > 0) row["pushMs"] = text(6).toInt();
        if (text(7).toDouble() > 0) row["breakAdd"] = text(7).toDouble();
        rows.push_back(row);
        if (QTableWidgetItem* fx = results_->item(r, 3)) {
            filling_ = true;
            fx->setBackground(!text(3).isEmpty() && !effects_.contains(text(3)) ? QColor(255, 200, 200) : QColor(Qt::white));
            filling_ = false;
        }
    }
    if (rows.empty()) w.erase("results"); else w["results"] = rows;
    weaponsDirty_ = true;
    setWindowTitle(tr("TOMS Battle Editor *"));
}

// ---------------------------------------------------------------- Player (and the growth calculator)

QWidget* BattleEditorWindow::buildPlayerTab() {
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    auto* box = new QGroupBox(tr("The test player"));
    auto* f = new QFormLayout(box);
    auto spin = [](int lo, int hi, int val) { auto* s = new QSpinBox(); s->setRange(lo, hi); s->setValue(val); return s; };
    pLv_ = spin(1, 99, 1);
    pHp_ = spin(1, 99999, 120);
    pMaxHp_ = spin(1, 99999, 120);
    pWeapon_ = new QComboBox();
    pWeapon_->addItem(tr("(unarmed)"), QString());
    for (auto it = weaponPlay_.begin(); it != weaponPlay_.end(); ++it)
        if (!it.key().empty() && it.key()[0] != '_' && it.key() != "default" && it.key() != "unarmed")
            pWeapon_->addItem(qs(it.key()), qs(it.key()));
    pWeapon_->setCurrentIndex(std::max(0, pWeapon_->findData(QStringLiteral("bow"))));
    pWins_ = spin(0, 9999, 0);
    pWins_->setToolTip(tr("Wins with the weapon: its level (weapon_play.json usesToLevel)."));
    pPoints_ = spin(0, 9999, 0);
    f->addRow(tr("level"), pLv_);
    f->addRow(tr("HP"), pHp_);
    f->addRow(tr("max HP"), pMaxHp_);
    f->addRow(tr("weapon"), pWeapon_);
    f->addRow(tr("weapon wins"), pWins_);
    for (const QString& id : attrIds_) {
        auto* s = spin(0, 999, 0);
        (*pAttrs_)[id] = s;
        f->addRow(id.toUpper() + tr(" (placed)"), s);
    }
    f->addRow(tr("points to place"), pPoints_);
    v->addWidget(box);

    auto* calc = new QGroupBox(tr("Growth calculator (13 §4.3): T points by level L, on a curve"));
    auto* cv = new QVBoxLayout(calc);
    auto* row = new QHBoxLayout();
    cTotal_ = spin(0, 9999, 100);
    cLevel_ = spin(2, 99, 20);
    cShape_ = new QDoubleSpinBox();
    cShape_->setRange(0.2, 4.0);
    cShape_->setSingleStep(0.1);
    cShape_->setValue(1.3);
    cShape_->setToolTip(tr("k: 1 = the same each level, above 1 = more later, below 1 = more early."));
    row->addWidget(new QLabel(tr("T")));
    row->addWidget(cTotal_);
    row->addWidget(new QLabel(tr("by level L")));
    row->addWidget(cLevel_);
    row->addWidget(new QLabel(tr("k")));
    row->addWidget(cShape_);
    cv->addLayout(row);
    cTable_ = new QTableWidget(0, 3);
    cTable_->setHorizontalHeaderLabels({tr("level"), tr("points"), tr("total")});
    cTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    cTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cv->addWidget(cTable_, 1);
    cSum_ = new QLabel();
    cv->addWidget(cSum_);
    auto* row2 = new QHBoxLayout();
    auto* apply = new QPushButton(tr("Apply to the test player"));
    apply->setToolTip(tr("The test player at level L with T points to place."));
    auto* saveT = new QPushButton(tr("Save as the game's table"));
    saveT->setToolTip(tr("data/stats.json levelUp \"points\": neighbouring levels with the same points merged."));
    row2->addWidget(apply);
    row2->addWidget(saveT);
    cv->addLayout(row2);
    v->addWidget(calc, 1);

    auto recalc = [this] { calculate(cTotal_->value(), cLevel_->value(), cShape_->value()); };
    connect(cTotal_, qOverload<int>(&QSpinBox::valueChanged), this, recalc);
    connect(cLevel_, qOverload<int>(&QSpinBox::valueChanged), this, recalc);
    connect(cShape_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, recalc);
    connect(apply, &QPushButton::clicked, this, [this] {
        pLv_->setValue(cLevel_->value());
        pPoints_->setValue(cTotal_->value());
        for (QSpinBox* s : *pAttrs_) s->setValue(0);
        say(tr("The test player: level %1 with %2 points to place").arg(cLevel_->value()).arg(cTotal_->value()));
    });
    connect(saveT, &QPushButton::clicked, this, [this] { saveGrowthTable(); });
    recalc();
    return w;
}

std::vector<int> BattleEditorWindow::calculate(int total, int level, double k) {
    const std::vector<int> per = bed::levelPoints(total, level, k);
    if (cTable_) {
        cTable_->setRowCount((int)per.size());
        int run = 0;
        for (size_t i = 0; i < per.size(); i++) {
            run += per[i];
            cTable_->setItem((int)i, 0, new QTableWidgetItem(QString::number(i + 2)));
            auto* bar = new QTableWidgetItem(QString::number(per[i]) + QStringLiteral("  ") + QString(per[i], QChar(0x2588)));
            cTable_->setItem((int)i, 1, bar);
            cTable_->setItem((int)i, 2, new QTableWidgetItem(QString::number(run)));
        }
        cSum_->setText(tr("Lv 2–%1: %2 points in all").arg(level).arg(run));
    }
    return per;
}

// ---------------------------------------------------------------- Events

QWidget* BattleEditorWindow::buildEventsTab() {
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    evMonster_ = new QLabel();
    v->addWidget(evMonster_);
    v->addWidget(new QLabel(tr("Triggers: beforeBattle, battleStart, hpBelow, playerHpBelow, attacks, everyMs, perfectHit, falls.\n"
                               "Actions: say, stat (atk / def / interval, mul / add), transformInto (hp keepShare / full), heal, "
                               "summon, flee, anim, fx, sound, flag (13 §4.4).")));
    evText_ = new QPlainTextEdit();
    evText_->setFont(QFont(QStringLiteral("Consolas"), 10));
    v->addWidget(evText_, 2);
    auto* row = new QHBoxLayout();
    auto* apply = new QPushButton(tr("Apply"));
    apply->setToolTip(tr("Check the text and keep it as the monster's events (Save monsters writes the file)."));
    auto* example = new QPushButton(tr("Insert an example"));
    row->addWidget(apply);
    row->addWidget(example);
    row->addStretch(1);
    v->addLayout(row);
    v->addWidget(new QLabel(tr("In the running battle:")));
    evList_ = new QListWidget();
    v->addWidget(evList_, 1);
    auto* fire = new QPushButton(tr("Fire now"));
    fire->setToolTip(tr("Run the selected event's actions now, in the running battle (the monster must be in it)."));
    v->addWidget(fire);
    connect(apply, &QPushButton::clicked, this, &BattleEditorWindow::applyEventsText);
    connect(example, &QPushButton::clicked, this, [this] {
        evText_->setPlainText(QStringLiteral(
            "[\n  { \"on\": \"hpBelow\", \"value\": 50, \"do\": [ { \"say\": \"It grows angry!\" }, { \"stat\": \"atk\", \"mul\": 1.3 }, "
            "{ \"anim\": \"special\" } ] },\n  { \"on\": \"hpBelow\", \"value\": 20, \"do\": [ { \"transformInto\": \"slime\", \"hp\": \"keepShare\" } ] }\n]\n"));
    });
    connect(fire, &QPushButton::clicked, this, [this] {
        Game* g = view_->session().game();
        const int foe = inBattle_.indexOf(current_);
        if (!g || foe < 0 || g->battleFoeCount() == 0) { say(tr("Start a battle with this monster in it first.")); return; }
        g->debugFireFoeEvent(foe, evList_->currentRow());
    });
    return w;
}

void BattleEditorWindow::showEvents() {
    if (!evText_) return;
    const ojson m = !current_.isEmpty() && enemies_.contains(ss(current_)) ? enemies_[ss(current_)] : ojson::object();
    evMonster_->setText(tr("<b>%1</b>'s events (enemies.json \"events\")").arg(current_));
    const ojson ev = m.value("events", ojson::array());
    evText_->setPlainText(ev.empty() ? QStringLiteral("[]") : qs(ev.dump(2)));
    evList_->clear();
    for (const auto& e : ev) {
        QString line = qs(e.value("on", std::string()));
        if (e.contains("value")) line += QStringLiteral(" ") + qs(e["value"].dump());
        line += QStringLiteral("  →  ") + qs(e.value("do", ojson::array()).dump());
        evList_->addItem(line);
    }
}

void BattleEditorWindow::applyEventsText() {
    if (current_.isEmpty()) return;
    ojson ev;
    if (!parse(evText_->toPlainText(), ev) || !ev.is_array()) {
        warn(tr("Events"), tr("The events are not a JSON list: [ { \"on\": ..., \"do\": [ ... ] }, ... ]"));
        return;
    }
    for (const auto& e : ev)
        if (!e.is_object() || !e.contains("on") || !e.contains("do")) {
            warn(tr("Events"), tr("Each event needs \"on\" and \"do\": %1").arg(qs(e.dump())));
            return;
        }
    ojson& m = enemies_[ss(current_)];
    if (ev.empty()) m.erase("events"); else m["events"] = ev;
    monstersDirty_ = true;
    setWindowTitle(tr("TOMS Battle Editor *"));
    applyToRunningBattle(current_);
    showEvents();
    say(tr("Events kept for %1 (▶ Battle uses them; Save monsters writes them)").arg(current_));
}

// ---------------------------------------------------------------- Log

QWidget* BattleEditorWindow::buildLogTab() {
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    log_ = new QPlainTextEdit();
    log_->setReadOnly(true);
    log_->setFont(QFont(QStringLiteral("Consolas"), 10));
    v->addWidget(new QLabel(tr("Every event of the running battle, with its time (13 §4.5).")));
    v->addWidget(log_, 1);
    return w;
}

void BattleEditorWindow::refreshLog() {
    Game* g = view_->session().game();
    if (!g || !log_) return;
    // the battle ended (won or lost): start it again, after a moment to see how it ended
    const bool running = g->battleFoeCount() > 0;
    if (battleRunning_ && !running && autoRestart_ && autoRestart_->isChecked() && !restartPending_) {
        restartPending_ = true;
        say(tr("The battle is over -- again in %1 s (auto restart)").arg(kAutoRestartMs / 1000.0));
        QTimer::singleShot(kAutoRestartMs, this, [this] {
            restartPending_ = false;
            if (autoRestart_->isChecked() && !inBattle_.isEmpty()) startBattle();
        });
    }
    battleRunning_ = running;
    const std::vector<toms::BattleEvent>& ev = g->battleEvents();
    if ((int)ev.size() == logShown_) return;
    logShown_ = (int)ev.size();
    const std::vector<std::string> ids = g->battleFoeIds();
    QString text;
    for (const toms::BattleEvent& e : ev) text += qs(toms::battleEventLine(e, ids)) + QLatin1Char('\n');
    log_->setPlainText(text);
    log_->moveCursor(QTextCursor::End);
    updateReadout();
}

QString BattleEditorWindow::logText() const { return log_ ? log_->toPlainText() : QString(); }

// ---------------------------------------------------------------- test setups

QWidget* BattleEditorWindow::buildSetupsBar() {
    auto* w = new QWidget();
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(new QLabel(tr("<b>TEST SETUPS</b> (tools/battle_editor/setups: editor files, not tests)")));
    setups_ = new QComboBox();
    setups_->setMinimumWidth(220);
    auto* load = new QPushButton(tr("Load"));
    auto* save = new QPushButton(tr("+ Save current as…"));
    h->addWidget(setups_);
    h->addWidget(load);
    h->addWidget(save);
    h->addStretch(1);
    connect(load, &QPushButton::clicked, this, [this] { if (loadSetup(setups_->currentText())) startBattle(); });
    connect(save, &QPushButton::clicked, this, [this] {
        const QString name = QInputDialog::getText(this, tr("Save the setup"), tr("Its name (e.g. 3 bats Lv5 sword):"),
                                                   QLineEdit::Normal, setups_->currentText()).trimmed();
        if (!name.isEmpty()) saveSetupAs(name);
    });
    return w;
}

void BattleEditorWindow::fillSetups() {
    const QString keep = setups_->currentText();
    setups_->clear();
    for (const QFileInfo& f : QDir(setupsDir_).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        setups_->addItem(f.completeBaseName());
    const int i = setups_->findText(keep);
    if (i >= 0) setups_->setCurrentIndex(i);
}

bool BattleEditorWindow::saveSetupAs(const QString& name) {
    QString file = name;
    file.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    bed::Setup s = currentSetup();
    s.name = ss(name);
    const QString path = QDir(setupsDir_).filePath(file + QStringLiteral(".json"));
    if (!writeText(path, qs(bed::setupToScenario(s).dump(2) + "\n"))) { warn(tr("Setup"), tr("Could not write %1").arg(path)); return false; }
    fillSetups();
    setups_->setCurrentIndex(setups_->findText(file));
    say(tr("Saved the setup %1").arg(path));
    return true;
}

bool BattleEditorWindow::loadSetup(const QString& name) {
    ojson j;
    bed::Setup s;
    const QString path = QDir(setupsDir_).filePath(name + QStringLiteral(".json"));
    if (!parse(readText(path), j) || !bed::setupFromScenario(j, s)) {
        warn(tr("Setup"), tr("%1 is not a battle editor setup.").arg(path));
        return false;
    }
    for (const QString& id : QStringList(inBattle_)) setInBattle(id, false);
    for (const bed::SetupFoe& f : s.foes) addToBattle(qs(f.id));
    pLv_->setValue(s.player.lv);
    pMaxHp_->setValue(s.player.maxhp);
    pHp_->setValue(s.player.hp);
    pWeapon_->setCurrentIndex(std::max(0, pWeapon_->findData(qs(s.player.weapon))));
    pWins_->setValue(s.player.weaponWins);
    pPoints_->setValue(s.player.attrPoints);
    for (auto it = pAttrs_->begin(); it != pAttrs_->end(); ++it) it.value()->setValue(s.player.attrs.value(ss(it.key()), 0));
    setPace(qs(s.pace));
    say(tr("Loaded the setup %1").arg(name));
    return true;
}

// ---------------------------------------------------------------- the rest

void BattleEditorWindow::say(const QString& text) { statusBar()->showMessage(text, 6000); }

bool BattleEditorWindow::ask(const QString& title, const QString& text) {
    if (quiet_) return true;
    return QMessageBox::question(this, title, text) == QMessageBox::Yes;
}

void BattleEditorWindow::warn(const QString& title, const QString& text) {
    if (quiet_) { std::fprintf(stderr, "[battle_editor] %s: %s\n", ss(title).c_str(), ss(text).c_str()); return; }
    QMessageBox::warning(this, title, text);
}

void BattleEditorWindow::closeEvent(QCloseEvent* e) {
    if (isDirty() && !quiet_) {
        const auto b = QMessageBox::question(this, tr("Unsaved changes"),
                                             tr("Save the changed monsters and weapons before closing?"),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (b == QMessageBox::Cancel) { e->ignore(); return; }
        if (b == QMessageBox::Save) {
            if (monstersDirty_ && !saveMonsters()) { e->ignore(); return; }
            if (weaponsDirty_ && !saveWeapons()) { e->ignore(); return; }
        }
    }
    e->accept();
}
