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
    CommitText(const QString& text, QWidget* parent = nullptr);
    std::function<void(const QString&)> onCommit;
    std::function<void(const QString&)> onEdit;   // every keystroke
protected:
    void focusOutEvent(QFocusEvent* e) override;
private:
    void fit();
    QString m_orig;
};

// A line edit that commits on Enter or focus loss, only when its text changed.
class CommitLine : public QLineEdit {
public:
    CommitLine(const QString& text, QWidget* parent = nullptr);
    std::function<void(const QString&)> onCommit;
private:
    QString m_orig;
};

namespace EvUi {
QLabel* rich(const QString& html);
QLabel* muted(const QString& text);
QLabel* heading(const QString& text);
QToolButton* chip(const QString& text, const QColor& border = QColor(), const QString& tip = QString());
QWidget* banner(const QString& html, bool bad);
QString esc(const QString& s);
QString q(const std::string& s);
}  // namespace EvUi

// The floor map: chapters as rows, stairs as arrows; locked stairs gold and dashed with the event name.
class FloorMapView : public QWidget {
    Q_OBJECT
public:
    struct Node { QString id, act; bool boss = false, warn = false, ghost = false, isNew = false; QRectF r; };
    struct Edge { QString from, to, lock; bool miss = false; };
    void setGraph(std::vector<Node> nodes, std::vector<Edge> edges, std::vector<std::pair<QString, double>> rows, const QString& sel);
    QSize sizeHint() const override { return m_size; }
signals:
    void clicked(const QString& id);
protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
private:
    std::vector<Node> m_nodes;
    std::vector<Edge> m_edges;
    std::vector<std::pair<QString, double>> m_rows;
    QString m_sel;
    QSize m_size{400, 200};
};
