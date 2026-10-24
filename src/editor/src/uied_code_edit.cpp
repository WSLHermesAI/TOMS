// uied_code_edit.cpp -- see uied_code_edit.h.
#include "uied_code_edit.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QRegularExpression>
#include <QTextBlock>

namespace {

QTextCharFormat fmt(const QColor& c, bool bold = false, bool italic = false) {
    QTextCharFormat f;
    f.setForeground(c);
    if (bold) f.setFontWeight(QFont::Bold);
    f.setFontItalic(italic);
    return f;
}

class LineNumbers : public QWidget {
public:
    explicit LineNumbers(UiCodeEdit* edit) : QWidget(edit), edit_(edit) {}
    QSize sizeHint() const override { return QSize(edit_->lineNumberWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* e) override { edit_->paintLineNumbers(e); }

private:
    UiCodeEdit* edit_;
};

}  // namespace

UiHighlighter::UiHighlighter(QTextDocument* doc, Kind kind) : QSyntaxHighlighter(doc), kind_(kind) {
    const QColor tag(0x4f, 0x9d, 0xde), attr(0xc5, 0x86, 0xc0), str(0xce, 0x91, 0x78), bind(0xd7, 0xba, 0x7d),
        sel(0xd7, 0xba, 0x7d), prop(0x4f, 0xc1, 0xff), num(0xb5, 0xce, 0xa8), at(0xc5, 0x86, 0xc0);
    comment_ = fmt(QColor(0x6a, 0x99, 0x55), false, true);
    if (kind == Kind::Rml) {
        rules_.push_back({QRegularExpression(QStringLiteral(R"(</?([\w-]+))")), fmt(tag, true), 0});
        rules_.push_back({QRegularExpression(QStringLiteral(R"(\b([\w-]+)\s*=)")), fmt(attr), 1});
        rules_.push_back({QRegularExpression(QStringLiteral(R"("[^"]*")")), fmt(str), 0});
        rules_.push_back({QRegularExpression(QStringLiteral(R"(\{\{[^}]*\}\})")), fmt(bind, true), 0});
    } else {
        rules_.push_back({QRegularExpression(QStringLiteral(R"(^[^{}:;]*(?=\{))")), fmt(sel, true), 0});
        rules_.push_back({QRegularExpression(QStringLiteral(R"(\b([\w-]+)\s*:(?![^{]*\{))")), fmt(prop), 1});
        rules_.push_back({QRegularExpression(QStringLiteral(R"(-?\b\d+(\.\d+)?(px|dp|%|s|deg|em)?\b)")), fmt(num), 0});
        rules_.push_back({QRegularExpression(QStringLiteral(R"(@[\w-]+)")), fmt(at, true), 0});
        rules_.push_back({QRegularExpression(QStringLiteral(R"("[^"]*")")), fmt(str), 0});
    }
}

void UiHighlighter::highlightBlock(const QString& text) {
    for (const Rule& r : rules_)
        for (auto it = r.re.globalMatch(text); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            setFormat(m.capturedStart(r.group), m.capturedLength(r.group), r.fmt);
        }
    if (kind_ == Kind::Rcss) {   // a colour shows itself: #rrggbb(aa) on its own colour
        static const QRegularExpression hex(QStringLiteral(R"(#([0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b)"));
        for (auto it = hex.globalMatch(text); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            QString h = m.captured(1);
            if (h.size() == 3) h = QString(h[0]) + h[0] + h[1] + h[1] + h[2] + h[2];
            const QColor c(QStringLiteral("#") + h.left(6));
            QTextCharFormat f;
            f.setBackground(c);
            f.setForeground(c.lightnessF() > 0.55 ? Qt::black : Qt::white);
            setFormat(m.capturedStart(), m.capturedLength(), f);
        }
    }
    // comments last: /* */ in .rcss, <!-- --> in .rml, possibly over several lines
    const QString open = kind_ == Kind::Rcss ? QStringLiteral("/*") : QStringLiteral("<!--");
    const QString close = kind_ == Kind::Rcss ? QStringLiteral("*/") : QStringLiteral("-->");
    setCurrentBlockState(0);
    int start = previousBlockState() == 1 ? 0 : text.indexOf(open);
    while (start >= 0) {
        const int end = text.indexOf(close, start + (previousBlockState() == 1 && start == 0 ? 0 : open.size()));
        int len;
        if (end < 0) {
            setCurrentBlockState(1);
            len = text.size() - start;
        } else {
            len = end - start + close.size();
        }
        setFormat(start, len, comment_);
        start = end < 0 ? -1 : text.indexOf(open, start + len);
    }
}

UiCodeEdit::UiCodeEdit(QWidget* parent) : QPlainTextEdit(parent), numbers_(new LineNumbers(this)) {
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(10.5);
    setFont(mono);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')) * 4);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateMargins(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& r, int dy) {
        if (dy) numbers_->scroll(0, dy);
        else numbers_->update(0, r.y(), numbers_->width(), r.height());
    });
    updateMargins();
}

int UiCodeEdit::lineNumberWidth() const {
    int digits = 1;
    for (int n = qMax(1, blockCount()); n >= 10; n /= 10) digits++;
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void UiCodeEdit::updateMargins() { setViewportMargins(lineNumberWidth(), 0, 0, 0); }

void UiCodeEdit::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    numbers_->setGeometry(QRect(cr.left(), cr.top(), lineNumberWidth(), cr.height()));
}

void UiCodeEdit::paintLineNumbers(QPaintEvent* e) {
    QPainter p(numbers_);
    p.fillRect(e->rect(), palette().alternateBase());
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber();
    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            p.setPen(number == current ? palette().text().color() : palette().placeholderText().color());
            p.drawText(0, top, numbers_->width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        number++;
    }
}

void UiCodeEdit::goToLine(int line) {
    const QTextBlock b = document()->findBlockByNumber(qMax(0, line - 1));
    if (!b.isValid()) return;
    QTextCursor c(b);   // the line selected, the cursor at its start: the view stays at the left edge
    c.movePosition(QTextCursor::EndOfBlock);
    c.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    setTextCursor(c);
    centerCursor();
    horizontalScrollBar()->setValue(0);
    setFocus();
}

void UiCodeEdit::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Tab && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        insertPlainText(QStringLiteral("    "));
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {   // the next line starts where this one did
        const QString line = textCursor().block().text();
        int n = 0;
        while (n < line.size() && (line[n] == QLatin1Char(' ') || line[n] == QLatin1Char('\t'))) n++;
        QPlainTextEdit::keyPressEvent(e);
        insertPlainText(line.left(n));
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}
