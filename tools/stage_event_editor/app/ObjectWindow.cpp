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
using StageText::f_Q;

namespace {
QPointer<ObjectWindow> g_oOpen;

// A new type's id: lowercase letters, Chinese (or any other script's) characters, digits and _ -- it becomes a JSON key,
// part of "monster:<id>" and text keys ("item.<id>.name"), and an NPC's file name (npcs/<id>.json), so no spaces and none
// of : # . / , that those are joined with. Every file read and write here takes UTF-8 paths.
bool f_ValidTypeId(const std::string& l_strId) {
    static const QRegularExpression l_oRe(QStringLiteral("^[\\p{Ll}\\p{Lo}\\p{Nd}_]+$"), QRegularExpression::UseUnicodePropertiesOption);
    return l_oRe.match(QString::fromStdString(l_strId)).hasMatch();
}

std::string f_CatOf(const std::string& l_strSem) { return l_strSem.substr(0, l_strSem.find(':')); }
std::string f_IdOf(const std::string& l_strSem) { const size_t l_nC = l_strSem.find(':'); return l_nC == std::string::npos ? std::string() : l_strSem.substr(l_nC + 1); }

QString f_Verb(const std::string& l_strCat) {
    if (l_strCat == "monster") return ObjectWindow::tr("defeats this monster");
    if (l_strCat == "item" || l_strCat == "key") return ObjectWindow::tr("picks this up");
    if (l_strCat == "door" || l_strCat == "gate") return ObjectWindow::tr("opens this");
    if (l_strCat == "npc") return ObjectWindow::tr("talks to this NPC");
    return ObjectWindow::tr("steps here");
}

QString f_CatName(const std::string& l_strCat) {
    if (l_strCat == "monster") return ObjectWindow::tr("monster");
    if (l_strCat == "item") return ObjectWindow::tr("item");
    if (l_strCat == "story") return ObjectWindow::tr("door and key");
    return ObjectWindow::tr("NPC");
}

std::vector<std::string> f_CopyChoices(stage::Project& l_oP, const std::string& l_strCat) {
    if (l_strCat == "monster") return l_oP.f_EnemyIds();
    if (l_strCat == "npc") { std::vector<std::string> l_vOut; for (const auto& [l_strId, l_jN] : l_oP.m_mapNpcs) { (void)l_jN; l_vOut.push_back(l_strId); } return l_vOut; }
    // "story": a key item that opens its own gate (the palette's doors, keys and gates)
    const std::string l_strType = l_strCat == "story" ? "story" : "consumable";
    std::vector<std::string> l_vOut;
    for (const std::string& l_strId : l_oP.f_ItemIds()) if (l_oP.f_ItemType(l_strId) == l_strType) l_vOut.push_back(l_strId);
    return l_vOut;
}
}  // namespace

ObjectWindow* ObjectWindow::f_Current() { return g_oOpen; }

ObjectWindow::ObjectWindow(StageEditor* l_pEd, Kind l_eKind) : QDialog(l_pEd), m_pEd(l_pEd), m_eKind(l_eKind) {
    setAttribute(Qt::WA_DeleteOnClose);
    // A type's window follows the palette and leaves the editor usable (a tool window over it); an object's or a new
    // type's is answered first.
    if (l_eKind == Type) {
        setWindowFlag(Qt::Tool);
        setWindowModality(Qt::NonModal);
    } else {
        setWindowModality(Qt::WindowModal);
    }
    resize(760, 640);
    auto* l_pLay = new QVBoxLayout(this);
    m_pHead = new QLabel(this);
    m_pHead->setTextFormat(Qt::RichText);
    l_pLay->addWidget(m_pHead);
    m_pTabs = new QTabWidget(this);
    m_pTabs->setDocumentMode(true);
    l_pLay->addWidget(m_pTabs, 1);
    connect(m_pTabs, &QTabWidget::currentChanged, this, [this](int l_iI) { if (l_iI >= 0 && m_pTabs->isVisible()) { m_iTab = l_iI; } });
    auto* l_pFoot = new QHBoxLayout();
    m_pNote = new QLabel(this);
    m_pNote->setWordWrap(true);
    m_pNote->setStyleSheet(QStringLiteral("color: #ff9f43"));
    l_pFoot->addWidget(m_pNote, 1);
    m_pDel = new QPushButton(this);
    auto* l_pCancel = new QPushButton(tr("Cancel"), this);
    m_pApply = new QPushButton(this);
    m_pApply->setDefault(true);
    l_pFoot->addWidget(m_pDel);
    l_pFoot->addWidget(l_pCancel);
    l_pFoot->addWidget(m_pApply);
    l_pLay->addLayout(l_pFoot);
    connect(l_pCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_pApply, &QPushButton::clicked, this, &ObjectWindow::f_Apply);
    connect(m_pDel, &QPushButton::clicked, this, &ObjectWindow::f_DoDelete);
}

void ObjectWindow::f_OpenObject(StageEditor* l_pEd, int l_iX, int l_iY) {
    stage::StageDoc* l_pD = l_pEd->f_CurDoc();
    if (!l_pD) return;
    const std::string l_strSem = l_pEd->f_Project().f_SemAt(*l_pD, l_iX, l_iY);
    if (!stage::Project::f_IsEntity(l_strSem)) return;
    if (g_oOpen) g_oOpen->close();
    auto* l_pW = new ObjectWindow(l_pEd, Object);
    l_pW->m_iX = l_iX;
    l_pW->m_iY = l_iY;
    l_pW->m_strSem = l_strSem;
    const Json* l_pO = stage::Project::f_ObjOf(*l_pD, l_iX, l_iY);
    l_pW->m_jW = Json{{"look", l_pO && l_pO->contains("look") ? (*l_pO)["look"] : Json::object()},
                  {"events", l_pO && l_pO->contains("events") ? (*l_pO)["events"] : Json::array()}};
    g_oOpen = l_pW;
    l_pW->f_Render();
    l_pW->show();
}

void ObjectWindow::f_OpenType(StageEditor* l_pEd, const std::string& l_strSem) {
    if (!l_pEd->f_Project().f_TypeRec(l_strSem)) { l_pEd->f_Toast(tr("%1 has no record to edit.").arg(f_Q(l_strSem))); return; }
    if (g_oOpen && g_oOpen->m_eKind == Type) {   // the open type window shows this one, where it is
        if (g_oOpen->m_strSem == l_strSem) { g_oOpen->raise(); return; }
        if (!g_oOpen->f_ConfirmLeave()) return;
        g_oOpen->f_LoadType(l_strSem);
        g_oOpen->f_Render();
        g_oOpen->raise();
        return;
    }
    if (g_oOpen) g_oOpen->close();
    auto* l_pW = new ObjectWindow(l_pEd, Type);
    l_pW->f_LoadType(l_strSem);
    g_oOpen = l_pW;
    l_pW->f_Render();
    l_pW->show();
}

void ObjectWindow::f_LoadType(const std::string& l_strSem) {
    const Json* l_pR = m_pEd->f_Project().f_TypeRec(l_strSem);
    m_strSem = l_strSem;
    m_strCat = f_CatOf(l_strSem);
    m_strId = f_IdOf(l_strSem);
    m_jW = l_pR ? *l_pR : Json::object();
    m_mapTexts.clear();
    if (l_pR)
        for (const char* l_pszK : {"name", "desc", "greeting"})
            if (l_pR->contains(l_pszK) && (*l_pR)[l_pszK].is_string()) m_mapTexts[l_pszK] = m_pEd->f_Project().f_TextOf((*l_pR)[l_pszK].get<std::string>());
    m_jOrig = m_jW;
    m_mapOrigTexts = m_mapTexts;
}

bool ObjectWindow::f_Dirty() const { return m_eKind == Type && (m_jW != m_jOrig || m_mapTexts != m_mapOrigTexts); }

bool ObjectWindow::f_ConfirmLeave() {
    if (!f_Dirty()) return true;
    QMessageBox l_oBox(QMessageBox::Question, tr("Changes not applied"),
                    tr("%1 has changes that are not applied yet.").arg(f_Q(m_pEd->f_Project().f_NameOf(m_strSem))), QMessageBox::NoButton, this);
    QPushButton* l_pApplyB = l_oBox.addButton(tr("Apply"), QMessageBox::AcceptRole);
    QPushButton* l_pDiscardB = l_oBox.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    l_oBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
    l_oBox.setDefaultButton(l_pApplyB);
    l_oBox.exec();
    if (l_oBox.clickedButton() == l_pApplyB) { f_Apply(); return true; }
    return l_oBox.clickedButton() == l_pDiscardB;
}

void ObjectWindow::f_OpenNew(StageEditor* l_pEd, const std::string& l_strCat, const std::string& l_strBrush, const std::string& l_strGroup) {
    if (g_oOpen) g_oOpen->close();
    auto* l_pW = new ObjectWindow(l_pEd, New);
    l_pW->m_strCat = l_strCat;
    l_pW->m_strGroup = l_strGroup;
    l_pW->m_strBrushPrefix = !l_strBrush.empty() ? l_strBrush : l_strCat == "story" ? std::string("gate:") : l_strCat + ":";
    const std::vector<std::string> l_vIds = f_CopyChoices(l_pEd->f_Project(), l_strCat);
    l_pW->m_strFrom = l_vIds.empty() ? std::string() : l_vIds[0];
    g_oOpen = l_pW;
    l_pW->f_Render();
    l_pW->show();
}

// ---- the look being edited -------------------------------------------------------------------------------------------

Json ObjectWindow::f_Look() const {
    if (m_eKind == Object) return m_jW["look"];
    Json l_jL = m_jW.contains("look") && m_jW["look"].is_object() ? m_jW["look"] : Json::object();
    if (m_jW.contains("sprite") && m_jW["sprite"].is_string()) l_jL["sprite"] = m_jW["sprite"];
    return l_jL;
}

void ObjectWindow::f_SetLook(const std::string& l_strKey, const Json& l_jV) {
    if (m_eKind == Object) {
        if (l_jV.is_null()) m_jW["look"].erase(l_strKey);
        else m_jW["look"][l_strKey] = l_jV;
        return;
    }
    // a type: its sprite is the record's "sprite" ("slime.png" for monsters and items); animation and tint go in "look"
    if (l_strKey == "sprite") {
        if (l_jV.is_null()) m_jW.erase("sprite");
        else m_jW["sprite"] = m_strCat == "npc" ? l_jV : Json(l_jV.get<std::string>() + ".png");
        return;
    }
    if (l_jV.is_null()) {
        if (m_jW.contains("look")) { m_jW["look"].erase(l_strKey); if (m_jW["look"].empty()) m_jW.erase("look"); }
    } else {
        if (!m_jW.contains("look") || !m_jW["look"].is_object()) m_jW["look"] = Json::object();
        m_jW["look"][l_strKey] = l_jV;
    }
}

void ObjectWindow::f_SetLookSprite(const std::string& l_strSprite) {
    f_SetLook("sprite", l_strSprite.empty() ? Json() : Json(l_strSprite));
    f_Render();
}

void ObjectWindow::f_LinkEvent(const std::string& l_strId) {
    if (m_eKind != Object || l_strId.empty()) return;
    if (!m_pEd->f_Project().f_Event(l_strId)) { m_pEd->f_Toast(tr("No event %1.").arg(f_Q(l_strId)), true); return; }
    Json& l_jEvs = m_jW["events"];
    if (std::find(l_jEvs.begin(), l_jEvs.end(), Json(l_strId)) == l_jEvs.end()) l_jEvs.push_back(l_strId);
    f_Render();
}

void ObjectWindow::f_SetNewId(const std::string& l_strId, const std::string& l_strFrom) {
    m_strId = l_strId;
    if (!l_strFrom.empty()) m_strFrom = l_strFrom;
}

// ---- rendering -----------------------------------------------------------------------------------------------------

void ObjectWindow::f_Render() {
    stage::Project& l_oP = m_pEd->f_Project();
    const int l_iKeepTab = m_iTab;
    m_pTabs->blockSignals(true);
    while (m_pTabs->count()) { QWidget* l_pW = m_pTabs->widget(0); m_pTabs->removeTab(0); l_pW->deleteLater(); }
    m_pNote->clear();
    m_pDel->setEnabled(true);
    m_pDel->setToolTip(QString());
    if (m_eKind == Object) {
        setWindowTitle(tr("Object settings"));
        m_pHead->setText(QStringLiteral("<b>%1</b> <span style='color:#8b929c'>%2 · %3 · %4</span>")
                            .arg(f_Q(l_oP.f_NameOf(m_strSem)).toHtmlEscaped(), f_Q(m_strSem).toHtmlEscaped(), tr("Cell %1, %2").arg(m_iX).arg(m_iY), f_Q(m_pEd->f_CurId())));
        m_pDel->setText(tr("Reset (type look, no events)"));
        m_pApply->setText(tr("Apply"));
        m_pTabs->addTab(f_LookTab(), tr("Look"));
        m_pTabs->addTab(f_EventsTab(), tr("Events"));
    } else if (m_eKind == Type) {
        setWindowTitle(tr("Type details"));
        const std::string l_strFile = m_strCat == "monster" ? stage::Project::f_EnemiesPath() : m_strCat == "item" ? stage::Project::f_ItemsPath() : stage::Project::f_NpcPath(m_strId);
        m_pHead->setText(QStringLiteral("<b>%1</b> <span style='color:#8b929c'>%2 · %3</span>")
                            .arg(f_Q(l_oP.f_NameOf(m_strSem)).toHtmlEscaped(), f_Q(m_strSem).toHtmlEscaped(), f_Q(l_strFile)));
        m_pDel->setText(tr("Delete this type"));
        m_pApply->setText(tr("Apply"));
        const std::vector<std::string> l_vUses = l_oP.f_TypeUses(m_strSem);
        if (!l_vUses.empty()) {
            QStringList l_oF;
            for (const std::string& l_strU : l_vUses) l_oF << f_Q(l_strU);
            const QString l_oMsg = tr("Placed on %1: remove it from those floors first.").arg(l_oF.join(QStringLiteral(", ")));
            m_pDel->setEnabled(false);
            m_pDel->setToolTip(l_oMsg);
            m_pNote->setText(l_oMsg);
        }
        m_pTabs->addTab(f_LookTab(), tr("Look"));
        m_pTabs->addTab(f_DataTab(), tr("Data"));
    } else {
        setWindowTitle(tr("New %1 type").arg(f_CatName(m_strCat)));
        m_pHead->setText(QStringLiteral("<b>%1</b>").arg(tr("New %1 type").arg(f_CatName(m_strCat))));
        m_pDel->setVisible(false);
        m_pApply->setText(tr("Create"));
        m_pTabs->addTab(f_NewForm(), tr("New"));
    }
    m_pTabs->tabBar()->setVisible(m_eKind != New);
    m_iTab = std::min(l_iKeepTab, m_pTabs->count() - 1);
    m_pTabs->setCurrentIndex(m_iTab);
    m_pTabs->blockSignals(false);
}

QWidget* ObjectWindow::f_LookTab() {
    stage::Project& l_oP = m_pEd->f_Project();
    SpriteBank& l_oBank = m_pEd->f_Bank();
    const Json l_jLk = f_Look();
    const Json l_jBase = m_eKind == Object ? l_oP.f_TypeLook(m_strSem) : Json();
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);

    // the preview, as the game draws it: its type's look, then its own
    auto* l_pTop = new QHBoxLayout();
    QImage l_oPrev(96, 96, QImage::Format_ARGB32_Premultiplied);
    l_oPrev.fill(QColor(0x2b, 0x2b, 0x33));
    {
        QPainter l_oG(&l_oPrev);
        StageDraw::f_DrawSem(l_oG, l_oP, l_oBank, "floor", QRectF(0, 0, 96, 96));
        Json l_jMerged = l_jBase.is_object() ? l_jBase : Json::object();
        for (auto& [l_strK, l_jVal] : l_jLk.items()) l_jMerged[l_strK] = l_jVal;
        StageDraw::f_DrawSem(l_oG, l_oP, l_oBank, m_strSem, QRectF(0, 0, 96, 96), l_jMerged);
    }
    auto* l_pPv = new QLabel;
    l_pPv->setPixmap(QPixmap::fromImage(l_oPrev));
    l_pPv->setFrameShape(QFrame::Box);
    l_pTop->addWidget(l_pPv);
    auto* l_pNote = new QLabel((m_eKind == Object ? tr("This one object only: empty fields use its type's look.")
                                              : tr("Every object of this type, unless an object sets its own (✎ Edit)."))
                            + QStringLiteral("\n") + tr("The tint multiplies the sprite's colours (white = unchanged)."));
    l_pNote->setWordWrap(true);
    l_pNote->setStyleSheet(QStringLiteral("color: #8b929c"));
    l_pTop->addWidget(l_pNote, 1);
    l_pV->addLayout(l_pTop);

    // sprite
    const QString l_oSp = l_jLk.contains("sprite") ? StageDraw::f_SpriteName(l_jLk["sprite"]) : QString();
    auto* l_pRow = new QHBoxLayout();
    l_pRow->addWidget(new QLabel(tr("Picture")));
    QString l_oShown = l_oSp;
    if (l_oShown.isEmpty()) {
        const QString l_oDef = l_jBase.is_object() && l_jBase.contains("sprite") ? StageDraw::f_SpriteName(l_jBase["sprite"]) : f_Q(l_oP.f_SpriteOf(m_strSem));
        l_oShown = tr("type's: %1").arg(l_oDef.isEmpty() ? QStringLiteral("—") : l_oDef);
    }
    auto* l_pCur = new QLabel(QStringLiteral("<b>%1</b>").arg(l_oShown.toHtmlEscaped()));
    l_pRow->addWidget(l_pCur);
    if (!l_oSp.isEmpty()) {
        auto* l_pClear = new QPushButton(m_eKind == Object ? tr("Use the type's") : tr("None"));
        connect(l_pClear, &QPushButton::clicked, this, [this] { f_SetLookSprite(std::string()); });
        l_pRow->addWidget(l_pClear);
    }
    l_pRow->addStretch(1);
    auto* l_pFind = new QLineEdit(m_oSpriteFilter);
    l_pFind->setPlaceholderText(tr("find a sprite"));
    l_pFind->setClearButtonEnabled(true);
    l_pFind->setMaximumWidth(180);
    l_pRow->addWidget(l_pFind);
    l_pV->addLayout(l_pRow);
    auto* l_pPick = new QLabel(tr("The game's pictures: click one to draw this with it."));
    l_pPick->setStyleSheet(QStringLiteral("color: #8b929c"));
    l_pV->addWidget(l_pPick);
    auto* l_pGrid = new QListWidget;
    l_pGrid->setViewMode(QListView::IconMode);
    l_pGrid->setIconSize(QSize(32, 32));
    l_pGrid->setGridSize(QSize(72, 56));
    l_pGrid->setResizeMode(QListView::Adjust);
    l_pGrid->setMovement(QListView::Static);
    l_pGrid->setMinimumHeight(180);
    for (const std::string& l_strN : l_oP.m_vSprites) {
        const QString l_oName = f_Q(l_strN);
        auto* l_pIt = new QListWidgetItem(QIcon(QPixmap::fromImage(l_oBank.f_Image(l_oName).scaled(32, 32))), l_oName);
        l_pIt->setData(Qt::UserRole, l_oName);
        l_pIt->setToolTip(l_oName);
        l_pGrid->addItem(l_pIt);
        if (l_oName == l_oSp) l_pIt->setSelected(true);
    }
    auto l_fnFilter = [l_pGrid](const QString& l_oT) {
        for (int l_iI = 0; l_iI < l_pGrid->count(); l_iI++) l_pGrid->item(l_iI)->setHidden(!l_oT.isEmpty() && !l_pGrid->item(l_iI)->text().contains(l_oT, Qt::CaseInsensitive));
    };
    l_fnFilter(m_oSpriteFilter);
    connect(l_pFind, &QLineEdit::textChanged, this, [this, l_fnFilter](const QString& l_oT) { m_oSpriteFilter = l_oT.trimmed(); l_fnFilter(m_oSpriteFilter); });
    connect(l_pGrid, &QListWidget::itemClicked, this, [this](QListWidgetItem* l_pIt) { f_SetLookSprite(l_pIt->data(Qt::UserRole).toString().toStdString()); });
    l_pV->addWidget(l_pGrid, 1);

    // animation: "anim/<file>.anim#<clip>"
    auto* l_pArow = new QHBoxLayout();
    l_pArow->addWidget(new QLabel(tr("Animation")));
    auto* l_pAnim = new QComboBox;
    const std::string l_strCurAnim = l_jLk.contains("anim") && l_jLk["anim"].is_string() ? l_jLk["anim"].get<std::string>() : std::string();
    l_pAnim->addItem(tr("(none: the sprite)"), QString());
    bool l_bFound = l_strCurAnim.empty();
    for (const auto& [l_strFile, l_vClips] : l_oP.m_vAnims) {
        l_pAnim->addItem(f_Q(l_strFile), QStringLiteral("#header"));
        if (auto* l_pModel = qobject_cast<QStandardItemModel*>(l_pAnim->model())) l_pModel->item(l_pAnim->count() - 1)->setEnabled(false);
        for (const std::string& l_strC : l_vClips) {
            const std::string l_strRef = l_strFile + "#" + l_strC;
            l_pAnim->addItem(QStringLiteral("    ") + f_Q(l_strC), f_Q(l_strRef));
            if (l_strRef == l_strCurAnim) { l_pAnim->setCurrentIndex(l_pAnim->count() - 1); l_bFound = true; }
        }
    }
    if (!l_bFound) { l_pAnim->addItem(QStringLiteral("⛔ ") + f_Q(l_strCurAnim), f_Q(l_strCurAnim)); l_pAnim->setCurrentIndex(l_pAnim->count() - 1); }
    connect(l_pAnim, &QComboBox::currentIndexChanged, this, [this, l_pAnim](int l_iI) {
        const QString l_oRef = l_pAnim->itemData(l_iI).toString();
        if (l_oRef == QLatin1String("#header")) return;
        f_SetLook("anim", l_oRef.isEmpty() ? Json() : Json(l_oRef.toStdString()));
        f_Render();
    });
    l_pArow->addWidget(l_pAnim, 1);
    if (l_jBase.is_object() && l_jBase.contains("anim") && l_strCurAnim.empty())
        l_pArow->addWidget(new QLabel(tr("type's: %1").arg(f_Q(l_jBase["anim"].get<std::string>()))));
    l_pV->addLayout(l_pArow);

    // tint
    float l_arrT[4];
    const bool l_bTinted = StageDraw::f_LookTint(l_jLk, l_arrT);
    auto* l_pTrow = new QHBoxLayout();
    l_pTrow->addWidget(new QLabel(tr("Colour")));
    auto* l_pUse = new QCheckBox(tr("tint"));
    l_pUse->setChecked(l_bTinted);
    connect(l_pUse, &QCheckBox::toggled, this, [this](bool l_bOn) {
        f_SetLook("tint", l_bOn ? Json::array({1, 0.6, 0.6, 1}) : Json());
        f_Render();
    });
    l_pTrow->addWidget(l_pUse);
    if (l_bTinted) {
        const QColor l_oC = QColor::fromRgbF(l_arrT[0], l_arrT[1], l_arrT[2]);
        auto* l_pPick = new QPushButton(l_oC.name());
        l_pPick->setStyleSheet(QStringLiteral("background:%1; color:%2").arg(l_oC.name(), l_oC.lightness() > 128 ? "#000" : "#fff"));
        connect(l_pPick, &QPushButton::clicked, this, [this, l_oC, l_arrT] {
            const QColor l_oN = QColorDialog::getColor(l_oC, this, tr("Colour"));
            if (!l_oN.isValid()) return;
            auto l_fnR3 = [](double l_dX) { return std::round(l_dX * 1000) / 1000; };
            f_SetLook("tint", Json::array({l_fnR3(l_oN.redF()), l_fnR3(l_oN.greenF()), l_fnR3(l_oN.blueF()), l_arrT[3]}));
            f_Render();
        });
        l_pTrow->addWidget(l_pPick);
        l_pTrow->addWidget(new QLabel(tr("opacity")));
        auto* l_pAlpha = new QSlider(Qt::Horizontal);
        l_pAlpha->setRange(0, 20);
        l_pAlpha->setValue(int(std::lround(l_arrT[3] * 20)));
        l_pAlpha->setMaximumWidth(140);
        auto* l_pAv = new QLabel(QStringLiteral("%1%").arg(std::lround(l_arrT[3] * 100)));
        connect(l_pAlpha, &QSlider::valueChanged, this, [this, l_pAv](int l_iVal) {
            Json l_jTint = f_Look()["tint"];
            l_jTint[3] = l_iVal / 20.0;
            f_SetLook("tint", l_jTint);
            l_pAv->setText(QStringLiteral("%1%").arg(l_iVal * 5));
        });
        connect(l_pAlpha, &QSlider::sliderReleased, this, [this] { f_Render(); });
        l_pTrow->addWidget(l_pAlpha);
        l_pTrow->addWidget(l_pAv);
    }
    l_pTrow->addStretch(1);
    l_pV->addLayout(l_pTrow);
    return l_pW;
}

QWidget* ObjectWindow::f_EventsTab() {
    stage::Project& l_oP = m_pEd->f_Project();
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    auto* l_pIntro = new QLabel(tr("Linked events fire when the player %1 (a talk event becomes a topic).").arg(f_Verb(f_CatOf(m_strSem))));
    l_pIntro->setWordWrap(true);
    l_pIntro->setStyleSheet(QStringLiteral("color: #8b929c"));
    l_pV->addWidget(l_pIntro);
    Json& l_jEvs = m_jW["events"];
    if (l_jEvs.empty()) l_pV->addWidget(new QLabel(tr("No linked events.")));
    for (const Json& l_jE : l_jEvs) {
        const std::string l_strId = l_jE.get<std::string>();
        const stage::Event* l_pEv = l_oP.f_Event(l_strId);
        auto* l_pRow = new QHBoxLayout();
        auto* l_pLbl = new QLabel(l_pEv ? QStringLiteral("%1 · %2").arg(f_Q(l_strId), f_Q(l_pEv->m_strKind)) : QStringLiteral("%1 ⛔").arg(f_Q(l_strId)));
        if (!l_pEv) l_pLbl->setStyleSheet(QStringLiteral("color: #ff9b9b"));
        l_pRow->addWidget(l_pLbl, 1);
        auto* l_pGo = new QPushButton(QStringLiteral("↗"));
        l_pGo->setToolTip(tr("Open in the event editor"));
        connect(l_pGo, &QPushButton::clicked, this, [this, l_strId] { Q_EMIT m_pEd->f_OpenEventEditor(f_Q(l_strId)); });
        auto* l_pUn = new QPushButton(QStringLiteral("✕"));
        l_pUn->setToolTip(tr("Unlink"));
        connect(l_pUn, &QPushButton::clicked, this, [this, l_strId] {
            Json& l_jL = m_jW["events"];
            l_jL.erase(std::remove(l_jL.begin(), l_jL.end(), Json(l_strId)), l_jL.end());
            f_Render();
        });
        l_pRow->addWidget(l_pGo);
        l_pRow->addWidget(l_pUn);
        l_pV->addLayout(l_pRow);
    }
    auto* l_pAdd = new QHBoxLayout();
    l_pAdd->addWidget(new QLabel(tr("Link an event")));
    auto* l_pBox = new QComboBox;
    l_pBox->setEditable(true);
    l_pBox->setInsertPolicy(QComboBox::NoInsert);
    for (const stage::Event& l_oE : l_oP.m_vEvents) {
        if (std::find(l_jEvs.begin(), l_jEvs.end(), Json(l_oE.m_strId)) != l_jEvs.end()) continue;
        const QString l_oTitle = f_Q(l_oP.f_Localized(Json(l_oE.m_strTitle)));
        l_pBox->addItem(f_Q(l_oE.m_strId) + (l_oTitle.isEmpty() || l_oTitle == f_Q(l_oE.m_strTitle) ? QString() : QStringLiteral("  —  ") + l_oTitle), f_Q(l_oE.m_strId));
    }
    l_pBox->setCurrentIndex(-1);
    l_pBox->lineEdit()->setPlaceholderText(tr("event id"));
    l_pBox->completer()->setFilterMode(Qt::MatchContains);
    l_pBox->completer()->setCompletionMode(QCompleter::PopupCompletion);
    auto* l_pBtn = new QPushButton(tr("+ Link"));
    auto l_fnDoAdd = [this, l_pBox] {
        QString l_oT = l_pBox->currentData().toString();
        if (l_oT.isEmpty() || l_pBox->currentText() != l_pBox->itemText(l_pBox->currentIndex())) l_oT = l_pBox->currentText().section(QStringLiteral("  —  "), 0, 0).trimmed();
        f_LinkEvent(l_oT.toStdString());
    };
    connect(l_pBtn, &QPushButton::clicked, this, l_fnDoAdd);
    l_pAdd->addWidget(l_pBox, 1);
    l_pAdd->addWidget(l_pBtn);
    l_pV->addLayout(l_pAdd);
    l_pV->addStretch(1);
    return l_pW;
}

QWidget* ObjectWindow::f_DataTab() {
    stage::Project& l_oP = m_pEd->f_Project();
    auto* l_pW = new QWidget;
    auto* l_pForm = new QFormLayout(l_pW);
    auto l_fnNum = [&](const char* l_pszKey, const QString& l_oLabel) {
        auto* l_pSb = new QSpinBox;
        l_pSb->setRange(-999999, 999999);
        l_pSb->setValue(m_jW.contains(l_pszKey) && m_jW[l_pszKey].is_number() ? m_jW[l_pszKey].get<int>() : 0);
        const std::string l_strK = l_pszKey;
        connect(l_pSb, &QSpinBox::valueChanged, this, [this, l_strK](int l_iVal) { m_jW[l_strK] = l_iVal; });
        l_pForm->addRow(l_oLabel, l_pSb);
    };
    // a text: zh_TW and en; a monster's name is in its record, an item's / NPC's is a text.json key
    auto l_fnText = [&](const char* l_pszKey, const QString& l_oLabel) {
        const std::string l_strK = l_pszKey;
        const Json l_jVal = m_strCat == "monster" && l_strK == "name" ? (m_jW.contains("name") && m_jW["name"].is_object() ? m_jW["name"] : Json::object())
                                                           : (m_mapTexts.count(l_strK) ? m_mapTexts[l_strK] : Json::object());
        auto* l_pRow = new QHBoxLayout();
        for (const char* l_pszL : {"zh_TW", "en"}) {
            auto* l_pE = new QLineEdit(l_jVal.contains(l_pszL) && l_jVal[l_pszL].is_string() ? f_Q(l_jVal[l_pszL].get<std::string>()) : QString());
            l_pE->setPlaceholderText(QString::fromLatin1(l_pszL));
            const std::string l_strLang = l_pszL;
            connect(l_pE, &QLineEdit::textEdited, this, [this, l_strK, l_strLang](const QString& l_oS) {
                if (m_strCat == "monster" && l_strK == "name") {
                    if (!m_jW.contains("name") || !m_jW["name"].is_object()) m_jW["name"] = Json::object();
                    m_jW["name"][l_strLang] = l_oS.toStdString();
                    return;
                }
                if (!m_jW.contains(l_strK) || !m_jW[l_strK].is_string()) m_jW[l_strK] = (m_strCat == "npc" ? "npc." : "item.") + m_strId + "." + l_strK;
                m_mapTexts[l_strK][l_strLang] = l_oS.toStdString();
            });
            l_pRow->addWidget(l_pE);
        }
        l_pForm->addRow(l_oLabel, l_pRow);
    };
    if (m_strCat == "monster") {
        l_fnText("name", tr("Name"));
        l_fnNum("hp", tr("HP"));
        l_fnNum("atk", tr("ATK"));
        l_fnNum("def", tr("DEF"));
        l_fnNum("exp", tr("EXP"));
        l_fnNum("gold", tr("Gold"));
    }
    if (m_strCat == "item") {
        l_fnText("name", tr("Name"));
        l_fnText("desc", tr("Description"));
        auto* l_pType = new QComboBox;
        for (const char* l_pszT : {"consumable", "story", "key"}) l_pType->addItem(QString::fromLatin1(l_pszT));
        l_pType->setCurrentText(f_Q(m_jW.value("type", std::string("consumable"))));
        connect(l_pType, &QComboBox::currentTextChanged, this, [this](const QString& l_oT) { m_jW["type"] = l_oT.toStdString(); });
        l_pForm->addRow(tr("Type"), l_pType);
        l_fnNum("price", tr("Price"));
        auto* l_pImp = new QCheckBox;
        l_pImp->setChecked(m_jW.value("important", false));
        connect(l_pImp, &QCheckBox::toggled, this, [this](bool l_bOn) { m_jW["important"] = l_bOn; });
        l_pForm->addRow(tr("Important"), l_pImp);
        auto* l_pStats = new QWidget;
        auto* l_pSv = new QVBoxLayout(l_pStats);
        l_pSv->setContentsMargins(0, 0, 0, 0);
        if (!m_jW.contains("stats") || !m_jW["stats"].is_object()) m_jW["stats"] = Json::object();
        std::vector<std::string> l_vKeys;
        for (auto& [l_strK, l_jVal] : m_jW["stats"].items()) { (void)l_jVal; l_vKeys.push_back(l_strK); }
        for (const std::string& l_strK : l_vKeys) {
            auto* l_pRow = new QHBoxLayout();
            auto* l_pKey = new QComboBox;
            std::vector<std::string> l_vNames = {"hp", "maxhp", "atk", "def", "gold", "exp", "key_yellow", "key_blue", "key_red"};
            if (std::find(l_vNames.begin(), l_vNames.end(), l_strK) == l_vNames.end()) l_vNames.push_back(l_strK);
            for (const std::string& l_strN : l_vNames) l_pKey->addItem(f_Q(l_strN));
            l_pKey->setCurrentText(f_Q(l_strK));
            connect(l_pKey, &QComboBox::currentTextChanged, this, [this, l_strK](const QString& l_oNk) {
                const Json l_jVal = m_jW["stats"][l_strK];
                m_jW["stats"].erase(l_strK);
                m_jW["stats"][l_oNk.toStdString()] = l_jVal;
                f_Render();
            });
            auto* l_pVal = new QSpinBox;
            l_pVal->setRange(-99999, 99999);
            l_pVal->setValue(m_jW["stats"][l_strK].is_number() ? m_jW["stats"][l_strK].get<int>() : 0);
            connect(l_pVal, &QSpinBox::valueChanged, this, [this, l_strK](int l_iX) { m_jW["stats"][l_strK] = l_iX; });
            auto* l_pDel = new QPushButton(QStringLiteral("✕"));
            connect(l_pDel, &QPushButton::clicked, this, [this, l_strK] { m_jW["stats"].erase(l_strK); f_Render(); });
            l_pRow->addWidget(l_pKey);
            l_pRow->addWidget(l_pVal);
            l_pRow->addWidget(l_pDel);
            l_pRow->addStretch(1);
            l_pSv->addLayout(l_pRow);
        }
        auto* l_pAdd = new QPushButton(tr("+ stat"));
        connect(l_pAdd, &QPushButton::clicked, this, [this] {
            std::string l_strK = "hp";
            for (const char* l_pszC : {"hp", "atk", "def", "gold", "exp"}) if (!m_jW["stats"].contains(l_pszC)) { l_strK = l_pszC; break; }
            m_jW["stats"][l_strK] = 1;
            f_Render();
        });
        l_pSv->addWidget(l_pAdd, 0, Qt::AlignLeft);
        l_pForm->addRow(tr("Effect"), l_pStats);
    }
    if (m_strCat == "npc") {
        l_fnText("name", tr("Name"));
        l_fnText("greeting", tr("Greeting"));
        auto* l_pNote = new QLabel(tr("Its chat lines, topics and dialogues are in the event editor's NPCs tab."));
        l_pNote->setWordWrap(true);
        l_pForm->addRow(l_pNote);
        auto* l_pGo = new QPushButton(tr("Edit in the event editor →"));
        connect(l_pGo, &QPushButton::clicked, this, [this] { Q_EMIT m_pEd->f_OpenNpcInEventEditor(f_Q(m_strId)); });
        l_pForm->addRow(l_pGo);
    }
    (void)l_oP;
    return l_pW;
}

QWidget* ObjectWindow::f_NewForm() {
    auto* l_pW = new QWidget;
    auto* l_pForm = new QFormLayout(l_pW);
    auto* l_pId = new QLineEdit(f_Q(m_strId));
    l_pId->setPlaceholderText(m_strCat == "monster" ? QStringLiteral("slime_red") : m_strCat == "item" ? QStringLiteral("potion_green")
                           : m_strCat == "story" ? QStringLiteral("crypt_key") : QStringLiteral("guard"));
    connect(l_pId, &QLineEdit::textEdited, this, [this](const QString& l_oS) { m_strId = l_oS.trimmed().toStdString(); });
    l_pForm->addRow(tr("Id"), l_pId);
    auto* l_pFrom = new QComboBox;
    for (const std::string& l_strK : f_CopyChoices(m_pEd->f_Project(), m_strCat)) l_pFrom->addItem(f_Q(l_strK));
    l_pFrom->setCurrentText(f_Q(m_strFrom));
    connect(l_pFrom, &QComboBox::currentTextChanged, this, [this](const QString& l_oS) { m_strFrom = l_oS.toStdString(); });
    l_pForm->addRow(tr("Copy from"), l_pFrom);
    QString l_oNote;
    if (m_strCat == "monster") l_oNote = tr("A new entry in enemies.json, copied from the one picked (stats, sprite); then set its name, stats and look.");
    else if (m_strCat == "item") l_oNote = tr("A new entry in items.json, copied from the one picked (effect, sprite); its name goes to text.json.");
    else if (m_strCat == "story") l_oNote = tr("A new key item in items.json (type \"story\"), copied from the one picked. Its gate -- a door only this key opens -- appears in the Gates group; put both on the map.");
    else l_oNote = tr("A new NPC file (npcs/<id>.json) with a one-page dialogue (dialogue/<id>.json); write its chat lines and topics in the event editor.");
    auto* l_pN = new QLabel(l_oNote);
    l_pN->setWordWrap(true);
    l_pN->setStyleSheet(QStringLiteral("color: #8b929c"));
    l_pForm->addRow(l_pN);
    return l_pW;
}

// ---- apply / delete --------------------------------------------------------------------------------------------------

void ObjectWindow::f_Apply() {
    stage::Project& l_oP = m_pEd->f_Project();
    if (m_eKind == Object) {
        Json l_jO = Json::object();
        if (!m_jW["look"].empty()) l_jO["look"] = m_jW["look"];
        if (!m_jW["events"].empty()) l_jO["events"] = m_jW["events"];
        const std::string l_strKey = std::to_string(m_iX) + "," + std::to_string(m_iY);
        m_pEd->f_Edit(tr("Object settings: %1").arg(f_Q(l_oP.f_NameOf(m_strSem))), [&](stage::StageDoc& l_oD) {
            if (const Json* l_pOld = stage::Project::f_ObjOf(l_oD, m_iX, m_iY); l_pOld && l_pOld->contains("uid"))
                l_jO["uid"] = (*l_pOld)["uid"];   // its save id stays (docs/gameplay/05_SAVE_SYSTEM.md)
            if (!l_oD.m_jSt.contains("objects") || !l_oD.m_jSt["objects"].is_object()) l_oD.m_jSt["objects"] = Json::object();
            if (!l_jO.empty()) l_oD.m_jSt["objects"][l_strKey] = l_jO;
            else l_oD.m_jSt["objects"].erase(l_strKey);
            if (l_oD.m_jSt["objects"].empty()) l_oD.m_jSt.erase("objects");
        });
        close();
        return;
    }
    if (m_eKind == New) {
        const std::string l_strId = m_strId;
        if (!f_ValidTypeId(l_strId)) { m_pEd->f_Toast(tr("Use lowercase letters, Chinese characters, 0-9 and _ only (no spaces)."), true); return; }
        const bool l_bStory = m_strCat == "story";   // a door and its key: an item of type "story"
        const std::string l_strCat = l_bStory ? std::string("item") : m_strCat;
        const std::string l_strSem = l_strCat + ":" + l_strId;
        if (l_oP.f_TypeRec(l_strSem) || (m_strCat == "npc" && l_oP.m_mapNpcs.count(l_strId))) { m_pEd->f_Toast(tr("%1 already exists.").arg(f_Q(l_strId)), true); return; }
        const Json* l_pSrcP = l_oP.f_TypeRec(l_strCat + ":" + m_strFrom);
        Json l_jSrc = l_pSrcP ? *l_pSrcP : Json::object();
        l_jSrc.erase("group");   // its category is the + that made it, not the copied one's
        const Json l_jIdText = Json{{"zh_TW", l_strId}, {"en", l_strId}};
        if (m_strCat == "monster") {
            l_jSrc["name"] = l_jIdText;
            l_oP.m_mapDataFiles[stage::Project::f_EnemiesPath()].m_jObj[l_strId] = l_jSrc;
        } else if (l_strCat == "item") {
            if (l_bStory) {   // a door and its key: the key opens its gate ("opens"), shown in Doors and Keys
                l_jSrc["type"] = "story";
                l_jSrc["opens"] = "gate:" + l_strId;
            }
            l_jSrc["name"] = "item." + l_strId + ".name";
            l_jSrc["desc"] = "item." + l_strId + ".desc";
            l_oP.m_mapTextEdits["item." + l_strId + ".name"] = l_jIdText;
            l_oP.m_mapTextEdits["item." + l_strId + ".desc"] = Json{{"zh_TW", ""}, {"en", ""}};
            l_oP.m_mapDataFiles[stage::Project::f_ItemsPath()].m_jObj[l_strId] = l_jSrc;
        } else {   // a new NPC: its file, and a first chat line (a one-page dialogue to rewrite in the event editor)
            Json l_jNpc = Json::object();
            l_jNpc["npcId"] = l_strId;
            l_jNpc["name"] = "npc." + l_strId + ".name";
            l_jNpc["chat"] = Json{{"lines", Json::array({Json{{"dialogue", l_strId}}})}, {"order", "loop"}};
            l_jNpc["topics"] = Json{{"menu", "auto"}};
            if (l_jSrc.contains("sprite")) l_jNpc["sprite"] = l_jSrc["sprite"];
            if (l_jSrc.contains("look")) l_jNpc["look"] = l_jSrc["look"];
            l_oP.m_mapTextEdits["npc." + l_strId + ".name"] = l_jIdText;
            stage::DataFile l_oDf;
            l_oDf.m_jObj = l_jNpc;
            l_oP.m_mapDataFiles[stage::Project::f_NpcPath(l_strId)] = l_oDf;
            const std::string l_strDlg = stage::Project::f_DialoguePath(l_strId);
            if (!l_oP.m_mapDataFiles.count(l_strDlg) && !QFileInfo::exists(f_Q(l_oP.f_Abs(l_strDlg)))) {   // never over a dialogue already there
                stage::DataFile l_oDd;
                l_oDd.m_jObj = Json{{"npcId", l_strId}, {"start", "root"},
                              {"nodes", Json{{"root", Json{{"text", Json{{"zh_TW", "……"}, {"en", "..."}}},
                                                           {"choices", Json::array({Json{{"label", Json{{"zh_TW", "……"}, {"en", "..."}}}, {"next", nullptr}}})}}}}}};
                l_oP.m_mapDataFiles[l_strDlg] = l_oDd;
            }
        }
        // painted with right away, shown in the palette group whose + made it, its details open on Data
        if (!m_strGroup.empty()) l_oP.f_SetGroup(l_strSem, m_strGroup);
        m_pEd->f_TypesChanged();
        m_pEd->f_SetBrush(m_strBrushPrefix + l_strId);
        m_pEd->f_RevealBrush();
        m_pEd->f_Toast(tr("%1 created: set its data and look here, then Save.").arg(f_Q(l_strId)));
        close();
        f_OpenType(m_pEd, l_strSem);
        if (ObjectWindow* l_pW = g_oOpen; l_pW && l_pW->m_eKind == Type) {
            l_pW->m_iTab = 1;   // Data
            l_pW->f_Render();
        }
        return;
    }
    // a type: the working copy replaces the record; changed names go to text.json
    const std::string l_strPath = m_strCat == "monster" ? stage::Project::f_EnemiesPath() : m_strCat == "item" ? stage::Project::f_ItemsPath() : stage::Project::f_NpcPath(m_strId);
    if (m_strCat == "npc") l_oP.m_mapDataFiles[l_strPath].m_jObj = m_jW;
    else l_oP.m_mapDataFiles[l_strPath].m_jObj[m_strId] = m_jW;
    for (const auto& [l_strK, l_jV] : m_mapTexts) {
        if (!m_jW.contains(l_strK) || !m_jW[l_strK].is_string()) continue;
        Json l_jMerged = l_oP.f_TextOf(m_jW[l_strK].get<std::string>());
        for (auto& [l_strL, l_jT] : l_jV.items()) l_jMerged[l_strL] = l_jT;
        l_oP.m_mapTextEdits[m_jW[l_strK].get<std::string>()] = l_jMerged;
    }
    m_jOrig = m_jW;
    m_mapOrigTexts = m_mapTexts;
    m_pEd->f_TypesChanged();
    m_pEd->f_Toast(tr("%1 changed (Save writes it).").arg(f_Q(m_strId)));
    f_Render();   // stays open: the palette's details window
}

void ObjectWindow::f_DoDelete() {
    stage::Project& l_oP = m_pEd->f_Project();
    if (m_eKind == Object) {   // "Reset": back to the type's look, no events
        m_jW = Json{{"look", Json::object()}, {"events", Json::array()}};
        f_Render();
        return;
    }
    if (m_eKind != Type || !l_oP.f_TypeUses(m_strSem).empty()) return;
    if (QMessageBox::question(this, tr("Delete this type"), tr("Delete the type %1?").arg(f_Q(m_strId))) != QMessageBox::Yes) return;
    if (m_strCat == "monster") l_oP.m_mapDataFiles[stage::Project::f_EnemiesPath()].m_jObj.erase(m_strId);
    else if (m_strCat == "item") l_oP.m_mapDataFiles[stage::Project::f_ItemsPath()].m_jObj.erase(m_strId);
    else l_oP.m_mapDataFiles[stage::Project::f_NpcPath(m_strId)].m_bDeleted = true;
    if (m_pEd->f_View().m_strBrush == m_strSem) m_pEd->f_SetBrush("wall");
    m_pEd->f_TypesChanged();
    m_pEd->f_Toast(tr("%1 deleted (Save writes it).").arg(f_Q(m_strId)));
    close();
}
