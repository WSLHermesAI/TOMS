// uied_code_edit.cpp -- see uied_code_edit.h.
#include "uied_code_edit.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QRegularExpression>
#include <QTextBlock>

namespace {

QTextCharFormat f_Fmt(const QColor& l_oC, bool l_bBold = false, bool l_bItalic = false) {
    QTextCharFormat l_oF;
    l_oF.setForeground(l_oC);
    if (l_bBold) l_oF.setFontWeight(QFont::Bold);
    l_oF.setFontItalic(l_bItalic);
    return l_oF;
}

class LineNumbers : public QWidget {
public:
    explicit LineNumbers(UiCodeEdit* l_pEdit) : QWidget(l_pEdit), m_pEdit(l_pEdit) {}
    QSize sizeHint() const override { return QSize(m_pEdit->f_LineNumberWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* l_pE) override { m_pEdit->f_PaintLineNumbers(l_pE); }

private:
    UiCodeEdit* m_pEdit;
};

}  // namespace

UiHighlighter::UiHighlighter(QTextDocument* l_pDoc, Kind l_eKind) : QSyntaxHighlighter(l_pDoc), m_eKind(l_eKind) {
    const QColor l_oTag(0x4f, 0x9d, 0xde), l_oAttr(0xc5, 0x86, 0xc0), l_oStr(0xce, 0x91, 0x78), l_oBind(0xd7, 0xba, 0x7d),
        l_oSel(0xd7, 0xba, 0x7d), l_oProp(0x4f, 0xc1, 0xff), l_oNum(0xb5, 0xce, 0xa8), l_oAt(0xc5, 0x86, 0xc0);
    m_oComment = f_Fmt(QColor(0x6a, 0x99, 0x55), false, true);
    if (l_eKind == Kind::Rml) {
        m_vRules.push_back({QRegularExpression(QStringLiteral(R"(</?([\w-]+))")), f_Fmt(l_oTag, true), 0});
        m_vRules.push_back({QRegularExpression(QStringLiteral(R"(\b([\w-]+)\s*=)")), f_Fmt(l_oAttr), 1});
        m_vRules.push_back({QRegularExpression(QStringLiteral(R"("[^"]*")")), f_Fmt(l_oStr), 0});
        m_vRules.push_back({QRegularExpression(QStringLiteral(R"(\{\{[^}]*\}\})")), f_Fmt(l_oBind, true), 0});
    } else {
        m_vRules.push_back({QRegularExpression(QStringLiteral(R"(^[^{}:;]*(?=\{))")), f_Fmt(l_oSel, true), 0});
        m_vRules.push_back({QRegularExpression(QStringLiteral(R"(\b([\w-]+)\s*:(?![^{]*\{))")), f_Fmt(l_oProp), 1});
        m_vRules.push_back({QRegularExpression(QStringLiteral(R"(-?\b\d+(\.\d+)?(px|dp|%|s|deg|em)?\b)")), f_Fmt(l_oNum), 0});
        m_vRules.push_back({QRegularExpression(QStringLiteral(R"(@[\w-]+)")), f_Fmt(l_oAt, true), 0});
        m_vRules.push_back({QRegularExpression(QStringLiteral(R"("[^"]*")")), f_Fmt(l_oStr), 0});
    }
}

void UiHighlighter::highlightBlock(const QString& l_oText) {
    for (const Rule& l_oR : m_vRules)
        for (auto l_oIt = l_oR.m_oRe.globalMatch(l_oText); l_oIt.hasNext();) {
            const QRegularExpressionMatch l_oM = l_oIt.next();
            setFormat(l_oM.capturedStart(l_oR.m_iGroup), l_oM.capturedLength(l_oR.m_iGroup), l_oR.m_oFmt);
        }
    if (m_eKind == Kind::Rcss) {   // a colour shows itself: #rrggbb(aa) on its own colour
        static const QRegularExpression l_oHex(QStringLiteral(R"(#([0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b)"));
        for (auto l_oIt = l_oHex.globalMatch(l_oText); l_oIt.hasNext();) {
            const QRegularExpressionMatch l_oM = l_oIt.next();
            QString l_oH = l_oM.captured(1);
            if (l_oH.size() == 3) l_oH = QString(l_oH[0]) + l_oH[0] + l_oH[1] + l_oH[1] + l_oH[2] + l_oH[2];
            const QColor l_oC(QStringLiteral("#") + l_oH.left(6));
            QTextCharFormat l_oF;
            l_oF.setBackground(l_oC);
            l_oF.setForeground(l_oC.lightnessF() > 0.55 ? Qt::black : Qt::white);
            setFormat(l_oM.capturedStart(), l_oM.capturedLength(), l_oF);
        }
    }
    // comments last: /* */ in .rcss, <!-- --> in .rml, possibly over several lines
    const QString l_oOpen = m_eKind == Kind::Rcss ? QStringLiteral("/*") : QStringLiteral("<!--");
    const QString l_oClose = m_eKind == Kind::Rcss ? QStringLiteral("*/") : QStringLiteral("-->");
    setCurrentBlockState(0);
    int l_iStart = previousBlockState() == 1 ? 0 : l_oText.indexOf(l_oOpen);
    while (l_iStart >= 0) {
        const int l_iEnd = l_oText.indexOf(l_oClose, l_iStart + (previousBlockState() == 1 && l_iStart == 0 ? 0 : l_oOpen.size()));
        int l_iLen;
        if (l_iEnd < 0) {
            setCurrentBlockState(1);
            l_iLen = l_oText.size() - l_iStart;
        } else {
            l_iLen = l_iEnd - l_iStart + l_oClose.size();
        }
        setFormat(l_iStart, l_iLen, m_oComment);
        l_iStart = l_iEnd < 0 ? -1 : l_oText.indexOf(l_oOpen, l_iStart + l_iLen);
    }
}

UiCodeEdit::UiCodeEdit(QWidget* l_pParent) : QPlainTextEdit(l_pParent), m_pNumbers(new LineNumbers(this)) {
    QFont l_oMono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    l_oMono.setPointSizeF(10.5);
    setFont(l_oMono);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(QFontMetricsF(l_oMono).horizontalAdvance(QLatin1Char(' ')) * 4);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { f_UpdateMargins(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& l_oR, int l_iDy) {
        if (l_iDy) m_pNumbers->scroll(0, l_iDy);
        else m_pNumbers->update(0, l_oR.y(), m_pNumbers->width(), l_oR.height());
    });
    f_UpdateMargins();
}

int UiCodeEdit::f_LineNumberWidth() const {
    int l_iDigits = 1;
    for (int l_iN = qMax(1, blockCount()); l_iN >= 10; l_iN /= 10) l_iDigits++;
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, l_iDigits);
}

void UiCodeEdit::f_UpdateMargins() { setViewportMargins(f_LineNumberWidth(), 0, 0, 0); }

void UiCodeEdit::resizeEvent(QResizeEvent* l_pE) {
    QPlainTextEdit::resizeEvent(l_pE);
    const QRect l_oCr = contentsRect();
    m_pNumbers->setGeometry(QRect(l_oCr.left(), l_oCr.top(), f_LineNumberWidth(), l_oCr.height()));
}

void UiCodeEdit::f_PaintLineNumbers(QPaintEvent* l_pE) {
    QPainter l_oP(m_pNumbers);
    l_oP.fillRect(l_pE->rect(), palette().alternateBase());
    QTextBlock l_oBlock = firstVisibleBlock();
    int l_iNumber = l_oBlock.blockNumber();
    int l_iTop = qRound(blockBoundingGeometry(l_oBlock).translated(contentOffset()).top());
    int l_iBottom = l_iTop + qRound(blockBoundingRect(l_oBlock).height());
    const int l_iCurrent = textCursor().blockNumber();
    while (l_oBlock.isValid() && l_iTop <= l_pE->rect().bottom()) {
        if (l_oBlock.isVisible() && l_iBottom >= l_pE->rect().top()) {
            l_oP.setPen(l_iNumber == l_iCurrent ? palette().text().color() : palette().placeholderText().color());
            l_oP.drawText(0, l_iTop, m_pNumbers->width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number(l_iNumber + 1));
        }
        l_oBlock = l_oBlock.next();
        l_iTop = l_iBottom;
        l_iBottom = l_iTop + qRound(blockBoundingRect(l_oBlock).height());
        l_iNumber++;
    }
}

void UiCodeEdit::f_GoToLine(int l_iLine) {
    const QTextBlock l_oB = document()->findBlockByNumber(qMax(0, l_iLine - 1));
    if (!l_oB.isValid()) return;
    QTextCursor l_oC(l_oB);   // the line selected, the cursor at its start: the view stays at the left edge
    l_oC.movePosition(QTextCursor::EndOfBlock);
    l_oC.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    setTextCursor(l_oC);
    centerCursor();
    horizontalScrollBar()->setValue(0);
    setFocus();
}

void UiCodeEdit::keyPressEvent(QKeyEvent* l_pE) {
    if (l_pE->key() == Qt::Key_Tab && !(l_pE->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        insertPlainText(QStringLiteral("    "));
        return;
    }
    if (l_pE->key() == Qt::Key_Return || l_pE->key() == Qt::Key_Enter) {   // the next line starts where this one did
        const QString l_oLine = textCursor().block().text();
        int l_iN = 0;
        while (l_iN < l_oLine.size() && (l_oLine[l_iN] == QLatin1Char(' ') || l_oLine[l_iN] == QLatin1Char('\t'))) l_iN++;
        QPlainTextEdit::keyPressEvent(l_pE);
        insertPlainText(l_oLine.left(l_iN));
        return;
    }
    QPlainTextEdit::keyPressEvent(l_pE);
}
