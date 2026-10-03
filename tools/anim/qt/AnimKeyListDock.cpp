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

constexpr int kEditRole = Qt::UserRole + 1;   // what the inline editor starts with
const QString kNoKey = QStringLiteral("·");

QString qs(const std::string& s) { return QString::fromStdString(s); }

// --selftest runs without anyone to answer a dialog: prompts take their default, notes go to the log.
bool selftestRunning() { return qApp && qApp->property("toms.selftest").toBool(); }

int columnChannel(int col)
{
    switch (col) {
    case AnimKeyListDock::ColPos: return int(Channel::Pos);
    case AnimKeyListDock::ColRot: return int(Channel::Rot);
    case AnimKeyListDock::ColScale: return int(Channel::Scale);
    case AnimKeyListDock::ColColor: return int(Channel::Color);
    case AnimKeyListDock::ColSprite: return int(Channel::Sprite);
    case AnimKeyListDock::ColVisible: return int(Channel::Visible);
    case AnimKeyListDock::ColEvent: return int(Channel::Event);
    default: return -1;
    }
}

QIcon swatch(const glm::vec4& c)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.fillRect(QRect(0, 0, 7, 7), Qt::white);
    p.fillRect(QRect(7, 7, 7, 7), Qt::white);
    p.fillRect(QRect(7, 0, 7, 7), QColor(0xb0, 0xb0, 0xb0));
    p.fillRect(QRect(0, 7, 7, 7), QColor(0xb0, 0xb0, 0xb0));
    p.fillRect(pm.rect(), QColor::fromRgbF(c.r, c.g, c.b, c.a));
    p.setPen(QColor(0, 0, 0, 120));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    return QIcon(pm);
}

// Inline editors: combos for sprite / visible / ease, a line edit for the rest. Committing goes
// through AnimKeyListDock::commitCell (after the editor closed, since it rebuilds the table).
class KeyCellDelegate : public QStyledItemDelegate
{
public:
    KeyCellDelegate(AnimKeyListDock* dock, AnimDocument* doc)
        : QStyledItemDelegate(dock)
        , m_dock(dock)
        , m_doc(doc)
    {
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        switch (index.column()) {
        case AnimKeyListDock::ColIndex: return nullptr;
        case AnimKeyListDock::ColSprite: {
            auto* c = new QComboBox(parent);
            c->setEditable(true);
            c->addItem(m_doc->spriteDisplay(std::string()));
            c->addItems(m_doc->spriteChoices());   // "name (atlas)": picking one stores "atlas:name"
            c->setMaxVisibleItems(20);
            return c;
        }
        case AnimKeyListDock::ColVisible: {
            auto* c = new QComboBox(parent);
            c->addItems({QStringLiteral("true"), QStringLiteral("false")});
            return c;
        }
        case AnimKeyListDock::ColEase: {
            auto* c = new QComboBox(parent);
            c->setEditable(true);
            for (const std::string& n : toms::anim::easeNames()) c->addItem(qs(n));
            c->setMaxVisibleItems(20);
            c->setToolTip(QObject::tr("A name, or bezier(x1, y1, x2, y2)"));
            return c;
        }
        default: return QStyledItemDelegate::createEditor(parent, option, index);
        }
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        const QString text = index.data(kEditRole).toString();
        if (auto* c = qobject_cast<QComboBox*>(editor)) {
            if (c->isEditable()) c->setEditText(text);
            else c->setCurrentText(text);
        } else if (auto* e = qobject_cast<QLineEdit*>(editor)) {
            e->setText(text);
            e->selectAll();
        }
    }

    void setModelData(QWidget* editor, QAbstractItemModel*, const QModelIndex& index) const override
    {
        QString text;
        if (auto* c = qobject_cast<QComboBox*>(editor)) text = c->currentText();
        else if (auto* e = qobject_cast<QLineEdit*>(editor)) text = e->text();
        if (text == index.data(kEditRole).toString() && index.data(Qt::DisplayRole).toString() != kNoKey) return;   // unchanged
        const int row = index.row(), col = index.column();
        AnimKeyListDock* dock = m_dock;
        QTimer::singleShot(0, dock, [dock, row, col, text] { dock->commitCell(row, col, text); });
    }

private:
    AnimKeyListDock* m_dock;
    AnimDocument* m_doc;
};

}  // namespace

// ---- AnimKeyListDock ----------------------------------------------------------------------------

AnimKeyListDock::AnimKeyListDock(AnimDocument* doc, QWidget* parent)
    : QDockWidget(tr("Keys"), parent)
    , m_doc(doc)
    , m_table(new QTableWidget(0, ColCount, this))
    , m_toolBar(new QToolBar(this))
{
    setObjectName(QStringLiteral("AnimKeyListDock"));
    m_table->setHorizontalHeaderLabels({tr("#"), tr("Time"), tr("Pos"), tr("Rot"), tr("Scale"), tr("Color"), tr("Sprite"),
                                        tr("Visible"), tr("Event"), tr("Ease")});
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(22);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    const int widths[ColCount] = {34, 64, 110, 64, 90, 150, 110, 58, 90, 120};
    for (int i = 0; i < ColCount; i++) m_table->setColumnWidth(i, widths[i]);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setItemDelegate(new KeyCellDelegate(this, doc));
    m_table->setWordWrap(false);

    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toolBar->setIconSize(QSize(16, 16));
    QAction* ins = m_toolBar->addAction(Icons::icon(Icons::Id::KeyOn), tr("Insert"), this, &AnimKeyListDock::insertKey);
    ins->setToolTip(tr("Insert a key at the playhead (all interpolated channels when 'Key all channels together' is on, "
                       "else the channel of the current column)"));
    auto* delButton = new QToolButton(m_toolBar);
    delButton->setText(tr("Delete"));
    delButton->setIcon(Icons::icon(Icons::Id::Remove));
    delButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    delButton->setPopupMode(QToolButton::MenuButtonPopup);
    auto* delMenu = new QMenu(delButton);
    QAction* delAll = delMenu->addAction(tr("Delete selected rows (all channels)"), this, [this] { deleteRows(false); });
    delMenu->addAction(tr("Delete only the current column's keys in the selected rows"), this, [this] { deleteRows(true); });
    delButton->setMenu(delMenu);
    delButton->setDefaultAction(delAll);
    delButton->setText(tr("Delete"));
    delButton->setIcon(Icons::icon(Icons::Id::Remove));
    m_toolBar->addWidget(delButton);
    m_toolBar->addSeparator();
    m_toolBar->addAction(tr("Set Time…"), this, &AnimKeyListDock::setTimeDialog)
        ->setToolTip(tr("Move the selected rows (they keep their spacing)"));
    m_toolBar->addAction(tr("Even"), this, &AnimKeyListDock::evenSpacing)
        ->setToolTip(tr("Spread the selected rows evenly between the first and the last selected"));
    m_toolBar->addAction(tr("Rescale…"), this, &AnimKeyListDock::rescaleDialog)
        ->setToolTip(tr("Scale every key time of the node or of the whole clip to a new duration"));
    m_toolBar->addSeparator();
    m_toolBar->addAction(tr("Ramp…"), this, &AnimKeyListDock::rampDialog)
        ->setToolTip(tr("Set a channel on the first and last selected rows and interpolate linearly between (MPDI AverageAssign)"));
    m_toolBar->addAction(tr("Fade In"), this, &AnimKeyListDock::fadeIn)->setToolTip(tr("Alpha 0 → 1 over the selected rows"));
    m_toolBar->addAction(tr("Fade Out"), this, &AnimKeyListDock::fadeOut)->setToolTip(tr("Alpha 1 → 0 over the selected rows"));
    m_toolBar->addAction(Icons::icon(Icons::Id::SpriteNode), tr("Sprite Seq"), this, &AnimKeyListDock::spriteSequence)
        ->setToolTip(tr("Sprite keys on the selected rows, cycling the sprites selected in the Sprites dock"));
    m_toolBar->addAction(tr("Ease…"), this, &AnimKeyListDock::setEaseDialog)
        ->setToolTip(tr("Set the ease of every interpolated key in the selected rows"));
    m_toolBar->addSeparator();
    m_together = new QCheckBox(tr("Key animated channels together"), m_toolBar);
    m_together->setChecked(doc->keyTogether());
    m_together->setToolTip(tr("Editing an interpolated cell also keys the node's other animated channels at that time "
                              "(only channels that have keys already; no new tracks)"));
    m_toolBar->addWidget(m_together);
    connect(m_together, &QCheckBox::toggled, this, [this](bool on) { m_doc->setKeyTogether(on); });

    auto* body = new QWidget(this);
    auto* l = new QVBoxLayout(body);
    l->setContentsMargins(2, 0, 2, 2);
    l->setSpacing(0);
    l->addWidget(m_toolBar);
    l->addWidget(m_table, 1);
    setWidget(body);

    connect(doc, &AnimDocument::fileChanged, this, &AnimKeyListDock::rebuild);
    connect(doc, &AnimDocument::selectionChanged, this, &AnimKeyListDock::rebuild);
    connect(doc, &AnimDocument::clipChanged, this, &AnimKeyListDock::rebuild);
    connect(doc, &AnimDocument::atlasChanged, this, &AnimKeyListDock::rebuild);   // sprite cells show the atlas
    connect(doc, &AnimDocument::timeChanged, this, &AnimKeyListDock::markPlayhead);
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row, int, int prevRow, int) {
        if (!m_updating && row >= 0 && row != prevRow && row < int(m_times.size())) m_doc->setTime(m_times[size_t(row)]);
    });
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row) {
        if (row >= 0 && row < int(m_times.size())) m_doc->setTime(m_times[size_t(row)]);
    });
    rebuild();
}

void AnimKeyListDock::warn(const QString& text)
{
    if (!isVisible() || !window()->isVisible() || selftestRunning()) {
        emit m_doc->message(text);
        return;
    }
    QMessageBox::information(this, tr("Keys"), text);
}

std::vector<float> AnimKeyListDock::selectedTimes() const
{
    std::set<int> rows;
    for (const QModelIndex& i : m_table->selectionModel()->selectedRows()) rows.insert(i.row());
    std::vector<float> out;
    for (int r : rows)
        if (r < int(m_times.size())) out.push_back(m_times[size_t(r)]);
    return out;
}

void AnimKeyListDock::selectRows(const std::vector<int>& rows)
{
    QItemSelection sel;
    for (int r : rows)
        if (r >= 0 && r < m_table->rowCount()) sel.select(m_table->model()->index(r, 0), m_table->model()->index(r, ColCount - 1));
    m_table->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
}

int AnimKeyListDock::currentChannel() const { return columnChannel(m_table->currentColumn()); }

void AnimKeyListDock::rebuild()
{
    const std::vector<float> keepSelected = selectedTimes();
    const int keepColumn = std::max(int(ColTime), m_table->currentColumn());
    m_updating = true;
    const Node* n = m_doc->selectedNode();
    m_times = n ? unionKeyTimes(*n) : std::vector<float>();
    m_table->clearContents();
    m_table->setRowCount(int(m_times.size()));
    const QColor dim = palette().color(QPalette::PlaceholderText);
    const QColor bad(236, 96, 96);
    // A cell's text: sprites as the fields show them ("name (atlas)"), the rest as formatValue.
    auto cellText = [this](Channel c, const Value& v) {
        return c == Channel::Sprite ? m_doc->spriteDisplay(v.s) : qs(formatValue(c, v));
    };
    for (int r = 0; r < int(m_times.size()); r++) {
        const float t = m_times[size_t(r)];
        auto* idx = new QTableWidgetItem(QString::number(r + 1));
        idx->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        idx->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(r, ColIndex, idx);
        auto* time = new QTableWidgetItem(QString::number(double(t), 'f', 3));
        time->setData(kEditRole, time->text());
        m_table->setItem(r, ColTime, time);
        for (int col = ColPos; col <= ColEvent; col++) {
            const Channel c = Channel(columnChannel(col));
            Value v;
            QTableWidgetItem* it;
            if (keyAt(*n, c, t, v)) {
                it = new QTableWidgetItem(cellText(c, v));
                it->setData(kEditRole, it->text());
                if (c == Channel::Color) it->setIcon(swatch(v.v));
                if (c == Channel::Sprite && !v.s.empty()) {
                    // "name (atlas)"; the stored reference and whether it resolves in the tooltip.
                    QString tip = tr("Stored as \"%1\"").arg(qs(v.s));
                    if (!m_doc->findSprite(v.s)) {
                        it->setForeground(bad);
                        tip += tr("\nNot found in the atlases: see Problems");
                    } else if (toms::anim::parseSpriteRef(v.s).atlas.empty()) {
                        tip += tr("\nA bare name: the first atlas that has it is used (Key > Qualify Sprite References stores the atlas)");
                    }
                    it->setToolTip(tip);
                }
            } else {
                it = new QTableWidgetItem(kNoKey);
                it->setForeground(dim);
                it->setTextAlignment(Qt::AlignCenter);
                it->setData(kEditRole, c == Channel::Event ? QString() : cellText(c, valueAt(*n, c, t)));
                it->setToolTip(tr("No %1 key here; double-click to add one").arg(QLatin1String(channelName(c))));
            }
            m_table->setItem(r, col, it);
        }
        // Ease of the interpolated keys at this time.
        std::vector<std::string> eases;
        for (Channel c : kInterpolated) {
            Ease e;
            if (easeAt(*n, c, t, e)) eases.push_back(formatEase(e));
        }
        QTableWidgetItem* ease;
        if (eases.empty()) {
            ease = new QTableWidgetItem(kNoKey);
            ease->setForeground(dim);
            ease->setTextAlignment(Qt::AlignCenter);
            ease->setData(kEditRole, QStringLiteral("linear"));
        } else {
            const bool same = std::all_of(eases.begin(), eases.end(), [&](const std::string& s) { return s == eases.front(); });
            ease = new QTableWidgetItem(same ? qs(eases.front()) : tr("mixed"));
            ease->setData(kEditRole, qs(eases.front()));
            if (!same) ease->setToolTip(tr("The channels' keys at this time ease differently"));
        }
        m_table->setItem(r, ColEase, ease);
    }
    setWindowTitle(n ? tr("Keys: %1 (%2)").arg(qs(n->name)).arg(m_times.size()) : tr("Keys"));
    // Keep the rows that were selected (by time), else the row at the playhead.
    std::vector<int> rows;
    for (int r = 0; r < int(m_times.size()); r++)
        for (float t : keepSelected)
            if (sameTime(t, m_times[size_t(r)])) rows.push_back(r);
    if (rows.empty())
        for (int r = 0; r < int(m_times.size()); r++)
            if (sameTime(m_times[size_t(r)], m_doc->time())) rows.push_back(r);
    if (!rows.empty()) {
        m_table->setCurrentCell(rows.back(), keepColumn, QItemSelectionModel::NoUpdate);
        selectRows(rows);
    }
    m_updating = false;
    markPlayhead();
}

void AnimKeyListDock::markPlayhead()
{
    QColor mark = Theme::colors().key;
    mark.setAlpha(110);
    for (int r = 0; r < m_table->rowCount(); r++)
        if (QTableWidgetItem* it = m_table->item(r, ColIndex))
            it->setBackground(sameTime(m_times[size_t(r)], m_doc->time()) ? QBrush(mark) : QBrush());
}

bool AnimKeyListDock::commitCell(int row, int column, const QString& text)
{
    if (row < 0 || row >= int(m_times.size()) || !m_doc->selectedNode()) return false;
    const float t = m_times[size_t(row)];
    const QString s = text.trimmed();
    if (column == ColTime) {
        bool ok = false;
        const float nt = s.toFloat(&ok);
        if (!ok || nt < 0) {
            warn(tr("'%1' is not a time in seconds.").arg(s));
            return false;
        }
        return moveWithShift({t}, nt - t, tr("Set key time"));
    }
    if (column == ColEase) {
        Ease e;
        if (!parseEaseText(s.toStdString(), e)) {
            warn(tr("Unknown ease '%1'. Use a name (linear, quadraticOut, …) or bezier(x1, y1, x2, y2).").arg(s));
            return false;
        }
        if (!m_doc->setEaseRows({t}, e)) {
            warn(tr("There is no pos / rot / scale / colour key at %1 s to ease.").arg(t));
            return false;
        }
        return true;
    }
    const int ch = columnChannel(column);
    if (ch < 0) return false;
    const Channel c = Channel(ch);
    if (s.isEmpty() || s == kNoKey) return m_doc->removeCell(t, c);
    Value v;
    std::string err;
    if (c == Channel::Sprite) v.s = m_doc->spriteFromText(s);   // "name (atlas)", "atlas:name" or a name -> "atlas:name"
    else if (!parseValue(c, s.toStdString(), v, &err)) {
        warn(tr("%1: %2").arg(QLatin1String(channelName(c)), qs(err)));
        return false;
    }
    return m_doc->setCell(t, c, v, m_doc->keyTogether());
}

void AnimKeyListDock::insertKey()
{
    if (!m_doc->selectedNode()) return;
    const int ch = m_doc->keyTogether() ? -1 : currentChannel();
    const float t = m_doc->time();
    if (!m_doc->insertKeys(t, ch)) warn(tr("There are keys at %1 s already.").arg(t));
}

void AnimKeyListDock::deleteRows(bool onlyCurrentChannel)
{
    const std::vector<float> times = selectedTimes();
    if (times.empty()) return warn(tr("Select the rows to delete first."));
    const int ch = onlyCurrentChannel ? currentChannel() : -1;
    if (onlyCurrentChannel && ch < 0) return warn(tr("Click a cell of the channel (Pos … Event) whose keys should go."));
    m_doc->deleteKeys(times, ch);
}

bool AnimKeyListDock::moveWithShift(const std::vector<float>& from, float delta, const QString& text)
{
    if (from.empty() || delta == 0) return false;
    std::vector<float> to;
    for (float t : from) to.push_back(t + delta);
    QString conflict;
    if (m_doc->moveRows(from, to, &conflict, text)) return true;
    // Offer to move every key after the selection by the same amount.
    const float last = *std::max_element(from.begin(), from.end());
    std::vector<float> from2 = from;
    for (float t : m_times)
        if (t > last + kTimeEps) from2.push_back(t);
    if (from2.size() == from.size() || delta < 0) {
        warn(tr("Cannot move the keys: %1.").arg(conflict));
        return false;
    }
    bool shift = true;
    if (isVisible() && window()->isVisible() && !selftestRunning())
        shift = QMessageBox::question(this, tr("Keys"),
                                      tr("Cannot move the keys: %1.\n\nShift the keys after them by %2 s too?").arg(conflict).arg(delta)) ==
                QMessageBox::Yes;
    if (!shift) return false;
    std::vector<float> to2;
    for (float t : from2) to2.push_back(t + delta);
    if (m_doc->moveRows(from2, to2, &conflict, text)) return true;
    warn(tr("Cannot move the keys: %1.").arg(conflict));
    return false;
}

void AnimKeyListDock::setTimeDialog()
{
    const std::vector<float> times = selectedTimes();
    if (times.empty()) return warn(tr("Select the rows to move first."));
    bool ok = false;
    const double nt = QInputDialog::getDouble(this, tr("Set Time"), tr("New time of the first selected row (the others keep their spacing):"),
                                              times.front(), 0, 3600, 3, &ok);
    if (ok) moveWithShift(times, float(nt) - times.front(), tr("Set key time"));
}

void AnimKeyListDock::evenSpacing()
{
    std::vector<float> times = selectedTimes();
    if (times.size() < 3) return warn(tr("Select three or more rows: the first and the last stay, the ones between are spread evenly."));
    std::sort(times.begin(), times.end());
    std::vector<float> to;
    const float a = times.front(), b = times.back();
    for (size_t i = 0; i < times.size(); i++) to.push_back(a + (b - a) * float(i) / float(times.size() - 1));
    QString conflict;
    if (!m_doc->moveRows(times, to, &conflict, tr("Even spacing"))) warn(tr("Cannot space the keys evenly: %1.").arg(conflict));
}

void AnimKeyListDock::rescaleDialog()
{
    if (!m_doc->clip() || !m_doc->selectedNode()) return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Rescale Key Times"));
    auto* dur = new QDoubleSpinBox(&dlg);
    dur->setRange(0.001, 3600);
    dur->setDecimals(3);
    dur->setSuffix(QStringLiteral(" s"));
    dur->setValue(m_doc->duration() > 0 ? m_doc->duration() : 1.0);
    auto* node = new QRadioButton(tr("This node's keys (to its last key)"), &dlg);
    auto* clip = new QRadioButton(tr("The whole clip (every node, and its length)"), &dlg);
    clip->setChecked(true);
    auto* form = new QFormLayout(&dlg);
    form->addRow(tr("New duration"), dur);
    form->addRow(clip);
    form->addRow(node);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    if (!m_doc->rescale(float(dur->value()), clip->isChecked(), &err)) warn(tr("Cannot rescale: %1.").arg(err));
}

void AnimKeyListDock::rampDialog()
{
    std::vector<float> times = selectedTimes();
    const Node* n = m_doc->selectedNode();
    if (!n || times.size() < 2) return warn(tr("Select two or more rows: the first and last get the values you enter, the rows between are interpolated."));
    std::sort(times.begin(), times.end());
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Ramp"));
    auto* channel = new QComboBox(&dlg);
    channel->addItems({tr("Pos"), tr("Rot"), tr("Scale"), tr("Color"), tr("Alpha")});
    channel->setCurrentIndex(std::clamp(currentChannel(), 0, 3));
    auto* from = new QLineEdit(&dlg);
    auto* to = new QLineEdit(&dlg);
    auto fill = [&] {
        const int i = channel->currentIndex();
        if (i == 4) {
            from->setText(QString::number(double(toms::anim::colorAt(*n, times.front()).a)));
            to->setText(QString::number(double(toms::anim::colorAt(*n, times.back()).a)));
        } else {
            const Channel c = kInterpolated[i];
            from->setText(qs(formatValue(c, valueAt(*n, c, times.front()))));
            to->setText(qs(formatValue(c, valueAt(*n, c, times.back()))));
        }
    };
    fill();
    connect(channel, &QComboBox::currentIndexChanged, &dlg, fill);
    auto* form = new QFormLayout(&dlg);
    form->addRow(tr("Channel"), channel);
    form->addRow(tr("First row (%1 s)").arg(times.front(), 0, 'f', 3), from);
    form->addRow(tr("Last row (%1 s)").arg(times.back(), 0, 'f', 3), to);
    auto* hint = new QLabel(tr("%1 rows; the values between follow the key times linearly.").arg(times.size()), &dlg);
    hint->setForegroundRole(QPalette::PlaceholderText);
    form->addRow(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    const int i = channel->currentIndex();
    const Channel c = i == 4 ? Channel::Color : kInterpolated[i];
    const Channel parseAs = i == 4 ? Channel::Rot : c;   // alpha: one number
    Value a, b;
    std::string err;
    if (!parseValue(parseAs, from->text().toStdString(), a, &err) || !parseValue(parseAs, to->text().toStdString(), b, &err))
        return warn(tr("Ramp: %1").arg(qs(err)));
    m_doc->ramp(times, c, a, b, i == 4);
}

void AnimKeyListDock::fade(bool in)
{
    const std::vector<float> times = selectedTimes();
    if (times.size() < 2) return warn(tr("Select two or more rows to fade over."));
    m_doc->fade(times, in);
}

void AnimKeyListDock::spriteSequence()
{
    const std::vector<float> times = selectedTimes();
    const QStringList sprites = m_spriteSource ? m_spriteSource() : QStringList();
    if (sprites.size() < 2) return warn(tr("Select two or more sprites in the Sprites dock (Ctrl+click, in order) to cycle through."));
    if (times.empty()) return warn(tr("Select the rows that get the sprite keys."));
    m_doc->spriteSequence(times, sprites);
}

void AnimKeyListDock::setEaseDialog()
{
    const std::vector<float> times = selectedTimes();
    const Node* n = m_doc->selectedNode();
    if (!n || times.empty()) return warn(tr("Select the rows whose keys get the ease."));
    Ease cur;
    for (Channel c : kInterpolated)
        if (easeAt(*n, c, times.front(), cur)) break;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Set Ease"));
    auto* combo = new QComboBox(&dlg);
    EaseUi::fill(combo);
    EaseUi::show(combo, &cur);
    Ease chosen = cur;
    connect(combo, &QComboBox::activated, &dlg, [&](int index) {
        Ease e;
        if (EaseUi::picked(combo, index, chosen, &dlg, e)) {
            chosen = e;
            EaseUi::show(combo, &chosen);
        }
    });
    auto* form = new QFormLayout(&dlg);
    form->addRow(new QLabel(tr("Ease of the pos / rot / scale / colour keys in %1 row(s)").arg(times.size()), &dlg));
    form->addRow(tr("Ease"), combo);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    if (!m_doc->setEaseRows(times, chosen)) warn(tr("The selected rows have no pos / rot / scale / colour keys."));
}

// ---- AnimEventsDock -----------------------------------------------------------------------------

AnimEventsDock::AnimEventsDock(AnimDocument* doc, QWidget* parent)
    : QDockWidget(tr("Events"), parent)
    , m_doc(doc)
    , m_table(new QTableWidget(0, 2, this))
{
    setObjectName(QStringLiteral("AnimEventsDock"));
    m_table->setHorizontalHeaderLabels({tr("Time"), tr("Event")});
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(22);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 80);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    auto* tb = new QToolBar(this);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setIconSize(QSize(16, 16));
    tb->addAction(Icons::icon(Icons::Id::Event), tr("Add at Playhead"), this, [this] {
        if (!m_doc->selectedNode()) return;
        if (!m_doc->insertKeys(m_doc->time(), int(Channel::Event)))
            emit m_doc->message(tr("There is an event at %1 s already.").arg(m_doc->time()));
    });
    tb->addAction(Icons::icon(Icons::Id::Remove), tr("Remove"), this, [this] {
        std::vector<float> times;
        for (const QModelIndex& i : m_table->selectionModel()->selectedRows())
            if (i.row() < int(m_times.size())) times.push_back(m_times[size_t(i.row())]);
        if (!times.empty()) m_doc->deleteKeys(times, int(Channel::Event));
    });
    auto* body = new QWidget(this);
    auto* l = new QVBoxLayout(body);
    l->setContentsMargins(2, 0, 2, 2);
    l->setSpacing(0);
    l->addWidget(tb);
    l->addWidget(m_table, 1);
    setWidget(body);

    connect(doc, &AnimDocument::fileChanged, this, &AnimEventsDock::rebuild);
    connect(doc, &AnimDocument::selectionChanged, this, &AnimEventsDock::rebuild);
    connect(doc, &AnimDocument::clipChanged, this, &AnimEventsDock::rebuild);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row) {
        if (row >= 0 && row < int(m_times.size())) m_doc->setTime(m_times[size_t(row)]);
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (m_updating || it->row() >= int(m_times.size())) return;
        const float t = m_times[size_t(it->row())];
        const QString text = it->text().trimmed();
        // Commit after the editor has closed: the edit rebuilds the table.
        QTimer::singleShot(0, this, [this, t, text, col = it->column()] {
            if (col == 0) {
                bool ok = false;
                const float nt = text.toFloat(&ok);
                QString conflict;
                if (!ok || !m_doc->moveChannelKey(Channel::Event, t, nt, &conflict)) {
                    emit m_doc->message(ok ? tr("Cannot move the event: %1.").arg(conflict) : tr("'%1' is not a time.").arg(text));
                    rebuild();
                }
            } else if (text.isEmpty()) {
                m_doc->removeCell(t, Channel::Event);
            } else {
                Value v;
                v.s = text.toStdString();
                m_doc->setCell(t, Channel::Event, v, false);
            }
        });
    });
    rebuild();
}

void AnimEventsDock::rebuild()
{
    m_updating = true;
    const Node* n = m_doc->selectedNode();
    m_times.clear();
    m_table->setRowCount(0);
    if (n) {
        m_table->setRowCount(int(n->eventKeys.size()));
        for (size_t i = 0; i < n->eventKeys.size(); i++) {
            m_times.push_back(n->eventKeys[i].t);
            m_table->setItem(int(i), 0, new QTableWidgetItem(QString::number(double(n->eventKeys[i].t), 'f', 3)));
            m_table->setItem(int(i), 1, new QTableWidgetItem(qs(n->eventKeys[i].v)));
        }
    }
    setWindowTitle(n && !n->eventKeys.empty() ? tr("Events (%1)").arg(n->eventKeys.size()) : tr("Events"));
    m_updating = false;
}
