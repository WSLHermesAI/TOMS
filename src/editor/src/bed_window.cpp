// bed_window.cpp -- see bed_window.h.
#include "bed_window.h"

#include "battle_party.h"
#include "bgfx_viewport.h"
#include "game.h"
#include "weapon_play.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
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
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

using bed::ojson;

// The game view at a chosen shape (landscape, portrait, a tall phone), centred in the space it has. The viewport is a
// fixed child (its native window never moves to another parent), only its size changes.
class AspectBox : public QWidget {
public:
    explicit AspectBox(QWidget* l_pParent = nullptr) : QWidget(l_pParent) {}
    void f_SetChild(QWidget* l_pW) { m_pChild = l_pW; l_pW->setParent(this); f_Place(); }
    void f_SetAspect(double l_dWOverH) { m_dAspect = l_dWOverH; f_Place(); }
protected:
    void resizeEvent(QResizeEvent*) override { f_Place(); }
private:
    void f_Place() {
        if (!m_pChild) return;
        if (m_dAspect <= 0.0) { m_pChild->setGeometry(rect()); return; }   // fill
        int l_iW = width(), l_iH = (int)(l_iW / m_dAspect);
        if (l_iH > height()) { l_iH = height(); l_iW = (int)(l_iH * m_dAspect); }
        m_pChild->setGeometry((width() - l_iW) / 2, (height() - l_iH) / 2, l_iW, l_iH);
    }
    QWidget* m_pChild = nullptr;
    double m_dAspect = 0.0;
};

namespace {

QString f_ReadText(const QString& l_oPath) {
    QFile l_oF(l_oPath);
    return l_oF.open(QIODevice::ReadOnly) ? QString::fromUtf8(l_oF.readAll()) : QString();
}

bool f_WriteText(const QString& l_oPath, const QString& l_oText) {
    QFile l_oF(l_oPath);
    if (!l_oF.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray l_oB = l_oText.toUtf8();
    return l_oF.write(l_oB) == l_oB.size();
}

bool f_Parse(const QString& l_oText, ojson& l_jOut) {
    l_jOut = ojson::parse(l_oText.toStdString(), nullptr, false);
    return !l_jOut.is_discarded();
}

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }
std::string f_Ss(const QString& l_oS) { return l_oS.toStdString(); }

QString f_NameOf(const ojson& l_jM) {
    if (l_jM.contains("name") && l_jM["name"].is_object()) return f_Qs(l_jM["name"].value("en", l_jM["name"].value("zh_TW", std::string())));
    return QString();
}

}  // namespace

BattleEditorWindow::BattleEditorWindow() {
    setWindowTitle(tr("TOMS Battle Editor"));
    resize(1600, 940);
    m_pMNumbers = new QMap<QString, QSpinBox*>();
    m_pWNumbers = new QMap<QString, QSpinBox*>();
    m_pPAttrs = new QMap<QString, QSpinBox*>();

    m_pView = new BgfxViewport();
    m_oDataDir = QDir::cleanPath(QDir(m_pView->f_AssetDir()).filePath(QStringLiteral("../data")));
    m_oSetupsDir = QDir::cleanPath(QDir(m_pView->f_AssetDir()).filePath(QStringLiteral("../../tools/battle_editor/setups")));
    m_oFxFile = QDir(m_pView->f_AssetDir()).filePath(QStringLiteral("fx/battle.particle"));
    m_oScratchDir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("toms_battle_editor"));
    QDir().mkpath(QDir(m_oScratchDir).filePath(QStringLiteral("current")));
    QDir().mkpath(m_oSetupsDir);
    f_LoadData();

    auto* l_pSplit = new QSplitter(Qt::Horizontal);
    l_pSplit->addWidget(f_BuildMonsterList());
    l_pSplit->addWidget(f_BuildCentre());
    m_pTabs = new QTabWidget();
    m_pTabs->addTab(f_BuildMonsterTab(), tr("Monster"));
    m_pTabs->addTab(f_BuildWeaponTab(), tr("Weapon"));
    m_pTabs->addTab(f_BuildPlayerTab(), tr("Player"));
    m_pTabs->addTab(f_BuildEventsTab(), tr("Events"));
    m_pTabs->addTab(f_BuildLogTab(), tr("Log"));
    m_pTabs->addTab(f_BuildScenariosTab(), tr("Scenarios"));
    l_pSplit->addWidget(m_pTabs);
    l_pSplit->setStretchFactor(0, 0);
    l_pSplit->setStretchFactor(1, 1);
    l_pSplit->setStretchFactor(2, 0);
    l_pSplit->setSizes({240, 860, 480});

    auto* l_pCentral = new QWidget();
    auto* l_pV = new QVBoxLayout(l_pCentral);
    l_pV->setContentsMargins(4, 4, 4, 4);
    l_pV->addWidget(l_pSplit, 1);
    l_pV->addWidget(f_BuildSetupsBar());
    setCentralWidget(l_pCentral);
    statusBar()->showMessage(tr("Tick up to three monsters (+ Battle), then ▶ Battle."));
    connect(m_pView, &BgfxViewport::f_StatusMessage, this, [this](const QString& l_oT) { statusBar()->showMessage(l_oT, 4000); });

    f_FillMonsterList();
    if (m_pMonsters->count() > 0) {
        m_pMonsters->setCurrentRow(0);
        f_SetInBattle(m_pMonsters->item(0)->data(Qt::UserRole).toString(), true);
    }
    if (m_pWeapons->count() > 0) m_pWeapons->setCurrentRow(0);
    f_FillSetups();
    m_oLogTimer.setInterval(250);
    connect(&m_oLogTimer, &QTimer::timeout, this, &BattleEditorWindow::f_RefreshLog);
    m_oLogTimer.start();
}

// ---------------------------------------------------------------- the data files

bool BattleEditorWindow::f_LoadData() {
    bool l_bOk = f_Parse(f_ReadText(QDir(m_oDataDir).filePath(QStringLiteral("enemies.json"))), m_jEnemies) && m_jEnemies.is_object();
    if (!l_bOk) { m_jEnemies = ojson::object(); f_Warn(tr("enemies.json"), tr("data/enemies.json could not be read.")); }
    if (!f_Parse(f_ReadText(QDir(m_oDataDir).filePath(QStringLiteral("weapon_play.json"))), m_jWeaponPlay) || !m_jWeaponPlay.is_object()) {
        m_jWeaponPlay = ojson::object();
        f_Warn(tr("weapon_play.json"), tr("data/weapon_play.json could not be read."));
        l_bOk = false;
    }
    ojson l_jStats;
    m_oAttrIds.clear();
    if (f_Parse(f_ReadText(QDir(m_oDataDir).filePath(QStringLiteral("stats.json"))), l_jStats) && l_jStats.contains("attributes"))
        for (const auto& l_jA : l_jStats["attributes"])
            if (l_jA.contains("id")) m_oAttrIds << f_Qs(l_jA["id"].get<std::string>());
    ojson l_jFx;
    m_oEffects.clear();
    if (f_Parse(f_ReadText(m_oFxFile), l_jFx) && l_jFx.contains("effects"))
        for (const auto& l_jE : l_jFx["effects"])
            if (l_jE.contains("name")) m_oEffects << f_Qs(l_jE["name"].get<std::string>());
    m_bMonstersDirty = m_bWeaponsDirty = false;
    return l_bOk;
}

bool BattleEditorWindow::f_SaveMonsters() {
    const QString l_oPath = QDir(m_oDataDir).filePath(QStringLiteral("enemies.json"));
    if (!f_WriteText(l_oPath, f_Qs(bed::f_DumpCompact(m_jEnemies)))) { f_Warn(tr("Save"), tr("Could not write %1").arg(l_oPath)); return false; }
    m_bMonstersDirty = false;
    f_Say(tr("Saved data/enemies.json"));
    return true;
}

bool BattleEditorWindow::f_SaveWeapons() {
    const QString l_oPath = QDir(m_oDataDir).filePath(QStringLiteral("weapon_play.json"));
    if (!f_WriteText(l_oPath, f_Qs(m_jWeaponPlay.dump(2, ' ', false) + "\n"))) { f_Warn(tr("Save"), tr("Could not write %1").arg(l_oPath)); return false; }
    m_bWeaponsDirty = false;
    f_Say(tr("Saved data/weapon_play.json (the next battle uses it)"));
    return true;
}

bool BattleEditorWindow::f_SaveGrowthTable() {
    const QString l_oPath = QDir(m_oDataDir).filePath(QStringLiteral("stats.json"));
    const QString l_oText = f_ReadText(l_oPath);
    ojson l_jStats;
    if (!f_Parse(l_oText, l_jStats)) { f_Warn(tr("Save"), tr("data/stats.json could not be read.")); return false; }
    const ojson l_jOld = l_jStats.contains("levelUp") ? l_jStats["levelUp"].value("points", ojson::array()) : ojson::array();
    const std::vector<int> l_vPer = f_Calculate(m_pCTotal->value(), m_pCLevel->value(), m_pCShape->value());
    const ojson l_jRows = bed::f_PointsRows(l_vPer, m_pCLevel->value(), l_jOld);
    std::string l_strT = f_Ss(l_oText);
    if (!bed::f_ReplaceLevelPoints(l_strT, l_jRows)) { f_Warn(tr("Save"), tr("data/stats.json has no levelUp \"points\" table.")); return false; }
    if (!f_Ask(tr("Save the growth table"), tr("Replace data/stats.json's levelUp points with these %1 rows?\n\n%2")
                                                .arg(l_jRows.size()).arg(f_Qs(l_jRows.dump()))))
        return false;
    if (!f_WriteText(l_oPath, f_Qs(l_strT))) { f_Warn(tr("Save"), tr("Could not write %1").arg(l_oPath)); return false; }
    f_Say(tr("Saved data/stats.json levelUp points (%1 rows)").arg(l_jRows.size()));
    return true;
}

// ---------------------------------------------------------------- the monster list

QWidget* BattleEditorWindow::f_BuildMonsterList() {
    auto* l_pW = new QWidget();
    auto* l_pV = new QVBoxLayout(l_pW);
    l_pV->setContentsMargins(0, 0, 0, 0);
    auto* l_pTitle = new QLabel(tr("<b>MONSTERS</b>"));
    m_pInBattleLabel = new QLabel();
    m_pSearch = new QLineEdit();
    m_pSearch->setPlaceholderText(tr("search…"));
    m_pMonsters = new QListWidget();
    m_pMonsters->setToolTip(tr("Tick + Battle for the monsters in the test battle (up to three, in the order ticked)."));
    l_pV->addWidget(l_pTitle);
    l_pV->addWidget(m_pInBattleLabel);
    l_pV->addWidget(m_pSearch);
    l_pV->addWidget(m_pMonsters, 1);
    auto* l_pRow = new QHBoxLayout();
    auto* l_pAdd = new QPushButton(tr("New"));
    auto* l_pDup = new QPushButton(tr("Duplicate"));
    auto* l_pDel = new QPushButton(tr("Delete"));
    l_pRow->addWidget(l_pAdd);
    l_pRow->addWidget(l_pDup);
    l_pRow->addWidget(l_pDel);
    l_pV->addLayout(l_pRow);
    auto* l_pAgain = new QPushButton(tr("+ Again"));
    l_pAgain->setToolTip(tr("The selected monster once more in the battle (e.g. three bats)."));
    l_pV->addWidget(l_pAgain);
    connect(l_pAgain, &QPushButton::clicked, this, [this] { if (!m_oCurrent.isEmpty()) f_AddToBattle(m_oCurrent); });
    auto* l_pRow2 = new QHBoxLayout();
    auto* l_pSave = new QPushButton(tr("Save monsters"));
    auto* l_pRevert = new QPushButton(tr("Revert"));
    l_pRow2->addWidget(l_pSave);
    l_pRow2->addWidget(l_pRevert);
    l_pV->addLayout(l_pRow2);

    connect(m_pSearch, &QLineEdit::textChanged, this, [this](const QString& l_oT) {
        for (int l_iI = 0; l_iI < m_pMonsters->count(); l_iI++) {
            QListWidgetItem* l_pIt = m_pMonsters->item(l_iI);
            l_pIt->setHidden(!l_oT.isEmpty() && !l_pIt->text().contains(l_oT, Qt::CaseInsensitive));
        }
    });
    connect(m_pMonsters, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* l_pIt) {
        if (l_pIt) f_ShowMonster(l_pIt->data(Qt::UserRole).toString());
    });
    connect(m_pMonsters, &QListWidget::itemChanged, this, [this](QListWidgetItem* l_pIt) {
        if (m_bFilling) return;
        const QString l_oId = l_pIt->data(Qt::UserRole).toString();
        const bool l_bOn = l_pIt->checkState() == Qt::Checked;
        if (l_bOn != m_oInBattle.contains(l_oId) && !f_SetInBattle(l_oId, l_bOn)) {
            m_bFilling = true;
            l_pIt->setCheckState(Qt::Unchecked);
            m_bFilling = false;
        }
    });
    connect(l_pAdd, &QPushButton::clicked, this, [this] {
        const QString l_oId = QInputDialog::getText(this, tr("New monster"), tr("Its id (enemies.json key, e.g. slime_red):")).trimmed();
        if (l_oId.isEmpty()) return;
        if (m_jEnemies.contains(f_Ss(l_oId))) { f_Warn(tr("New monster"), tr("\"%1\" is already there.").arg(l_oId)); return; }
        ojson l_jM = ojson::object();
        ojson l_jName = ojson::object();
        l_jName["en"] = f_Ss(l_oId);
        l_jM["name"] = l_jName;
        l_jM["sprite"] = f_Ss(l_oId) + ".png";
        l_jM["hp"] = 20; l_jM["atk"] = 8; l_jM["def"] = 2; l_jM["exp"] = 5; l_jM["weaponExp"] = 1; l_jM["gold"] = 3;
        m_jEnemies[f_Ss(l_oId)] = l_jM;
        m_bMonstersDirty = true;
        f_FillMonsterList();
        f_SelectMonster(l_oId);
    });
    connect(l_pDup, &QPushButton::clicked, this, [this] {
        if (m_oCurrent.isEmpty()) return;
        const QString l_oId = QInputDialog::getText(this, tr("Duplicate"), tr("The copy's id:"), QLineEdit::Normal,
                                                 m_oCurrent + QStringLiteral("_2")).trimmed();
        if (l_oId.isEmpty() || m_jEnemies.contains(f_Ss(l_oId))) return;
        m_jEnemies[f_Ss(l_oId)] = m_jEnemies[f_Ss(m_oCurrent)];
        m_bMonstersDirty = true;
        f_FillMonsterList();
        f_SelectMonster(l_oId);
    });
    connect(l_pDel, &QPushButton::clicked, this, [this] {
        if (m_oCurrent.isEmpty()) return;
        // which floors place it (data/stages/*.json name "monster:<id>")
        QStringList l_oFloors;
        QDirIterator l_oIt(QDir(m_oDataDir).filePath(QStringLiteral("stages")), {QStringLiteral("*.json")}, QDir::Files);
        while (l_oIt.hasNext()) {
            const QString l_oF = l_oIt.next();
            if (f_ReadText(l_oF).contains(QStringLiteral("\"monster:%1\"").arg(m_oCurrent))) l_oFloors << QFileInfo(l_oF).baseName();
        }
        QString l_oText = tr("Delete \"%1\" from enemies.json (when saved)?").arg(m_oCurrent);
        if (!l_oFloors.isEmpty()) l_oText += tr("\n\nThese floors place it: %1").arg(l_oFloors.join(QStringLiteral(", ")));
        if (!f_Ask(tr("Delete"), l_oText)) return;
        f_SetInBattle(m_oCurrent, false);
        m_jEnemies.erase(f_Ss(m_oCurrent));
        m_bMonstersDirty = true;
        m_oCurrent.clear();
        f_FillMonsterList();
    });
    connect(l_pSave, &QPushButton::clicked, this, [this] { f_SaveMonsters(); });
    connect(l_pRevert, &QPushButton::clicked, this, [this] {
        if (m_bMonstersDirty && !f_Ask(tr("Revert"), tr("Drop the monster changes not saved?"))) return;
        f_LoadData();
        f_FillMonsterList();
        f_ShowMonster(m_oCurrent);
    });
    return l_pW;
}

void BattleEditorWindow::f_FillMonsterList() {
    m_bFilling = true;
    m_pMonsters->clear();
    for (auto l_oIt = m_jEnemies.begin(); l_oIt != m_jEnemies.end(); ++l_oIt) {
        if (l_oIt.key().empty() || l_oIt.key()[0] == '_' || !l_oIt.value().is_object()) continue;
        const QString l_oId = f_Qs(l_oIt.key());
        QString l_oLabel = l_oId;
        const QString l_oName = f_NameOf(l_oIt.value());
        if (!l_oName.isEmpty() && l_oName != l_oId) l_oLabel += QStringLiteral("  ·  ") + l_oName;
        if (l_oIt.value().value("boss", false)) l_oLabel = QStringLiteral("★ ") + l_oLabel;
        if (l_oIt.value().value("draft", false)) l_oLabel += tr("  (draft)");
        auto* l_pItem = new QListWidgetItem(l_oLabel, m_pMonsters);
        l_pItem->setData(Qt::UserRole, l_oId);
        l_pItem->setFlags(l_pItem->flags() | Qt::ItemIsUserCheckable);
        l_pItem->setCheckState(m_oInBattle.contains(l_oId) ? Qt::Checked : Qt::Unchecked);
    }
    m_bFilling = false;
    for (int l_iI = 0; l_iI < m_pMonsters->count(); l_iI++)
        if (m_pMonsters->item(l_iI)->data(Qt::UserRole).toString() == m_oCurrent) m_pMonsters->setCurrentRow(l_iI);
    f_UpdateInBattleLabel();
}

int BattleEditorWindow::f_MonsterCount() const { return m_pMonsters->count(); }

bool BattleEditorWindow::f_SetInBattle(const QString& l_oId, bool l_bOn) {
    if (l_bOn) {
        if (m_oInBattle.contains(l_oId)) return true;
        return f_AddToBattle(l_oId);
    }
    m_oInBattle.removeAll(l_oId);
    m_bFilling = true;
    for (int l_iI = 0; l_iI < m_pMonsters->count(); l_iI++) {
        QListWidgetItem* l_pIt = m_pMonsters->item(l_iI);
        if (l_pIt->data(Qt::UserRole).toString() == l_oId) l_pIt->setCheckState(Qt::Unchecked);
    }
    m_bFilling = false;
    f_UpdateInBattleLabel();
    return true;
}

bool BattleEditorWindow::f_AddToBattle(const QString& l_oId) {
    if (m_oInBattle.size() >= toms::g_iMaxFoes) { f_Say(tr("A battle has up to three monsters.")); return false; }
    if (!m_jEnemies.contains(f_Ss(l_oId))) return false;
    m_oInBattle << l_oId;
    m_bFilling = true;
    for (int l_iI = 0; l_iI < m_pMonsters->count(); l_iI++) {
        QListWidgetItem* l_pIt = m_pMonsters->item(l_iI);
        if (l_pIt->data(Qt::UserRole).toString() == l_oId) l_pIt->setCheckState(Qt::Checked);
    }
    m_bFilling = false;
    f_UpdateInBattleLabel();
    return true;
}

void BattleEditorWindow::f_UpdateInBattleLabel() {
    m_pInBattleLabel->setText(tr("IN BATTLE: %1  %2").arg(m_oInBattle.size()).arg(m_oInBattle.join(QStringLiteral(" + "))));
}

bool BattleEditorWindow::f_SelectMonster(const QString& l_oId) {
    for (int l_iI = 0; l_iI < m_pMonsters->count(); l_iI++)
        if (m_pMonsters->item(l_iI)->data(Qt::UserRole).toString() == l_oId) { m_pMonsters->setCurrentRow(l_iI); return true; }
    return false;
}

// ---------------------------------------------------------------- the centre: battle bar + game

QWidget* BattleEditorWindow::f_BuildCentre() {
    auto* l_pW = new QWidget();
    auto* l_pV = new QVBoxLayout(l_pW);
    l_pV->setContentsMargins(0, 0, 0, 0);
    auto* l_pBar = new QHBoxLayout();
    auto* l_pBattle = new QPushButton(tr("▶ Battle"));
    l_pBattle->setToolTip(tr("Start the test battle with the ticked monsters and the test player (again: restart it)."));
    l_pBattle->setStyleSheet(QStringLiteral("font-weight: bold;"));
    m_pPause = new QPushButton(tr("⏸ Pause"));
    m_pPause->setCheckable(true);
    m_pSpeed = new QComboBox();
    m_pSpeed->addItem(QStringLiteral("×0.5"), 0.5);
    m_pSpeed->addItem(QStringLiteral("×1"), 1.0);
    m_pSpeed->addItem(QStringLiteral("×2"), 2.0);
    m_pSpeed->setCurrentIndex(1);
    m_pPace = new QComboBox();
    m_pPace->addItem(tr("pace: from the data"), QString());
    m_pPace->addItem(tr("pace: cooldown"), QStringLiteral("cooldown"));
    m_pPace->addItem(tr("pace: stamina"), QStringLiteral("stamina"));
    m_pPace->setToolTip(tr("Cooldown: each bar waits after a tap. Stamina: no waits, attacks and guards cost stamina. "
                         "Switches the running battle too (docs/gameplay/13 §6.3)."));
    auto* l_pWin = new QPushButton(tr("Win now"));
    auto* l_pLose = new QPushButton(tr("Lose now"));
    m_pAutoRestart = new QCheckBox(tr("auto restart"));
    m_pAutoRestart->setChecked(true);
    m_pAutoRestart->setToolTip(tr("When the battle ends (every monster down, Win now, or a loss), the same setup starts "
                                "again after %1 s.").arg(m_iAutoRestartMs / 1000.0));
    m_pShape = new QComboBox();
    m_pShape->addItem(tr("fill"), 0.0);
    m_pShape->addItem(tr("4:3 landscape"), 4.0 / 3.0);
    m_pShape->addItem(tr("16:9 landscape"), 16.0 / 9.0);
    m_pShape->addItem(tr("3:4 portrait"), 3.0 / 4.0);
    m_pShape->addItem(tr("phone portrait"), 9.0 / 19.5);
    m_pShape->setToolTip(tr("The game view's shape: the battle screen's landscape and portrait layouts (docs/gameplay/12)."));
    l_pBar->addWidget(l_pBattle);
    l_pBar->addWidget(m_pPause);
    l_pBar->addWidget(m_pSpeed);
    l_pBar->addWidget(m_pPace);
    l_pBar->addWidget(l_pWin);
    l_pBar->addWidget(l_pLose);
    l_pBar->addWidget(m_pAutoRestart);
    l_pBar->addStretch(1);
    l_pBar->addWidget(m_pShape);
    l_pV->addLayout(l_pBar);
    m_pViewBox = new AspectBox();
    m_pViewBox->f_SetChild(m_pView);
    l_pV->addWidget(m_pViewBox, 1);

    connect(l_pBattle, &QPushButton::clicked, this, [this] { f_StartBattle(); });
    connect(m_pPause, &QPushButton::toggled, this, [this](bool l_bOn) {
        m_pView->f_SetTimeScale(l_bOn ? 0.0 : m_pSpeed->currentData().toDouble());
    });
    connect(m_pSpeed, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (!m_pPause->isChecked()) m_pView->f_SetTimeScale(m_pSpeed->currentData().toDouble());
    });
    connect(m_pPace, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (Game* l_pG = m_pView->f_Session().f_Game()) l_pG->f_SetBattlePace(f_Ss(m_pPace->currentData().toString()));
    });
    connect(l_pWin, &QPushButton::clicked, this, [this] { if (Game* l_pG = m_pView->f_Session().f_Game()) l_pG->f_DebugForceWin(); });
    connect(l_pLose, &QPushButton::clicked, this, [this] { if (Game* l_pG = m_pView->f_Session().f_Game()) l_pG->f_DebugForceLose(); });
    connect(m_pShape, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_pViewBox->f_SetAspect(m_pShape->currentData().toDouble());
    });
    return l_pW;
}

void BattleEditorWindow::f_SetPace(const QString& l_oPace) {
    const int l_iI = m_pPace->findData(l_oPace);
    if (l_iI >= 0) m_pPace->setCurrentIndex(l_iI);
}

bed::Setup BattleEditorWindow::f_CurrentSetup() const {
    bed::Setup l_oS;
    l_oS.m_strName = m_pSetups ? f_Ss(m_pSetups->currentText()) : std::string();
    l_oS.m_strPace = f_Ss(m_pPace->currentData().toString());
    for (const QString& l_oId : m_oInBattle) {
        bed::SetupFoe l_oF;
        l_oF.m_strId = f_Ss(l_oId);
        // the monster as edited here, saved or not: the scenario's overrides (enemyFromData reads them)
        const ojson& l_jM = m_jEnemies[f_Ss(l_oId)];
        for (const char* l_pszK : {"hp", "atk", "def", "atk_interval_ms", "moves", "look", "break", "events"})
            if (l_jM.contains(l_pszK)) l_oF.m_jOverrides[l_pszK] = l_jM[l_pszK];
        l_oS.m_vFoes.push_back(l_oF);
    }
    l_oS.m_oPlayer.m_iLv = m_pPLv->value();
    l_oS.m_oPlayer.m_iHp = m_pPHp->value();
    l_oS.m_oPlayer.m_iMaxhp = m_pPMaxHp->value();
    l_oS.m_oPlayer.m_strWeapon = f_Ss(m_pPWeapon->currentData().toString());
    l_oS.m_oPlayer.m_iWeaponLevel = m_pPWeaponLevel->value();
    l_oS.m_oPlayer.m_iAttrPoints = m_pPPoints->value();
    for (auto l_oIt = m_pPAttrs->begin(); l_oIt != m_pPAttrs->end(); ++l_oIt)
        if (l_oIt.value()->value() > 0) l_oS.m_oPlayer.m_jAttrs[f_Ss(l_oIt.key())] = l_oIt.value()->value();
    return l_oS;
}

bool BattleEditorWindow::f_StartBattle() {
    if (m_oInBattle.isEmpty()) { f_Warn(tr("Battle"), tr("Tick a monster (+ Battle) first.")); return false; }
    const QString l_oDir = QDir(m_oScratchDir).filePath(QStringLiteral("current"));
    const QString l_oFile = QDir(l_oDir).filePath(QStringLiteral("scenario.json"));
    if (!f_WriteText(l_oFile, f_Qs(bed::f_SetupToScenario(f_CurrentSetup()).dump(2) + "\n"))) {
        f_Warn(tr("Battle"), tr("Could not write %1").arg(l_oFile));
        return false;
    }
    QString l_oErr;
    if (!m_pView->f_StartScenario(l_oDir, l_oErr)) {
        f_Warn(tr("Battle"), tr("The battle could not start:\n%1").arg(l_oErr));
        return false;
    }
    m_iLogShown = -1;
    if (m_bWeaponsDirty) f_Say(tr("Battle started -- the weapon changes are used once saved"));
    else f_Say(tr("Battle: %1").arg(m_oInBattle.join(QStringLiteral(" + "))));
    m_pView->setFocus();
    return true;
}

// ---------------------------------------------------------------- Monster

QWidget* BattleEditorWindow::f_BuildMonsterTab() {
    auto* l_pW = new QWidget();
    auto* l_pF = new QFormLayout(l_pW);
    m_pMId = new QLabel();
    m_pMNameEn = new QLineEdit();
    m_pMNameZh = new QLineEdit();
    m_pMSprite = new QLineEdit();
    m_pMLook = new QLineEdit();
    m_pMLook->setToolTip(tr("Its status map in data/looks.json (empty: \"monster\")."));
    m_pMMoves = new QLineEdit();
    m_pMMoves->setToolTip(tr("What it may do next (data/moves.json), e.g. \"strike, heavy:2, guard\" (id:weight). "
                           "Empty: a plain strike."));
    m_pMParty = new QLineEdit();
    m_pMParty->setToolTip(tr("Monsters that fight beside it when met on a floor, e.g. \"bat, bat\" (up to two)."));
    m_pMBoss = new QCheckBox(tr("boss"));
    l_pF->addRow(tr("id"), m_pMId);
    l_pF->addRow(tr("name (en)"), m_pMNameEn);
    l_pF->addRow(tr("name (zh_TW)"), m_pMNameZh);
    l_pF->addRow(tr("sprite"), m_pMSprite);
    struct N { const char* m_pszKey; const char* m_pszLabel; int m_iMax; };
    for (const N& l_oN : {N{"hp", "HP", 99999}, N{"atk", "ATK", 9999}, N{"def", "DEF", 9999},
                       N{"atk_interval_ms", "attack interval (ms)", 60000}, N{"break", "break points (0: battle.json)", 999},
                       N{"exp", "EXP", 99999}, N{"weaponExp", "weapon EXP", 9999}, N{"gold", "gold", 99999}}) {
        auto* l_pSb = new QSpinBox();
        l_pSb->setRange(0, l_oN.m_iMax);
        (*m_pMNumbers)[QString::fromLatin1(l_oN.m_pszKey)] = l_pSb;
        l_pF->addRow(tr(l_oN.m_pszLabel), l_pSb);
        connect(l_pSb, qOverload<int>(&QSpinBox::valueChanged), this, [this] { f_MonsterEdited(); });
    }
    l_pF->addRow(tr("moves"), m_pMMoves);
    l_pF->addRow(tr("look"), m_pMLook);
    l_pF->addRow(tr("party"), m_pMParty);
    l_pF->addRow(QString(), m_pMBoss);
    m_pReadout = new QLabel();
    m_pReadout->setWordWrap(true);
    m_pReadout->setStyleSheet(QStringLiteral("color: #555;"));
    l_pF->addRow(tr("last battle"), m_pReadout);
    for (QLineEdit* l_pE : {m_pMNameEn, m_pMNameZh, m_pMSprite, m_pMLook, m_pMMoves, m_pMParty})
        connect(l_pE, &QLineEdit::textEdited, this, [this] { f_MonsterEdited(); });
    connect(m_pMBoss, &QCheckBox::toggled, this, [this] { f_MonsterEdited(); });
    return l_pW;
}

void BattleEditorWindow::f_ShowMonster(const QString& l_oId) {
    m_oCurrent = l_oId;
    m_bFilling = true;
    const bool l_bHas = !l_oId.isEmpty() && m_jEnemies.contains(f_Ss(l_oId));
    const ojson l_jM = l_bHas ? m_jEnemies[f_Ss(l_oId)] : ojson::object();
    m_pMId->setText(l_oId);
    m_pMNameEn->setText(l_bHas && l_jM.contains("name") ? f_Qs(l_jM["name"].value("en", std::string())) : QString());
    m_pMNameZh->setText(l_bHas && l_jM.contains("name") ? f_Qs(l_jM["name"].value("zh_TW", std::string())) : QString());
    m_pMSprite->setText(f_Qs(l_jM.value("sprite", std::string())));
    for (auto l_oIt = m_pMNumbers->begin(); l_oIt != m_pMNumbers->end(); ++l_oIt)
        l_oIt.value()->setValue(l_jM.value(f_Ss(l_oIt.key()), l_oIt.key() == QStringLiteral("atk_interval_ms") ? 4000 : 0));
    m_pMMoves->setText(f_Qs(bed::f_MovesToText(l_jM.value("moves", ojson::array()))));
    m_pMLook->setText(f_Qs(l_jM.value("look", std::string())));
    m_pMParty->setText(f_Qs(bed::f_IdsToText(l_jM.value("party", ojson::array()))));
    m_pMBoss->setChecked(l_jM.value("boss", false));
    m_bFilling = false;
    f_ShowEvents();
}

void BattleEditorWindow::f_SetMonsterNumber(const QString& l_oKey, int l_iValue) {
    if (QSpinBox* l_pSb = m_pMNumbers->value(l_oKey)) l_pSb->setValue(l_iValue);
}

void BattleEditorWindow::f_MonsterEdited() {
    if (m_bFilling || m_oCurrent.isEmpty() || !m_jEnemies.contains(f_Ss(m_oCurrent))) return;
    ojson& l_jM = m_jEnemies[f_Ss(m_oCurrent)];
    if (!l_jM.contains("name") || !l_jM["name"].is_object()) l_jM["name"] = ojson::object();
    l_jM["name"]["en"] = f_Ss(m_pMNameEn->text());
    if (!m_pMNameZh->text().isEmpty()) l_jM["name"]["zh_TW"] = f_Ss(m_pMNameZh->text());
    l_jM["sprite"] = f_Ss(m_pMSprite->text());
    for (auto l_oIt = m_pMNumbers->begin(); l_oIt != m_pMNumbers->end(); ++l_oIt) {
        const std::string l_strK = f_Ss(l_oIt.key());
        const int l_iV = l_oIt.value()->value();
        const bool l_bOptional = l_strK == "atk_interval_ms" || l_strK == "break";   // left out when it is the default
        if (l_bOptional && (l_iV == 0 || (l_strK == "atk_interval_ms" && l_iV == 4000) )) l_jM.erase(l_strK);
        else l_jM[l_strK] = l_iV;
    }
    auto l_fnSetOrErase = [&l_jM](const char* l_pszK, const ojson& l_jV) { if (l_jV.empty()) l_jM.erase(l_pszK); else l_jM[l_pszK] = l_jV; };
    l_fnSetOrErase("moves", bed::f_MovesFromText(f_Ss(m_pMMoves->text())));
    l_fnSetOrErase("party", bed::f_IdsFromText(f_Ss(m_pMParty->text())));
    if (m_pMLook->text().trimmed().isEmpty()) l_jM.erase("look"); else l_jM["look"] = f_Ss(m_pMLook->text().trimmed());
    if (m_pMBoss->isChecked()) l_jM["boss"] = true; else l_jM.erase("boss");
    m_bMonstersDirty = true;
    setWindowTitle(tr("TOMS Battle Editor *"));
    f_ApplyToRunningBattle(m_oCurrent);
}

// An edit reaches the battle that is running at once (every monster of that kind in it), not only the next ▶ Battle.
void BattleEditorWindow::f_ApplyToRunningBattle(const QString& l_oId) {
    Game* l_pG = m_pView->f_Session().f_Game();
    if (!l_pG || l_pG->f_BattleFoeCount() == 0 || !m_jEnemies.contains(f_Ss(l_oId))) return;
    l_pG->f_DebugUpdateMonster(f_Ss(l_oId), nlohmann::json::parse(m_jEnemies[f_Ss(l_oId)].dump()));
}

// The last battle's numbers from its log: how hard the monsters are for this player (13 §4.1's readout).
void BattleEditorWindow::f_UpdateReadout() {
    Game* l_pG = m_pView->f_Session().f_Game();
    if (!l_pG || !m_pReadout) return;
    const std::vector<toms::BattleEvent>& l_vEv = l_pG->f_BattleEvents();
    int l_iDealt = 0, l_iDealtN = 0, l_iTaken = 0, l_iTakenN = 0, l_iEndMs = 0;
    for (const toms::BattleEvent& l_oE : l_vEv) {
        if (l_oE.m_strType == "hit" && l_oE.m_iWho < 0) { l_iDealt += l_oE.m_iValue; l_iDealtN++; }
        if (l_oE.m_strType == "hit" && l_oE.m_iWho >= 0) { l_iTaken += l_oE.m_iValue; l_iTakenN++; }
        l_iEndMs = std::max(l_iEndMs, l_oE.m_iMs);
    }
    if (l_vEv.empty()) { m_pReadout->setText(tr("(no battle yet)")); return; }
    m_pReadout->setText(tr("you hit %1 times, %2 on average; the monsters hit %3 times, %4 on average; %5 s")
                          .arg(l_iDealtN).arg(l_iDealtN ? l_iDealt / l_iDealtN : 0).arg(l_iTakenN).arg(l_iTakenN ? l_iTaken / l_iTakenN : 0)
                          .arg(l_iEndMs / 1000.0, 0, 'f', 1));
}

// ---------------------------------------------------------------- Weapon

QWidget* BattleEditorWindow::f_BuildWeaponTab() {
    auto* l_pW = new QWidget();
    auto* l_pV = new QVBoxLayout(l_pW);
    m_pWeapons = new QListWidget();
    m_pWeapons->setMaximumHeight(130);
    for (auto l_oIt = m_jWeaponPlay.begin(); l_oIt != m_jWeaponPlay.end(); ++l_oIt)
        if (!l_oIt.key().empty() && l_oIt.key()[0] != '_') m_pWeapons->addItem(f_Qs(l_oIt.key()));
    l_pV->addWidget(new QLabel(tr("<b>weapon_play.json</b>")));
    l_pV->addWidget(m_pWeapons);
    auto* l_pUse = new QPushButton(tr("Use this weapon in the battle"));
    l_pV->addWidget(l_pUse);
    m_pWLabel = new QLabel();
    l_pV->addWidget(m_pWLabel);
    auto* l_pForm = new QFormLayout();
    for (const char* l_pszK : {"sweepMs", "chargeMs", "hitWindowMs", "lateMs"}) {
        auto* l_pSb = new QSpinBox();
        l_pSb->setRange(0, 10000);
        (*m_pWNumbers)[QString::fromLatin1(l_pszK)] = l_pSb;
        l_pForm->addRow(QString::fromLatin1(l_pszK), l_pSb);
        connect(l_pSb, qOverload<int>(&QSpinBox::valueChanged), this, [this] { f_WeaponEdited(); });
    }
    m_pWPace = new QComboBox();
    m_pWPace->addItem(tr("(the battle's)"), QString());
    m_pWPace->addItem(QStringLiteral("cooldown"), QStringLiteral("cooldown"));
    m_pWPace->addItem(QStringLiteral("stamina"), QStringLiteral("stamina"));
    l_pForm->addRow(tr("pace"), m_pWPace);
    connect(m_pWPace, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { f_WeaponEdited(); });
    m_pWExpFirst = new QSpinBox();
    m_pWExpFirst->setRange(1, 1000000);
    m_pWExpFirst->setToolTip(tr("Weapon EXP from Lv 1 to Lv 2 (levelExp first)."));
    l_pForm->addRow(tr("EXP to Lv 2"), m_pWExpFirst);
    m_pWExpScale = new QDoubleSpinBox();
    m_pWExpScale->setRange(1.0, 10.0);
    m_pWExpScale->setSingleStep(0.1);
    m_pWExpScale->setToolTip(tr("Each next level needs this many times the last (levelExp scale)."));
    l_pForm->addRow(tr("EXP growth x"), m_pWExpScale);
    connect(m_pWExpFirst, qOverload<int>(&QSpinBox::valueChanged), this, [this] { f_WeaponEdited(); f_ShowLevelExp(); });
    connect(m_pWExpScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { f_WeaponEdited(); f_ShowLevelExp(); });
    l_pV->addLayout(l_pForm);
    l_pV->addWidget(new QLabel(tr("<b>Levels</b>: points = the prompts or circles of an attack; the rest = what the level adds to the player's attributes.")));
    QStringList l_oLevelHeads{tr("Lv"), tr("EXP to next"), QStringLiteral("points")};
    for (const QString& l_oId : m_oAttrIds) l_oLevelHeads << l_oId;
    m_pWLevels = new QTableWidget(0, l_oLevelHeads.size());
    m_pWLevels->setHorizontalHeaderLabels(l_oLevelHeads);
    m_pWLevels->verticalHeader()->setVisible(false);
    m_pWLevels->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    l_pV->addWidget(m_pWLevels, 2);
    connect(m_pWLevels, &QTableWidget::cellChanged, this, [this] { f_WeaponEdited(); });
    l_pV->addWidget(new QLabel(tr("<b>Results</b> by points matched (13 §4.2): the highest row reached applies; hits \"all\" = every point. "
                               "A particle not in battle.particle is red.")));
    m_pResults = new QTableWidget(0, 8);
    m_pResults->setHorizontalHeaderLabels({tr("hits"), tr("name"), tr("damage"), tr("fx"), tr("sound"), tr("stunMs"),
                                         tr("pushMs"), tr("breakAdd")});
    m_pResults->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    l_pV->addWidget(m_pResults, 1);
    auto* l_pRow = new QHBoxLayout();
    auto* l_pAddRow = new QPushButton(tr("+ Row"));
    auto* l_pDelRow = new QPushButton(tr("− Row"));
    auto* l_pSave = new QPushButton(tr("Save weapons"));
    l_pRow->addWidget(l_pAddRow);
    l_pRow->addWidget(l_pDelRow);
    l_pRow->addStretch(1);
    l_pRow->addWidget(l_pSave);
    l_pV->addLayout(l_pRow);

    connect(m_pWeapons, &QListWidget::currentTextChanged, this, [this](const QString& l_oK) { f_ShowWeapon(l_oK); });
    connect(l_pUse, &QPushButton::clicked, this, [this] {
        const QString l_oK = m_oCurrentWeapon;
        if (l_oK == QStringLiteral("default")) { f_Say(tr("\"default\" is the settings of weapons without their own.")); return; }
        const int l_iI = m_pPWeapon->findData(l_oK == QStringLiteral("unarmed") ? QString() : l_oK);
        if (l_iI >= 0) m_pPWeapon->setCurrentIndex(l_iI);
        else { m_pPWeapon->addItem(l_oK, l_oK); m_pPWeapon->setCurrentIndex(m_pPWeapon->count() - 1); }
        f_Say(tr("The test player uses %1 (▶ Battle to try it)").arg(l_oK));
    });
    connect(m_pResults, &QTableWidget::cellChanged, this, [this] { f_WeaponEdited(); });
    connect(l_pAddRow, &QPushButton::clicked, this, [this] {
        m_bFilling = true;
        const int l_iR = m_pResults->rowCount();
        m_pResults->insertRow(l_iR);
        for (int l_iC = 0; l_iC < m_pResults->columnCount(); l_iC++) m_pResults->setItem(l_iR, l_iC, new QTableWidgetItem());
        m_pResults->item(l_iR, 0)->setText(QStringLiteral("all"));
        m_pResults->item(l_iR, 2)->setText(QStringLiteral("1"));
        m_bFilling = false;
        f_WeaponEdited();
    });
    connect(l_pDelRow, &QPushButton::clicked, this, [this] {
        if (m_pResults->currentRow() >= 0) { m_pResults->removeRow(m_pResults->currentRow()); f_WeaponEdited(); }
    });
    connect(l_pSave, &QPushButton::clicked, this, [this] { f_SaveWeapons(); });
    return l_pW;
}

void BattleEditorWindow::f_ShowWeapon(const QString& l_oKey) {
    m_oCurrentWeapon = l_oKey;
    m_bFilling = true;
    const ojson l_jW = m_jWeaponPlay.value(f_Ss(l_oKey), ojson::object());
    const ojson l_jDef = m_jWeaponPlay.value("default", ojson::object());
    for (auto l_oIt = m_pWNumbers->begin(); l_oIt != m_pWNumbers->end(); ++l_oIt)
        l_oIt.value()->setValue(l_jW.value(f_Ss(l_oIt.key()), l_jDef.value(f_Ss(l_oIt.key()), 0)));
    m_pWPace->setCurrentIndex(std::max(0, m_pWPace->findData(f_Qs(l_jW.value("pace", std::string())))));
    const bool l_bOwn = l_jW.contains("results");
    m_pWLabel->setText(l_bOwn ? tr("Its own results:") : tr("No results of its own: the default's apply (adding a row gives it its own)."));
    {   // each level: a sparse list in the file shows every level, each with what it has from the entry at or below it
        const ojson l_jLevels = l_jW.value("levels", ojson::array());
        const ojson l_jExp = l_jW.value("levelExp", ojson::object());
        m_pWExpFirst->setValue(l_jExp.value("first", 5));
        m_pWExpScale->setValue(l_jExp.value("scale", 2.0));
        int l_iTop = 10;
        for (const auto& l_jL : l_jLevels) l_iTop = std::max(l_iTop, l_jL.value("lv", 1));
        m_pWLevels->setRowCount(l_iTop);
        for (int l_iLv = 1; l_iLv <= l_iTop; l_iLv++) {
            ojson l_jAt = ojson::object();
            for (const auto& l_jL : l_jLevels) if (l_jL.value("lv", 1) <= l_iLv) l_jAt = l_jL;
            auto l_fnSet = [&](int l_iC, const QString& l_oT, bool l_bEditable) {
                auto* l_pIt = new QTableWidgetItem(l_oT);
                l_pIt->setTextAlignment(Qt::AlignCenter);
                if (!l_bEditable) { l_pIt->setFlags(l_pIt->flags() & ~Qt::ItemIsEditable); l_pIt->setForeground(QColor(120, 120, 120)); }
                m_pWLevels->setItem(l_iLv - 1, l_iC, l_pIt);
            };
            l_fnSet(0, QString::number(l_iLv), false);
            l_fnSet(1, QString(), false);
            l_fnSet(2, QString::number(l_jAt.value("points", 3)), true);
            const ojson l_jStat = l_jAt.value("stats", ojson::object());
            for (int l_iA = 0; l_iA < m_oAttrIds.size(); l_iA++) l_fnSet(3 + l_iA, QString::number(l_jStat.value(f_Ss(m_oAttrIds[l_iA]), 0)), true);
        }
        f_ShowLevelExp();
    }
    m_pResults->setRowCount(0);
    const ojson l_jRows = l_bOwn ? l_jW["results"] : ojson::array();
    for (const auto& l_jR : l_jRows) {
        const int l_iI = m_pResults->rowCount();
        m_pResults->insertRow(l_iI);
        auto l_fnCell = [&](int l_iC, const QString& l_oT) { m_pResults->setItem(l_iI, l_iC, new QTableWidgetItem(l_oT)); };
        l_fnCell(0, l_jR.contains("hits") ? (l_jR["hits"].is_string() ? f_Qs(l_jR["hits"].get<std::string>()) : QString::number(l_jR["hits"].get<int>())) : QString());
        l_fnCell(1, f_Qs(l_jR.value("name", std::string())));
        l_fnCell(2, QString::number(l_jR.value("damage", 1.0)));
        l_fnCell(3, f_Qs(l_jR.value("fx", std::string())));
        l_fnCell(4, f_Qs(l_jR.value("sound", std::string())));
        l_fnCell(5, QString::number(l_jR.value("stunMs", 0)));
        l_fnCell(6, QString::number(l_jR.value("pushMs", 0)));
        l_fnCell(7, QString::number(l_jR.value("breakAdd", 0.0)));
        const QString l_oFx = f_Qs(l_jR.value("fx", std::string()));
        if (!l_oFx.isEmpty() && !m_oEffects.contains(l_oFx)) m_pResults->item(l_iI, 3)->setBackground(QColor(255, 200, 200));
    }
    m_bFilling = false;
}

void BattleEditorWindow::f_ShowLevelExp() {
    const bool l_bWasFilling = m_bFilling;
    m_bFilling = true;
    toms::WeaponPlay l_oW;
    l_oW.m_iLevelExpFirst = m_pWExpFirst->value();
    l_oW.m_fLevelExpScale = (float)m_pWExpScale->value();
    toms::WeaponLevel l_oTop;
    l_oTop.m_iLv = m_pWLevels->rowCount();
    l_oW.m_vLevels.push_back(l_oTop);
    for (int l_iR = 0; l_iR < m_pWLevels->rowCount(); l_iR++) {
        const int l_iNeed = toms::f_ExpToNextLevel(l_oW, l_iR + 1);
        if (QTableWidgetItem* l_pIt = m_pWLevels->item(l_iR, 1)) l_pIt->setText(l_iNeed > 0 ? QString::number(l_iNeed) : tr("top"));
    }
    m_bFilling = l_bWasFilling;
}

bool BattleEditorWindow::f_SelectWeapon(const QString& l_oKey) {
    const auto l_vFound = m_pWeapons->findItems(l_oKey, Qt::MatchExactly);
    if (l_vFound.isEmpty()) return false;
    m_pWeapons->setCurrentItem(l_vFound.first());
    m_pTabs->setCurrentIndex(1);
    return m_oCurrentWeapon == l_oKey;
}

bool BattleEditorWindow::f_SetWeaponLevelCell(int l_iLv, const QString& l_oColumn, int l_iValue) {
    const int l_iC = l_oColumn == QStringLiteral("points") ? 2 : m_oAttrIds.indexOf(l_oColumn) < 0 ? -1 : 3 + (int)m_oAttrIds.indexOf(l_oColumn);
    QTableWidgetItem* l_pIt = l_iC >= 0 && l_iLv >= 1 && l_iLv <= m_pWLevels->rowCount() ? m_pWLevels->item(l_iLv - 1, l_iC) : nullptr;
    if (!l_pIt) return false;
    l_pIt->setText(QString::number(l_iValue));
    return true;
}

QString BattleEditorWindow::f_WeaponLevelsJson(const QString& l_oKey) const {
    return f_Qs(m_jWeaponPlay.value(f_Ss(l_oKey), ojson::object()).value("levels", ojson::array()).dump());
}

bool BattleEditorWindow::f_GrabWeaponTab(const QString& l_oPng) {
    resize(1500, 1000);
    m_pTabs->setCurrentIndex(1);
    m_pTabs->resize(760, 980);
    return m_pTabs->grab().save(l_oPng);
}

void BattleEditorWindow::f_WeaponEdited() {
    if (m_bFilling || m_oCurrentWeapon.isEmpty()) return;
    ojson& l_jW = m_jWeaponPlay[f_Ss(m_oCurrentWeapon)];
    if (!l_jW.is_object()) l_jW = ojson::object();
    for (auto l_oIt = m_pWNumbers->begin(); l_oIt != m_pWNumbers->end(); ++l_oIt) l_jW[f_Ss(l_oIt.key())] = l_oIt.value()->value();
    if (m_pWPace->currentData().toString().isEmpty()) l_jW.erase("pace"); else l_jW["pace"] = f_Ss(m_pWPace->currentData().toString());
    ojson l_jRows = ojson::array();
    for (int l_iR = 0; l_iR < m_pResults->rowCount(); l_iR++) {
        auto l_fnText = [&](int l_iC) { QTableWidgetItem* l_pIt = m_pResults->item(l_iR, l_iC); return l_pIt ? l_pIt->text().trimmed() : QString(); };
        ojson l_jRow = ojson::object();
        bool l_bNum = false;
        const int l_iHits = l_fnText(0).toInt(&l_bNum);
        l_jRow["hits"] = l_bNum ? ojson(l_iHits) : ojson("all");
        if (!l_fnText(1).isEmpty()) l_jRow["name"] = f_Ss(l_fnText(1));
        l_jRow["damage"] = l_fnText(2).isEmpty() ? 1.0 : l_fnText(2).toDouble();
        if (!l_fnText(3).isEmpty()) l_jRow["fx"] = f_Ss(l_fnText(3));
        if (!l_fnText(4).isEmpty()) l_jRow["sound"] = f_Ss(l_fnText(4));
        if (l_fnText(5).toInt() > 0) l_jRow["stunMs"] = l_fnText(5).toInt();
        if (l_fnText(6).toInt() > 0) l_jRow["pushMs"] = l_fnText(6).toInt();
        if (l_fnText(7).toDouble() > 0) l_jRow["breakAdd"] = l_fnText(7).toDouble();
        l_jRows.push_back(l_jRow);
        if (QTableWidgetItem* l_pFx = m_pResults->item(l_iR, 3)) {
            m_bFilling = true;
            l_pFx->setBackground(!l_fnText(3).isEmpty() && !m_oEffects.contains(l_fnText(3)) ? QColor(255, 200, 200) : QColor(Qt::white));
            m_bFilling = false;
        }
    }
    if (l_jRows.empty()) l_jW.erase("results"); else l_jW["results"] = l_jRows;
    {   // each level, every one written out; a stat at 0 is left out
        ojson l_jLevels = ojson::array();
        for (int l_iR = 0; l_iR < m_pWLevels->rowCount(); l_iR++) {
            auto l_fnNum = [&](int l_iC) { QTableWidgetItem* l_pIt = m_pWLevels->item(l_iR, l_iC); return l_pIt ? l_pIt->text().trimmed().toInt() : 0; };
            ojson l_jLv = ojson::object();
            l_jLv["lv"] = l_iR + 1;
            l_jLv["points"] = std::max(1, l_fnNum(2));
            ojson l_jStat = ojson::object();
            for (int l_iA = 0; l_iA < m_oAttrIds.size(); l_iA++) if (l_fnNum(3 + l_iA) != 0) l_jStat[f_Ss(m_oAttrIds[l_iA])] = l_fnNum(3 + l_iA);
            if (!l_jStat.empty()) l_jLv["stats"] = l_jStat;
            l_jLevels.push_back(l_jLv);
        }
        if (!l_jLevels.empty()) l_jW["levels"] = l_jLevels;
        ojson l_jExp = ojson::object();
        l_jExp["first"] = m_pWExpFirst->value();
        l_jExp["scale"] = m_pWExpScale->value();
        l_jW["levelExp"] = l_jExp;
    }
    m_bWeaponsDirty = true;
    setWindowTitle(tr("TOMS Battle Editor *"));
}

// ---------------------------------------------------------------- Player (and the growth calculator)

QWidget* BattleEditorWindow::f_BuildPlayerTab() {
    auto* l_pW = new QWidget();
    auto* l_pV = new QVBoxLayout(l_pW);
    auto* l_pBox = new QGroupBox(tr("The test player"));
    auto* l_pF = new QFormLayout(l_pBox);
    auto l_fnSpin = [](int l_iLo, int l_iHi, int l_iVal) { auto* l_pS = new QSpinBox(); l_pS->setRange(l_iLo, l_iHi); l_pS->setValue(l_iVal); return l_pS; };
    m_pPLv = l_fnSpin(1, 99, 1);
    m_pPHp = l_fnSpin(1, 99999, 120);
    m_pPMaxHp = l_fnSpin(1, 99999, 120);
    m_pPWeapon = new QComboBox();
    m_pPWeapon->addItem(tr("(unarmed)"), QString());
    for (auto l_oIt = m_jWeaponPlay.begin(); l_oIt != m_jWeaponPlay.end(); ++l_oIt)
        if (!l_oIt.key().empty() && l_oIt.key()[0] != '_' && l_oIt.key() != "default" && l_oIt.key() != "unarmed")
            m_pPWeapon->addItem(f_Qs(l_oIt.key()), f_Qs(l_oIt.key()));
    m_pPWeapon->setCurrentIndex(std::max(0, m_pPWeapon->findData(QStringLiteral("bow"))));
    m_pPWeaponLevel = l_fnSpin(1, 99, 1);
    m_pPWeaponLevel->setToolTip(tr("The weapon's level for this battle (the game stops at its top level). EXP per level: weapon_play.json levelExp."));
    m_pPPoints = l_fnSpin(0, 9999, 0);
    l_pF->addRow(tr("level"), m_pPLv);
    l_pF->addRow(tr("HP"), m_pPHp);
    l_pF->addRow(tr("max HP"), m_pPMaxHp);
    l_pF->addRow(tr("weapon"), m_pPWeapon);
    l_pF->addRow(tr("weapon level"), m_pPWeaponLevel);
    for (const QString& l_oId : m_oAttrIds) {
        auto* l_pS = l_fnSpin(0, 999, 0);
        (*m_pPAttrs)[l_oId] = l_pS;
        l_pF->addRow(l_oId.toUpper() + tr(" (placed)"), l_pS);
    }
    l_pF->addRow(tr("points to place"), m_pPPoints);
    l_pV->addWidget(l_pBox);

    auto* l_pCalc = new QGroupBox(tr("Growth calculator (13 §4.3): T points by level L, on a curve"));
    auto* l_pCv = new QVBoxLayout(l_pCalc);
    auto* l_pRow = new QHBoxLayout();
    m_pCTotal = l_fnSpin(0, 9999, 100);
    m_pCLevel = l_fnSpin(2, 99, 20);
    m_pCShape = new QDoubleSpinBox();
    m_pCShape->setRange(0.2, 4.0);
    m_pCShape->setSingleStep(0.1);
    m_pCShape->setValue(1.3);
    m_pCShape->setToolTip(tr("k: 1 = the same each level, above 1 = more later, below 1 = more early."));
    l_pRow->addWidget(new QLabel(tr("T")));
    l_pRow->addWidget(m_pCTotal);
    l_pRow->addWidget(new QLabel(tr("by level L")));
    l_pRow->addWidget(m_pCLevel);
    l_pRow->addWidget(new QLabel(tr("k")));
    l_pRow->addWidget(m_pCShape);
    l_pCv->addLayout(l_pRow);
    m_pCTable = new QTableWidget(0, 3);
    m_pCTable->setHorizontalHeaderLabels({tr("level"), tr("points"), tr("total")});
    m_pCTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pCTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    l_pCv->addWidget(m_pCTable, 1);
    m_pCSum = new QLabel();
    l_pCv->addWidget(m_pCSum);
    auto* l_pRow2 = new QHBoxLayout();
    auto* l_pApply = new QPushButton(tr("Apply to the test player"));
    l_pApply->setToolTip(tr("The test player at level L with T points to place."));
    auto* l_pSaveT = new QPushButton(tr("Save as the game's table"));
    l_pSaveT->setToolTip(tr("data/stats.json levelUp \"points\": neighbouring levels with the same points merged."));
    l_pRow2->addWidget(l_pApply);
    l_pRow2->addWidget(l_pSaveT);
    l_pCv->addLayout(l_pRow2);
    l_pV->addWidget(l_pCalc, 1);

    auto l_fnRecalc = [this] { f_Calculate(m_pCTotal->value(), m_pCLevel->value(), m_pCShape->value()); };
    connect(m_pCTotal, qOverload<int>(&QSpinBox::valueChanged), this, l_fnRecalc);
    connect(m_pCLevel, qOverload<int>(&QSpinBox::valueChanged), this, l_fnRecalc);
    connect(m_pCShape, qOverload<double>(&QDoubleSpinBox::valueChanged), this, l_fnRecalc);
    connect(l_pApply, &QPushButton::clicked, this, [this] {
        m_pPLv->setValue(m_pCLevel->value());
        m_pPPoints->setValue(m_pCTotal->value());
        for (QSpinBox* l_pS : *m_pPAttrs) l_pS->setValue(0);
        f_Say(tr("The test player: level %1 with %2 points to place").arg(m_pCLevel->value()).arg(m_pCTotal->value()));
    });
    connect(l_pSaveT, &QPushButton::clicked, this, [this] { f_SaveGrowthTable(); });
    l_fnRecalc();
    return l_pW;
}

std::vector<int> BattleEditorWindow::f_Calculate(int l_iTotal, int l_iLevel, double l_dK) {
    const std::vector<int> l_vPer = bed::f_LevelPoints(l_iTotal, l_iLevel, l_dK);
    if (m_pCTable) {
        m_pCTable->setRowCount((int)l_vPer.size());
        int l_iRun = 0;
        for (size_t l_nI = 0; l_nI < l_vPer.size(); l_nI++) {
            l_iRun += l_vPer[l_nI];
            m_pCTable->setItem((int)l_nI, 0, new QTableWidgetItem(QString::number(l_nI + 2)));
            auto* l_pBar = new QTableWidgetItem(QString::number(l_vPer[l_nI]) + QStringLiteral("  ") + QString(l_vPer[l_nI], QChar(0x2588)));
            m_pCTable->setItem((int)l_nI, 1, l_pBar);
            m_pCTable->setItem((int)l_nI, 2, new QTableWidgetItem(QString::number(l_iRun)));
        }
        m_pCSum->setText(tr("Lv 2–%1: %2 points in all").arg(l_iLevel).arg(l_iRun));
    }
    return l_vPer;
}

// ---------------------------------------------------------------- Events

QWidget* BattleEditorWindow::f_BuildEventsTab() {
    auto* l_pW = new QWidget();
    auto* l_pV = new QVBoxLayout(l_pW);
    m_pEvMonster = new QLabel();
    l_pV->addWidget(m_pEvMonster);
    l_pV->addWidget(new QLabel(tr("Triggers: beforeBattle, battleStart, hpBelow, playerHpBelow, attacks, everyMs, perfectHit, falls.\n"
                               "Actions: say, stat (atk / def / interval, mul / add), transformInto (hp keepShare / full), heal, "
                               "summon, flee, anim, fx, sound, flag (13 §4.4).")));
    m_pEvText = new QPlainTextEdit();
    m_pEvText->setFont(QFont(QStringLiteral("Consolas"), 10));
    l_pV->addWidget(m_pEvText, 2);
    auto* l_pRow = new QHBoxLayout();
    auto* l_pApply = new QPushButton(tr("Apply"));
    l_pApply->setToolTip(tr("Check the text and keep it as the monster's events (Save monsters writes the file)."));
    auto* l_pExample = new QPushButton(tr("Insert an example"));
    l_pRow->addWidget(l_pApply);
    l_pRow->addWidget(l_pExample);
    l_pRow->addStretch(1);
    l_pV->addLayout(l_pRow);
    l_pV->addWidget(new QLabel(tr("In the running battle:")));
    m_pEvList = new QListWidget();
    l_pV->addWidget(m_pEvList, 1);
    auto* l_pFire = new QPushButton(tr("Fire now"));
    l_pFire->setToolTip(tr("Run the selected event's actions now, in the running battle (the monster must be in it)."));
    l_pV->addWidget(l_pFire);
    connect(l_pApply, &QPushButton::clicked, this, &BattleEditorWindow::f_ApplyEventsText);
    connect(l_pExample, &QPushButton::clicked, this, [this] {
        m_pEvText->setPlainText(QStringLiteral(
            "[\n  { \"on\": \"hpBelow\", \"value\": 50, \"do\": [ { \"say\": \"It grows angry!\" }, { \"stat\": \"atk\", \"mul\": 1.3 }, "
            "{ \"anim\": \"special\" } ] },\n  { \"on\": \"hpBelow\", \"value\": 20, \"do\": [ { \"transformInto\": \"slime\", \"hp\": \"keepShare\" } ] }\n]\n"));
    });
    connect(l_pFire, &QPushButton::clicked, this, [this] {
        Game* l_pG = m_pView->f_Session().f_Game();
        const int l_iFoe = m_oInBattle.indexOf(m_oCurrent);
        if (!l_pG || l_iFoe < 0 || l_pG->f_BattleFoeCount() == 0) { f_Say(tr("Start a battle with this monster in it first.")); return; }
        l_pG->f_DebugFireFoeEvent(l_iFoe, m_pEvList->currentRow());
    });
    return l_pW;
}

void BattleEditorWindow::f_ShowEvents() {
    if (!m_pEvText) return;
    const ojson l_jM = !m_oCurrent.isEmpty() && m_jEnemies.contains(f_Ss(m_oCurrent)) ? m_jEnemies[f_Ss(m_oCurrent)] : ojson::object();
    m_pEvMonster->setText(tr("<b>%1</b>'s events (enemies.json \"events\")").arg(m_oCurrent));
    const ojson l_jEv = l_jM.value("events", ojson::array());
    m_pEvText->setPlainText(l_jEv.empty() ? QStringLiteral("[]") : f_Qs(l_jEv.dump(2)));
    m_pEvList->clear();
    for (const auto& l_jE : l_jEv) {
        QString l_oLine = f_Qs(l_jE.value("on", std::string()));
        if (l_jE.contains("value")) l_oLine += QStringLiteral(" ") + f_Qs(l_jE["value"].dump());
        l_oLine += QStringLiteral("  →  ") + f_Qs(l_jE.value("do", ojson::array()).dump());
        m_pEvList->addItem(l_oLine);
    }
}

void BattleEditorWindow::f_ApplyEventsText() {
    if (m_oCurrent.isEmpty()) return;
    ojson l_jEv;
    if (!f_Parse(m_pEvText->toPlainText(), l_jEv) || !l_jEv.is_array()) {
        f_Warn(tr("Events"), tr("The events are not a JSON list: [ { \"on\": ..., \"do\": [ ... ] }, ... ]"));
        return;
    }
    for (const auto& l_jE : l_jEv)
        if (!l_jE.is_object() || !l_jE.contains("on") || !l_jE.contains("do")) {
            f_Warn(tr("Events"), tr("Each event needs \"on\" and \"do\": %1").arg(f_Qs(l_jE.dump())));
            return;
        }
    ojson& l_jM = m_jEnemies[f_Ss(m_oCurrent)];
    if (l_jEv.empty()) l_jM.erase("events"); else l_jM["events"] = l_jEv;
    m_bMonstersDirty = true;
    setWindowTitle(tr("TOMS Battle Editor *"));
    f_ApplyToRunningBattle(m_oCurrent);
    f_ShowEvents();
    f_Say(tr("Events kept for %1 (▶ Battle uses them; Save monsters writes them)").arg(m_oCurrent));
}

// ---------------------------------------------------------------- Log

QWidget* BattleEditorWindow::f_BuildLogTab() {
    auto* l_pW = new QWidget();
    auto* l_pV = new QVBoxLayout(l_pW);
    m_pLog = new QPlainTextEdit();
    m_pLog->setReadOnly(true);
    m_pLog->setFont(QFont(QStringLiteral("Consolas"), 10));
    l_pV->addWidget(new QLabel(tr("Every event of the running battle, with its time (13 §4.5).")));
    l_pV->addWidget(m_pLog, 1);
    return l_pW;
}

// ---- the Scenarios tab (it was toms_editor's Scenarios dock until 2026-10-31; docs/gameplay/08_TEST_SCENARIOS.md) ----

// tests/scenarios/ of the repository: the assets folder is <repo>/assets/media.
QString BattleEditorWindow::f_ScenarioRoot() const {
    return QDir::cleanPath(QDir(m_pView->f_AssetDir()).filePath(QStringLiteral("../../tests/scenarios")));
}

int BattleEditorWindow::f_ScenarioCount() const {
    int l_iN = 0;
    for (int l_iG = 0; m_pScenarioList && l_iG < m_pScenarioList->topLevelItemCount(); l_iG++) l_iN += m_pScenarioList->topLevelItem(l_iG)->childCount();
    return l_iN;
}

// One row per scenario folder (a folder holding scenario.json): its name, its scene, and its _comment as the tooltip.
// "_shared" and other folders starting with "_" hold shared files, not scenarios; "_cloud" (downloaded ones, docs/gameplay/09)
// is listed as a group of its own when it is there.
void BattleEditorWindow::f_FillScenarioList() {
    m_pScenarioList->clear();
    auto l_fnAddRoot = [this](const QString& l_oDir, const QString& l_oTitle) {
        const QDir l_oRoot(l_oDir);
        if (!l_oRoot.exists()) return;
        auto* l_pGroup = new QTreeWidgetItem(m_pScenarioList, {l_oTitle});
        l_pGroup->setToolTip(0, QDir::toNativeSeparators(l_oRoot.absolutePath()));
        l_pGroup->setFirstColumnSpanned(true);
        for (const QFileInfo& l_oF : l_oRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString l_oFile = QDir(l_oF.absoluteFilePath()).filePath(QStringLiteral("scenario.json"));
            if (l_oF.fileName().startsWith(QLatin1Char('_')) || !QFileInfo::exists(l_oFile)) continue;
            QFile l_oIn(l_oFile);
            QJsonObject l_oJ;
            if (l_oIn.open(QIODevice::ReadOnly)) l_oJ = QJsonDocument::fromJson(l_oIn.readAll()).object();
            auto* l_pIt = new QTreeWidgetItem(l_pGroup, {l_oF.fileName(), l_oJ.value(QStringLiteral("scene")).toString(QStringLiteral("stage"))});
            l_pIt->setData(0, Qt::UserRole, l_oF.absoluteFilePath());
            QString l_oTip = l_oJ.value(QStringLiteral("_comment")).toString();
            if (l_oJ.isEmpty()) l_oTip = tr("scenario.json cannot be read");
            l_pIt->setToolTip(0, l_oTip.isEmpty() ? QDir::toNativeSeparators(l_oF.absoluteFilePath()) : l_oTip);
            l_pIt->setToolTip(1, l_pIt->toolTip(0));
        }
        l_pGroup->setExpanded(true);
        if (l_pGroup->childCount() == 0) delete l_pGroup;
    };
    l_fnAddRoot(f_ScenarioRoot(), tr("tests/scenarios"));
    l_fnAddRoot(QDir(f_ScenarioRoot()).filePath(QStringLiteral("_cloud")), tr("downloaded (_cloud)"));
    if (!m_oExtraScenarioDir.isEmpty()) l_fnAddRoot(m_oExtraScenarioDir, QFileInfo(m_oExtraScenarioDir).fileName());
    if (m_pScenarioList->topLevelItemCount() == 0) {
        auto* l_pNone = new QTreeWidgetItem(m_pScenarioList, {tr("No scenarios in %1").arg(QDir::toNativeSeparators(f_ScenarioRoot()))});
        l_pNone->setFirstColumnSpanned(true);
        l_pNone->setFlags(Qt::NoItemFlags);
    }
}

bool BattleEditorWindow::f_PlayScenario(const QString& l_oPath) {
    // the test battle's auto restart would replace the scenario when its battle ends: off while a scenario plays
    if (m_pAutoRestart) m_pAutoRestart->setChecked(false);
    m_bRestartPending = false;
    QString l_oErr;
    if (!m_pView->f_StartScenario(l_oPath, l_oErr)) {
        f_Warn(tr("The scenario could not start"), l_oErr + tr("\n\nSee docs/gameplay/08_TEST_SCENARIOS.md"));
        return false;
    }
    f_Say(tr("Scenario %1: playing (auto restart off). ▶ Battle goes back to the test battle.").arg(QFileInfo(l_oPath).fileName()));
    m_pView->setFocus();
    return true;
}

QWidget* BattleEditorWindow::f_BuildScenariosTab() {
    auto* l_pW = new QWidget();
    auto* l_pV = new QVBoxLayout(l_pW);
    auto* l_pNote = new QLabel(tr("Every test scenario of tests/scenarios (docs/gameplay/08). Double-click one to play it in the "
                               "game view; ▶ Battle goes back to the test battle."));
    l_pNote->setWordWrap(true);
    l_pV->addWidget(l_pNote);
    m_pScenarioList = new QTreeWidget();
    m_pScenarioList->setColumnCount(2);
    m_pScenarioList->setHeaderLabels({tr("Scenario (double-click to play)"), tr("Scene")});
    m_pScenarioList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pScenarioList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_pScenarioList->header()->setStretchLastSection(false);
    m_pScenarioList->setRootIsDecorated(true);
    m_pScenarioList->setContextMenuPolicy(Qt::CustomContextMenu);
    l_pV->addWidget(m_pScenarioList, 1);
    auto* l_pRow = new QHBoxLayout();
    auto* l_pPlay = new QPushButton(tr("Play"));
    auto* l_pReload = new QPushButton(tr("Reload the list"));
    auto* l_pOpen = new QPushButton(tr("Open scenario folder..."));
    l_pRow->addWidget(l_pPlay);
    l_pRow->addWidget(l_pReload);
    l_pRow->addWidget(l_pOpen);
    l_pRow->addStretch(1);
    l_pV->addLayout(l_pRow);
    f_FillScenarioList();
    auto l_fnPathOf = [](QTreeWidgetItem* l_pIt) { return l_pIt ? l_pIt->data(0, Qt::UserRole).toString() : QString(); };
    connect(m_pScenarioList, &QTreeWidget::itemDoubleClicked, this, [this, l_fnPathOf](QTreeWidgetItem* l_pIt, int) {
        const QString l_oPath = l_fnPathOf(l_pIt);
        if (!l_oPath.isEmpty()) f_PlayScenario(l_oPath);
    });
    connect(l_pPlay, &QPushButton::clicked, this, [this, l_fnPathOf] {
        const QString l_oPath = l_fnPathOf(m_pScenarioList->currentItem());
        if (!l_oPath.isEmpty()) f_PlayScenario(l_oPath);
    });
    connect(l_pReload, &QPushButton::clicked, this, [this] { f_FillScenarioList(); });
    connect(l_pOpen, &QPushButton::clicked, this, [this] {
        const QString l_oDir = QFileDialog::getExistingDirectory(this, tr("A folder of scenario folders, or one scenario"), f_ScenarioRoot());
        if (l_oDir.isEmpty()) return;
        if (QFileInfo::exists(QDir(l_oDir).filePath(QStringLiteral("scenario.json")))) { f_PlayScenario(l_oDir); return; }   // one scenario
        m_oExtraScenarioDir = l_oDir;
        f_FillScenarioList();
    });
    connect(m_pScenarioList, &QWidget::customContextMenuRequested, this, [this, l_fnPathOf](const QPoint& l_oPos) {
        const QString l_oPath = l_fnPathOf(m_pScenarioList->itemAt(l_oPos));
        QMenu l_oM(this);
        if (!l_oPath.isEmpty()) {
            l_oM.addAction(tr("Play"), this, [this, l_oPath] { f_PlayScenario(l_oPath); });
            l_oM.addAction(tr("Open its folder"), this, [l_oPath] { QDesktopServices::openUrl(QUrl::fromLocalFile(l_oPath)); });
            l_oM.addAction(tr("Edit scenario.json"), this, [l_oPath] {
                QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(l_oPath).filePath(QStringLiteral("scenario.json"))));
            });
            l_oM.addSeparator();
        }
        l_oM.addAction(tr("Reload the list"), this, [this] { f_FillScenarioList(); });
        l_oM.exec(m_pScenarioList->viewport()->mapToGlobal(l_oPos));
    });
    return l_pW;
}

void BattleEditorWindow::f_RefreshLog() {
    Game* l_pG = m_pView->f_Session().f_Game();
    if (!l_pG || !m_pLog) return;
    // the battle ended (won or lost): start it again, after a moment to see how it ended
    const bool l_bRunning = l_pG->f_BattleFoeCount() > 0;
    if (m_bBattleRunning && !l_bRunning && m_pAutoRestart && m_pAutoRestart->isChecked() && !m_bRestartPending) {
        m_bRestartPending = true;
        f_Say(tr("The battle is over -- again in %1 s (auto restart)").arg(m_iAutoRestartMs / 1000.0));
        QTimer::singleShot(m_iAutoRestartMs, this, [this] {
            m_bRestartPending = false;
            if (m_pAutoRestart->isChecked() && !m_oInBattle.isEmpty()) f_StartBattle();
        });
    }
    m_bBattleRunning = l_bRunning;
    const std::vector<toms::BattleEvent>& l_vEv = l_pG->f_BattleEvents();
    if ((int)l_vEv.size() == m_iLogShown) return;
    m_iLogShown = (int)l_vEv.size();
    const std::vector<std::string> l_vIds = l_pG->f_BattleFoeIds();
    QString l_oText;
    for (const toms::BattleEvent& l_oE : l_vEv) l_oText += f_Qs(toms::f_BattleEventLine(l_oE, l_vIds)) + QLatin1Char('\n');
    m_pLog->setPlainText(l_oText);
    m_pLog->moveCursor(QTextCursor::End);
    f_UpdateReadout();
}

QString BattleEditorWindow::f_LogText() const { return m_pLog ? m_pLog->toPlainText() : QString(); }

// ---------------------------------------------------------------- test setups

QWidget* BattleEditorWindow::f_BuildSetupsBar() {
    auto* l_pW = new QWidget();
    auto* l_pH = new QHBoxLayout(l_pW);
    l_pH->setContentsMargins(0, 0, 0, 0);
    l_pH->addWidget(new QLabel(tr("<b>TEST SETUPS</b> (tools/battle_editor/setups: editor files, not tests)")));
    m_pSetups = new QComboBox();
    m_pSetups->setMinimumWidth(220);
    auto* l_pLoad = new QPushButton(tr("Load"));
    auto* l_pSave = new QPushButton(tr("+ Save current as…"));
    l_pH->addWidget(m_pSetups);
    l_pH->addWidget(l_pLoad);
    l_pH->addWidget(l_pSave);
    l_pH->addStretch(1);
    connect(l_pLoad, &QPushButton::clicked, this, [this] { if (f_LoadSetup(m_pSetups->currentText())) f_StartBattle(); });
    connect(l_pSave, &QPushButton::clicked, this, [this] {
        const QString l_oName = QInputDialog::getText(this, tr("Save the setup"), tr("Its name (e.g. 3 bats Lv5 sword):"),
                                                   QLineEdit::Normal, m_pSetups->currentText()).trimmed();
        if (!l_oName.isEmpty()) f_SaveSetupAs(l_oName);
    });
    return l_pW;
}

void BattleEditorWindow::f_FillSetups() {
    const QString l_oKeep = m_pSetups->currentText();
    m_pSetups->clear();
    for (const QFileInfo& l_oF : QDir(m_oSetupsDir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        m_pSetups->addItem(l_oF.completeBaseName());
    const int l_iI = m_pSetups->findText(l_oKeep);
    if (l_iI >= 0) m_pSetups->setCurrentIndex(l_iI);
}

bool BattleEditorWindow::f_SaveSetupAs(const QString& l_oName) {
    QString l_oFile = l_oName;
    l_oFile.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    bed::Setup l_oS = f_CurrentSetup();
    l_oS.m_strName = f_Ss(l_oName);
    const QString l_oPath = QDir(m_oSetupsDir).filePath(l_oFile + QStringLiteral(".json"));
    if (!f_WriteText(l_oPath, f_Qs(bed::f_SetupToScenario(l_oS).dump(2) + "\n"))) { f_Warn(tr("Setup"), tr("Could not write %1").arg(l_oPath)); return false; }
    f_FillSetups();
    m_pSetups->setCurrentIndex(m_pSetups->findText(l_oFile));
    f_Say(tr("Saved the setup %1").arg(l_oPath));
    return true;
}

bool BattleEditorWindow::f_LoadSetup(const QString& l_oName) {
    ojson l_jJ;
    bed::Setup l_oS;
    const QString l_oPath = QDir(m_oSetupsDir).filePath(l_oName + QStringLiteral(".json"));
    if (!f_Parse(f_ReadText(l_oPath), l_jJ) || !bed::f_SetupFromScenario(l_jJ, l_oS)) {
        f_Warn(tr("Setup"), tr("%1 is not a battle editor setup.").arg(l_oPath));
        return false;
    }
    for (const QString& l_oId : QStringList(m_oInBattle)) f_SetInBattle(l_oId, false);
    for (const bed::SetupFoe& l_oF : l_oS.m_vFoes) f_AddToBattle(f_Qs(l_oF.m_strId));
    m_pPLv->setValue(l_oS.m_oPlayer.m_iLv);
    m_pPMaxHp->setValue(l_oS.m_oPlayer.m_iMaxhp);
    m_pPHp->setValue(l_oS.m_oPlayer.m_iHp);
    m_pPWeapon->setCurrentIndex(std::max(0, m_pPWeapon->findData(f_Qs(l_oS.m_oPlayer.m_strWeapon))));
    m_pPWeaponLevel->setValue(l_oS.m_oPlayer.m_iWeaponLevel);
    m_pPPoints->setValue(l_oS.m_oPlayer.m_iAttrPoints);
    for (auto l_oIt = m_pPAttrs->begin(); l_oIt != m_pPAttrs->end(); ++l_oIt) l_oIt.value()->setValue(l_oS.m_oPlayer.m_jAttrs.value(f_Ss(l_oIt.key()), 0));
    f_SetPace(f_Qs(l_oS.m_strPace));
    f_Say(tr("Loaded the setup %1").arg(l_oName));
    return true;
}

// ---------------------------------------------------------------- the rest

void BattleEditorWindow::f_Say(const QString& l_oText) { statusBar()->showMessage(l_oText, 6000); }

bool BattleEditorWindow::f_Ask(const QString& l_oTitle, const QString& l_oText) {
    if (m_bQuiet) return true;
    return QMessageBox::question(this, l_oTitle, l_oText) == QMessageBox::Yes;
}

void BattleEditorWindow::f_Warn(const QString& l_oTitle, const QString& l_oText) {
    if (m_bQuiet) { std::fprintf(stderr, "[battle_editor] %s: %s\n", f_Ss(l_oTitle).c_str(), f_Ss(l_oText).c_str()); return; }
    QMessageBox::warning(this, l_oTitle, l_oText);
}

void BattleEditorWindow::closeEvent(QCloseEvent* l_pE) {
    if (f_IsDirty() && !m_bQuiet) {
        const auto l_eB = QMessageBox::question(this, tr("Unsaved changes"),
                                             tr("Save the changed monsters and weapons before closing?"),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (l_eB == QMessageBox::Cancel) { l_pE->ignore(); return; }
        if (l_eB == QMessageBox::Save) {
            if (m_bMonstersDirty && !f_SaveMonsters()) { l_pE->ignore(); return; }
            if (m_bWeaponsDirty && !f_SaveWeapons()) { l_pE->ignore(); return; }
        }
    }
    l_pE->accept();
}
