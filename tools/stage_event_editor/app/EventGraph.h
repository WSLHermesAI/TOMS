// EventGraph.h -- the event editor's logic graph (Logic graph tab): the HTML event editor's node graph, native
// (tools/web_editors/event_editor.html "logic graph", after thedmd/imgui-node-editor's blueprint style).
//
// Nodes: events (Fire and their conditions in; Then, done and their actions out), event variables, counters, flags and
// NPCs. Every link IS a piece of event data -- a trigger, a condition, an action or a "next" link -- so connecting two
// pins or deleting a link is an ordinary edit of the event (one undo step, saved like any other edit).
//
//   drag pin -> pin        connect (pins that fit turn green); drop on empty space: the add-node menu, filtered
//   right-click            the add-node menu (search, Fit view)       right / middle drag: pan     wheel: zoom
//   click a link, Delete   remove it                                 click an event: show it in the inspector
//   drag on empty space    box-select nodes (Shift adds)             double-click a value: open its tab      F: fit
//
// The widget is rebuilt with the rest of the editor after every edit; what must survive that (pan, zoom, node
// positions, the extra nodes added by hand, the selection) lives in LogicGraphState, owned by the editor. Node
// positions are also kept between runs (QSettings).
#pragma once

#include "event_project.h"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class QTimer;

struct LogicGraphState {
    QPointF pan{30, 24};
    double zoom = 1.0;
    std::map<std::string, QPointF> pos;   // node id ("ev:x", "var:x" ...) -> top-left, in graph units
    std::set<std::string> extra;          // nodes added from the menu (shown even without links)
    std::set<std::string> sel;
    int selLink = -1;
    bool loaded = false;                  // positions read from QSettings
};

class LogicGraph : public QWidget {
    Q_OBJECT
public:
    struct Pin { std::string id, kind; QString label; bool ghost = false; };
    struct Node {
        std::string id, type, name;
        QString title, sub;
        QColor color;
        std::vector<Pin> ins, outs;
        bool missing = false;
        double w = 190, h = 60;
    };
    struct Link { std::string from, to, kind, who; std::function<void()> del; };   // from / to: "node|pin"

    // edit(label, fn): one undo step; textOf: a {zh_TW, en} object in the editor's language
    LogicGraph(stage::EventProject& p, LogicGraphState& st, std::function<void(const QString&, std::function<void()>)> edit,
               std::function<QString(const stage::Json&)> textOf, QWidget* parent = nullptr);

    std::function<void(const std::string&)> onSelectEvent;   // an event node clicked
    std::function<void(const std::string&)> onOpenValue;     // a value node double-clicked ("var:x" ...)

    int eventNodes() const;
    int valueNodes() const;
    // for the self test: connect two pins as a drag would; true when it made an edit
    bool connectPins(const std::string& from, const std::string& to);
    bool hasNode(const std::string& id) const { return m_nodes.count(id) > 0; }
    std::vector<std::string> nodeIds() const { return m_order; }
    const std::vector<Link>& links() const { return m_links; }
    void fit();

    QSize sizeHint() const override { return {800, 420}; }
    QSize minimumSizeHint() const override { return {200, 160}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    // the model (from the project, like the HTML editor's neGraph / makeNode / linkAction)
    void rebuild();
    bool makeNode(const std::string& id, Node& n) const;
    using Action = std::pair<QString, std::function<void()>>;
    bool linkAction(const Node& outN, const Pin& outP, const Node& inN, const Pin& inP, Action& act) const;
    struct PinRef { const Node* n = nullptr; const Pin* p = nullptr; bool out = false; };
    PinRef pinOf(const std::string& pid) const;
    bool actionFor(const PinRef& a, const PinRef& b, Action* act = nullptr) const;
    void autoLayout();
    void measure(Node& n) const;
    QString condLabel(const stage::Json& l) const;
    QString actLabel(const stage::Json& a) const;
    QString trigSub(const stage::Json& e) const;

    // geometry (graph units)
    QPointF pinPos(const std::string& pid) const;
    QRectF nodeRect(const Node& n) const;
    QPointF toGraph(const QPointF& w) const { return (w - m_st.pan) / m_st.zoom; }
    std::string pinAt(const QPointF& g) const;
    std::string nodeAt(const QPointF& g) const;
    int linkAt(const QPointF& g) const;
    QPainterPath curve(const QPointF& a, const QPointF& b) const;

    void addMenu(const QPoint& at, const std::string& pending);
    void savePositions();
    void updateFlow();

    stage::EventProject& m_p;
    LogicGraphState& m_st;
    std::function<void(const QString&, std::function<void()>)> m_edit;
    std::function<QString(const stage::Json&)> m_textOf;
    std::map<std::string, Node> m_nodes;
    std::vector<std::string> m_order;     // drawing order
    std::vector<Link> m_links;
    std::set<std::string> m_used;         // pins with a link

    enum class Drag { None, Pan, Node, Link, Box };
    Drag m_drag = Drag::None;
    QPoint m_press;
    bool m_moved = false;
    Qt::MouseButton m_btn = Qt::NoButton;
    QPointF m_panStart;
    std::string m_dragNode, m_linkFrom;
    QPointF m_linkTo;                     // graph units
    std::map<std::string, QPointF> m_origPos;
    std::set<std::string> m_ok;           // pins the dragged link fits
    QRect m_box;
    QTimer* m_flowTimer = nullptr;
    double m_flowOffset = 0;
    bool m_fitPending = false;
};
