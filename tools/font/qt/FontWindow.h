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

// font_editor's window (docs/22_FONT_TOOL.md, docs/23_FONT_TOOL_GUIDE.md): add pictures, give each one
// its character, and save them combined as a picture font (.fnt + .png). A test text is drawn with the
// font the way the game lays text out (BitmapFont::layout). Opening a .fnt turns its glyphs back into
// pictures to change and save again.
class FontWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit FontWindow(QWidget* parent = nullptr);

    // Adds PNG files as pictures; the character is guessed from each file name (0.png, dot.png,
    // U+00D7.png), else left empty for you to type. Returns how many were added.
    int addPictures(const QStringList& files);
    // Sets a picture's character: one character, a name (space, dot, comma, ...) or U+XXXX; "" clears it.
    void setCharacter(int row, const QString& text);
    void removePicture(int row);
    void movePicture(int row, int delta);   // the order of the glyphs on the page
    void clearPictures();
    // Replaces the pictures with a finished font's glyphs (to change it and save again).
    bool openFont(const QString& fntPath);
    // Writes <out>.fnt + .png. False while a picture has no character or a repeated one.
    bool saveFont(const QString& fntPath);

    void setTestText(const QString& text);
    QString testText() const;
    void setScale(double scale);
    void setLayout(fonttool::PageLayout layout);
    void setSpaceAdvance(int px);        // -1: auto (a space picture's width, else the '1' picture's, else 5)
    void setLineHeight(int px);          // -1: auto (the tallest picture)
    // Selects a row: its glyph is highlighted on the combined picture.
    void selectRow(int row);
    int selectedRow() const;
    // What a click at this point of the combined picture (page pixels) does: selects that glyph's row.
    void clickPage(const QPoint& pagePos);
    int highlightedGlyph() const;        // on the combined picture, -1 none
    const atlas::Image& page() const { return page_; }

    int pictureCount() const { return (int)pics_.size(); }
    int problemCount() const { return (int)problems_.size(); }
    const QImage& previewImage() const { return preview_; }
    int missingChars() const { return missing_; }
    const BitmapFont& font() const { return font_; }
    int glyphCount() const { return (int)font_.glyphs.size(); }
    QString log() const;

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void browseAdd();
    void browseOpen();
    void browseSave();
    void rebuild();          // pictures + options -> font_ (in memory), table marks, preview
    void fillTable();
    void updatePreview();
    void message(const QString& line);
    fonttool::BuildOptions options(const QString& face) const;
    void showSelection();                // the selected row's glyph -> the page view

    QTableWidget* table_ = nullptr;
    PageView* pageView_ = nullptr;
    QLabel* pageLabel_ = nullptr;
    QComboBox* layoutCombo_ = nullptr;
    QLineEdit* faceEdit_ = nullptr;
    QSpinBox* spaceSpin_ = nullptr;
    QSpinBox* lineSpin_ = nullptr;
    QSpinBox* padSpin_ = nullptr;
    QSpinBox* maxWidthSpin_ = nullptr;
    QPlainTextEdit* textEdit_ = nullptr;
    QDoubleSpinBox* scaleSpin_ = nullptr;
    QComboBox* bgCombo_ = nullptr;
    QLabel* previewLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QLabel* problemLabel_ = nullptr;
    QPlainTextEdit* log_ = nullptr;

    std::vector<fonttool::GlyphPicture> pics_;
    std::vector<std::string> problems_;
    std::vector<int> rowGlyph_;          // row -> glyph index in font_ (-1: not built, a problem row)
    std::vector<int> glyphRow_;          // glyph index -> row (-1: the space added for you)
    bool filling_ = false;
    BitmapFont font_;
    atlas::Image page_;
    QImage preview_;
    int missing_ = 0;
};
