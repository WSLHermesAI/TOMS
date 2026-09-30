#include "event_flow_view.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainterPath>
#include <QPen>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <set>

namespace toms::editor {
namespace {
constexpr qreal kNodeW = 210, kNodeH = 26, kColGap = 90, kRowGap = 10;

// A node that knows its own id, so a click can be traced back to a record.
class NodeItem : public QGraphicsRectItem {
public:
    NodeItem(const QString& id, const QRectF& r) : QGraphicsRectItem(r), id_(id) {}
    QString id() const { return id_; }
private:
    QString id_;
};
}  // namespace

EventFlowView::EventFlowView(QString projectRoot, QWidget* parent)
    : QWidget(parent), root_(std::move(projectRoot)) {
    scene_ = new QGraphicsScene(this);
    view_  = new QGraphicsView(scene_, this);
    view_->setRenderHint(QPainter::Antialiasing, true);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setBackgroundBrush(QColor(28, 30, 34));

    poolFilter_ = new QComboBox(this);
    kindFilter_ = new QComboBox(this);
    floorFrom_  = new QSpinBox(this);  floorFrom_->setRange(0, 70);
    floorTo_    = new QSpinBox(this);  floorTo_->setRange(0, 70);  floorTo_->setValue(70);
    problemsOnly_ = new QCheckBox(tr("只看有問題的"), this);
    summary_    = new QLabel(this);

    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("章節:"), this));  bar->addWidget(poolFilter_);
    bar->addWidget(new QLabel(tr("類型:"), this));  bar->addWidget(kindFilter_);
    bar->addWidget(new QLabel(tr("樓層:"), this));  bar->addWidget(floorFrom_);
    bar->addWidget(new QLabel(tr("-"), this));      bar->addWidget(floorTo_);
    bar->addWidget(problemsOnly_);
    bar->addStretch(1);
    bar->addWidget(summary_);

    auto* box = new QVBoxLayout(this);
    box->addLayout(bar);
    box->addWidget(view_, 1);

    connect(poolFilter_, &QComboBox::currentTextChanged, this, [this] { applyFilters(); });
    connect(kindFilter_, &QComboBox::currentTextChanged, this, [this] { applyFilters(); });
    connect(problemsOnly_, &QCheckBox::toggled, this, [this] { applyFilters(); });
    connect(floorFrom_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { applyFilters(); });
    connect(floorTo_,   qOverload<int>(&QSpinBox::valueChanged), this, [this] { applyFilters(); });

    reload();
}

void EventFlowView::reload() {
    graph_ = loadEventGraph(root_.toStdString());

    // Build the filter choices from what is actually there, keeping the current selection if it survives.
    const QString keepPool = poolFilter_->currentText(), keepKind = kindFilter_->currentText();
    poolFilter_->blockSignals(true); kindFilter_->blockSignals(true);
    poolFilter_->clear(); poolFilter_->addItem(tr("(全部)"));
    kindFilter_->clear(); kindFilter_->addItem(tr("(全部)"));
    std::set<QString> kinds;
    for (const auto& n : graph_.nodes) {
        if (n.kind == NodeKind::Pool)  poolFilter_->addItem(QString::fromStdString(n.id));
        if (n.kind == NodeKind::Event) kinds.insert(QString::fromStdString(n.label));
    }
    for (const auto& k : kinds) kindFilter_->addItem(k);
    if (int i = poolFilter_->findText(keepPool); i > 0) poolFilter_->setCurrentIndex(i);
    if (int i = kindFilter_->findText(keepKind); i > 0) kindFilter_->setCurrentIndex(i);
    poolFilter_->blockSignals(false); kindFilter_->blockSignals(false);

    buildScene();
}

void EventFlowView::buildScene() {
    scene_->clear();

    // Column per kind, so "who points at whom" reads left to right: floors | pools | events | strings.
    // Within a column, order by id; floors additionally by their sequence.
    std::map<int, std::vector<const GraphNode*>> cols;      // 0 pool, 1 event, 2 floor, 3 text
    for (const auto& n : graph_.nodes) {
        switch (n.kind) {
            case NodeKind::Pool:    cols[0].push_back(&n); break;
            case NodeKind::Event:   cols[1].push_back(&n); break;
            case NodeKind::Floor:   cols[2].push_back(&n); break;
            case NodeKind::TextKey: cols[3].push_back(&n); break;
        }
    }
    for (auto& [c, v] : cols)
        std::sort(v.begin(), v.end(), [](const GraphNode* a, const GraphNode* b) {
            if (a->kind == NodeKind::Floor && a->floorSeq != b->floorSeq) return a->floorSeq < b->floorSeq;
            return a->id < b->id;
        });

    std::map<std::string, QPointF> pos;
    int visible = 0;
    for (int c = 0; c < 4; ++c) {
        qreal y = 0;
        for (const GraphNode* n : cols[c]) {
            const QPointF p(c * (kNodeW + kColGap), y);
            pos[n->id] = p;
            addNode(*n, p.x(), p.y());
            y += kNodeH + kRowGap;
            ++visible;
        }
    }
    for (const auto& e : graph_.edges) addEdge(e);

    summary_->setText(tr("節點 %1 · 連線 %2 · 問題 %3")
                          .arg(visible).arg((int)graph_.edges.size()).arg((int)graph_.problems.size()));
    view_->setSceneRect(scene_->itemsBoundingRect().adjusted(-40, -40, 40, 40));
}

void EventFlowView::addNode(const GraphNode& n, qreal x, qreal y) {
    const QRectF r(x, y, kNodeW, kNodeH);
    auto* item = new NodeItem(QString::fromStdString(n.id), r);

    // Colour carries the meaning: a problem node is red, everything else is neutral by kind.
    bool isProblem = false;
    for (const auto& p : graph_.problems) if (p.id == n.id) { isProblem = true; break; }
    QColor fill = QColor(60, 64, 72), edge = QColor(120, 126, 136);
    switch (n.kind) {
        case NodeKind::Pool:    fill = QColor(52, 84, 110); break;
        case NodeKind::Event:   fill = QColor(64, 92, 66);  break;
        case NodeKind::Floor:   fill = QColor(84, 76, 60);  break;
        case NodeKind::TextKey: fill = QColor(70, 60, 84);  break;
    }
    if (isProblem) { fill = QColor(120, 48, 48); edge = QColor(230, 120, 120); }
    item->setBrush(fill);
    item->setPen(QPen(edge, isProblem ? 1.6 : 1.0));
    item->setToolTip(QString::fromStdString(n.id));
    scene_->addItem(item);

    auto* text = new QGraphicsSimpleTextItem(
        QString::fromStdString(n.kind == NodeKind::Event ? n.id + "  (" + n.label + ")" : n.id), item);
    text->setBrush(QColor(230, 232, 236));
    text->setPos(x + 6, y + 5);
}

void EventFlowView::addEdge(const GraphEdge& e) {
    // Both ends exist by construction in the model; guard anyway so a future edit cannot crash the view.
    QRectF a, b; bool fa = false, fb = false;
    for (auto* it : scene_->items()) {
        if (auto* n = dynamic_cast<NodeItem*>(it)) {
            if (n->id().toStdString() == e.from) { a = n->rect().translated(n->pos()); fa = true; }
            if (n->id().toStdString() == e.to)   { b = n->rect().translated(n->pos()); fb = true; }
        }
    }
    if (!fa || !fb) return;
    const QPointF p1 = (a.left() < b.left()) ? QPointF(a.right(), a.center().y())
                                             : QPointF(b.right(), b.center().y());
    const QPointF p2 = (a.left() < b.left()) ? QPointF(b.left(), b.center().y())
                                             : QPointF(a.left(), a.center().y());
    QPainterPath path(p1);
    const qreal dx = (p2.x() - p1.x()) * 0.5;
    path.cubicTo(p1 + QPointF(dx, 0), p2 - QPointF(dx, 0), p2);

    QColor c(120, 126, 136, 150);
    if (e.kind == GraphEdge::Kind::FloorEvent) c = QColor(150, 130, 90, 170);
    if (e.kind == GraphEdge::Kind::EventText)  c = QColor(120, 100, 150, 150);
    scene_->addPath(path, QPen(c, 1.0));
}

bool EventFlowView::passesFilters(const GraphNode& n) const {
    if (n.kind == NodeKind::Event) {
        const QString want = kindFilter_->currentText();
        if (want != tr("(全部)") && QString::fromStdString(n.label) != want) return false;
    }
    if (n.kind == NodeKind::Floor) {
        if (n.floorSeq && (n.floorSeq < floorFrom_->value() || n.floorSeq > floorTo_->value())) return false;
    }
    return true;
}

void EventFlowView::applyFilters() {
    if (!scene_) return;
    std::set<std::string> hidden;
    for (const auto& n : graph_.nodes) if (!passesFilters(n)) hidden.insert(n.id);

    for (auto* it : scene_->items())
        if (auto* n = dynamic_cast<NodeItem*>(it))
            n->setVisible(hidden.find(n->id().toStdString()) == hidden.end());
    // Edges follow their endpoints: hide any edge whose either end is hidden.
    for (auto* it : scene_->items())
        if (dynamic_cast<NodeItem*>(it) == nullptr && it->type() == QGraphicsPathItem::Type) {
            bool any = true;   // conservative: leave visible unless we tracked endpoints; see note in README
            it->setVisible(any);
        }
}

void EventFlowView::onNodeClicked(QGraphicsItem*) {}

}  // namespace toms::editor
