// EventWidgets.h -- small widgets the event editor builds its forms from.
#pragma once

#include <QColor>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QWidget>

#include <functional>
#include <map>
#include <string>
#include <vector>

// A text box that commits when it loses focus (as an HTML "change"), and grows with its lines.
class CommitText : public QPlainTextEdit {
public:
    CommitText(const QString& l_oText, QWidget* l_pParent = nullptr);
    std::function<void(const QString&)> m_fnOnCommit;
    std::function<void(const QString&)> m_fnOnEdit;   // every keystroke
protected:
    void focusOutEvent(QFocusEvent* l_pE) override;
private:
    void f_Fit();
    QString m_oOrig;
};

// A line edit that commits on Enter or focus loss, only when its text changed.
class CommitLine : public QLineEdit {
public:
    CommitLine(const QString& l_oText, QWidget* l_pParent = nullptr);
    std::function<void(const QString&)> m_fnOnCommit;
private:
    QString m_oOrig;
};

namespace EvUi {
QLabel* f_Rich(const QString& l_oHtml);
QLabel* f_Muted(const QString& l_oText);
QLabel* f_Heading(const QString& l_oText);
QToolButton* f_Chip(const QString& l_oText, const QColor& l_oBorder = QColor(), const QString& l_oTip = QString());
QWidget* f_Banner(const QString& l_oHtml, bool l_bBad);
QString f_Esc(const QString& l_oS);
QString f_Q(const std::string& l_strS);
}  // namespace EvUi

// The floor map: chapters as rows, stairs as arrows; locked stairs gold and dashed with the event name.
class FloorMapView : public QWidget {
    Q_OBJECT
public:
    struct Node { QString m_oId, m_oAct; bool m_bBoss = false, m_bWarn = false, m_bGhost = false, m_bIsNew = false; QRectF m_oR; };
    struct Edge { QString m_oFrom, m_oTo, m_oLock; bool m_bMiss = false; };
    void f_SetGraph(std::vector<Node> l_vNodes, std::vector<Edge> l_vEdges, std::vector<std::pair<QString, double>> l_vRows, const QString& l_oSel);
    QSize sizeHint() const override { return m_oSize; }
signals:
    void f_Clicked(const QString& l_oId);
protected:
    void paintEvent(QPaintEvent* l_pE) override;
    void mousePressEvent(QMouseEvent* l_pE) override;
private:
    std::vector<Node> m_vNodes;
    std::vector<Edge> m_vEdges;
    std::vector<std::pair<QString, double>> m_vRows;
    QString m_oSel;
    QSize m_oSize{400, 200};
};
