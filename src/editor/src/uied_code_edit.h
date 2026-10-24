// uied_code_edit.h -- the UI editor's text box: line numbers, colours for .rml and .rcss, a jump to a line.
#pragma once

#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <vector>

// Colours for RmlUi's two languages: tags, attributes, strings, comments and data bindings ({{...}}) in .rml;
// selectors, properties, values and colours (#rrggbb) in .rcss. Readable on the dark and the light theme.
class UiHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    enum class Kind { Rml, Rcss };
    UiHighlighter(QTextDocument* doc, Kind kind);
    Kind kind() const { return kind_; }

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule { QRegularExpression re; QTextCharFormat fmt; int group = 0; };
    Kind kind_;
    std::vector<Rule> rules_;
    QTextCharFormat comment_;
};

class UiCodeEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit UiCodeEdit(QWidget* parent = nullptr);
    void goToLine(int line);   // 1-based; selects the line and scrolls it to the middle
    int lineNumberWidth() const;
    void paintLineNumbers(QPaintEvent* e);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;   // Tab inserts four spaces; Enter keeps the indentation

private:
    void updateMargins();
    QWidget* numbers_;
};
