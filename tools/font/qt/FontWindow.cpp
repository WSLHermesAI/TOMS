#include "FontWindow.h"
#include "PageView.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <set>

namespace {

enum Column { ColPicture, ColChar, ColCode, ColSize, ColSource, ColCount };

QImage toQImage(const atlas::Image& img) {
    if (!img.valid()) return QImage();
    return QImage(img.px.data(), img.w, img.h, img.w * 4, QImage::Format_RGBA8888).copy();
}

std::string str(const QString& s) { return s.toUtf8().toStdString(); }
QString qstr(const std::string& s) { return QString::fromUtf8(s.data(), (int)s.size()); }

// What the Character cell shows for a code point.
QString charText(uint32_t cp) {
    if (!cp) return QString();
    if (cp == ' ') return QStringLiteral("space");
    return qstr(fonttool::utf8Encode(cp));
}

QPixmap thumbnail(const atlas::Image& img, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    const QImage q = toQImage(img);
    if (!q.isNull()) {
        QPainter p(&pm);
        const QImage s = q.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage((size - s.width()) / 2, (size - s.height()) / 2, s);
    }
    return pm;
}

}  // namespace

FontWindow::FontWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("TOMS Font Tool"));
    resize(1180, 760);
    setAcceptDrops(true);

    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"), this, &FontWindow::clearPictures)->setShortcut(QKeySequence::New);
    file->addAction(tr("&Add pictures..."), this, &FontWindow::browseAdd)->setShortcut(QKeySequence(tr("Ctrl+O")));
    file->addAction(tr("Open &font (.fnt)..."), this, &FontWindow::browseOpen);
    file->addAction(tr("&Save font as..."), this, &FontWindow::browseSave)->setShortcut(QKeySequence::Save);
    file->addSeparator();
    file->addAction(tr("E&xit"), this, &QWidget::close);

    // left: the pictures and their characters, the font options
    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(new QLabel(tr("Pictures (one per character). Type each picture's character in the "
                                        "Character column:\none character, a name (space, dot, comma) or U+XXXX.")));
    auto* buttons = new QHBoxLayout;
    auto* addBtn = new QPushButton(tr("Add pictures..."));
    auto* removeBtn = new QPushButton(tr("Remove"));
    auto* upBtn = new QPushButton(tr("Up"));
    auto* downBtn = new QPushButton(tr("Down"));
    buttons->addWidget(addBtn);
    buttons->addWidget(removeBtn);
    buttons->addWidget(upBtn);
    buttons->addWidget(downBtn);
    buttons->addStretch(1);
    leftLayout->addLayout(buttons);

    table_ = new QTableWidget(0, ColCount);
    table_->setHorizontalHeaderLabels({tr("Picture"), tr("Character"), tr("Code"), tr("Size"), tr("Source")});
    table_->verticalHeader()->setDefaultSectionSize(52);
    table_->setIconSize(QSize(48, 48));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setColumnWidth(ColPicture, 64);
    table_->setColumnWidth(ColChar, 80);
    table_->setColumnWidth(ColCode, 70);
    table_->setColumnWidth(ColSize, 70);
    table_->horizontalHeader()->setStretchLastSection(true);
    leftLayout->addWidget(table_, 1);
    problemLabel_ = new QLabel;
    problemLabel_->setWordWrap(true);
    problemLabel_->setStyleSheet(QStringLiteral("color: #ff7070;"));
    leftLayout->addWidget(problemLabel_);

    auto* form = new QFormLayout;
    faceEdit_ = new QLineEdit;
    faceEdit_->setPlaceholderText(tr("empty: the .fnt file name"));
    faceEdit_->setToolTip(tr("The font's name, written into the .fnt (info face=\"...\"). A label only: the game and "
                             "the tools do not read it."));
    layoutCombo_ = new QComboBox;
    layoutCombo_->addItem(tr("One row (as convert2fnt-web)"), (int)fonttool::PageLayout::Row);
    layoutCombo_->addItem(tr("Tight (atlas packer)"), (int)fonttool::PageLayout::Tight);
    layoutCombo_->addItem(tr("Tight, power of 2 (atlas packer)"), (int)fonttool::PageLayout::TightPow2);
    layoutCombo_->setToolTip(tr("How the pictures are combined into the page. Tight uses the atlas editor's packer "
                                "for the smallest page; power of 2 makes its width and height 64, 128, 256, ..."));
    spaceSpin_ = new QSpinBox;
    spaceSpin_->setRange(-1, 4096);
    spaceSpin_->setSpecialValueText(tr("auto"));
    spaceSpin_->setValue(-1);
    spaceSpin_->setSuffix(tr(" px"));
    spaceSpin_->setToolTip(tr("How far a space moves the next character. Auto: a space picture's width if there is "
                              "one, else the width of the 1 picture, else 5. Lowest value = auto."));
    lineSpin_ = new QSpinBox;
    lineSpin_->setRange(-1, 4096);
    lineSpin_->setSpecialValueText(tr("auto"));
    lineSpin_->setValue(-1);
    lineSpin_->setSuffix(tr(" px"));
    lineSpin_->setToolTip(tr("How far a new line ('\\n') moves down. Auto: the tallest picture's height. Lowest "
                             "value = auto."));
    padSpin_ = new QSpinBox;
    padSpin_->setRange(-256, 256);
    maxWidthSpin_ = new QSpinBox;
    maxWidthSpin_->setRange(64, 16384);
    maxWidthSpin_->setValue(4096);
    maxWidthSpin_->setToolTip(tr("One row: the row wraps past this width. Tight: the largest width and height."));
    form->addRow(tr("Font name (label only)"), faceEdit_);
    form->addRow(tr("Page layout"), layoutCombo_);
    form->addRow(tr("Page max size"), maxWidthSpin_);
    form->addRow(tr("Space width"), spaceSpin_);
    form->addRow(tr("Line height"), lineSpin_);
    form->addRow(tr("Letter spacing"), padSpin_);
    leftLayout->addLayout(form);
    auto* save = new QPushButton(tr("Save font as..."));
    leftLayout->addWidget(save);

    // right, top: the test text and its preview
    auto* textPane = new QWidget;
    auto* rightLayout = new QVBoxLayout(textPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel(tr("Test text (Enter starts a new line):")));
    textEdit_ = new QPlainTextEdit(QStringLiteral("0123456789\n1,234.5K  9,999M"));
    textEdit_->setMaximumHeight(80);
    rightLayout->addWidget(textEdit_);
    auto* viewRow = new QHBoxLayout;
    scaleSpin_ = new QDoubleSpinBox;
    scaleSpin_->setRange(0.1, 8.0);
    scaleSpin_->setSingleStep(0.25);
    scaleSpin_->setValue(1.0);
    bgCombo_ = new QComboBox;
    bgCombo_->addItems({tr("Checker"), tr("Game background"), tr("Black"), tr("White")});
    viewRow->addWidget(new QLabel(tr("Scale")));
    viewRow->addWidget(scaleSpin_);
    viewRow->addWidget(new QLabel(tr("Background")));
    viewRow->addWidget(bgCombo_);
    viewRow->addStretch(1);
    rightLayout->addLayout(viewRow);
    previewLabel_ = new QLabel;
    previewLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto* scroll = new QScrollArea;
    scroll->setWidget(previewLabel_);
    scroll->setWidgetResizable(true);
    rightLayout->addWidget(scroll, 1);
    infoLabel_ = new QLabel;
    rightLayout->addWidget(infoLabel_);

    // right, middle: the combined picture (the page as it will be saved)
    auto* pagePane = new QWidget;
    auto* pageLayout = new QVBoxLayout(pagePane);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLabel_ = new QLabel;
    pageLayout->addWidget(pageLabel_);
    pageView_ = new PageView;
    pageLayout->addWidget(pageView_, 1);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);

    auto* rightSplit = new QSplitter(Qt::Vertical);
    rightSplit->addWidget(textPane);
    rightSplit->addWidget(pagePane);
    rightSplit->addWidget(log_);
    rightSplit->setSizes({330, 300, 90});

    auto* split = new QSplitter;
    split->addWidget(left);
    split->addWidget(rightSplit);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({460, 720});
    setCentralWidget(split);

    connect(addBtn, &QPushButton::clicked, this, &FontWindow::browseAdd);
    connect(removeBtn, &QPushButton::clicked, this, [this] { removePicture(selectedRow()); });
    connect(upBtn, &QPushButton::clicked, this, [this] { movePicture(selectedRow(), -1); });
    connect(downBtn, &QPushButton::clicked, this, [this] { movePicture(selectedRow(), +1); });
    connect(save, &QPushButton::clicked, this, &FontWindow::browseSave);
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!filling_ && item->column() == ColChar) setCharacter(item->row(), item->text());
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, &FontWindow::showSelection);
    connect(pageView_, &PageView::glyphClicked, this, [this](int g) {
        if (g >= 0 && g < (int)glyphRow_.size() && glyphRow_[g] >= 0) selectRow(glyphRow_[g]);
    });
    connect(faceEdit_, &QLineEdit::editingFinished, this, &FontWindow::rebuild);
    connect(layoutCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &FontWindow::rebuild);
    connect(spaceSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &FontWindow::rebuild);
    connect(lineSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &FontWindow::rebuild);
    connect(padSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &FontWindow::rebuild);
    connect(maxWidthSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &FontWindow::rebuild);
    connect(textEdit_, &QPlainTextEdit::textChanged, this, &FontWindow::updatePreview);
    connect(scaleSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &FontWindow::updatePreview);
    connect(bgCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &FontWindow::updatePreview);

    message(tr("Add pictures (File > Add pictures, or drop PNG files here), type each one's character, then "
               "Save font as. Open font (.fnt) loads a finished font to change it."));
    rebuild();
}

QString FontWindow::log() const { return log_->toPlainText(); }
void FontWindow::setTestText(const QString& text) { textEdit_->setPlainText(text); }
QString FontWindow::testText() const { return textEdit_->toPlainText(); }
void FontWindow::setScale(double scale) { scaleSpin_->setValue(scale); }
void FontWindow::message(const QString& line) { log_->appendPlainText(line); }

void FontWindow::setLayout(fonttool::PageLayout layout) {
    const int i = layoutCombo_->findData((int)layout);
    if (i >= 0) layoutCombo_->setCurrentIndex(i);
}

fonttool::BuildOptions FontWindow::options(const QString& face) const {
    fonttool::BuildOptions opt;
    opt.face = str(faceEdit_->text().isEmpty() ? face : faceEdit_->text());
    opt.layout = (fonttool::PageLayout)layoutCombo_->currentData().toInt();
    opt.spaceAdvance = spaceSpin_->value();
    opt.lineHeight = lineSpin_->value() > 0 ? lineSpin_->value() : -1;
    opt.advancePad = padSpin_->value();
    opt.maxWidth = maxWidthSpin_->value();
    return opt;
}

void FontWindow::setSpaceAdvance(int px) { spaceSpin_->setValue(px < 0 ? -1 : px); }
void FontWindow::setLineHeight(int px) { lineSpin_->setValue(px <= 0 ? -1 : px); }

int FontWindow::selectedRow() const {
    const QList<QTableWidgetItem*> sel = table_->selectedItems();
    return sel.isEmpty() ? -1 : sel.first()->row();
}

void FontWindow::selectRow(int row) {
    if (row < 0 || row >= table_->rowCount()) return;
    table_->selectRow(row);
    table_->scrollToItem(table_->item(row, ColPicture));
    showSelection();
}

void FontWindow::showSelection() {
    const int row = selectedRow();
    pageView_->setHighlight(row >= 0 && row < (int)rowGlyph_.size() ? rowGlyph_[row] : -1);
}

void FontWindow::clickPage(const QPoint& pagePos) {
    const int g = pageView_->glyphAt(pagePos);
    if (g >= 0 && g < (int)glyphRow_.size() && glyphRow_[g] >= 0) selectRow(glyphRow_[g]);
}

int FontWindow::highlightedGlyph() const { return pageView_->highlight(); }

void FontWindow::browseAdd() {
    QSettings s;
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add pictures"), s.value("font/lastPictures").toString(),
                                                            tr("PNG pictures (*.png)"));
    if (files.isEmpty()) return;
    s.setValue("font/lastPictures", QFileInfo(files.first()).absolutePath());
    addPictures(files);
}

void FontWindow::browseOpen() {
    QSettings s;
    const QString f = QFileDialog::getOpenFileName(this, tr("Open font"), s.value("font/lastFont").toString(),
                                                   tr("BMFont text (*.fnt)"));
    if (f.isEmpty()) return;
    s.setValue("font/lastFont", f);
    openFont(f);
}

void FontWindow::browseSave() {
    if (pics_.empty()) {
        message(tr("Nothing to save: add pictures first."));
        return;
    }
    QSettings s;
    const QString f = QFileDialog::getSaveFileName(this, tr("Save font"), s.value("font/lastSave").toString(),
                                                   tr("BMFont text (*.fnt)"));
    if (f.isEmpty()) return;
    s.setValue("font/lastSave", f);
    saveFont(f);
}

void FontWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void FontWindow::dropEvent(QDropEvent* e) {
    QStringList pngs;
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString f = u.toLocalFile();
        if (f.endsWith(QLatin1String(".fnt"), Qt::CaseInsensitive)) openFont(f);
        else if (f.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) pngs << f;
    }
    if (!pngs.isEmpty()) addPictures(pngs);
}

int FontWindow::addPictures(const QStringList& files) {
    int added = 0;
    for (const QString& f : files) {
        fonttool::GlyphPicture g;
        std::string err;
        if (!fonttool::loadPicture(str(f), g, err)) {
            message(tr("error: %1: %2").arg(f, qstr(err)));
            continue;
        }
        pics_.push_back(std::move(g));
        added++;
    }
    if (added) message(tr("added %1 picture(s)").arg(added));
    fillTable();
    rebuild();
    return added;
}

void FontWindow::setCharacter(int row, const QString& text) {
    if (row < 0 || row >= (int)pics_.size()) return;
    const QString t = text.trimmed().isEmpty() && !text.isEmpty() ? QStringLiteral(" ") : text.trimmed();
    const uint32_t cp = t.isEmpty() ? 0 : fonttool::charFromName(str(t));
    if (!t.isEmpty() && !cp) message(tr("'%1' is not a character (one character, a name like dot, or U+XXXX)").arg(t));
    pics_[row].cp = cp;
    fillTable();
    rebuild();
}

void FontWindow::removePicture(int row) {
    if (row < 0 || row >= (int)pics_.size()) return;
    pics_.erase(pics_.begin() + row);
    fillTable();
    rebuild();
}

void FontWindow::movePicture(int row, int delta) {
    const int to = row + delta;
    if (row < 0 || row >= (int)pics_.size() || to < 0 || to >= (int)pics_.size()) return;
    std::swap(pics_[row], pics_[to]);
    fillTable();
    table_->selectRow(to);
    rebuild();
}

void FontWindow::clearPictures() {
    pics_.clear();
    faceEdit_->clear();
    setSpaceAdvance(-1);
    setLineHeight(-1);
    fillTable();
    rebuild();
}

bool FontWindow::openFont(const QString& fntPath) {
    std::vector<fonttool::GlyphPicture> glyphs;
    std::string err;
    if (!fonttool::glyphsFromFont(str(fntPath), glyphs, err)) {
        message(tr("error: ") + qstr(err));
        return false;
    }
    pics_ = std::move(glyphs);
    BitmapFont f;
    atlas::Image page;
    if (fonttool::loadFont(str(fntPath), f, page, err)) {
        faceEdit_->setText(qstr(f.face));
        // the space comes back as a blank picture of its width; a line height other than the tallest
        // picture is kept in the field
        setSpaceAdvance(-1);
        setLineHeight(f.lineHeight != fonttool::defaultLineHeight(pics_) ? f.lineHeight : -1);
    }
    message(tr("%1: %2 glyphs opened as pictures").arg(fntPath).arg(pics_.size()));
    fillTable();
    rebuild();
    return true;
}

bool FontWindow::saveFont(const QString& fntPath) {
    if (pics_.empty()) return false;
    const std::vector<std::string> problems = fonttool::checkGlyphs(pics_);
    if (!problems.empty()) {
        message(tr("not saved: %1").arg(qstr(problems[0])));
        return false;
    }
    const fonttool::BuildOptions opt = options(QFileInfo(fntPath).completeBaseName());
    fonttool::BuiltFont out;
    std::string err;
    const QString png = QFileInfo(fntPath).completeBaseName() + ".png";
    if (!fonttool::buildFont(pics_, opt, str(png), out, err) || !fonttool::writeFont(out, str(fntPath), err)) {
        message(tr("error: ") + qstr(err));
        return false;
    }
    message(tr("saved %1 and %2 (%3 glyphs, page %4x%5)").arg(fntPath, png).arg(out.font.glyphs.size())
                .arg(out.page.w).arg(out.page.h));
    return true;
}

void FontWindow::fillTable() {
    filling_ = true;
    table_->setRowCount((int)pics_.size());
    for (int r = 0; r < (int)pics_.size(); r++) {
        const fonttool::GlyphPicture& g = pics_[r];
        auto* pic = new QTableWidgetItem(QIcon(thumbnail(g.img, 48)), QString());
        pic->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto* ch = new QTableWidgetItem(charText(g.cp));
        ch->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        ch->setTextAlignment(Qt::AlignCenter);
        QFont big = ch->font();
        big.setPointSize(big.pointSize() + 4);
        ch->setFont(big);
        auto* code = new QTableWidgetItem(
            g.cp ? QStringLiteral("U+") + QString::number(g.cp, 16).toUpper().rightJustified(4, QLatin1Char('0')) : QString());
        auto* size = new QTableWidgetItem(QStringLiteral("%1x%2").arg(g.img.w).arg(g.img.h));
        auto* src = new QTableWidgetItem(qstr(g.file));
        for (QTableWidgetItem* it : {code, size, src}) it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table_->setItem(r, ColPicture, pic);
        table_->setItem(r, ColChar, ch);
        table_->setItem(r, ColCode, code);
        table_->setItem(r, ColSize, size);
        table_->setItem(r, ColSource, src);
    }
    filling_ = false;
}

void FontWindow::rebuild() {
    // mark the rows that cannot build: no character, or one an earlier row already has
    problems_ = fonttool::checkGlyphs(pics_);
    std::vector<fonttool::GlyphPicture> usable;
    std::set<uint32_t> seen;
    rowGlyph_.assign(pics_.size(), -1);
    glyphRow_.clear();
    filling_ = true;
    for (int r = 0; r < (int)pics_.size(); r++) {
        const bool ok = pics_[r].cp && seen.insert(pics_[r].cp).second;
        if (ok) {
            rowGlyph_[r] = (int)usable.size();   // buildFont keeps the order (and adds a space last)
            glyphRow_.push_back(r);
            usable.push_back(pics_[r]);
        }
        if (QTableWidgetItem* it = table_->item(r, ColChar))
            it->setBackground(ok ? QBrush() : QBrush(QColor(140, 40, 40)));
    }
    filling_ = false;
    QString problemText;
    for (size_t i = 0; i < problems_.size() && i < 4; i++) problemText += qstr(problems_[i]) + "\n";
    if (problems_.size() > 4) problemText += tr("... %1 more").arg(problems_.size() - 4);
    if (!problems_.empty()) problemText = tr("Not saved until fixed (red rows):\n") + problemText;
    problemLabel_->setText(problemText.trimmed());
    // the auto values these pictures give, shown in the fields
    spaceSpin_->setSpecialValueText(usable.empty() ? tr("auto") : tr("auto (%1 px)").arg(fonttool::defaultSpaceAdvance(usable)));
    lineSpin_->setSpecialValueText(usable.empty() ? tr("auto") : tr("auto (%1 px, the tallest)").arg(fonttool::defaultLineHeight(usable)));

    font_ = BitmapFont();
    page_ = atlas::Image();
    if (!usable.empty()) {
        fonttool::BuiltFont built;
        std::string err;
        if (fonttool::buildFont(usable, options(QStringLiteral("font")), "font.png", built, err)) {
            font_ = built.font;
            page_ = built.page;
        } else {
            message(tr("error: ") + qstr(err));
        }
    }
    while (glyphRow_.size() < font_.glyphs.size()) glyphRow_.push_back(-1);   // the space added for you

    // the combined picture, every glyph's rect on it
    QVector<QRect> rects;
    for (const BitmapGlyph& g : font_.glyphs) rects.push_back(QRect(g.x, g.y, g.w, g.h));
    pageView_->setPage(toQImage(page_), rects);
    if (page_.valid()) {
        const bool pot = (page_.w & (page_.w - 1)) == 0 && (page_.h & (page_.h - 1)) == 0;
        pageLabel_->setText(tr("Combined picture: %1 x %2%3. Select a row to find its glyph; click a glyph to select "
                               "its row.").arg(page_.w).arg(page_.h).arg(pot ? tr(" (power of 2)") : QString()));
    } else {
        pageLabel_->setText(tr("Combined picture"));
    }
    showSelection();
    updatePreview();
}

void FontWindow::updatePreview() {
    if (font_.glyphs.empty()) {
        preview_ = QImage();
        previewLabel_->clear();
        infoLabel_->setText(pics_.empty() ? tr("No pictures yet.") : tr("No picture has a character yet."));
        return;
    }
    const std::string text = str(textEdit_->toPlainText());
    const atlas::Image img = fonttool::renderText(font_, page_, text, (float)scaleSpin_->value(), 8, nullptr, &missing_);
    const QImage textImg = toQImage(img);
    const int w = textImg.width(), h = textImg.height();
    preview_ = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&preview_);
    switch (bgCombo_->currentIndex()) {
    case 0:   // checker
        for (int y = 0; y < h; y += 8)
            for (int x = 0; x < w; x += 8)
                p.fillRect(x, y, 8, 8, ((x / 8 + y / 8) & 1) ? QColor(90, 90, 90) : QColor(60, 60, 60));
        break;
    case 1: p.fillRect(0, 0, w, h, QColor::fromRgbF(kBackgroundClearColor[0], kBackgroundClearColor[1], kBackgroundClearColor[2])); break;
    case 2: p.fillRect(0, 0, w, h, Qt::black); break;
    default: p.fillRect(0, 0, w, h, Qt::white); break;
    }
    p.drawImage(0, 0, textImg);
    p.end();
    previewLabel_->setPixmap(QPixmap::fromImage(preview_));

    float tw = 0, th = 0;
    font_.measure(text, (float)scaleSpin_->value(), tw, th);
    QString info = tr("%1 glyphs, line height %2, page %3x%4   |   text %5x%6 px")
                       .arg(font_.glyphs.size()).arg(font_.lineHeight).arg(page_.w).arg(page_.h)
                       .arg((int)std::ceil(tw)).arg((int)std::ceil(th));
    if (missing_) info += tr("   |   %1 character(s) not in the font (skipped)").arg(missing_);
    infoLabel_->setText(info);
}
