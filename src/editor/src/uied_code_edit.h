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
    UiHighlighter(QTextDocument* l_pDoc, Kind l_eKind);
    Kind f_Kind() const { return m_eKind; }

protected:
    void highlightBlock(const QString& l_oText) override;

private:
    struct Rule { QRegularExpression m_oRe; QTextCharFormat m_oFmt; int m_iGroup = 0; };
    Kind m_eKind;
    std::vector<Rule> m_vRules;
    QTextCharFormat m_oComment;
};

class UiCodeEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit UiCodeEdit(QWidget* l_pParent = nullptr);
    void f_GoToLine(int l_iLine);   // 1-based; selects the line and scrolls it to the middle
    int f_LineNumberWidth() const;
    void f_PaintLineNumbers(QPaintEvent* l_pE);

protected:
    void resizeEvent(QResizeEvent* l_pE) override;
    void keyPressEvent(QKeyEvent* l_pE) override;   // Tab inserts four spaces; Enter keeps the indentation

private:
    void f_UpdateMargins();
    QWidget* m_pNumbers;
};
