// stagescene.h — grid canvas (node system) for placing/editing game objects per layer.
#pragma once
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include "stagemodel.h"

class StageScene;   // fwd

// One placed object glyph on the canvas. Holds its GameObject by VALUE so it
// stays valid across model edits (the model's QList may reallocate).
// NOTE: QGraphicsRectItem is NOT a QObject, so NodeItem cannot use Q_OBJECT;
// it reports events back to the owning StageScene via plain callbacks.
class NodeItem : public QGraphicsRectItem {
public:
    NodeItem(const GameObject& o, int cell, StageScene* scene, QGraphicsItem* parent = nullptr);
    GameObject data;            // copy of the model object (coords + type + props)
    void refresh();

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
private:
    int cell;
    StageScene* host;
};

class StageScene : public QGraphicsScene {
    Q_OBJECT
public:
    StageScene(StageModel* m, int cell, QObject* parent = nullptr);
    void rebuild();
    void setActiveLayer(int l) { activeLayer = l; }
    int  cellSize() const { return cell; }

    void setBrush(const QString& category, const QString& typeId) { brushCat=category; brushType=typeId; }
    void clearBrush() { brushCat.clear(); brushType.clear(); }

    GameObject* selectedObject() const;

    // callbacks from NodeItem (because NodeItem can't be a QObject)
    void nodePressed(NodeItem* n);
    void nodeMoved(NodeItem* n, int x, int y);

signals:
    void cellClicked(int x, int y);
    void selectionChanged(NodeItem* node);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;

private:
    StageModel* model;
    int cell;
    int activeLayer = 1;
    QString brushCat, brushType;
    NodeItem* selNode = nullptr;
    QList<NodeItem*> nodes;
    QList<GameObject*> terrainOwned;
    void drawGrid();
    QPoint cellOf(const QPointF& p) const;
};
