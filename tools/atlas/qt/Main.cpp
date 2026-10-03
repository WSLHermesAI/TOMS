// atlas_editor -- the Qt editor for .atlasproj files, and (with --headless) the atlaspack command
// line without a window:
//
//   atlas_editor                          the editor
//   atlas_editor game.atlasproj           the editor with that project open
//   atlas_editor --headless build game.atlasproj    same as `atlaspack build game.atlasproj`
#include "AtlasDocument.h"
#include "CanvasPanel.h"
#include "Console.h"
#include "Icons.h"
#include "MainWindow.h"
#include "ReferencesDialog.h"
#include "Theme.h"
#include "atlas_cli.h"
#include "atlas_store.h"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileInfo>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QUndoStack>

#include <cstdio>

namespace {

// Waits (running the event loop) until the document has no build pending.
void waitForBuild(AtlasDocument* doc, int timeoutMs = 30000)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(doc, &AtlasDocument::buildFinished, &loop, [&] {
        if (!doc->isBuilding()) loop.quit();
    });
    timeout.start(timeoutMs);
    if (doc->isBuilding()) loop.exec();
    QCoreApplication::processEvents();
}

// Hidden switch for automated checks (run with -platform offscreen): opens a project, waits for
// the build, exercises edits / images / undo / Export Copy / Save As (all into outDir, never next
// to the project) and saves screenshots of the atlas view, sprite edit mode and the light theme.
int runSelfTest(MainWindow& w, const QString& project, const QString& outDir)
{
    AtlasDocument* doc = w.document();
    QString err;
    if (!doc->open(project, &err)) {
        std::fprintf(stderr, "selftest: %s\n", err.toUtf8().constData());
        return 2;
    }
    const bool converted = doc->isDirty();   // a folder project is converted on open
    std::printf("selftest: opened %s (%s, %zu image(s))\n", project.toUtf8().constData(),
                converted ? "folder project, converted to embedded" : "embedded", doc->project().images.size());
    waitForBuild(doc);
    const BuildSnapshotPtr snap = doc->snapshot();
    if (!snap) return 2;
    // Show off a sprite with children when there is one.
    QString pick = snap->sprites.empty() ? QString() : snap->sprites.front().name;
    for (const SpriteInfo& s : snap->sprites)
        if (s.child) {
            pick = s.parent;
            break;
        }
    doc->setSelection({pick});
    QDir().mkpath(outDir);
    QCoreApplication::processEvents();
    const bool okMain = w.grab().save(QDir(outDir).filePath(QStringLiteral("main.png")));

    // An edit goes through the undo stack and the background build, and undoes cleanly.
    const QString probe = doc->uniqueName(pick + QStringLiteral("_selftest"));
    doc->addChildren(pick, {{probe, atlas::IRect{0, 0, 2, 2}}}, QStringLiteral("selftest"));
    waitForBuild(doc);
    bool okEdits = doc->isDirty() && doc->snapshot()->region(probe);
    doc->undoStack()->undo();
    waitForBuild(doc);
    okEdits = okEdits && doc->isDirty() == converted && !doc->snapshot()->region(probe);
    doc->setSelection({pick});

    // Adding an image (the path Add Images and drops take), undone.
    const size_t imagesBefore = doc->project().images.size();
    const QString png = QDir(outDir).filePath(QStringLiteral("selftest_add.png"));
    QImage red(12, 9, QImage::Format_ARGB32);
    red.fill(QColor(220, 30, 40));
    bool okImages = red.save(png);
    const int added = w.addImagePaths({png});
    waitForBuild(doc);
    okImages = okImages && added == 1 && doc->isImage(QStringLiteral("selftest_add")) &&
               doc->snapshot()->region(QStringLiteral("selftest_add")) && doc->project().images.size() == imagesBefore + 1;
    // Rename moves the image (one undo step), and undo brings the old name back.
    QString renameErr;
    okImages = okImages && doc->renameSprite(QStringLiteral("selftest_add"), QStringLiteral("selftest_renamed"), &renameErr) &&
               doc->isImage(QStringLiteral("selftest_renamed")) && !doc->isImage(QStringLiteral("selftest_add"));
    doc->undoStack()->undo();
    okImages = okImages && doc->isImage(QStringLiteral("selftest_add"));
    doc->undoStack()->undo();
    waitForBuild(doc);
    okImages = okImages && !doc->isImage(QStringLiteral("selftest_add")) && doc->project().images.size() == imagesBefore &&
               !doc->snapshot()->region(QStringLiteral("selftest_add")) && doc->isDirty() == converted;
    std::printf("selftest: add image/rename/undo %s\n", okImages ? "ok" : "FAILED");

    // Export Copy writes the .plist even when the project does not list it, into a folder of
    // our choice; the project itself is untouched.
    const QString exportDir = QDir(outDir).filePath(QStringLiteral("export"));
    bool exportOk = false;
    {
        QEventLoop loop;
        QObject::connect(doc, &AtlasDocument::exportFinished, &loop, [&](bool ok, const QString&) { exportOk = ok; loop.quit(); });
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        doc->exportCopy(exportDir, true);
        loop.exec();
    }
    const QString base = QString::fromStdString(doc->project().outputName(doc->project().output));
    bool okExport = exportOk && QFileInfo::exists(QDir(exportDir).filePath(base + QStringLiteral(".plist"))) &&
                    QFileInfo::exists(QDir(exportDir).filePath(base + QStringLiteral(".png")));
    for (const atlas::Variant& v : doc->project().variants) {
        const QString vdir = QDir(exportDir).filePath(QString::fromStdString(v.id));
        const QString vname = QString::fromStdString(doc->project().outputName(v.output));
        okExport = okExport && QFileInfo::exists(QDir(vdir).filePath(vname + QStringLiteral(".plist")));
    }
    okExport = okExport && doc->isDirty() == converted;

    // Save As into outDir/saveas. Variant outputs that point elsewhere are moved under outDir
    // first (in memory only), so the test never writes next to the original project.
    const QString saveDir = QDir(outDir).filePath(QStringLiteral("saveas"));
    QDir(saveDir).removeRecursively();
    doc->edit(QStringLiteral("selftest variant outputs"), [&](atlas::Project& p) {
        for (atlas::Variant& v : p.variants)
            v.output.dir = QDir(saveDir).filePath(QStringLiteral("variants/") + QString::fromStdString(v.id)).toStdString();
    });
    const QString savePath = QDir(saveDir).filePath(QString::fromStdString(doc->project().name) + QStringLiteral(".atlasproj"));
    const size_t imageCount = doc->project().images.size();
    std::vector<size_t> variantCounts;
    for (const atlas::Variant& v : doc->project().variants) variantCounts.push_back(v.images.size());
    // Reference folders must still point at the same places after the move.
    auto existingRefs = [doc] {
        QStringList out;
        for (const atlas::SourceFolder& f : doc->project().references) {
            const QFileInfo fi(doc->resolvePath(QString::fromStdString(f.path)));
            if (fi.isDir()) out << fi.canonicalFilePath();
        }
        return out;
    };
    const QStringList refsBefore = existingRefs();
    SaveReport report;
    QString saveErr;
    bool okSave = doc->save(savePath, &report, &saveErr);
    if (!okSave) std::fprintf(stderr, "selftest: save: %s\n", saveErr.toUtf8().constData());
    std::printf("selftest: save wrote %d file(s)\n", int(report.written.size()));
    okSave = okSave && !doc->isDirty() && QFileInfo(doc->filePath()) == QFileInfo(savePath);
    if (existingRefs() != refsBefore) {
        std::fprintf(stderr, "selftest: reference folders moved on Save As\n");
        okSave = false;
    }
    const QString store = doc->storeFile();
    const QString storeBase = QFileInfo(store).completeBaseName();
    okSave = okSave && QFileInfo::exists(savePath) && QFileInfo::exists(store) &&
             QFileInfo::exists(QDir(QFileInfo(store).absolutePath()).filePath(storeBase + QStringLiteral(".png"))) &&
             QFileInfo::exists(QDir(QFileInfo(store).absolutePath()).filePath(storeBase + QStringLiteral(".plist"))) &&
             QFileInfo(store).absolutePath() == QFileInfo(savePath).absolutePath();
    for (const atlas::Variant& v : doc->project().variants)
        okSave = okSave && QFileInfo::exists(doc->storeFile(QString::fromStdString(v.id)));
    // Reopen what was saved: every image comes back out of the packed atlas.
    atlas::Project reopened;
    std::string openErr;
    okSave = okSave && atlas::openProject(savePath.toStdString(), reopened, &openErr) && reopened.embedded &&
             reopened.images.size() == imageCount && reopened.variants.size() == variantCounts.size();
    for (size_t i = 0; okSave && i < variantCounts.size(); i++) okSave = reopened.variants[i].images.size() == variantCounts[i];
    if (!openErr.empty()) std::fprintf(stderr, "selftest: reopen: %s\n", openErr.c_str());
    std::printf("selftest: embedded save/reopen %s (%zu image(s) in %s)\n", okSave ? "ok" : "FAILED", reopened.images.size(),
                QDir::toNativeSeparators(savePath).toUtf8().constData());

    w.canvasPanel()->enterEditMode(pick);
    waitForBuild(doc);
    for (const SpriteInfo& s : snap->sprites)
        if (s.child && s.parent == pick) {
            doc->setSelection({s.name});
            break;
        }
    QCoreApplication::processEvents();
    const bool okEdit = w.grab().save(QDir(outDir).filePath(QStringLiteral("edit.png")));
    w.canvasPanel()->exitEditMode();

    // A variant shown, with its Properties section.
    if (!doc->project().variants.empty()) {
        doc->setVariant(QString::fromStdString(doc->project().variants.front().id));
        waitForBuild(doc);
        doc->setSelection({pick});
        QCoreApplication::processEvents();
        w.grab().save(QDir(outDir).filePath(QStringLiteral("variant.png")));
        doc->setVariant(QString());
        waitForBuild(doc);
    }

    // The Properties dock scrolled down to the Atlas output and References sections.
    if (auto* props = w.findChild<QDockWidget*>(QStringLiteral("PropertiesDock")))
        if (auto* scroll = props->findChild<QScrollArea*>()) {
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            QCoreApplication::processEvents();
            props->grab().save(QDir(outDir).filePath(QStringLiteral("properties.png")));
            scroll->verticalScrollBar()->setValue(0);
        }

    // Light theme, with the Animations dock in front.
    if (auto* anim = w.findChild<QDockWidget*>(QStringLiteral("AnimationsDock"))) anim->raise();
    Theme::apply(Theme::Mode::Light);
    QCoreApplication::processEvents();
    const bool okLight = w.grab().save(QDir(outDir).filePath(QStringLiteral("light.png")));
    Theme::apply(Theme::Mode::Dark);

    // The Import From References dialog over the window.
    {
        ReferencesDialog dlg(doc, &w);
        dlg.show();
        QCoreApplication::processEvents();
        dlg.grab().save(QDir(outDir).filePath(QStringLiteral("references.png")));
        std::printf("selftest: references dialog lists %d image(s) to import\n", int(dlg.chosen().size()));
    }

    std::printf("selftest: edit/undo %s\n", okEdits ? "ok" : "FAILED");
    std::printf("selftest: export copy (+plist) %s\n", okExport ? "ok" : "FAILED");
    std::printf("selftest: %zu sprite(s), %zu page(s), %d error(s), %d warning(s), build %lld ms\n", snap->result.regions.size(),
                snap->result.pages.size(), snap->result.errorCount(), snap->result.warningCount(), (long long)snap->elapsedMs);
    return okMain && okEdit && okLight && okEdits && okImages && okExport && okSave ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args = utf8CommandLine(argc, argv);
    if (!args.empty() && args[0] == "--headless") {
        Console::attachParent();
        const int code = atlasCliMain(std::vector<std::string>(args.begin() + 1, args.end()));
        std::fflush(stdout);
        std::fflush(stderr);
        return code;
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TOMS"));
    QApplication::setApplicationName(QStringLiteral("AtlasEditor"));
    QApplication::setApplicationDisplayName(QStringLiteral("Atlas Editor"));
    QApplication::setWindowIcon(Icons::icon(Icons::Id::Grid));
    Theme::apply(Theme::Mode::Dark);

    MainWindow w;
    // QApplication has removed its own options (-platform ...) from arguments().
    const QStringList qargs = QCoreApplication::arguments().mid(1);
    if (qargs.size() == 3 && qargs[0] == QLatin1String("--selftest")) {
        w.resize(1440, 900);
        w.show();
        Theme::apply(Theme::Mode::Dark);
        return runSelfTest(w, qargs[1], qargs[2]);
    }
    w.show();
    if (!qargs.isEmpty()) w.openProject(qargs.first());
    return app.exec();
}
