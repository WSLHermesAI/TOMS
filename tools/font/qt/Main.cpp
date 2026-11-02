// font_editor -- the Qt window of the font tool (docs/engine/09_FONT_TOOL.md, docs/engine/10_FONT_TOOL_GUIDE.md):
//
//   font_editor                                 the window
//   font_editor <a.png b.png ... | font.fnt>    the window with those pictures added (or that font opened)
//   font_editor --headless <fontpack command>   the command line, as fontpack (cli/font_cli.cpp)
//   font_editor --selftest <pictures folder> <outdir>
//                                               automated check (-platform offscreen): add the folder's
//                                               pictures, edit characters, test texts, save, open, save again
#include "FontWindow.h"
#include "Language.h"
#include "Theme.h"
#include "atlas_cli.h"   // utf8CommandLine
#include "font_cli.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

namespace {

bool f_Check(bool l_bOk, const char* l_pszWhat) {
    std::printf("[%s] %s\n", l_bOk ? "ok" : "FAILED", l_pszWhat);
    return l_bOk;
}

QByteArray f_ReadAll(const QString& l_oPath) {
    QFile l_oF(l_oPath);
    return l_oF.open(QIODevice::ReadOnly) ? l_oF.readAll() : QByteArray();
}

int f_RunSelfTest(FontWindow& l_oW, const QString& l_oFolder, const QString& l_oOutDir) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QDir().mkpath(l_oOutDir);
    QDir l_oOut(l_oOutDir);
    bool l_bOk = true;

    // (a) add pictures: the character comes from each file name
    QStringList l_oFiles;
    for (const QString& l_oF : QDir(l_oFolder).entryList({"*.png"}, QDir::Files, QDir::Name)) l_oFiles << QDir(l_oFolder).filePath(l_oF);
    l_bOk &= f_Check(l_oW.f_AddPictures(l_oFiles) == l_oFiles.size() && l_oFiles.size() == 15, "(a) add 15 pictures");
    l_bOk &= f_Check(l_oW.f_ProblemCount() == 0 && l_oW.f_GlyphCount() == 15, "(a) every file name gives a character");

    // (b) a picture whose name is no character waits for one to be typed
    QFile::remove(l_oOut.filePath("my_glyph.png"));
    QFile::copy(QDir(l_oFolder).filePath("0.png"), l_oOut.filePath("my_glyph.png"));
    l_bOk &= f_Check(l_oW.f_AddPictures({l_oOut.filePath("my_glyph.png")}) == 1 && l_oW.f_ProblemCount() == 1,
                "(b) a picture named my_glyph has no character: one problem");
    l_bOk &= f_Check(!l_oW.f_SaveFont(l_oOut.filePath("refused.fnt")) && !QFileInfo::exists(l_oOut.filePath("refused.fnt")),
                "(b) save is refused while a picture has no character");
    const int l_iLast = l_oW.f_PictureCount() - 1;
    l_oW.f_SetCharacter(l_iLast, QStringLiteral("%"));
    l_bOk &= f_Check(l_oW.f_ProblemCount() == 0 && l_oW.f_Font().f_Glyph('%') != nullptr, "(c) typing % gives it the character");
    l_oW.f_SetCharacter(l_iLast, QStringLiteral("7"));
    l_bOk &= f_Check(l_oW.f_ProblemCount() == 1, "(c) a character another picture has is a problem");
    l_oW.f_SetCharacter(l_iLast, QStringLiteral("U+0025"));
    l_bOk &= f_Check(l_oW.f_ProblemCount() == 0 && l_oW.f_Font().f_Glyph('%') != nullptr, "(c) U+0025 is % too");
    l_oW.f_RemovePicture(l_iLast);
    l_bOk &= f_Check(l_oW.f_PictureCount() == 15 && !l_oW.f_Font().f_Glyph('%'), "(c) remove the picture");

    // (d) order on the page
    l_oW.f_MovePicture(0, +1);
    l_bOk &= f_Check(l_oW.f_Font().m_vGlyphs.size() > 1 && l_oW.f_Font().m_vGlyphs[0].m_uId == '1', "(d) move '0' down: '1' comes first");
    l_oW.f_MovePicture(1, -1);
    l_bOk &= f_Check(!l_oW.f_Font().m_vGlyphs.empty() && l_oW.f_Font().m_vGlyphs[0].m_uId == '0', "(d) and back");

    // (h) the combined picture: a selected row's glyph is highlighted; a click on a glyph selects its row
    l_oW.f_SelectRow(3);
    l_bOk &= f_Check(l_oW.f_HighlightedGlyph() == 3, "(h) select row 3: glyph 3 is highlighted on the combined picture");
    const BitmapGlyph& l_oG5 = l_oW.f_Font().m_vGlyphs.at(5);
    l_oW.f_ClickPage(QPoint(l_oG5.m_iX + l_oG5.m_iW / 2, l_oG5.m_iY + l_oG5.m_iH / 2));
    l_bOk &= f_Check(l_oW.f_SelectedRow() == 5 && l_oW.f_HighlightedGlyph() == 5, "(h) click glyph 5 on the picture: row 5 selected");
    l_oW.f_ClickPage(QPoint(0, 0));
    l_bOk &= f_Check(l_oW.f_SelectedRow() == 5, "(h) a click between glyphs changes nothing");

    // (i) page layouts: tight + power of 2 (the atlas packer); the text draws the same
    l_oW.f_SetTestText(QStringLiteral("12,345.6K"));
    const QImage l_oRowPreview = l_oW.f_PreviewImage();
    l_oW.f_SetLayout(fonttool::PageLayout::TightPow2);
    const atlas::Image& l_oPg = l_oW.f_Page();
    l_bOk &= f_Check(l_oPg.f_Valid() && (l_oPg.m_iW & (l_oPg.m_iW - 1)) == 0 && (l_oPg.m_iH & (l_oPg.m_iH - 1)) == 0,
                "(i) Tight, power of 2: the combined picture's sides are powers of 2");
    l_bOk &= f_Check(l_oW.f_PreviewImage() == l_oRowPreview, "(i) the test text draws the same as with one row");
    l_oW.f_SelectRow(5);
    l_bOk &= f_Check(l_oW.f_HighlightedGlyph() == 5, "(i) the highlight follows the glyph to its new place");
    l_bOk &= f_Check(l_oW.f_SaveFont(l_oOut.filePath("pot_font.fnt")), "(i) save the power-of-2 font");
    BitmapFont l_oPotFont;
    atlas::Image l_oPotPage;
    std::string l_strPotErr;
    l_bOk &= f_Check(fonttool::f_LoadFont(QDir(l_oOutDir).filePath("pot_font.fnt").toUtf8().toStdString(), l_oPotFont, l_oPotPage, l_strPotErr) &&
                    l_oPotPage.m_iW == l_oPg.m_iW && l_oPotPage.m_iH == l_oPg.m_iH,
                "(i) the saved page has the same power-of-2 size");
    l_oW.f_SetLayout(fonttool::PageLayout::Row);

    // (j) space width and line height: auto values, then set by hand
    const QString l_oFntBefore = l_oOut.filePath("before_lines.fnt");
    l_oW.f_SaveFont(l_oFntBefore);   // to come back to afterwards
    l_bOk &= f_Check(l_oW.f_Font().f_Glyph(' ') && l_oW.f_Font().f_Glyph(' ')->m_iXadvance == 86 && l_oW.f_Font().m_iLineHeight == 94,
                "(j) auto: the space picture's width (86), the tallest picture's height (94)");
    l_oW.f_SetTestText(QStringLiteral("1\n1"));
    const int l_iAutoLines = l_oW.f_PreviewImage().height();
    l_oW.f_SetLineHeight(150);
    // two lines measure 2 x the line height: the preview grows by 2 x (150 - 94)
    l_bOk &= f_Check(l_oW.f_Font().m_iLineHeight == 150 && l_oW.f_PreviewImage().height() == l_iAutoLines + 2 * (150 - 94),
                "(j) line height 150: the font says so and two lines take 112 px more");
    l_oW.f_SetSpaceAdvance(20);
    l_bOk &= f_Check(l_oW.f_Font().f_Glyph(' ')->m_iXadvance == 20, "(j) space width 20 wins over the space picture");
    const QString l_oLinesFnt = l_oOut.filePath("lines_font.fnt");
    l_bOk &= f_Check(l_oW.f_SaveFont(l_oLinesFnt) && l_oW.f_OpenFont(l_oLinesFnt) && l_oW.f_Font().m_iLineHeight == 150,
                "(j) saved and opened again: the line height 150 is kept");
    l_oW.f_SetSpaceAdvance(-1);
    l_oW.f_SetLineHeight(-1);
    l_bOk &= f_Check(l_oW.f_Font().m_iLineHeight == 94, "(j) back to auto");
    l_oW.f_OpenFont(l_oFntBefore);

    // (e) the test text
    l_oW.f_SetTestText(QStringLiteral("12,345.6K"));
    l_bOk &= f_Check(!l_oW.f_PreviewImage().isNull() && l_oW.f_MissingChars() == 0, "(e) the test text draws, every character found");
    const int l_iOneLine = l_oW.f_PreviewImage().height();
    l_oW.f_SetTestText(QStringLiteral("12\n34"));
    l_bOk &= f_Check(l_oW.f_PreviewImage().height() > l_iOneLine, "(e) a second line makes the preview taller");
    l_oW.f_SetTestText(QStringLiteral("1#2"));
    l_bOk &= f_Check(l_oW.f_MissingChars() == 1, "(e) a character the font lacks is counted");
    l_oW.f_SetTestText(QStringLiteral("12"));
    const int l_iWidth1 = l_oW.f_PreviewImage().width();
    l_oW.f_SetScale(2.0);
    l_bOk &= f_Check(l_oW.f_PreviewImage().width() > l_iWidth1 * 3 / 2, "(e) scale 2 draws it larger");
    l_oW.f_PreviewImage().save(l_oOut.filePath("preview.png"));

    // (f) save, open the saved font as pictures, save again: the same file
    const QString l_oFnt = l_oOut.filePath("selftest_font.fnt");
    l_bOk &= f_Check(l_oW.f_SaveFont(l_oFnt) && QFileInfo::exists(l_oOut.filePath("selftest_font.png")), "(f) save writes .fnt + .png");
    l_bOk &= f_Check(l_oW.f_OpenFont(l_oFnt) && l_oW.f_PictureCount() == 15 && l_oW.f_ProblemCount() == 0,
                "(f) open the saved font: its 15 glyphs come back as pictures");
    const QString l_oAgain = l_oOut.filePath("selftest_again.fnt");
    l_bOk &= f_Check(l_oW.f_SaveFont(l_oAgain), "(f) save it again");
    QByteArray l_oA = f_ReadAll(l_oFnt), l_oB = f_ReadAll(l_oAgain);
    l_oA.replace("selftest_font.png", "X");
    l_oB.replace("selftest_again.png", "X");
    l_bOk &= f_Check(!l_oA.isEmpty() && l_oA == l_oB, "(f) the same .fnt again (but the page name)");
    l_bOk &= f_Check(f_FontCliMain({"info", l_oFnt.toUtf8().toStdString()}) == 0, "(g) --headless info on it: exit 0");

    l_oW.f_SetScale(0.5);
    l_oW.f_SetTestText(QStringLiteral("12,345.6K\n9,999M 0"));
    l_oW.resize(1180, 760);
    l_oW.f_SelectRow(5);
    l_oW.grab().save(l_oOut.filePath("window.png"));   // to look at, not compared

    std::printf("font_editor selftest: %s\n", l_bOk ? "PASSED" : "FAILED");
    if (!l_bOk) std::printf("log:\n%s\n", l_oW.f_Log().toUtf8().constData());
    return l_bOk ? 0 : 1;
}

}  // namespace

int main(int l_iArgc, char** l_pArgv) {
    const std::vector<std::string> l_vArgs = f_Utf8CommandLine(l_iArgc, l_pArgv);
    if (!l_vArgs.empty() && l_vArgs[0] == "--headless") return f_FontCliMain({l_vArgs.begin() + 1, l_vArgs.end()});

    QApplication l_oApp(l_iArgc, l_pArgv);
    QApplication::setOrganizationName("TOMS");
    QApplication::setApplicationName("font_editor");
    Theme::f_Apply(Theme::Mode::Dark);
    Language::f_Init();   // English / 中文 (tools/studio_common/Language.h), before any window
    FontWindow l_oW;

    const QStringList l_oQargs = l_oApp.arguments();   // Qt removed its own options (-platform ...)
    const int l_iSt = l_oQargs.indexOf("--selftest");
    if (l_iSt >= 0) {
        if (l_iSt + 2 >= l_oQargs.size()) {
            std::fprintf(stderr, "usage: font_editor --selftest <pictures folder> <outdir>\n");
            return 3;
        }
        return f_RunSelfTest(l_oW, l_oQargs[l_iSt + 1], l_oQargs[l_iSt + 2]);
    }
    QStringList l_oPngs;
    for (int l_iI = 1; l_iI < l_oQargs.size(); l_iI++) {
        if (l_oQargs[l_iI].endsWith(QLatin1String(".fnt"), Qt::CaseInsensitive)) l_oW.f_OpenFont(l_oQargs[l_iI]);
        else l_oPngs << l_oQargs[l_iI];
    }
    if (!l_oPngs.isEmpty()) l_oW.f_AddPictures(l_oPngs);
    l_oW.show();
    return l_oApp.exec();
}
