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

bool check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "ok" : "FAILED", what);
    return ok;
}

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

int runSelfTest(FontWindow& w, const QString& folder, const QString& outDir) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QDir().mkpath(outDir);
    QDir out(outDir);
    bool ok = true;

    // (a) add pictures: the character comes from each file name
    QStringList files;
    for (const QString& f : QDir(folder).entryList({"*.png"}, QDir::Files, QDir::Name)) files << QDir(folder).filePath(f);
    ok &= check(w.addPictures(files) == files.size() && files.size() == 15, "(a) add 15 pictures");
    ok &= check(w.problemCount() == 0 && w.glyphCount() == 15, "(a) every file name gives a character");

    // (b) a picture whose name is no character waits for one to be typed
    QFile::remove(out.filePath("my_glyph.png"));
    QFile::copy(QDir(folder).filePath("0.png"), out.filePath("my_glyph.png"));
    ok &= check(w.addPictures({out.filePath("my_glyph.png")}) == 1 && w.problemCount() == 1,
                "(b) a picture named my_glyph has no character: one problem");
    ok &= check(!w.saveFont(out.filePath("refused.fnt")) && !QFileInfo::exists(out.filePath("refused.fnt")),
                "(b) save is refused while a picture has no character");
    const int last = w.pictureCount() - 1;
    w.setCharacter(last, QStringLiteral("%"));
    ok &= check(w.problemCount() == 0 && w.font().glyph('%') != nullptr, "(c) typing % gives it the character");
    w.setCharacter(last, QStringLiteral("7"));
    ok &= check(w.problemCount() == 1, "(c) a character another picture has is a problem");
    w.setCharacter(last, QStringLiteral("U+0025"));
    ok &= check(w.problemCount() == 0 && w.font().glyph('%') != nullptr, "(c) U+0025 is % too");
    w.removePicture(last);
    ok &= check(w.pictureCount() == 15 && !w.font().glyph('%'), "(c) remove the picture");

    // (d) order on the page
    w.movePicture(0, +1);
    ok &= check(w.font().glyphs.size() > 1 && w.font().glyphs[0].id == '1', "(d) move '0' down: '1' comes first");
    w.movePicture(1, -1);
    ok &= check(!w.font().glyphs.empty() && w.font().glyphs[0].id == '0', "(d) and back");

    // (h) the combined picture: a selected row's glyph is highlighted; a click on a glyph selects its row
    w.selectRow(3);
    ok &= check(w.highlightedGlyph() == 3, "(h) select row 3: glyph 3 is highlighted on the combined picture");
    const BitmapGlyph& g5 = w.font().glyphs.at(5);
    w.clickPage(QPoint(g5.x + g5.w / 2, g5.y + g5.h / 2));
    ok &= check(w.selectedRow() == 5 && w.highlightedGlyph() == 5, "(h) click glyph 5 on the picture: row 5 selected");
    w.clickPage(QPoint(0, 0));
    ok &= check(w.selectedRow() == 5, "(h) a click between glyphs changes nothing");

    // (i) page layouts: tight + power of 2 (the atlas packer); the text draws the same
    w.setTestText(QStringLiteral("12,345.6K"));
    const QImage rowPreview = w.previewImage();
    w.setLayout(fonttool::PageLayout::TightPow2);
    const atlas::Image& pg = w.page();
    ok &= check(pg.valid() && (pg.w & (pg.w - 1)) == 0 && (pg.h & (pg.h - 1)) == 0,
                "(i) Tight, power of 2: the combined picture's sides are powers of 2");
    ok &= check(w.previewImage() == rowPreview, "(i) the test text draws the same as with one row");
    w.selectRow(5);
    ok &= check(w.highlightedGlyph() == 5, "(i) the highlight follows the glyph to its new place");
    ok &= check(w.saveFont(out.filePath("pot_font.fnt")), "(i) save the power-of-2 font");
    BitmapFont potFont;
    atlas::Image potPage;
    std::string potErr;
    ok &= check(fonttool::loadFont(QDir(outDir).filePath("pot_font.fnt").toUtf8().toStdString(), potFont, potPage, potErr) &&
                    potPage.w == pg.w && potPage.h == pg.h,
                "(i) the saved page has the same power-of-2 size");
    w.setLayout(fonttool::PageLayout::Row);

    // (j) space width and line height: auto values, then set by hand
    const QString fntBefore = out.filePath("before_lines.fnt");
    w.saveFont(fntBefore);   // to come back to afterwards
    ok &= check(w.font().glyph(' ') && w.font().glyph(' ')->xadvance == 86 && w.font().lineHeight == 94,
                "(j) auto: the space picture's width (86), the tallest picture's height (94)");
    w.setTestText(QStringLiteral("1\n1"));
    const int autoLines = w.previewImage().height();
    w.setLineHeight(150);
    // two lines measure 2 x the line height: the preview grows by 2 x (150 - 94)
    ok &= check(w.font().lineHeight == 150 && w.previewImage().height() == autoLines + 2 * (150 - 94),
                "(j) line height 150: the font says so and two lines take 112 px more");
    w.setSpaceAdvance(20);
    ok &= check(w.font().glyph(' ')->xadvance == 20, "(j) space width 20 wins over the space picture");
    const QString linesFnt = out.filePath("lines_font.fnt");
    ok &= check(w.saveFont(linesFnt) && w.openFont(linesFnt) && w.font().lineHeight == 150,
                "(j) saved and opened again: the line height 150 is kept");
    w.setSpaceAdvance(-1);
    w.setLineHeight(-1);
    ok &= check(w.font().lineHeight == 94, "(j) back to auto");
    w.openFont(fntBefore);

    // (e) the test text
    w.setTestText(QStringLiteral("12,345.6K"));
    ok &= check(!w.previewImage().isNull() && w.missingChars() == 0, "(e) the test text draws, every character found");
    const int oneLine = w.previewImage().height();
    w.setTestText(QStringLiteral("12\n34"));
    ok &= check(w.previewImage().height() > oneLine, "(e) a second line makes the preview taller");
    w.setTestText(QStringLiteral("1#2"));
    ok &= check(w.missingChars() == 1, "(e) a character the font lacks is counted");
    w.setTestText(QStringLiteral("12"));
    const int width1 = w.previewImage().width();
    w.setScale(2.0);
    ok &= check(w.previewImage().width() > width1 * 3 / 2, "(e) scale 2 draws it larger");
    w.previewImage().save(out.filePath("preview.png"));

    // (f) save, open the saved font as pictures, save again: the same file
    const QString fnt = out.filePath("selftest_font.fnt");
    ok &= check(w.saveFont(fnt) && QFileInfo::exists(out.filePath("selftest_font.png")), "(f) save writes .fnt + .png");
    ok &= check(w.openFont(fnt) && w.pictureCount() == 15 && w.problemCount() == 0,
                "(f) open the saved font: its 15 glyphs come back as pictures");
    const QString again = out.filePath("selftest_again.fnt");
    ok &= check(w.saveFont(again), "(f) save it again");
    QByteArray a = readAll(fnt), b = readAll(again);
    a.replace("selftest_font.png", "X");
    b.replace("selftest_again.png", "X");
    ok &= check(!a.isEmpty() && a == b, "(f) the same .fnt again (but the page name)");
    ok &= check(fontCliMain({"info", fnt.toUtf8().toStdString()}) == 0, "(g) --headless info on it: exit 0");

    w.setScale(0.5);
    w.setTestText(QStringLiteral("12,345.6K\n9,999M 0"));
    w.resize(1180, 760);
    w.selectRow(5);
    w.grab().save(out.filePath("window.png"));   // to look at, not compared

    std::printf("font_editor selftest: %s\n", ok ? "PASSED" : "FAILED");
    if (!ok) std::printf("log:\n%s\n", w.log().toUtf8().constData());
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args = utf8CommandLine(argc, argv);
    if (!args.empty() && args[0] == "--headless") return fontCliMain({args.begin() + 1, args.end()});

    QApplication app(argc, argv);
    QApplication::setOrganizationName("TOMS");
    QApplication::setApplicationName("font_editor");
    Theme::apply(Theme::Mode::Dark);
    Language::init();   // English / 中文 (tools/studio_common/Language.h), before any window
    FontWindow w;

    const QStringList qargs = app.arguments();   // Qt removed its own options (-platform ...)
    const int st = qargs.indexOf("--selftest");
    if (st >= 0) {
        if (st + 2 >= qargs.size()) {
            std::fprintf(stderr, "usage: font_editor --selftest <pictures folder> <outdir>\n");
            return 3;
        }
        return runSelfTest(w, qargs[st + 1], qargs[st + 2]);
    }
    QStringList pngs;
    for (int i = 1; i < qargs.size(); i++) {
        if (qargs[i].endsWith(QLatin1String(".fnt"), Qt::CaseInsensitive)) w.openFont(qargs[i]);
        else pngs << qargs[i];
    }
    if (!pngs.isEmpty()) w.addPictures(pngs);
    w.show();
    return app.exec();
}
