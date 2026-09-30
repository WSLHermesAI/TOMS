#pragma once
// event_flow_view.h -- the event editor's visual flow map.
//
// A thin QGraphicsView over event_graph.h: that model owns the rules (which pools exist, which floors roll
// an event, which text keys are missing, which events are orphaned) and this only positions and draws what
// it produces. Qt Widgets only -- no platform-specific code -- so the same file builds into toms_editor on
// Windows and Linux.
//
// The graph, as the data actually has it:
//   chapter pool ---> event ---> i18n text key        (membership, and where the string lives)
//   floor        ---> event                            (which floors can roll it)
// Nodes are laid out in columns by kind so the direction of the relationship is readable left to right.

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QWidget>

#include "event_graph.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

namespace toms::editor {

// Draws one graph. Owned by the tab; `reload()` re-reads the same root so the map can follow a save.
class EventFlowView : public QWidget {
    Q_OBJECT
public:
    explicit EventFlowView(QString projectRoot, QWidget* parent = nullptr);

    void reload();                       // re-read the files and rebuild the scene
    const EventGraph& graph() const { return graph_; }

private slots:
    void applyFilters();                 // show/hide per the controls
    void onNodeClicked(class QGraphicsItem* item);   // future: select the event for editing

private:
    void buildScene();
    void addNode(const GraphNode& n, qreal x, qreal y);
    void addEdge(const GraphEdge& e);
    bool passesFilters(const GraphNode& n) const;

    QString             root_;
    EventGraph          graph_;
    QGraphicsScene*     scene_ = nullptr;
    QGraphicsView*      view_  = nullptr;
    QComboBox*          poolFilter_   = nullptr;
    QComboBox*          kindFilter_   = nullptr;
    QSpinBox*           floorFrom_    = nullptr;
    QSpinBox*           floorTo_      = nullptr;
    QCheckBox*          problemsOnly_ = nullptr;
    QLabel*             summary_      = nullptr;
};

}  // namespace toms::editor
