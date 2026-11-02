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

QImage f_ToQImage(const atlas::Image& l_oImg) {
    if (!l_oImg.f_Valid()) return QImage();
    return QImage(l_oImg.m_vPx.data(), l_oImg.m_iW, l_oImg.m_iH, l_oImg.m_iW * 4, QImage::Format_RGBA8888).copy();
}

std::string f_Str(const QString& l_oS) { return l_oS.toUtf8().toStdString(); }
QString f_Qstr(const std::string& l_strS) { return QString::fromUtf8(l_strS.data(), (int)l_strS.size()); }

// What the Character cell shows for a code point.
QString f_CharText(uint32_t l_uCp) {
    if (!l_uCp) return QString();
    if (l_uCp == ' ') return QStringLiteral("space");
    return f_Qstr(fonttool::f_Utf8Encode(l_uCp));
}

QPixmap f_Thumbnail(const atlas::Image& l_oImg, int l_iSize) {
    QPixmap l_oPm(l_iSize, l_iSize);
    l_oPm.fill(Qt::transparent);
    const QImage l_oQ = f_ToQImage(l_oImg);
    if (!l_oQ.isNull()) {
        QPainter l_oP(&l_oPm);
        const QImage l_oS = l_oQ.scaled(l_iSize, l_iSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        l_oP.drawImage((l_iSize - l_oS.width()) / 2, (l_iSize - l_oS.height()) / 2, l_oS);
    }
    return l_oPm;
}

}  // namespace

FontWindow::FontWindow(QWidget* l_pParent) : QMainWindow(l_pParent) {
    setWindowTitle(tr("TOMS Font Tool"));
    resize(1180, 760);
    setAcceptDrops(true);

    QMenu* l_pFile = menuBar()->addMenu(tr("&File"));
    l_pFile->addAction(tr("&New"), this, &FontWindow::f_ClearPictures)->setShortcut(QKeySequence::New);
    l_pFile->addAction(tr("&Add pictures..."), this, &FontWindow::f_BrowseAdd)->setShortcut(QKeySequence(tr("Ctrl+O")));
    l_pFile->addAction(tr("Open &font (.fnt)..."), this, &FontWindow::f_BrowseOpen);
    l_pFile->addAction(tr("&Save font as..."), this, &FontWindow::f_BrowseSave)->setShortcut(QKeySequence::Save);
    l_pFile->addSeparator();
    l_pFile->addAction(tr("E&xit"), this, &QWidget::close);

    // left: the pictures and their characters, the font options
    auto* l_pLeft = new QWidget;
    auto* l_pLeftLayout = new QVBoxLayout(l_pLeft);
    l_pLeftLayout->addWidget(new QLabel(tr("Pictures (one per character). Type each picture's character in the "
                                        "Character column:\none character, a name (space, dot, comma) or U+XXXX.")));
    auto* l_pButtons = new QHBoxLayout;
    auto* l_pAddBtn = new QPushButton(tr("Add pictures..."));
    auto* l_pRemoveBtn = new QPushButton(tr("Remove"));
    auto* l_pUpBtn = new QPushButton(tr("Up"));
    auto* l_pDownBtn = new QPushButton(tr("Down"));
    l_pButtons->addWidget(l_pAddBtn);
    l_pButtons->addWidget(l_pRemoveBtn);
    l_pButtons->addWidget(l_pUpBtn);
    l_pButtons->addWidget(l_pDownBtn);
    l_pButtons->addStretch(1);
    l_pLeftLayout->addLayout(l_pButtons);

    m_pTable = new QTableWidget(0, ColCount);
    m_pTable->setHorizontalHeaderLabels({tr("Picture"), tr("Character"), tr("Code"), tr("Size"), tr("Source")});
    m_pTable->verticalHeader()->setDefaultSectionSize(52);
    m_pTable->setIconSize(QSize(48, 48));
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pTable->setColumnWidth(ColPicture, 64);
    m_pTable->setColumnWidth(ColChar, 80);
    m_pTable->setColumnWidth(ColCode, 70);
    m_pTable->setColumnWidth(ColSize, 70);
    m_pTable->horizontalHeader()->setStretchLastSection(true);
    l_pLeftLayout->addWidget(m_pTable, 1);
    m_pProblemLabel = new QLabel;
    m_pProblemLabel->setWordWrap(true);
    m_pProblemLabel->setStyleSheet(QStringLiteral("color: #ff7070;"));
    l_pLeftLayout->addWidget(m_pProblemLabel);

    auto* l_pForm = new QFormLayout;
    m_pFaceEdit = new QLineEdit;
    m_pFaceEdit->setPlaceholderText(tr("empty: the .fnt file name"));
    m_pFaceEdit->setToolTip(tr("The font's name, written into the .fnt (info face=\"...\"). A label only: the game and "
                             "the tools do not read it."));
    m_pLayoutCombo = new QComboBox;
    m_pLayoutCombo->addItem(tr("One row (as convert2fnt-web)"), (int)fonttool::PageLayout::Row);
    m_pLayoutCombo->addItem(tr("Tight (atlas packer)"), (int)fonttool::PageLayout::Tight);
    m_pLayoutCombo->addItem(tr("Tight, power of 2 (atlas packer)"), (int)fonttool::PageLayout::TightPow2);
    m_pLayoutCombo->setToolTip(tr("How the pictures are combined into the page. Tight uses the atlas editor's packer "
                                "for the smallest page; power of 2 makes its width and height 64, 128, 256, ..."));
    m_pSpaceSpin = new QSpinBox;
    m_pSpaceSpin->setRange(-1, 4096);
    m_pSpaceSpin->setSpecialValueText(tr("auto"));
    m_pSpaceSpin->setValue(-1);
    m_pSpaceSpin->setSuffix(tr(" px"));
    m_pSpaceSpin->setToolTip(tr("How far a space moves the next character. Auto: a space picture's width if there is "
                              "one, else the width of the 1 picture, else 5. Lowest value = auto."));
    m_pLineSpin = new QSpinBox;
    m_pLineSpin->setRange(-1, 4096);
    m_pLineSpin->setSpecialValueText(tr("auto"));
    m_pLineSpin->setValue(-1);
    m_pLineSpin->setSuffix(tr(" px"));
    m_pLineSpin->setToolTip(tr("How far a new line ('\\n') moves down. Auto: the tallest picture's height. Lowest "
                             "value = auto."));
    m_pPadSpin = new QSpinBox;
    m_pPadSpin->setRange(-256, 256);
    m_pMaxWidthSpin = new QSpinBox;
    m_pMaxWidthSpin->setRange(64, 16384);
    m_pMaxWidthSpin->setValue(4096);
    m_pMaxWidthSpin->setToolTip(tr("One row: the row wraps past this width. Tight: the largest width and height."));
    l_pForm->addRow(tr("Font name (label only)"), m_pFaceEdit);
    l_pForm->addRow(tr("Page layout"), m_pLayoutCombo);
    l_pForm->addRow(tr("Page max size"), m_pMaxWidthSpin);
    l_pForm->addRow(tr("Space width"), m_pSpaceSpin);
    l_pForm->addRow(tr("Line height"), m_pLineSpin);
    l_pForm->addRow(tr("Letter spacing"), m_pPadSpin);
    l_pLeftLayout->addLayout(l_pForm);
    auto* l_pSave = new QPushButton(tr("Save font as..."));
    l_pLeftLayout->addWidget(l_pSave);

    // right, top: the test text and its preview
    auto* l_pTextPane = new QWidget;
    auto* l_pRightLayout = new QVBoxLayout(l_pTextPane);
    l_pRightLayout->setContentsMargins(0, 0, 0, 0);
    l_pRightLayout->addWidget(new QLabel(tr("Test text (Enter starts a new line):")));
    m_pTextEdit = new QPlainTextEdit(QStringLiteral("0123456789\n1,234.5K  9,999M"));
    m_pTextEdit->setMaximumHeight(80);
    l_pRightLayout->addWidget(m_pTextEdit);
    auto* l_pViewRow = new QHBoxLayout;
    m_pScaleSpin = new QDoubleSpinBox;
    m_pScaleSpin->setRange(0.1, 8.0);
    m_pScaleSpin->setSingleStep(0.25);
    m_pScaleSpin->setValue(1.0);
    m_pBgCombo = new QComboBox;
    m_pBgCombo->addItems({tr("Checker"), tr("Game background"), tr("Black"), tr("White")});
    l_pViewRow->addWidget(new QLabel(tr("Scale")));
    l_pViewRow->addWidget(m_pScaleSpin);
    l_pViewRow->addWidget(new QLabel(tr("Background")));
    l_pViewRow->addWidget(m_pBgCombo);
    l_pViewRow->addStretch(1);
    l_pRightLayout->addLayout(l_pViewRow);
    m_pPreviewLabel = new QLabel;
    m_pPreviewLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto* l_pScroll = new QScrollArea;
    l_pScroll->setWidget(m_pPreviewLabel);
    l_pScroll->setWidgetResizable(true);
    l_pRightLayout->addWidget(l_pScroll, 1);
    m_pInfoLabel = new QLabel;
    l_pRightLayout->addWidget(m_pInfoLabel);

    // right, middle: the combined picture (the page as it will be saved)
    auto* l_pPagePane = new QWidget;
    auto* l_pPageLayout = new QVBoxLayout(l_pPagePane);
    l_pPageLayout->setContentsMargins(0, 0, 0, 0);
    m_pPageLabel = new QLabel;
    l_pPageLayout->addWidget(m_pPageLabel);
    m_pPageView = new PageView;
    l_pPageLayout->addWidget(m_pPageView, 1);

    m_pLog = new QPlainTextEdit;
    m_pLog->setReadOnly(true);

    auto* l_pRightSplit = new QSplitter(Qt::Vertical);
    l_pRightSplit->addWidget(l_pTextPane);
    l_pRightSplit->addWidget(l_pPagePane);
    l_pRightSplit->addWidget(m_pLog);
    l_pRightSplit->setSizes({330, 300, 90});

    auto* l_pSplit = new QSplitter;
    l_pSplit->addWidget(l_pLeft);
    l_pSplit->addWidget(l_pRightSplit);
    l_pSplit->setStretchFactor(0, 0);
    l_pSplit->setStretchFactor(1, 1);
    l_pSplit->setSizes({460, 720});
    setCentralWidget(l_pSplit);

    connect(l_pAddBtn, &QPushButton::clicked, this, &FontWindow::f_BrowseAdd);
    connect(l_pRemoveBtn, &QPushButton::clicked, this, [this] { f_RemovePicture(f_SelectedRow()); });
    connect(l_pUpBtn, &QPushButton::clicked, this, [this] { f_MovePicture(f_SelectedRow(), -1); });
    connect(l_pDownBtn, &QPushButton::clicked, this, [this] { f_MovePicture(f_SelectedRow(), +1); });
    connect(l_pSave, &QPushButton::clicked, this, &FontWindow::f_BrowseSave);
    connect(m_pTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* l_pItem) {
        if (!m_bFilling && l_pItem->column() == ColChar) f_SetCharacter(l_pItem->row(), l_pItem->text());
    });
    connect(m_pTable, &QTableWidget::itemSelectionChanged, this, &FontWindow::f_ShowSelection);
    connect(m_pPageView, &PageView::f_GlyphClicked, this, [this](int l_iG) {
        if (l_iG >= 0 && l_iG < (int)m_vGlyphRow.size() && m_vGlyphRow[l_iG] >= 0) f_SelectRow(m_vGlyphRow[l_iG]);
    });
    connect(m_pFaceEdit, &QLineEdit::editingFinished, this, &FontWindow::f_Rebuild);
    connect(m_pLayoutCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &FontWindow::f_Rebuild);
    connect(m_pSpaceSpin, qOverload<int>(&QSpinBox::valueChanged), this, &FontWindow::f_Rebuild);
    connect(m_pLineSpin, qOverload<int>(&QSpinBox::valueChanged), this, &FontWindow::f_Rebuild);
    connect(m_pPadSpin, qOverload<int>(&QSpinBox::valueChanged), this, &FontWindow::f_Rebuild);
    connect(m_pMaxWidthSpin, qOverload<int>(&QSpinBox::valueChanged), this, &FontWindow::f_Rebuild);
    connect(m_pTextEdit, &QPlainTextEdit::textChanged, this, &FontWindow::f_UpdatePreview);
    connect(m_pScaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &FontWindow::f_UpdatePreview);
    connect(m_pBgCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &FontWindow::f_UpdatePreview);

    f_Message(tr("Add pictures (File > Add pictures, or drop PNG files here), type each one's character, then "
               "Save font as. Open font (.fnt) loads a finished font to change it."));
    f_Rebuild();
}

QString FontWindow::f_Log() const { return m_pLog->toPlainText(); }
void FontWindow::f_SetTestText(const QString& l_oText) { m_pTextEdit->setPlainText(l_oText); }
QString FontWindow::f_TestText() const { return m_pTextEdit->toPlainText(); }
void FontWindow::f_SetScale(double l_dScale) { m_pScaleSpin->setValue(l_dScale); }
void FontWindow::f_Message(const QString& l_oLine) { m_pLog->appendPlainText(l_oLine); }

void FontWindow::f_SetLayout(fonttool::PageLayout l_eLayout) {
    const int l_iI = m_pLayoutCombo->findData((int)l_eLayout);
    if (l_iI >= 0) m_pLayoutCombo->setCurrentIndex(l_iI);
}

fonttool::BuildOptions FontWindow::f_Options(const QString& l_oFace) const {
    fonttool::BuildOptions l_oOpt;
    l_oOpt.m_strFace = f_Str(m_pFaceEdit->text().isEmpty() ? l_oFace : m_pFaceEdit->text());
    l_oOpt.m_eLayout = (fonttool::PageLayout)m_pLayoutCombo->currentData().toInt();
    l_oOpt.m_iSpaceAdvance = m_pSpaceSpin->value();
    l_oOpt.m_iLineHeight = m_pLineSpin->value() > 0 ? m_pLineSpin->value() : -1;
    l_oOpt.m_iAdvancePad = m_pPadSpin->value();
    l_oOpt.m_iMaxWidth = m_pMaxWidthSpin->value();
    return l_oOpt;
}

void FontWindow::f_SetSpaceAdvance(int l_iPx) { m_pSpaceSpin->setValue(l_iPx < 0 ? -1 : l_iPx); }
void FontWindow::f_SetLineHeight(int l_iPx) { m_pLineSpin->setValue(l_iPx <= 0 ? -1 : l_iPx); }

int FontWindow::f_SelectedRow() const {
    const QList<QTableWidgetItem*> l_oSel = m_pTable->selectedItems();
    return l_oSel.isEmpty() ? -1 : l_oSel.first()->row();
}

void FontWindow::f_SelectRow(int l_iRow) {
    if (l_iRow < 0 || l_iRow >= m_pTable->rowCount()) return;
    m_pTable->selectRow(l_iRow);
    m_pTable->scrollToItem(m_pTable->item(l_iRow, ColPicture));
    f_ShowSelection();
}

void FontWindow::f_ShowSelection() {
    const int l_iRow = f_SelectedRow();
    m_pPageView->f_SetHighlight(l_iRow >= 0 && l_iRow < (int)m_vRowGlyph.size() ? m_vRowGlyph[l_iRow] : -1);
}

void FontWindow::f_ClickPage(const QPoint& l_oPagePos) {
    const int l_iG = m_pPageView->f_GlyphAt(l_oPagePos);
    if (l_iG >= 0 && l_iG < (int)m_vGlyphRow.size() && m_vGlyphRow[l_iG] >= 0) f_SelectRow(m_vGlyphRow[l_iG]);
}

int FontWindow::f_HighlightedGlyph() const { return m_pPageView->f_Highlight(); }

void FontWindow::f_BrowseAdd() {
    QSettings l_oS;
    const QStringList l_oFiles = QFileDialog::getOpenFileNames(this, tr("Add pictures"), l_oS.value("font/lastPictures").toString(),
                                                            tr("PNG pictures (*.png)"));
    if (l_oFiles.isEmpty()) return;
    l_oS.setValue("font/lastPictures", QFileInfo(l_oFiles.first()).absolutePath());
    f_AddPictures(l_oFiles);
}

void FontWindow::f_BrowseOpen() {
    QSettings l_oS;
    const QString l_oF = QFileDialog::getOpenFileName(this, tr("Open font"), l_oS.value("font/lastFont").toString(),
                                                   tr("BMFont text (*.fnt)"));
    if (l_oF.isEmpty()) return;
    l_oS.setValue("font/lastFont", l_oF);
    f_OpenFont(l_oF);
}

void FontWindow::f_BrowseSave() {
    if (m_vPics.empty()) {
        f_Message(tr("Nothing to save: add pictures first."));
        return;
    }
    QSettings l_oS;
    const QString l_oF = QFileDialog::getSaveFileName(this, tr("Save font"), l_oS.value("font/lastSave").toString(),
                                                   tr("BMFont text (*.fnt)"));
    if (l_oF.isEmpty()) return;
    l_oS.setValue("font/lastSave", l_oF);
    f_SaveFont(l_oF);
}

void FontWindow::dragEnterEvent(QDragEnterEvent* l_pE) {
    if (l_pE->mimeData()->hasUrls()) l_pE->acceptProposedAction();
}

void FontWindow::dropEvent(QDropEvent* l_pE) {
    QStringList l_oPngs;
    for (const QUrl& l_oU : l_pE->mimeData()->urls()) {
        const QString l_oF = l_oU.toLocalFile();
        if (l_oF.endsWith(QLatin1String(".fnt"), Qt::CaseInsensitive)) f_OpenFont(l_oF);
        else if (l_oF.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) l_oPngs << l_oF;
    }
    if (!l_oPngs.isEmpty()) f_AddPictures(l_oPngs);
}

int FontWindow::f_AddPictures(const QStringList& l_oFiles) {
    int l_iAdded = 0;
    for (const QString& l_oF : l_oFiles) {
        fonttool::GlyphPicture l_oG;
        std::string l_strErr;
        if (!fonttool::f_LoadPicture(f_Str(l_oF), l_oG, l_strErr)) {
            f_Message(tr("error: %1: %2").arg(l_oF, f_Qstr(l_strErr)));
            continue;
        }
        m_vPics.push_back(std::move(l_oG));
        l_iAdded++;
    }
    if (l_iAdded) f_Message(tr("added %1 picture(s)").arg(l_iAdded));
    f_FillTable();
    f_Rebuild();
    return l_iAdded;
}

void FontWindow::f_SetCharacter(int l_iRow, const QString& l_oText) {
    if (l_iRow < 0 || l_iRow >= (int)m_vPics.size()) return;
    const QString l_oT = l_oText.trimmed().isEmpty() && !l_oText.isEmpty() ? QStringLiteral(" ") : l_oText.trimmed();
    const uint32_t l_uCp = l_oT.isEmpty() ? 0 : fonttool::f_CharFromName(f_Str(l_oT));
    if (!l_oT.isEmpty() && !l_uCp) f_Message(tr("'%1' is not a character (one character, a name like dot, or U+XXXX)").arg(l_oT));
    m_vPics[l_iRow].m_uCp = l_uCp;
    f_FillTable();
    f_Rebuild();
}

void FontWindow::f_RemovePicture(int l_iRow) {
    if (l_iRow < 0 || l_iRow >= (int)m_vPics.size()) return;
    m_vPics.erase(m_vPics.begin() + l_iRow);
    f_FillTable();
    f_Rebuild();
}

void FontWindow::f_MovePicture(int l_iRow, int l_iDelta) {
    const int l_iTo = l_iRow + l_iDelta;
    if (l_iRow < 0 || l_iRow >= (int)m_vPics.size() || l_iTo < 0 || l_iTo >= (int)m_vPics.size()) return;
    std::swap(m_vPics[l_iRow], m_vPics[l_iTo]);
    f_FillTable();
    m_pTable->selectRow(l_iTo);
    f_Rebuild();
}

void FontWindow::f_ClearPictures() {
    m_vPics.clear();
    m_pFaceEdit->clear();
    f_SetSpaceAdvance(-1);
    f_SetLineHeight(-1);
    f_FillTable();
    f_Rebuild();
}

bool FontWindow::f_OpenFont(const QString& l_oFntPath) {
    std::vector<fonttool::GlyphPicture> l_vGlyphs;
    std::string l_strErr;
    if (!fonttool::f_GlyphsFromFont(f_Str(l_oFntPath), l_vGlyphs, l_strErr)) {
        f_Message(tr("error: ") + f_Qstr(l_strErr));
        return false;
    }
    m_vPics = std::move(l_vGlyphs);
    BitmapFont l_oF;
    atlas::Image l_oPage;
    if (fonttool::f_LoadFont(f_Str(l_oFntPath), l_oF, l_oPage, l_strErr)) {
        m_pFaceEdit->setText(f_Qstr(l_oF.m_strFace));
        // the space comes back as a blank picture of its width; a line height other than the tallest
        // picture is kept in the field
        f_SetSpaceAdvance(-1);
        f_SetLineHeight(l_oF.m_iLineHeight != fonttool::f_DefaultLineHeight(m_vPics) ? l_oF.m_iLineHeight : -1);
    }
    f_Message(tr("%1: %2 glyphs opened as pictures").arg(l_oFntPath).arg(m_vPics.size()));
    f_FillTable();
    f_Rebuild();
    return true;
}

bool FontWindow::f_SaveFont(const QString& l_oFntPath) {
    if (m_vPics.empty()) return false;
    const std::vector<std::string> l_vProblems = fonttool::f_CheckGlyphs(m_vPics);
    if (!l_vProblems.empty()) {
        f_Message(tr("not saved: %1").arg(f_Qstr(l_vProblems[0])));
        return false;
    }
    const fonttool::BuildOptions l_oOpt = f_Options(QFileInfo(l_oFntPath).completeBaseName());
    fonttool::BuiltFont l_oOut;
    std::string l_strErr;
    const QString l_oPng = QFileInfo(l_oFntPath).completeBaseName() + ".png";
    if (!fonttool::f_BuildFont(m_vPics, l_oOpt, f_Str(l_oPng), l_oOut, l_strErr) || !fonttool::f_WriteFont(l_oOut, f_Str(l_oFntPath), l_strErr)) {
        f_Message(tr("error: ") + f_Qstr(l_strErr));
        return false;
    }
    f_Message(tr("saved %1 and %2 (%3 glyphs, page %4x%5)").arg(l_oFntPath, l_oPng).arg(l_oOut.m_oFont.m_vGlyphs.size())
                .arg(l_oOut.m_oPage.m_iW).arg(l_oOut.m_oPage.m_iH));
    return true;
}

void FontWindow::f_FillTable() {
    m_bFilling = true;
    m_pTable->setRowCount((int)m_vPics.size());
    for (int l_iR = 0; l_iR < (int)m_vPics.size(); l_iR++) {
        const fonttool::GlyphPicture& l_oG = m_vPics[l_iR];
        auto* l_pPic = new QTableWidgetItem(QIcon(f_Thumbnail(l_oG.m_oImg, 48)), QString());
        l_pPic->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto* l_pCh = new QTableWidgetItem(f_CharText(l_oG.m_uCp));
        l_pCh->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        l_pCh->setTextAlignment(Qt::AlignCenter);
        QFont l_oBig = l_pCh->font();
        l_oBig.setPointSize(l_oBig.pointSize() + 4);
        l_pCh->setFont(l_oBig);
        auto* l_pCode = new QTableWidgetItem(
            l_oG.m_uCp ? QStringLiteral("U+") + QString::number(l_oG.m_uCp, 16).toUpper().rightJustified(4, QLatin1Char('0')) : QString());
        auto* l_pSize = new QTableWidgetItem(QStringLiteral("%1x%2").arg(l_oG.m_oImg.m_iW).arg(l_oG.m_oImg.m_iH));
        auto* l_pSrc = new QTableWidgetItem(f_Qstr(l_oG.m_strFile));
        for (QTableWidgetItem* l_pIt : {l_pCode, l_pSize, l_pSrc}) l_pIt->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_pTable->setItem(l_iR, ColPicture, l_pPic);
        m_pTable->setItem(l_iR, ColChar, l_pCh);
        m_pTable->setItem(l_iR, ColCode, l_pCode);
        m_pTable->setItem(l_iR, ColSize, l_pSize);
        m_pTable->setItem(l_iR, ColSource, l_pSrc);
    }
    m_bFilling = false;
}

void FontWindow::f_Rebuild() {
    // mark the rows that cannot build: no character, or one an earlier row already has
    m_vProblems = fonttool::f_CheckGlyphs(m_vPics);
    std::vector<fonttool::GlyphPicture> l_vUsable;
    std::set<uint32_t> l_setSeen;
    m_vRowGlyph.assign(m_vPics.size(), -1);
    m_vGlyphRow.clear();
    m_bFilling = true;
    for (int l_iR = 0; l_iR < (int)m_vPics.size(); l_iR++) {
        const bool l_bOk = m_vPics[l_iR].m_uCp && l_setSeen.insert(m_vPics[l_iR].m_uCp).second;
        if (l_bOk) {
            m_vRowGlyph[l_iR] = (int)l_vUsable.size();   // buildFont keeps the order (and adds a space last)
            m_vGlyphRow.push_back(l_iR);
            l_vUsable.push_back(m_vPics[l_iR]);
        }
        if (QTableWidgetItem* l_pIt = m_pTable->item(l_iR, ColChar))
            l_pIt->setBackground(l_bOk ? QBrush() : QBrush(QColor(140, 40, 40)));
    }
    m_bFilling = false;
    QString l_oProblemText;
    for (size_t l_nI = 0; l_nI < m_vProblems.size() && l_nI < 4; l_nI++) l_oProblemText += f_Qstr(m_vProblems[l_nI]) + "\n";
    if (m_vProblems.size() > 4) l_oProblemText += tr("... %1 more").arg(m_vProblems.size() - 4);
    if (!m_vProblems.empty()) l_oProblemText = tr("Not saved until fixed (red rows):\n") + l_oProblemText;
    m_pProblemLabel->setText(l_oProblemText.trimmed());
    // the auto values these pictures give, shown in the fields
    m_pSpaceSpin->setSpecialValueText(l_vUsable.empty() ? tr("auto") : tr("auto (%1 px)").arg(fonttool::f_DefaultSpaceAdvance(l_vUsable)));
    m_pLineSpin->setSpecialValueText(l_vUsable.empty() ? tr("auto") : tr("auto (%1 px, the tallest)").arg(fonttool::f_DefaultLineHeight(l_vUsable)));

    m_oFont = BitmapFont();
    m_oPage = atlas::Image();
    if (!l_vUsable.empty()) {
        fonttool::BuiltFont l_oBuilt;
        std::string l_strErr;
        if (fonttool::f_BuildFont(l_vUsable, f_Options(QStringLiteral("font")), "font.png", l_oBuilt, l_strErr)) {
            m_oFont = l_oBuilt.m_oFont;
            m_oPage = l_oBuilt.m_oPage;
        } else {
            f_Message(tr("error: ") + f_Qstr(l_strErr));
        }
    }
    while (m_vGlyphRow.size() < m_oFont.m_vGlyphs.size()) m_vGlyphRow.push_back(-1);   // the space added for you

    // the combined picture, every glyph's rect on it
    QVector<QRect> l_oRects;
    for (const BitmapGlyph& l_oG : m_oFont.m_vGlyphs) l_oRects.push_back(QRect(l_oG.m_iX, l_oG.m_iY, l_oG.m_iW, l_oG.m_iH));
    m_pPageView->f_SetPage(f_ToQImage(m_oPage), l_oRects);
    if (m_oPage.f_Valid()) {
        const bool l_bPot = (m_oPage.m_iW & (m_oPage.m_iW - 1)) == 0 && (m_oPage.m_iH & (m_oPage.m_iH - 1)) == 0;
        m_pPageLabel->setText(tr("Combined picture: %1 x %2%3. Select a row to find its glyph; click a glyph to select "
                               "its row.").arg(m_oPage.m_iW).arg(m_oPage.m_iH).arg(l_bPot ? tr(" (power of 2)") : QString()));
    } else {
        m_pPageLabel->setText(tr("Combined picture"));
    }
    f_ShowSelection();
    f_UpdatePreview();
}

void FontWindow::f_UpdatePreview() {
    if (m_oFont.m_vGlyphs.empty()) {
        m_oPreview = QImage();
        m_pPreviewLabel->clear();
        m_pInfoLabel->setText(m_vPics.empty() ? tr("No pictures yet.") : tr("No picture has a character yet."));
        return;
    }
    const std::string l_strText = f_Str(m_pTextEdit->toPlainText());
    const atlas::Image l_oImg = fonttool::f_RenderText(m_oFont, m_oPage, l_strText, (float)m_pScaleSpin->value(), 8, nullptr, &m_iMissing);
    const QImage l_oTextImg = f_ToQImage(l_oImg);
    const int l_iW = l_oTextImg.width(), l_iH = l_oTextImg.height();
    m_oPreview = QImage(l_iW, l_iH, QImage::Format_ARGB32_Premultiplied);
    QPainter l_oP(&m_oPreview);
    switch (m_pBgCombo->currentIndex()) {
    case 0:   // checker
        for (int l_iY = 0; l_iY < l_iH; l_iY += 8)
            for (int l_iX = 0; l_iX < l_iW; l_iX += 8)
                l_oP.fillRect(l_iX, l_iY, 8, 8, ((l_iX / 8 + l_iY / 8) & 1) ? QColor(90, 90, 90) : QColor(60, 60, 60));
        break;
    case 1: l_oP.fillRect(0, 0, l_iW, l_iH, QColor::fromRgbF(g_arrBackgroundClearColor[0], g_arrBackgroundClearColor[1], g_arrBackgroundClearColor[2])); break;
    case 2: l_oP.fillRect(0, 0, l_iW, l_iH, Qt::black); break;
    default: l_oP.fillRect(0, 0, l_iW, l_iH, Qt::white); break;
    }
    l_oP.drawImage(0, 0, l_oTextImg);
    l_oP.end();
    m_pPreviewLabel->setPixmap(QPixmap::fromImage(m_oPreview));

    float l_fTw = 0, l_fTh = 0;
    m_oFont.f_Measure(l_strText, (float)m_pScaleSpin->value(), l_fTw, l_fTh);
    QString l_oInfo = tr("%1 glyphs, line height %2, page %3x%4   |   text %5x%6 px")
                       .arg(m_oFont.m_vGlyphs.size()).arg(m_oFont.m_iLineHeight).arg(m_oPage.m_iW).arg(m_oPage.m_iH)
                       .arg((int)std::ceil(l_fTw)).arg((int)std::ceil(l_fTh));
    if (m_iMissing) l_oInfo += tr("   |   %1 character(s) not in the font (skipped)").arg(m_iMissing);
    m_pInfoLabel->setText(l_oInfo);
}
