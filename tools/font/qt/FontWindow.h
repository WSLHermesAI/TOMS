#pragma once

#include "font_build.h"

#include <QMainWindow>

class PageView;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;

// font_editor's window (docs/engine/09_FONT_TOOL.md, docs/engine/10_FONT_TOOL_GUIDE.md): add pictures, give each one
// its character, and save them combined as a picture font (.fnt + .png). A test text is drawn with the
// font the way the game lays text out (BitmapFont::layout). Opening a .fnt turns its glyphs back into
// pictures to change and save again.
class FontWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit FontWindow(QWidget* l_pParent = nullptr);

    // Adds PNG files as pictures; the character is guessed from each file name (0.png, dot.png,
    // U+00D7.png), else left empty for you to type. Returns how many were added.
    int f_AddPictures(const QStringList& l_oFiles);
    // Sets a picture's character: one character, a name (space, dot, comma, ...) or U+XXXX; "" clears it.
    void f_SetCharacter(int l_iRow, const QString& l_oText);
    void f_RemovePicture(int l_iRow);
    void f_MovePicture(int l_iRow, int l_iDelta);   // the order of the glyphs on the page
    void f_ClearPictures();
    // Replaces the pictures with a finished font's glyphs (to change it and save again).
    bool f_OpenFont(const QString& l_oFntPath);
    // Writes <out>.fnt + .png. False while a picture has no character or a repeated one.
    bool f_SaveFont(const QString& l_oFntPath);

    void f_SetTestText(const QString& l_oText);
    QString f_TestText() const;
    void f_SetScale(double l_dScale);
    void f_SetLayout(fonttool::PageLayout l_eLayout);
    void f_SetSpaceAdvance(int l_iPx);        // -1: auto (a space picture's width, else the '1' picture's, else 5)
    void f_SetLineHeight(int l_iPx);          // -1: auto (the tallest picture)
    // Selects a row: its glyph is highlighted on the combined picture.
    void f_SelectRow(int l_iRow);
    int f_SelectedRow() const;
    // What a click at this point of the combined picture (page pixels) does: selects that glyph's row.
    void f_ClickPage(const QPoint& l_oPagePos);
    int f_HighlightedGlyph() const;        // on the combined picture, -1 none
    const atlas::Image& f_Page() const { return m_oPage; }

    int f_PictureCount() const { return (int)m_vPics.size(); }
    int f_ProblemCount() const { return (int)m_vProblems.size(); }
    const QImage& f_PreviewImage() const { return m_oPreview; }
    int f_MissingChars() const { return m_iMissing; }
    const BitmapFont& f_Font() const { return m_oFont; }
    int f_GlyphCount() const { return (int)m_oFont.m_vGlyphs.size(); }
    QString f_Log() const;

protected:
    void dragEnterEvent(QDragEnterEvent* l_pE) override;
    void dropEvent(QDropEvent* l_pE) override;

private:
    void f_BrowseAdd();
    void f_BrowseOpen();
    void f_BrowseSave();
    void f_Rebuild();          // pictures + options -> font_ (in memory), table marks, preview
    void f_FillTable();
    void f_UpdatePreview();
    void f_Message(const QString& l_oLine);
    fonttool::BuildOptions f_Options(const QString& l_oFace) const;
    void f_ShowSelection();                // the selected row's glyph -> the page view

    QTableWidget* m_pTable = nullptr;
    PageView* m_pPageView = nullptr;
    QLabel* m_pPageLabel = nullptr;
    QComboBox* m_pLayoutCombo = nullptr;
    QLineEdit* m_pFaceEdit = nullptr;
    QSpinBox* m_pSpaceSpin = nullptr;
    QSpinBox* m_pLineSpin = nullptr;
    QSpinBox* m_pPadSpin = nullptr;
    QSpinBox* m_pMaxWidthSpin = nullptr;
    QPlainTextEdit* m_pTextEdit = nullptr;
    QDoubleSpinBox* m_pScaleSpin = nullptr;
    QComboBox* m_pBgCombo = nullptr;
    QLabel* m_pPreviewLabel = nullptr;
    QLabel* m_pInfoLabel = nullptr;
    QLabel* m_pProblemLabel = nullptr;
    QPlainTextEdit* m_pLog = nullptr;

    std::vector<fonttool::GlyphPicture> m_vPics;
    std::vector<std::string> m_vProblems;
    std::vector<int> m_vRowGlyph;          // row -> glyph index in font_ (-1: not built, a problem row)
    std::vector<int> m_vGlyphRow;          // glyph index -> row (-1: the space added for you)
    bool m_bFilling = false;
    BitmapFont m_oFont;
    atlas::Image m_oPage;
    QImage m_oPreview;
    int m_iMissing = 0;
};
