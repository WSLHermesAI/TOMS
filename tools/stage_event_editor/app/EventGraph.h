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
    QPointF m_oPan{30, 24};
    double m_dZoom = 1.0;
    std::map<std::string, QPointF> m_mapPos;   // node id ("ev:x", "var:x" ...) -> top-left, in graph units
    std::set<std::string> m_setExtra;          // nodes added from the menu (shown even without links)
    std::set<std::string> m_setSel;
    int m_iSelLink = -1;
    bool m_bLoaded = false;                  // positions read from QSettings
};

class LogicGraph : public QWidget {
    Q_OBJECT
public:
    struct Pin { std::string m_strId, m_strKind; QString m_oLabel; bool m_bGhost = false; };
    struct Node {
        std::string m_strId, m_strType, m_strName;
        QString m_oTitle, m_oSub;
        QColor m_oColor;
        std::vector<Pin> m_vIns, m_vOuts;
        bool m_bMissing = false;
        double m_dW = 190, m_dH = 60;
    };
    struct Link { std::string m_strFrom, m_strTo, m_strKind, m_strWho; std::function<void()> m_fnDel; };   // from / to: "node|pin"

    // edit(label, fn): one undo step; textOf: a {zh_TW, en} object in the editor's language
    LogicGraph(stage::EventProject& l_oP, LogicGraphState& l_oSt, std::function<void(const QString&, std::function<void()>)> l_fnEdit,
               std::function<QString(const stage::Json&)> l_fnTextOf, QWidget* l_pParent = nullptr);

    std::function<void(const std::string&)> m_fnOnSelectEvent;   // an event node clicked
    std::function<void(const std::string&)> m_fnOnOpenValue;     // a value node double-clicked ("var:x" ...)

    int f_EventNodes() const;
    int f_ValueNodes() const;
    // for the self test: connect two pins as a drag would; true when it made an edit
    bool f_ConnectPins(const std::string& l_strFrom, const std::string& l_strTo);
    bool f_HasNode(const std::string& l_strId) const { return m_mapNodes.count(l_strId) > 0; }
    std::vector<std::string> f_NodeIds() const { return m_vOrder; }
    const std::vector<Link>& f_Links() const { return m_vLinks; }
    void f_Fit();

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
    void f_Rebuild();
    bool f_MakeNode(const std::string& l_strId, Node& l_oN) const;
    using Action = std::pair<QString, std::function<void()>>;
    bool f_LinkAction(const Node& l_oOutN, const Pin& l_oOutP, const Node& l_oInN, const Pin& l_oInP, Action& l_pairAct) const;
    struct PinRef { const Node* m_pN = nullptr; const Pin* m_pP = nullptr; bool m_bOut = false; };
    PinRef f_PinOf(const std::string& l_strPid) const;
    bool f_ActionFor(const PinRef& l_oA, const PinRef& l_oB, Action* l_pAct = nullptr) const;
    void f_AutoLayout();
    void f_Measure(Node& l_oN) const;
    QString f_CondLabel(const stage::Json& l_jL) const;
    QString f_ActLabel(const stage::Json& l_jA) const;
    QString f_TrigSub(const stage::Json& l_jE) const;

    // geometry (graph units)
    QPointF f_PinPos(const std::string& l_strPid) const;
    QRectF f_NodeRect(const Node& l_oN) const;
    QPointF f_ToGraph(const QPointF& l_oW) const { return (l_oW - m_oSt.m_oPan) / m_oSt.m_dZoom; }
    std::string f_PinAt(const QPointF& l_oG) const;
    std::string f_NodeAt(const QPointF& l_oG) const;
    int f_LinkAt(const QPointF& l_oG) const;
    QPainterPath f_Curve(const QPointF& l_oA, const QPointF& l_oB) const;

    void f_AddMenu(const QPoint& l_oAt, const std::string& l_strPending);
    void f_SavePositions();
    void f_UpdateFlow();

    stage::EventProject& m_oP;
    LogicGraphState& m_oSt;
    std::function<void(const QString&, std::function<void()>)> m_fnEdit;
    std::function<QString(const stage::Json&)> m_fnTextOf;
    std::map<std::string, Node> m_mapNodes;
    std::vector<std::string> m_vOrder;     // drawing order
    std::vector<Link> m_vLinks;
    std::set<std::string> m_setUsed;         // pins with a link

    enum class Drag { None, Pan, Node, Link, Box };
    Drag m_eDrag = Drag::None;
    QPoint m_oPress;
    bool m_bMoved = false;
    Qt::MouseButton m_eBtn = Qt::NoButton;
    QPointF m_oPanStart;
    std::string m_strDragNode, m_strLinkFrom;
    QPointF m_oLinkTo;                     // graph units
    std::map<std::string, QPointF> m_mapOrigPos;
    std::set<std::string> m_setOk;           // pins the dragged link fits
    QRect m_oBox;
    QTimer* m_pFlowTimer = nullptr;
    double m_dFlowOffset = 0;
    bool m_bFitPending = false;
};
