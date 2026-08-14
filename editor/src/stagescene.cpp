// stagescene.cpp — grid canvas implementation.
#include "stagescene.h"
#include "catalog.h"
#include <QGraphicsSceneMouseEvent>
#include <QPen>
#include <QBrush>
#include <QFont>

NodeItem::NodeItem(const GameObject& o, int cellSize, StageScene* sc, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), data(o), cell(cellSize), host(sc) {
    setRect(0, 0, cell, cell);
    setFlags(ItemIsSelectable | ItemIsMovable);
    refresh();
}

void NodeItem::refresh() {
    QColor col(categoryColor(data.category));
    if (data.category == "terrain" && data.typeId == "wall") {
        setBrush(QBrush(col)); setPen(QPen(Qt::black, 1));
    } else if (data.category == "terrain" && data.typeId == "floor") {
        setBrush(QBrush(QColor("#2b2b33"))); setPen(QPen(Qt::black, 1));
    } else {
        setBrush(QBrush(col.lighter(120))); setPen(QPen(col.darker(140), 2));
    }
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    QGraphicsRectItem::mousePressEvent(e);
    host->nodePressed(this);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e) {
    QGraphicsRectItem::mouseReleaseEvent(e);
    QPointF p = pos();
    int x = qRound(p.x() / cell), y = qRound(p.y() / cell);
    host->nodeMoved(this, x, y);
}

StageScene::StageScene(StageModel* m, int cellSize, QObject* parent)
    : QGraphicsScene(parent), model(m), cell(cellSize) {
    rebuild();
}

void StageScene::drawGrid() {
    setSceneRect(0, 0, model->width * cell, model->height * cell);
    for (int x = 0; x <= model->width; ++x)
        addLine(x * cell, 0, x * cell, model->height * cell, QPen(QColor(60,60,70), 1));
    for (int y = 0; y <= model->height; ++y)
        addLine(0, y * cell, model->width * cell, y * cell, QPen(QColor(60,60,70), 1));
}

QPoint StageScene::cellOf(const QPointF& p) const {
    return QPoint((int)(p.x() / cell), (int)(p.y() / cell));
}

void StageScene::rebuild() {
    clear();
    qDeleteAll(terrainOwned); terrainOwned.clear();
    nodes.clear(); selNode = nullptr;
    drawGrid();
    for (int y = 0; y < model->height; ++y)
        for (int x = 0; x < model->width; ++x) {
            QChar c = model->terrainChar(x, y);
            if (c == '#' || c == '.') {
                GameObject* t = new GameObject();
                t->x = x; t->y = y; t->layer = 0;
                t->category = "terrain"; t->typeId = (c == '#' ? "wall" : "floor");
                terrainOwned.append(t);
                NodeItem* it = new NodeItem(*t, cell, this);
                it->setPos(x * cell, y * cell);
                addItem(it);
                it->setFlag(QGraphicsItem::ItemIsSelectable, false);
                it->setAcceptedMouseButtons(Qt::NoButton);
                nodes.append(it);
            }
        }
    for (const auto& o : model->objects) {
        NodeItem* it = new NodeItem(o, cell, this);
        it->setPos(o.x * cell, o.y * cell);
        addItem(it);
        nodes.append(it);
        const ObjectType* t = catalogByType(o.category, o.typeId);
        if (t) {
            QGraphicsTextItem* txt = addText(t->ch, QFont("monospace", cell/2));
            txt->setPos(o.x * cell + cell*0.25, o.y * cell + cell*0.1);
            txt->setDefaultTextColor(Qt::white);
        }
    }
}

void StageScene::nodePressed(NodeItem* n) {
    selNode = n;
    emit selectionChanged(n);
}

void StageScene::nodeMoved(NodeItem* n, int x, int y) {
    // update model object in place WITHOUT emitting changed
    // (a full rebuild here would delete 'n' mid-event -> crash)
    GameObject* m = model->objectAt(n->data.x, n->data.y, n->data.layer);
    if (m) { m->x = x; m->y = y; }
    n->data.x = x; n->data.y = y;
    n->setPos(x * cell, y * cell);
}

GameObject* StageScene::selectedObject() const {
    if (!selNode) return nullptr;
    return model->objectAt(selNode->data.x, selNode->data.y, selNode->data.layer);
}

void StageScene::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    QPoint c = cellOf(e->scenePos());
    if (!brushCat.isEmpty() && c.x() >= 0 && c.y() >= 0 && c.x() < model->width && c.y() < model->height) {
        GameObject* existing = model->objectAt(c.x(), c.y(), activeLayer);
        if (existing) model->removeObject(c.x(), c.y(), activeLayer);
        GameObject o; o.x = c.x(); o.y = c.y(); o.layer = activeLayer;
        o.category = brushCat; o.typeId = brushType;
        const ObjectType* t = catalogByType(brushCat, brushType);
        if (t) o.props = t->defaults;
        model->addObject(o);
        rebuild();
        selNode = nullptr;
        emit cellClicked(c.x(), c.y());
        return;
    }
    QGraphicsScene::mousePressEvent(e);
    emit cellClicked(c.x(), c.y());
}
