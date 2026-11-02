#include "AnimKeyListDock.h"

#include "BezierDialog.h"
#include "Icons.h"
#include "Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QRadioButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

using namespace animed;
using toms::anim::Ease;
using toms::anim::Node;

namespace {

constexpr int g_iEditRole = Qt::UserRole + 1;   // what the inline editor starts with
const QString g_oNoKey = QStringLiteral("·");

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }

// --selftest runs without anyone to answer a dialog: prompts take their default, notes go to the log.
bool f_SelftestRunning() { return qApp && qApp->property("toms.selftest").toBool(); }

int f_ColumnChannel(int l_iCol)
{
    switch (l_iCol) {
    case AnimKeyListDock::ColPos: return int(Channel::Pos);
    case AnimKeyListDock::ColRot: return int(Channel::Rot);
    case AnimKeyListDock::ColScale: return int(Channel::Scale);
    case AnimKeyListDock::ColColor: return int(Channel::Color);
    case AnimKeyListDock::ColSprite: return int(Channel::Sprite);
    case AnimKeyListDock::ColVisible: return int(Channel::Visible);
    case AnimKeyListDock::ColEvent: return int(Channel::Event);
    case AnimKeyListDock::ColDeform: return int(Channel::Deform);
    case AnimKeyListDock::ColFx: return int(Channel::Fx);
    case AnimKeyListDock::ColTrail: return int(Channel::Trail);
    default: return -1;
    }
}

QIcon f_Swatch(const glm::vec4& l_v4C)
{
    QPixmap l_oPm(14, 14);
    l_oPm.fill(Qt::transparent);
    QPainter l_oP(&l_oPm);
    l_oP.fillRect(QRect(0, 0, 7, 7), Qt::white);
    l_oP.fillRect(QRect(7, 7, 7, 7), Qt::white);
    l_oP.fillRect(QRect(7, 0, 7, 7), QColor(0xb0, 0xb0, 0xb0));
    l_oP.fillRect(QRect(0, 7, 7, 7), QColor(0xb0, 0xb0, 0xb0));
    l_oP.fillRect(l_oPm.rect(), QColor::fromRgbF(l_v4C.r, l_v4C.g, l_v4C.b, l_v4C.a));
    l_oP.setPen(QColor(0, 0, 0, 120));
    l_oP.drawRect(l_oPm.rect().adjusted(0, 0, -1, -1));
    return QIcon(l_oPm);
}

// Inline editors: combos for sprite / visible / ease, a line edit for the rest. Committing goes
// through AnimKeyListDock::commitCell (after the editor closed, since it rebuilds the table).
class KeyCellDelegate : public QStyledItemDelegate
{
public:
    KeyCellDelegate(AnimKeyListDock* l_pDock, AnimDocument* l_pDoc)
        : QStyledItemDelegate(l_pDock)
        , m_pDock(l_pDock)
        , m_pDoc(l_pDoc)
    {
    }

    QWidget* createEditor(QWidget* l_pParent, const QStyleOptionViewItem& l_oOption, const QModelIndex& l_oIndex) const override
    {
        switch (l_oIndex.column()) {
        case AnimKeyListDock::ColIndex: return nullptr;
        case AnimKeyListDock::ColSprite: {
            auto* l_pC = new QComboBox(l_pParent);
            l_pC->setEditable(true);
            l_pC->addItem(m_pDoc->f_SpriteDisplay(std::string()));
            l_pC->addItems(m_pDoc->f_SpriteChoices());   // "name (atlas)": picking one stores "atlas:name"
            l_pC->setMaxVisibleItems(20);
            return l_pC;
        }
        case AnimKeyListDock::ColVisible: {
            auto* l_pC = new QComboBox(l_pParent);
            l_pC->addItems({QStringLiteral("true"), QStringLiteral("false")});
            return l_pC;
        }
        case AnimKeyListDock::ColEase: {
            auto* l_pC = new QComboBox(l_pParent);
            l_pC->setEditable(true);
            for (const std::string& l_strN : toms::anim::f_EaseNames()) l_pC->addItem(f_Qs(l_strN));
            l_pC->setMaxVisibleItems(20);
            l_pC->setToolTip(QObject::tr("A name, or bezier(x1, y1, x2, y2)"));
            return l_pC;
        }
        default: return QStyledItemDelegate::createEditor(l_pParent, l_oOption, l_oIndex);
        }
    }

    void setEditorData(QWidget* l_pEditor, const QModelIndex& l_oIndex) const override
    {
        const QString l_oText = l_oIndex.data(g_iEditRole).toString();
        if (auto* l_pC = qobject_cast<QComboBox*>(l_pEditor)) {
            if (l_pC->isEditable()) l_pC->setEditText(l_oText);
            else l_pC->setCurrentText(l_oText);
        } else if (auto* l_pE = qobject_cast<QLineEdit*>(l_pEditor)) {
            l_pE->setText(l_oText);
            l_pE->selectAll();
        }
    }

    void setModelData(QWidget* l_pEditor, QAbstractItemModel*, const QModelIndex& l_oIndex) const override
    {
        QString l_oText;
        if (auto* l_pC = qobject_cast<QComboBox*>(l_pEditor)) l_oText = l_pC->currentText();
        else if (auto* l_pE = qobject_cast<QLineEdit*>(l_pEditor)) l_oText = l_pE->text();
        if (l_oText == l_oIndex.data(g_iEditRole).toString() && l_oIndex.data(Qt::DisplayRole).toString() != g_oNoKey) return;   // unchanged
        const int l_iRow = l_oIndex.row(), l_iCol = l_oIndex.column();
        AnimKeyListDock* l_pDock = m_pDock;
        QTimer::singleShot(0, l_pDock, [l_pDock, l_iRow, l_iCol, l_oText] { l_pDock->f_CommitCell(l_iRow, l_iCol, l_oText); });
    }

private:
    AnimKeyListDock* m_pDock;
    AnimDocument* m_pDoc;
};

}  // namespace

// ---- AnimKeyListDock ----------------------------------------------------------------------------

AnimKeyListDock::AnimKeyListDock(AnimDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Keys"), l_pParent)
    , m_pDoc(l_pDoc)
    , m_pTable(new QTableWidget(0, ColCount, this))
    , m_pToolBar(new QToolBar(this))
{
    setObjectName(QStringLiteral("AnimKeyListDock"));
    m_pTable->setHorizontalHeaderLabels({tr("#"), tr("Time"), tr("Pos"), tr("Rot"), tr("Scale"), tr("Color"), tr("Sprite"),
                                        tr("Visible"), tr("Event"), tr("Deform"), tr("Fx"), tr("Trail"), tr("Ease")});
    m_pTable->verticalHeader()->hide();
    m_pTable->verticalHeader()->setDefaultSectionSize(22);
    m_pTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_pTable->horizontalHeader()->setStretchLastSection(true);
    const int l_arrWidths[ColCount] = {34, 64, 110, 64, 90, 150, 110, 58, 90, 110, 120};
    for (int l_iI = 0; l_iI < ColCount; l_iI++) m_pTable->setColumnWidth(l_iI, l_arrWidths[l_iI]);
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_pTable->setAlternatingRowColors(true);
    m_pTable->setShowGrid(false);
    m_pTable->setItemDelegate(new KeyCellDelegate(this, l_pDoc));
    m_pTable->setWordWrap(false);

    m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pToolBar->setIconSize(QSize(16, 16));
    QAction* l_pIns = m_pToolBar->addAction(Icons::f_Icon(Icons::Id::KeyOn), tr("Insert"), this, &AnimKeyListDock::f_InsertKey);
    l_pIns->setToolTip(tr("Insert a key at the playhead (all interpolated channels when 'Key all channels together' is on, "
                       "else the channel of the current column)"));
    auto* l_pDelButton = new QToolButton(m_pToolBar);
    l_pDelButton->setText(tr("Delete"));
    l_pDelButton->setIcon(Icons::f_Icon(Icons::Id::Remove));
    l_pDelButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    l_pDelButton->setPopupMode(QToolButton::MenuButtonPopup);
    auto* l_pDelMenu = new QMenu(l_pDelButton);
    QAction* l_pDelAll = l_pDelMenu->addAction(tr("Delete selected rows (all channels)"), this, [this] { f_DeleteRows(false); });
    l_pDelMenu->addAction(tr("Delete only the current column's keys in the selected rows"), this, [this] { f_DeleteRows(true); });
    l_pDelButton->setMenu(l_pDelMenu);
    l_pDelButton->setDefaultAction(l_pDelAll);
    l_pDelButton->setText(tr("Delete"));
    l_pDelButton->setIcon(Icons::f_Icon(Icons::Id::Remove));
    m_pToolBar->addWidget(l_pDelButton);
    m_pToolBar->addSeparator();
    m_pToolBar->addAction(tr("Set Time…"), this, &AnimKeyListDock::f_SetTimeDialog)
        ->setToolTip(tr("Move the selected rows (they keep their spacing)"));
    m_pToolBar->addAction(tr("Even"), this, &AnimKeyListDock::f_EvenSpacing)
        ->setToolTip(tr("Spread the selected rows evenly between the first and the last selected"));
    m_pToolBar->addAction(tr("Rescale…"), this, &AnimKeyListDock::f_RescaleDialog)
        ->setToolTip(tr("Scale every key time of the node or of the whole clip to a new duration"));
    m_pToolBar->addSeparator();
    m_pToolBar->addAction(tr("Ramp…"), this, &AnimKeyListDock::f_RampDialog)
        ->setToolTip(tr("Set a channel on the first and last selected rows and interpolate linearly between (MPDI AverageAssign)"));
    m_pToolBar->addAction(tr("Fade In"), this, &AnimKeyListDock::f_FadeIn)->setToolTip(tr("Alpha 0 → 1 over the selected rows"));
    m_pToolBar->addAction(tr("Fade Out"), this, &AnimKeyListDock::f_FadeOut)->setToolTip(tr("Alpha 1 → 0 over the selected rows"));
    m_pToolBar->addAction(Icons::f_Icon(Icons::Id::SpriteNode), tr("Sprite Seq"), this, &AnimKeyListDock::f_SpriteSequence)
        ->setToolTip(tr("Sprite keys on the selected rows, cycling the sprites selected in the Sprites dock"));
    m_pToolBar->addAction(tr("Ease…"), this, &AnimKeyListDock::f_SetEaseDialog)
        ->setToolTip(tr("Set the ease of every interpolated key in the selected rows"));
    m_pToolBar->addSeparator();
    m_pTogether = new QCheckBox(tr("Key animated channels together"), m_pToolBar);
    m_pTogether->setChecked(l_pDoc->f_KeyTogether());
    m_pTogether->setToolTip(tr("Editing an interpolated cell also keys the node's other animated channels at that time "
                              "(only channels that have keys already; no new tracks)"));
    m_pToolBar->addWidget(m_pTogether);
    connect(m_pTogether, &QCheckBox::toggled, this, [this](bool l_bOn) { m_pDoc->f_SetKeyTogether(l_bOn); });

    auto* l_pBody = new QWidget(this);
    auto* l_pL = new QVBoxLayout(l_pBody);
    l_pL->setContentsMargins(2, 0, 2, 2);
    l_pL->setSpacing(0);
    l_pL->addWidget(m_pToolBar);
    l_pL->addWidget(m_pTable, 1);
    setWidget(l_pBody);

    connect(l_pDoc, &AnimDocument::f_FileChanged, this, &AnimKeyListDock::f_Rebuild);
    connect(l_pDoc, &AnimDocument::f_SelectionChanged, this, &AnimKeyListDock::f_Rebuild);
    connect(l_pDoc, &AnimDocument::f_ClipChanged, this, &AnimKeyListDock::f_Rebuild);
    connect(l_pDoc, &AnimDocument::f_AtlasChanged, this, &AnimKeyListDock::f_Rebuild);   // sprite cells show the atlas
    connect(l_pDoc, &AnimDocument::f_TimeChanged, this, &AnimKeyListDock::f_MarkPlayhead);
    connect(m_pTable, &QTableWidget::currentCellChanged, this, [this](int l_iRow, int, int l_iPrevRow, int) {
        if (!m_bUpdating && l_iRow >= 0 && l_iRow != l_iPrevRow && l_iRow < int(m_vTimes.size())) m_pDoc->f_SetTime(m_vTimes[size_t(l_iRow)]);
    });
    connect(m_pTable, &QTableWidget::cellClicked, this, [this](int l_iRow) {
        if (l_iRow >= 0 && l_iRow < int(m_vTimes.size())) m_pDoc->f_SetTime(m_vTimes[size_t(l_iRow)]);
    });
    f_Rebuild();
}

void AnimKeyListDock::f_Warn(const QString& l_oText)
{
    if (!isVisible() || !window()->isVisible() || f_SelftestRunning()) {
        emit m_pDoc->f_Message(l_oText);
        return;
    }
    QMessageBox::information(this, tr("Keys"), l_oText);
}

std::vector<float> AnimKeyListDock::f_SelectedTimes() const
{
    std::set<int> l_setRows;
    for (const QModelIndex& l_oI : m_pTable->selectionModel()->selectedRows()) l_setRows.insert(l_oI.row());
    std::vector<float> l_vOut;
    for (int l_iR : l_setRows)
        if (l_iR < int(m_vTimes.size())) l_vOut.push_back(m_vTimes[size_t(l_iR)]);
    return l_vOut;
}

void AnimKeyListDock::f_SelectRows(const std::vector<int>& l_vRows)
{
    QItemSelection l_oSel;
    for (int l_iR : l_vRows)
        if (l_iR >= 0 && l_iR < m_pTable->rowCount()) l_oSel.select(m_pTable->model()->index(l_iR, 0), m_pTable->model()->index(l_iR, ColCount - 1));
    m_pTable->selectionModel()->select(l_oSel, QItemSelectionModel::ClearAndSelect);
}

int AnimKeyListDock::f_CurrentChannel() const { return f_ColumnChannel(m_pTable->currentColumn()); }

void AnimKeyListDock::f_Rebuild()
{
    const std::vector<float> l_vKeepSelected = f_SelectedTimes();
    const int l_iKeepColumn = std::max(int(ColTime), m_pTable->currentColumn());
    m_bUpdating = true;
    const Node* l_pN = m_pDoc->f_SelectedNode();
    m_vTimes = l_pN ? f_UnionKeyTimes(*l_pN) : std::vector<float>();
    m_pTable->clearContents();
    m_pTable->setRowCount(int(m_vTimes.size()));
    const QColor l_oDim = palette().color(QPalette::PlaceholderText);
    const QColor l_oBad(236, 96, 96);
    // A cell's text: sprites as the fields show them ("name (atlas)"), the rest as formatValue.
    auto l_fnCellText = [this](Channel l_eC, const Value& l_oV) {
        return l_eC == Channel::Sprite ? m_pDoc->f_SpriteDisplay(l_oV.m_strS) : f_Qs(f_FormatValue(l_eC, l_oV));
    };
    for (int l_iR = 0; l_iR < int(m_vTimes.size()); l_iR++) {
        const float l_fT = m_vTimes[size_t(l_iR)];
        auto* l_pIdx = new QTableWidgetItem(QString::number(l_iR + 1));
        l_pIdx->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        l_pIdx->setTextAlignment(Qt::AlignCenter);
        m_pTable->setItem(l_iR, ColIndex, l_pIdx);
        auto* l_pTime = new QTableWidgetItem(QString::number(double(l_fT), 'f', 3));
        l_pTime->setData(g_iEditRole, l_pTime->text());
        m_pTable->setItem(l_iR, ColTime, l_pTime);
        for (int l_iCol = ColPos; l_iCol <= ColTrail; l_iCol++) {
            const Channel l_eC = Channel(f_ColumnChannel(l_iCol));
            Value l_oV;
            QTableWidgetItem* l_pIt;
            if (f_KeyAt(*l_pN, l_eC, l_fT, l_oV)) {
                l_pIt = new QTableWidgetItem(l_fnCellText(l_eC, l_oV));
                l_pIt->setData(g_iEditRole, l_pIt->text());
                if (l_eC == Channel::Color) l_pIt->setIcon(f_Swatch(l_oV.m_v4V));
                if (l_eC == Channel::Sprite && !l_oV.m_strS.empty()) {
                    // "name (atlas)"; the stored reference and whether it resolves in the tooltip.
                    QString l_oTip = tr("Stored as \"%1\"").arg(f_Qs(l_oV.m_strS));
                    if (!m_pDoc->f_FindSprite(l_oV.m_strS)) {
                        l_pIt->setForeground(l_oBad);
                        l_oTip += tr("\nNot found in the atlases: see Problems");
                    } else if (toms::anim::f_ParseSpriteRef(l_oV.m_strS).m_strAtlas.empty()) {
                        l_oTip += tr("\nA bare name: the first atlas that has it is used (Key > Qualify Sprite References stores the atlas)");
                    }
                    l_pIt->setToolTip(l_oTip);
                }
            } else {
                l_pIt = new QTableWidgetItem(g_oNoKey);
                l_pIt->setForeground(l_oDim);
                l_pIt->setTextAlignment(Qt::AlignCenter);
                l_pIt->setData(g_iEditRole, l_eC == Channel::Event ? QString() : l_fnCellText(l_eC, f_ValueAt(*l_pN, l_eC, l_fT)));
                l_pIt->setToolTip(tr("No %1 key here; double-click to add one").arg(QLatin1String(f_ChannelName(l_eC))));
            }
            m_pTable->setItem(l_iR, l_iCol, l_pIt);
        }
        // Ease of the interpolated keys at this time.
        std::vector<std::string> l_vEases;
        for (Channel l_eC : g_arrInterpolated) {
            Ease l_oE;
            if (f_EaseAt(*l_pN, l_eC, l_fT, l_oE)) l_vEases.push_back(f_FormatEase(l_oE));
        }
        QTableWidgetItem* l_pEase;
        if (l_vEases.empty()) {
            l_pEase = new QTableWidgetItem(g_oNoKey);
            l_pEase->setForeground(l_oDim);
            l_pEase->setTextAlignment(Qt::AlignCenter);
            l_pEase->setData(g_iEditRole, QStringLiteral("linear"));
        } else {
            const bool l_bSame = std::all_of(l_vEases.begin(), l_vEases.end(), [&](const std::string& l_strS) { return l_strS == l_vEases.front(); });
            l_pEase = new QTableWidgetItem(l_bSame ? f_Qs(l_vEases.front()) : tr("mixed"));
            l_pEase->setData(g_iEditRole, f_Qs(l_vEases.front()));
            if (!l_bSame) l_pEase->setToolTip(tr("The channels' keys at this time ease differently"));
        }
        m_pTable->setItem(l_iR, ColEase, l_pEase);
    }
    setWindowTitle(l_pN ? tr("Keys: %1 (%2)").arg(f_Qs(l_pN->m_strName)).arg(m_vTimes.size()) : tr("Keys"));
    // Keep the rows that were selected (by time), else the row at the playhead.
    std::vector<int> l_vRows;
    for (int l_iR = 0; l_iR < int(m_vTimes.size()); l_iR++)
        for (float l_fT : l_vKeepSelected)
            if (f_SameTime(l_fT, m_vTimes[size_t(l_iR)])) l_vRows.push_back(l_iR);
    if (l_vRows.empty())
        for (int l_iR = 0; l_iR < int(m_vTimes.size()); l_iR++)
            if (f_SameTime(m_vTimes[size_t(l_iR)], m_pDoc->f_Time())) l_vRows.push_back(l_iR);
    if (!l_vRows.empty()) {
        m_pTable->setCurrentCell(l_vRows.back(), l_iKeepColumn, QItemSelectionModel::NoUpdate);
        f_SelectRows(l_vRows);
    }
    m_bUpdating = false;
    f_MarkPlayhead();
}

void AnimKeyListDock::f_MarkPlayhead()
{
    QColor l_oMark = Theme::f_Colors().m_oKey;
    l_oMark.setAlpha(110);
    for (int l_iR = 0; l_iR < m_pTable->rowCount(); l_iR++)
        if (QTableWidgetItem* l_pIt = m_pTable->item(l_iR, ColIndex))
            l_pIt->setBackground(f_SameTime(m_vTimes[size_t(l_iR)], m_pDoc->f_Time()) ? QBrush(l_oMark) : QBrush());
}

bool AnimKeyListDock::f_CommitCell(int l_iRow, int l_iColumn, const QString& l_oText)
{
    if (l_iRow < 0 || l_iRow >= int(m_vTimes.size()) || !m_pDoc->f_SelectedNode()) return false;
    const float l_fT = m_vTimes[size_t(l_iRow)];
    const QString l_oS = l_oText.trimmed();
    if (l_iColumn == ColTime) {
        bool l_bOk = false;
        const float l_fNt = l_oS.toFloat(&l_bOk);
        if (!l_bOk || l_fNt < 0) {
            f_Warn(tr("'%1' is not a time in seconds.").arg(l_oS));
            return false;
        }
        return f_MoveWithShift({l_fT}, l_fNt - l_fT, tr("Set key time"));
    }
    if (l_iColumn == ColEase) {
        Ease l_oE;
        if (!f_ParseEaseText(l_oS.toStdString(), l_oE)) {
            f_Warn(tr("Unknown ease '%1'. Use a name (linear, quadraticOut, …) or bezier(x1, y1, x2, y2).").arg(l_oS));
            return false;
        }
        if (!m_pDoc->f_SetEaseRows({l_fT}, l_oE)) {
            f_Warn(tr("There is no pos / rot / scale / colour key at %1 s to ease.").arg(l_fT));
            return false;
        }
        return true;
    }
    const int l_iCh = f_ColumnChannel(l_iColumn);
    if (l_iCh < 0) return false;
    const Channel l_eC = Channel(l_iCh);
    if (l_oS.isEmpty() || l_oS == g_oNoKey) return m_pDoc->f_RemoveCell(l_fT, l_eC);
    Value l_oV;
    std::string l_strErr;
    if (l_eC == Channel::Sprite) l_oV.m_strS = m_pDoc->f_SpriteFromText(l_oS);   // "name (atlas)", "atlas:name" or a name -> "atlas:name"
    else if (!f_ParseValue(l_eC, l_oS.toStdString(), l_oV, &l_strErr)) {
        f_Warn(tr("%1: %2").arg(QLatin1String(f_ChannelName(l_eC)), f_Qs(l_strErr)));
        return false;
    }
    return m_pDoc->f_SetCell(l_fT, l_eC, l_oV, m_pDoc->f_KeyTogether());
}

void AnimKeyListDock::f_InsertKey()
{
    if (!m_pDoc->f_SelectedNode()) return;
    const int l_iCh = m_pDoc->f_KeyTogether() ? -1 : f_CurrentChannel();
    const float l_fT = m_pDoc->f_Time();
    if (!m_pDoc->f_InsertKeys(l_fT, l_iCh)) f_Warn(tr("There are keys at %1 s already.").arg(l_fT));
}

void AnimKeyListDock::f_DeleteRows(bool l_bOnlyCurrentChannel)
{
    const std::vector<float> l_vTimes = f_SelectedTimes();
    if (l_vTimes.empty()) return f_Warn(tr("Select the rows to delete first."));
    const int l_iCh = l_bOnlyCurrentChannel ? f_CurrentChannel() : -1;
    if (l_bOnlyCurrentChannel && l_iCh < 0) return f_Warn(tr("Click a cell of the channel (Pos … Event) whose keys should go."));
    m_pDoc->f_DeleteKeys(l_vTimes, l_iCh);
}

bool AnimKeyListDock::f_MoveWithShift(const std::vector<float>& l_vFrom, float l_fDelta, const QString& l_oText)
{
    if (l_vFrom.empty() || l_fDelta == 0) return false;
    std::vector<float> l_vTo;
    for (float l_fT : l_vFrom) l_vTo.push_back(l_fT + l_fDelta);
    QString l_oConflict;
    if (m_pDoc->f_MoveRows(l_vFrom, l_vTo, &l_oConflict, l_oText)) return true;
    // Offer to move every key after the selection by the same amount.
    const float l_fLast = *std::max_element(l_vFrom.begin(), l_vFrom.end());
    std::vector<float> l_vFrom2 = l_vFrom;
    for (float l_fT : m_vTimes)
        if (l_fT > l_fLast + g_fTimeEps) l_vFrom2.push_back(l_fT);
    if (l_vFrom2.size() == l_vFrom.size() || l_fDelta < 0) {
        f_Warn(tr("Cannot move the keys: %1.").arg(l_oConflict));
        return false;
    }
    bool l_bShift = true;
    if (isVisible() && window()->isVisible() && !f_SelftestRunning())
        l_bShift = QMessageBox::question(this, tr("Keys"),
                                      tr("Cannot move the keys: %1.\n\nShift the keys after them by %2 s too?").arg(l_oConflict).arg(l_fDelta)) ==
                QMessageBox::Yes;
    if (!l_bShift) return false;
    std::vector<float> l_vTo2;
    for (float l_fT : l_vFrom2) l_vTo2.push_back(l_fT + l_fDelta);
    if (m_pDoc->f_MoveRows(l_vFrom2, l_vTo2, &l_oConflict, l_oText)) return true;
    f_Warn(tr("Cannot move the keys: %1.").arg(l_oConflict));
    return false;
}

void AnimKeyListDock::f_SetTimeDialog()
{
    const std::vector<float> l_vTimes = f_SelectedTimes();
    if (l_vTimes.empty()) return f_Warn(tr("Select the rows to move first."));
    bool l_bOk = false;
    const double l_dNt = QInputDialog::getDouble(this, tr("Set Time"), tr("New time of the first selected row (the others keep their spacing):"),
                                              l_vTimes.front(), 0, 3600, 3, &l_bOk);
    if (l_bOk) f_MoveWithShift(l_vTimes, float(l_dNt) - l_vTimes.front(), tr("Set key time"));
}

void AnimKeyListDock::f_EvenSpacing()
{
    std::vector<float> l_vTimes = f_SelectedTimes();
    if (l_vTimes.size() < 3) return f_Warn(tr("Select three or more rows: the first and the last stay, the ones between are spread evenly."));
    std::sort(l_vTimes.begin(), l_vTimes.end());
    std::vector<float> l_vTo;
    const float l_fA = l_vTimes.front(), l_fB = l_vTimes.back();
    for (size_t l_nI = 0; l_nI < l_vTimes.size(); l_nI++) l_vTo.push_back(l_fA + (l_fB - l_fA) * float(l_nI) / float(l_vTimes.size() - 1));
    QString l_oConflict;
    if (!m_pDoc->f_MoveRows(l_vTimes, l_vTo, &l_oConflict, tr("Even spacing"))) f_Warn(tr("Cannot space the keys evenly: %1.").arg(l_oConflict));
}

void AnimKeyListDock::f_RescaleDialog()
{
    if (!m_pDoc->f_Clip() || !m_pDoc->f_SelectedNode()) return;
    QDialog l_oDlg(this);
    l_oDlg.setWindowTitle(tr("Rescale Key Times"));
    auto* l_pDur = new QDoubleSpinBox(&l_oDlg);
    l_pDur->setRange(0.001, 3600);
    l_pDur->setDecimals(3);
    l_pDur->setSuffix(QStringLiteral(" s"));
    l_pDur->setValue(m_pDoc->f_Duration() > 0 ? m_pDoc->f_Duration() : 1.0);
    auto* l_pNode = new QRadioButton(tr("This node's keys (to its last key)"), &l_oDlg);
    auto* l_pClip = new QRadioButton(tr("The whole clip (every node, and its length)"), &l_oDlg);
    l_pClip->setChecked(true);
    auto* l_pForm = new QFormLayout(&l_oDlg);
    l_pForm->addRow(tr("New duration"), l_pDur);
    l_pForm->addRow(l_pClip);
    l_pForm->addRow(l_pNode);
    auto* l_pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &l_oDlg);
    connect(l_pButtons, &QDialogButtonBox::accepted, &l_oDlg, &QDialog::accept);
    connect(l_pButtons, &QDialogButtonBox::rejected, &l_oDlg, &QDialog::reject);
    l_pForm->addRow(l_pButtons);
    if (l_oDlg.exec() != QDialog::Accepted) return;
    QString l_oErr;
    if (!m_pDoc->f_Rescale(float(l_pDur->value()), l_pClip->isChecked(), &l_oErr)) f_Warn(tr("Cannot rescale: %1.").arg(l_oErr));
}

void AnimKeyListDock::f_RampDialog()
{
    std::vector<float> l_vTimes = f_SelectedTimes();
    const Node* l_pN = m_pDoc->f_SelectedNode();
    if (!l_pN || l_vTimes.size() < 2) return f_Warn(tr("Select two or more rows: the first and last get the values you enter, the rows between are interpolated."));
    std::sort(l_vTimes.begin(), l_vTimes.end());
    QDialog l_oDlg(this);
    l_oDlg.setWindowTitle(tr("Ramp"));
    auto* l_pChannel = new QComboBox(&l_oDlg);
    l_pChannel->addItems({tr("Pos"), tr("Rot"), tr("Scale"), tr("Color"), tr("Alpha")});
    l_pChannel->setCurrentIndex(std::clamp(f_CurrentChannel(), 0, 3));
    auto* l_pFrom = new QLineEdit(&l_oDlg);
    auto* l_pTo = new QLineEdit(&l_oDlg);
    auto l_fnFill = [&] {
        const int l_iI = l_pChannel->currentIndex();
        if (l_iI == 4) {
            l_pFrom->setText(QString::number(double(toms::anim::f_ColorAt(*l_pN, l_vTimes.front()).a)));
            l_pTo->setText(QString::number(double(toms::anim::f_ColorAt(*l_pN, l_vTimes.back()).a)));
        } else {
            const Channel l_eC = g_arrInterpolated[l_iI];
            l_pFrom->setText(f_Qs(f_FormatValue(l_eC, f_ValueAt(*l_pN, l_eC, l_vTimes.front()))));
            l_pTo->setText(f_Qs(f_FormatValue(l_eC, f_ValueAt(*l_pN, l_eC, l_vTimes.back()))));
        }
    };
    l_fnFill();
    connect(l_pChannel, &QComboBox::currentIndexChanged, &l_oDlg, l_fnFill);
    auto* l_pForm = new QFormLayout(&l_oDlg);
    l_pForm->addRow(tr("Channel"), l_pChannel);
    l_pForm->addRow(tr("First row (%1 s)").arg(l_vTimes.front(), 0, 'f', 3), l_pFrom);
    l_pForm->addRow(tr("Last row (%1 s)").arg(l_vTimes.back(), 0, 'f', 3), l_pTo);
    auto* l_pHint = new QLabel(tr("%1 rows; the values between follow the key times linearly.").arg(l_vTimes.size()), &l_oDlg);
    l_pHint->setForegroundRole(QPalette::PlaceholderText);
    l_pForm->addRow(l_pHint);
    auto* l_pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &l_oDlg);
    connect(l_pButtons, &QDialogButtonBox::accepted, &l_oDlg, &QDialog::accept);
    connect(l_pButtons, &QDialogButtonBox::rejected, &l_oDlg, &QDialog::reject);
    l_pForm->addRow(l_pButtons);
    if (l_oDlg.exec() != QDialog::Accepted) return;
    const int l_iI = l_pChannel->currentIndex();
    const Channel l_eC = l_iI == 4 ? Channel::Color : g_arrInterpolated[l_iI];
    Value l_oA, l_oB;
    if (l_iI == 4) {   // alpha: one number, in x
        bool l_bOkA = false, l_bOkB = false;
        l_oA.m_v4V.x = l_pFrom->text().trimmed().toFloat(&l_bOkA);
        l_oB.m_v4V.x = l_pTo->text().trimmed().toFloat(&l_bOkB);
        if (!l_bOkA || !l_bOkB) return f_Warn(tr("Ramp: expected one number (alpha 0..1)"));
    } else {
        std::string l_strErr;
        if (!f_ParseValue(l_eC, l_pFrom->text().toStdString(), l_oA, &l_strErr) || !f_ParseValue(l_eC, l_pTo->text().toStdString(), l_oB, &l_strErr))
            return f_Warn(tr("Ramp: %1").arg(f_Qs(l_strErr)));
    }
    m_pDoc->f_Ramp(l_vTimes, l_eC, l_oA, l_oB, l_iI == 4);
}

void AnimKeyListDock::f_Fade(bool l_bIn)
{
    const std::vector<float> l_vTimes = f_SelectedTimes();
    if (l_vTimes.size() < 2) return f_Warn(tr("Select two or more rows to fade over."));
    m_pDoc->f_Fade(l_vTimes, l_bIn);
}

void AnimKeyListDock::f_SpriteSequence()
{
    const std::vector<float> l_vTimes = f_SelectedTimes();
    const QStringList l_oSprites = m_fnSpriteSource ? m_fnSpriteSource() : QStringList();
    if (l_oSprites.size() < 2) return f_Warn(tr("Select two or more sprites in the Sprites dock (Ctrl+click, in order) to cycle through."));
    if (l_vTimes.empty()) return f_Warn(tr("Select the rows that get the sprite keys."));
    m_pDoc->f_SpriteSequence(l_vTimes, l_oSprites);
}

void AnimKeyListDock::f_SetEaseDialog()
{
    const std::vector<float> l_vTimes = f_SelectedTimes();
    const Node* l_pN = m_pDoc->f_SelectedNode();
    if (!l_pN || l_vTimes.empty()) return f_Warn(tr("Select the rows whose keys get the ease."));
    Ease l_oCur;
    for (Channel l_eC : g_arrInterpolated)
        if (f_EaseAt(*l_pN, l_eC, l_vTimes.front(), l_oCur)) break;
    QDialog l_oDlg(this);
    l_oDlg.setWindowTitle(tr("Set Ease"));
    auto* l_pCombo = new QComboBox(&l_oDlg);
    EaseUi::f_Fill(l_pCombo);
    EaseUi::f_Show(l_pCombo, &l_oCur);
    Ease l_oChosen = l_oCur;
    connect(l_pCombo, &QComboBox::activated, &l_oDlg, [&](int l_iIndex) {
        Ease l_oE;
        if (EaseUi::f_Picked(l_pCombo, l_iIndex, l_oChosen, &l_oDlg, l_oE)) {
            l_oChosen = l_oE;
            EaseUi::f_Show(l_pCombo, &l_oChosen);
        }
    });
    auto* l_pForm = new QFormLayout(&l_oDlg);
    l_pForm->addRow(new QLabel(tr("Ease of the pos / rot / scale / colour keys in %1 row(s)").arg(l_vTimes.size()), &l_oDlg));
    l_pForm->addRow(tr("Ease"), l_pCombo);
    auto* l_pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &l_oDlg);
    connect(l_pButtons, &QDialogButtonBox::accepted, &l_oDlg, &QDialog::accept);
    connect(l_pButtons, &QDialogButtonBox::rejected, &l_oDlg, &QDialog::reject);
    l_pForm->addRow(l_pButtons);
    if (l_oDlg.exec() != QDialog::Accepted) return;
    if (!m_pDoc->f_SetEaseRows(l_vTimes, l_oChosen)) f_Warn(tr("The selected rows have no pos / rot / scale / colour keys."));
}

// ---- AnimEventsDock -----------------------------------------------------------------------------

AnimEventsDock::AnimEventsDock(AnimDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Events"), l_pParent)
    , m_pDoc(l_pDoc)
    , m_pTable(new QTableWidget(0, 2, this))
{
    setObjectName(QStringLiteral("AnimEventsDock"));
    m_pTable->setHorizontalHeaderLabels({tr("Time"), tr("Event")});
    m_pTable->verticalHeader()->hide();
    m_pTable->verticalHeader()->setDefaultSectionSize(22);
    m_pTable->horizontalHeader()->setStretchLastSection(true);
    m_pTable->setColumnWidth(0, 80);
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    auto* l_pTb = new QToolBar(this);
    l_pTb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    l_pTb->setIconSize(QSize(16, 16));
    l_pTb->addAction(Icons::f_Icon(Icons::Id::Event), tr("Add at Playhead"), this, [this] {
        if (!m_pDoc->f_SelectedNode()) return;
        if (!m_pDoc->f_InsertKeys(m_pDoc->f_Time(), int(Channel::Event)))
            emit m_pDoc->f_Message(tr("There is an event at %1 s already.").arg(m_pDoc->f_Time()));
    });
    l_pTb->addAction(Icons::f_Icon(Icons::Id::Remove), tr("Remove"), this, [this] {
        std::vector<float> l_vTimes;
        for (const QModelIndex& l_oI : m_pTable->selectionModel()->selectedRows())
            if (l_oI.row() < int(m_vTimes.size())) l_vTimes.push_back(m_vTimes[size_t(l_oI.row())]);
        if (!l_vTimes.empty()) m_pDoc->f_DeleteKeys(l_vTimes, int(Channel::Event));
    });
    auto* l_pBody = new QWidget(this);
    auto* l_pL = new QVBoxLayout(l_pBody);
    l_pL->setContentsMargins(2, 0, 2, 2);
    l_pL->setSpacing(0);
    l_pL->addWidget(l_pTb);
    l_pL->addWidget(m_pTable, 1);
    setWidget(l_pBody);

    connect(l_pDoc, &AnimDocument::f_FileChanged, this, &AnimEventsDock::f_Rebuild);
    connect(l_pDoc, &AnimDocument::f_SelectionChanged, this, &AnimEventsDock::f_Rebuild);
    connect(l_pDoc, &AnimDocument::f_ClipChanged, this, &AnimEventsDock::f_Rebuild);
    connect(m_pTable, &QTableWidget::cellClicked, this, [this](int l_iRow) {
        if (l_iRow >= 0 && l_iRow < int(m_vTimes.size())) m_pDoc->f_SetTime(m_vTimes[size_t(l_iRow)]);
    });
    connect(m_pTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* l_pIt) {
        if (m_bUpdating || l_pIt->row() >= int(m_vTimes.size())) return;
        const float l_fT = m_vTimes[size_t(l_pIt->row())];
        const QString l_oText = l_pIt->text().trimmed();
        // Commit after the editor has closed: the edit rebuilds the table.
        QTimer::singleShot(0, this, [this, l_fT, l_oText, col = l_pIt->column()] {
            if (col == 0) {
                bool l_bOk = false;
                const float l_fNt = l_oText.toFloat(&l_bOk);
                QString l_oConflict;
                if (!l_bOk || !m_pDoc->f_MoveChannelKey(Channel::Event, l_fT, l_fNt, &l_oConflict)) {
                    emit m_pDoc->f_Message(l_bOk ? tr("Cannot move the event: %1.").arg(l_oConflict) : tr("'%1' is not a time.").arg(l_oText));
                    f_Rebuild();
                }
            } else if (l_oText.isEmpty()) {
                m_pDoc->f_RemoveCell(l_fT, Channel::Event);
            } else {
                Value l_oV;
                l_oV.m_strS = l_oText.toStdString();
                m_pDoc->f_SetCell(l_fT, Channel::Event, l_oV, false);
            }
        });
    });
    f_Rebuild();
}

void AnimEventsDock::f_Rebuild()
{
    m_bUpdating = true;
    const Node* l_pN = m_pDoc->f_SelectedNode();
    m_vTimes.clear();
    m_pTable->setRowCount(0);
    if (l_pN) {
        m_pTable->setRowCount(int(l_pN->m_vEventKeys.size()));
        for (size_t l_nI = 0; l_nI < l_pN->m_vEventKeys.size(); l_nI++) {
            m_vTimes.push_back(l_pN->m_vEventKeys[l_nI].m_fT);
            m_pTable->setItem(int(l_nI), 0, new QTableWidgetItem(QString::number(double(l_pN->m_vEventKeys[l_nI].m_fT), 'f', 3)));
            m_pTable->setItem(int(l_nI), 1, new QTableWidgetItem(f_Qs(l_pN->m_vEventKeys[l_nI].m_oV)));
        }
    }
    setWindowTitle(l_pN && !l_pN->m_vEventKeys.empty() ? tr("Events (%1)").arg(l_pN->m_vEventKeys.size()) : tr("Events"));
    m_bUpdating = false;
}
