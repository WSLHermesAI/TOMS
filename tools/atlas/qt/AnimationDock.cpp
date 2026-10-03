#include "AnimationDock.h"

#include "AtlasDocument.h"
#include "Icons.h"
#include "Theme.h"

#include <QCheckBox>
#include <QCollator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

using atlas::Animation;
using atlas::Project;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string u8(const QString& s) { return s.toStdString(); }

// Works on a const or a mutable project (returns a pointer of matching constness).
template <class P>
auto findAnimation(P& p, const QString& name) -> decltype(&p.animations.front())
{
    for (auto& a : p.animations)
        if (qs(a.name) == name) return &a;
    return nullptr;
}

QToolButton* toolButton(QWidget* parent, Icons::Id icon, const QString& tip)
{
    auto* b = new QToolButton(parent);
    b->setIcon(Icons::icon(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

}  // namespace

// Plays one animation: each frame for its time, frames aligned on their pivots so the motion
// reads the way the game will draw it.
class AnimationPreview : public QWidget
{
public:
    AnimationPreview(AtlasDocument* doc, QWidget* parent)
        : QWidget(parent)
        , m_doc(doc)
        , m_timer(new QTimer(this))
    {
        setMinimumSize(120, 100);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, [this] { step(); });
    }

    void setAnimation(const QString& name)
    {
        if (name != m_name) m_frame = 0;
        m_name = name;
        update();
        if (m_playing) schedule();
    }
    bool isPlaying() const { return m_playing; }
    void setPlaying(bool on)
    {
        m_playing = on;
        if (on) schedule();
        else m_timer->stop();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const Theme::Colors& tc = Theme::colors();
        p.fillRect(rect(), tc.canvasBackground);
        const Animation* a = animation();
        if (!a || a->frames.empty()) {
            p.setPen(palette().color(QPalette::PlaceholderText));
            p.drawText(rect(), Qt::AlignCenter, a ? tr("No frames") : tr("No animation"));
            return;
        }
        // One scale and one anchor for all frames: from the largest extents around the pivot.
        double left = 0, right = 1, top = 0, bottom = 1;
        for (const atlas::AnimFrame& f : a->frames) {
            const QImage img = m_doc->spriteImage(qs(f.sprite));
            if (img.isNull()) continue;
            const QPointF pv = pivot(qs(f.sprite), img.size());
            left = std::max(left, pv.x());
            right = std::max(right, img.width() - pv.x());
            top = std::max(top, pv.y());
            bottom = std::max(bottom, img.height() - pv.y());
        }
        const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -22);
        double scale = std::min(area.width() / (left + right), area.height() / (top + bottom));
        if (scale >= 1) scale = std::floor(scale);   // crisp pixel art
        const QPointF anchor(area.center().x() + (left - right) * scale / 2, area.center().y() + (top - bottom) * scale / 2);

        const atlas::AnimFrame& f = a->frames[size_t(m_frame) % a->frames.size()];
        const QImage img = m_doc->spriteImage(qs(f.sprite));
        if (!img.isNull()) {
            const QPointF pv = pivot(qs(f.sprite), img.size());
            const QRectF target(anchor - pv * scale, QSizeF(img.size()) * scale);
            p.setRenderHint(QPainter::SmoothPixmapTransform, scale < 1);
            p.drawImage(target, img);
        }
        p.setPen(QPen(tc.pivot, 1));
        p.drawLine(QLineF(anchor.x() - 4, anchor.y(), anchor.x() + 4, anchor.y()));
        p.drawLine(QLineF(anchor.x(), anchor.y() - 4, anchor.x(), anchor.y() + 4));
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(QRectF(rect()).adjusted(6, 0, -6, -4), Qt::AlignBottom | Qt::AlignLeft,
                   tr("%1 / %2  %3").arg(m_frame % int(a->frames.size()) + 1).arg(a->frames.size()).arg(qs(f.sprite)));
    }

private:
    const Animation* animation() const
    {
        return findAnimation(m_doc->project(), m_name);
    }
    QPointF pivot(const QString& sprite, const QSize& size) const
    {
        const atlas::SpriteDef d = m_doc->spriteDef(sprite);
        const float* pv = d.hasPivot ? d.pivot : m_doc->project().settings.defaultPivot;
        return QPointF(pv[0] * size.width(), pv[1] * size.height());
    }
    void schedule()
    {
        const Animation* a = animation();
        if (!a || a->frames.empty()) return;
        const float t = a->frames[size_t(m_frame) % a->frames.size()].time;
        m_timer->start(std::max(16, int(std::lround(t * 1000))));
    }
    void step()
    {
        const Animation* a = animation();
        if (!a || a->frames.empty()) return;
        const int n = int(a->frames.size());
        if (m_frame + 1 >= n && !a->loop) {
            m_playing = false;   // a one-shot animation stops on its last frame
            update();
            return;
        }
        m_frame = (m_frame + 1) % n;
        update();
        schedule();
    }

    AtlasDocument* m_doc;
    QTimer* m_timer;
    QString m_name;
    int m_frame = 0;
    bool m_playing = true;
};

AnimationDock::AnimationDock(AtlasDocument* doc, QWidget* parent)
    : QDockWidget(tr("Animations"), parent)
    , m_doc(doc)
{
    setObjectName(QStringLiteral("AnimationsDock"));
    auto* split = new QSplitter(Qt::Horizontal, this);

    // Animations.
    auto* left = new QWidget(split);
    m_list = new QListWidget(left);
    auto* add = toolButton(left, Icons::Id::Add, tr("New animation"));
    auto* remove = toolButton(left, Icons::Id::Remove, tr("Delete animation"));
    auto* lbar = new QHBoxLayout;
    lbar->addWidget(new QLabel(tr("Animations"), left));
    lbar->addStretch(1);
    lbar->addWidget(add);
    lbar->addWidget(remove);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(4, 2, 4, 4);
    ll->addLayout(lbar);
    ll->addWidget(m_list, 1);

    // Frames.
    auto* mid = new QWidget(split);
    m_frames = new QTableWidget(0, 2, mid);
    m_frames->setHorizontalHeaderLabels({tr("Sprite"), tr("Time (s)")});
    m_frames->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_frames->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_frames->verticalHeader()->setDefaultSectionSize(22);
    m_frames->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_loop = new QCheckBox(tr("Loop"), mid);
    m_addFrames = toolButton(mid, Icons::Id::Add, tr("Add the selected sprites as frames"));
    m_removeFrames = toolButton(mid, Icons::Id::Remove, tr("Remove the selected frames"));
    m_up = toolButton(mid, Icons::Id::Up, tr("Move frame up"));
    m_down = toolButton(mid, Icons::Id::Down, tr("Move frame down"));
    auto* mbar = new QHBoxLayout;
    mbar->addWidget(new QLabel(tr("Frames"), mid));
    mbar->addWidget(m_loop);
    mbar->addStretch(1);
    for (QToolButton* b : {m_addFrames, m_removeFrames, m_up, m_down}) mbar->addWidget(b);
    auto* ml = new QVBoxLayout(mid);
    ml->setContentsMargins(4, 2, 4, 4);
    ml->addLayout(mbar);
    ml->addWidget(m_frames, 1);

    // Preview.
    auto* right = new QWidget(split);
    m_preview = new AnimationPreview(doc, right);
    m_play = toolButton(right, Icons::Id::Pause, tr("Play / pause"));
    auto* rbar = new QHBoxLayout;
    rbar->addWidget(new QLabel(tr("Preview"), right));
    rbar->addStretch(1);
    rbar->addWidget(m_play);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(4, 2, 4, 4);
    rl->addLayout(rbar);
    rl->addWidget(m_preview, 1);

    split->addWidget(left);
    split->addWidget(mid);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    split->setStretchFactor(2, 1);
    setWidget(split);

    connect(add, &QToolButton::clicked, this, &AnimationDock::onAdd);
    connect(remove, &QToolButton::clicked, this, &AnimationDock::onRemove);
    connect(m_addFrames, &QToolButton::clicked, this, &AnimationDock::onAddFrames);
    connect(m_removeFrames, &QToolButton::clicked, this, &AnimationDock::onRemoveFrames);
    connect(m_up, &QToolButton::clicked, this, [this] { onMoveFrame(-1); });
    connect(m_down, &QToolButton::clicked, this, [this] { onMoveFrame(1); });
    connect(m_play, &QToolButton::clicked, this, [this] {
        m_preview->setPlaying(!m_preview->isPlaying());
        m_play->setIcon(Icons::icon(m_preview->isPlaying() ? Icons::Id::Pause : Icons::Id::Play));
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this] {
        if (m_updating) return;
        refreshFrames();
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_updating) return;
        const QString from = item->data(Qt::UserRole).toString(), to = item->text().trimmed();
        if (to.isEmpty() || findAnimation(m_doc->project(), to)) {   // empty or taken: put the old name back
            m_updating = true;
            item->setText(from);
            m_updating = false;
            return;
        }
        m_doc->edit(tr("Rename animation"), [&](Project& p) {
            if (Animation* a = findAnimation(p, from)) a->name = u8(to);
        });
    });
    connect(m_loop, &QCheckBox::toggled, this, [this](bool on) {
        editCurrent(on ? tr("Loop animation") : tr("Play once"), [&](Animation& a) { a.loop = on; });
    });
    connect(m_frames, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_updating) return;
        const int row = item->row();
        if (item->column() == 0) {
            const std::string sprite = u8(item->text().trimmed());
            editCurrent(tr("Frame sprite"), [&](Animation& a) {
                if (row < int(a.frames.size())) a.frames[size_t(row)].sprite = sprite;
            });
        } else {
            const float t = std::max(0.001f, item->data(Qt::EditRole).toFloat());
            editCurrent(tr("Frame time"), [&](Animation& a) {
                if (row < int(a.frames.size())) a.frames[size_t(row)].time = t;
            }, QStringLiteral("frametime%1").arg(row));
        }
    });
    connect(doc, &AtlasDocument::projectChanged, this, &AnimationDock::refresh);
    connect(doc, &AtlasDocument::buildFinished, this, [this] {
        markMissingSprites();
        m_preview->update();
    });
    connect(doc, &AtlasDocument::selectionChanged, this, [this] { m_addFrames->setEnabled(!m_doc->selection().isEmpty() && m_list->currentItem()); });
    refresh();
}

QString AnimationDock::currentAnimation() const
{
    const QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void AnimationDock::refresh()
{
    const QString keep = currentAnimation();
    m_updating = true;
    m_list->clear();
    for (const Animation& a : m_doc->project().animations) {
        auto* item = new QListWidgetItem(qs(a.name), m_list);
        item->setData(Qt::UserRole, qs(a.name));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setIcon(Icons::icon(Icons::Id::Play));
    }
    int row = 0;
    for (int i = 0; i < m_list->count(); i++)
        if (m_list->item(i)->data(Qt::UserRole).toString() == keep) row = i;
    if (m_list->count()) m_list->setCurrentRow(row);
    m_updating = false;
    refreshFrames();
}

void AnimationDock::refreshFrames()
{
    const QString name = currentAnimation();
    const Animation* a = findAnimation(m_doc->project(), name);
    m_updating = true;
    const int keepRow = m_frames->currentRow();
    m_frames->setRowCount(a ? int(a->frames.size()) : 0);
    if (a) {
        for (size_t i = 0; i < a->frames.size(); i++) {
            const atlas::AnimFrame& f = a->frames[i];
            auto* sprite = new QTableWidgetItem(qs(f.sprite));
            auto* time = new QTableWidgetItem;
            time->setData(Qt::EditRole, double(f.time));
            m_frames->setItem(int(i), 0, sprite);
            m_frames->setItem(int(i), 1, time);
        }
        if (keepRow >= 0 && keepRow < m_frames->rowCount()) m_frames->selectRow(keepRow);
    }
    m_loop->setChecked(a && a->loop);
    markMissingSprites();
    m_updating = false;
    for (QWidget* w : std::initializer_list<QWidget*>{m_frames, m_loop, m_removeFrames, m_up, m_down}) w->setEnabled(a != nullptr);
    m_addFrames->setEnabled(a && !m_doc->selection().isEmpty());
    m_preview->setAnimation(name);
}

void AnimationDock::markMissingSprites()
{
    // Only recolours: rebuilding the rows would close an editor the user may have open.
    for (int r = 0; r < m_frames->rowCount(); r++)
        if (QTableWidgetItem* item = m_frames->item(r, 0)) {
            const bool missing = m_doc->snapshot() && !m_doc->spriteExists(item->text());
            const QSignalBlocker block(m_frames);   // a colour change is not an edit
            item->setData(Qt::ForegroundRole, missing ? QVariant(QColor(0xf0, 0x4a, 0x5a)) : QVariant());
            item->setToolTip(missing ? tr("No sprite with this name") : QString());
        }
}

void AnimationDock::editCurrent(const QString& text, const std::function<void(Animation&)>& change, const QString& mergeKey)
{
    const QString name = currentAnimation();
    if (m_updating || name.isEmpty()) return;
    m_doc->edit(text, [&](Project& p) {
        if (Animation* a = findAnimation(p, name)) change(*a);
    }, mergeKey.isEmpty() ? QString() : QStringLiteral("anim.%1.%2").arg(name, mergeKey));
}

void AnimationDock::onAdd()
{
    QString name = QStringLiteral("anim");
    for (int i = 1; findAnimation(m_doc->project(), name); i++) name = QStringLiteral("anim_%1").arg(i);
    // New animations start with the selected sprites, in natural name order (run_2 before run_10).
    QStringList sprites = m_doc->selection();
    QCollator coll;
    coll.setNumericMode(true);
    std::sort(sprites.begin(), sprites.end(), [&](const QString& a, const QString& b) { return coll.compare(a, b) < 0; });
    m_doc->edit(tr("New animation"), [&](Project& p) {
        Animation a;
        a.name = u8(name);
        for (const QString& s : sprites) a.frames.push_back({u8(s), 0.1f});
        p.animations.push_back(a);
    });
    for (int i = 0; i < m_list->count(); i++)
        if (m_list->item(i)->data(Qt::UserRole).toString() == name) {
            m_list->setCurrentRow(i);
            m_list->editItem(m_list->item(i));
        }
}

void AnimationDock::onRemove()
{
    const QString name = currentAnimation();
    if (name.isEmpty()) return;
    m_doc->edit(tr("Delete animation %1").arg(name), [&](Project& p) {
        p.animations.erase(std::remove_if(p.animations.begin(), p.animations.end(),
                                          [&](const Animation& a) { return qs(a.name) == name; }),
                           p.animations.end());
    });
}

void AnimationDock::onAddFrames()
{
    QStringList sprites = m_doc->selection();
    QCollator coll;
    coll.setNumericMode(true);
    std::sort(sprites.begin(), sprites.end(), [&](const QString& a, const QString& b) { return coll.compare(a, b) < 0; });
    // Insert after the selected frame, or at the end.
    const int at = m_frames->currentRow() >= 0 ? m_frames->currentRow() + 1 : m_frames->rowCount();
    editCurrent(tr("Add frames"), [&](Animation& a) {
        std::vector<atlas::AnimFrame> add;
        for (const QString& s : sprites) add.push_back({u8(s), 0.1f});
        a.frames.insert(a.frames.begin() + std::min<size_t>(size_t(at), a.frames.size()), add.begin(), add.end());
    });
}

void AnimationDock::onRemoveFrames()
{
    QList<int> rows;
    for (const QModelIndex& idx : m_frames->selectionModel()->selectedRows()) rows << idx.row();
    if (rows.isEmpty()) return;
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    editCurrent(tr("Remove frames"), [&](Animation& a) {
        for (int r : rows)
            if (r < int(a.frames.size())) a.frames.erase(a.frames.begin() + r);
    });
}

void AnimationDock::onMoveFrame(int delta)
{
    const int row = m_frames->currentRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || to >= m_frames->rowCount()) return;
    editCurrent(tr("Move frame"), [&](Animation& a) { std::swap(a.frames[size_t(row)], a.frames[size_t(to)]); });
    m_frames->selectRow(to);
}
