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
#include "Language.h"
#include "MainWindow.h"
#include "ReferencesDialog.h"
#include "Theme.h"
#include "atlas_cli.h"
#include "atlas_store.h"
#if TOMS_ATLAS_GAME_RENDERER
#include "AtlasCanvas.h"
#include "SpriteEditCanvas.h"
#include "bgfx_host.h"
#endif

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QMenuBar>
#include <QDateTime>
#include <QThread>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QUndoStack>

#include <cmath>
#include <cstdio>

namespace {

// Waits (running the event loop) until the document has no build pending.
void f_WaitForBuild(AtlasDocument* l_pDoc, int l_iTimeoutMs = 30000)
{
    QEventLoop l_oLoop;
    QTimer l_oTimeout;
    l_oTimeout.setSingleShot(true);
    QObject::connect(&l_oTimeout, &QTimer::timeout, &l_oLoop, &QEventLoop::quit);
    QObject::connect(l_pDoc, &AtlasDocument::f_BuildFinished, &l_oLoop, [&] {
        if (!l_pDoc->f_IsBuilding()) l_oLoop.quit();
    });
    l_oTimeout.start(l_iTimeoutMs);
    if (l_pDoc->f_IsBuilding()) l_oLoop.exec();
    QCoreApplication::processEvents();
}

// Hidden switch for automated checks (run with -platform offscreen): opens a project, waits for
// the build, exercises edits / images / undo / Export Copy / Save As (all into outDir, never next
// to the project) and saves screenshots of the atlas view, sprite edit mode and the light theme.
int f_RunSelfTest(MainWindow& l_oW, const QString& l_oProject, const QString& l_oOutDir)
{
    AtlasDocument* l_pDoc = l_oW.f_Document();
    QString l_oErr;
    if (!l_pDoc->f_Open(l_oProject, &l_oErr)) {
        std::fprintf(stderr, "selftest: %s\n", l_oErr.toUtf8().constData());
        return 2;
    }
    const bool l_bConverted = l_pDoc->f_IsDirty();   // a folder project is converted on open
    std::printf("selftest: opened %s (%s, %zu image(s))\n", l_oProject.toUtf8().constData(),
                l_bConverted ? "folder project, converted to embedded" : "embedded", l_pDoc->f_Project().m_mapImages.size());
    f_WaitForBuild(l_pDoc);
    const BuildSnapshotPtr l_spSnap = l_pDoc->f_Snapshot();
    if (!l_spSnap) return 2;
    // Show off a sprite with children when there is one.
    QString l_oPick = l_spSnap->m_vSprites.empty() ? QString() : l_spSnap->m_vSprites.front().m_oName;
    for (const SpriteInfo& l_oS : l_spSnap->m_vSprites)
        if (l_oS.m_bChild) {
            l_oPick = l_oS.m_oParent;
            break;
        }
    l_pDoc->f_SetSelection({l_oPick});
    QDir().mkpath(l_oOutDir);
    QCoreApplication::processEvents();
    const bool l_bOkMain = l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("main.png")));

    // An edit goes through the undo stack and the background build, and undoes cleanly.
    const QString l_oProbe = l_pDoc->f_UniqueName(l_oPick + QStringLiteral("_selftest"));
    l_pDoc->f_AddChildren(l_oPick, {{l_oProbe, atlas::IRect{0, 0, 2, 2}}}, QStringLiteral("selftest"));
    f_WaitForBuild(l_pDoc);
    bool l_bOkEdits = l_pDoc->f_IsDirty() && l_pDoc->f_Snapshot()->f_Region(l_oProbe);
    l_pDoc->f_UndoStack()->undo();
    f_WaitForBuild(l_pDoc);
    l_bOkEdits = l_bOkEdits && l_pDoc->f_IsDirty() == l_bConverted && !l_pDoc->f_Snapshot()->f_Region(l_oProbe);
    l_pDoc->f_SetSelection({l_oPick});

    // Adding an image (the path Add Images and drops take), undone.
    const size_t l_nImagesBefore = l_pDoc->f_Project().m_mapImages.size();
    const QString l_oPng = QDir(l_oOutDir).filePath(QStringLiteral("selftest_add.png"));
    QImage l_oRed(12, 9, QImage::Format_ARGB32);
    l_oRed.fill(QColor(220, 30, 40));
    bool l_bOkImages = l_oRed.save(l_oPng);
    const int l_iAdded = l_oW.f_AddImagePaths({l_oPng});
    f_WaitForBuild(l_pDoc);
    l_bOkImages = l_bOkImages && l_iAdded == 1 && l_pDoc->f_IsImage(QStringLiteral("selftest_add")) &&
               l_pDoc->f_Snapshot()->f_Region(QStringLiteral("selftest_add")) && l_pDoc->f_Project().m_mapImages.size() == l_nImagesBefore + 1;
    // Rename moves the image (one undo step), and undo brings the old name back.
    QString l_oRenameErr;
    l_bOkImages = l_bOkImages && l_pDoc->f_RenameSprite(QStringLiteral("selftest_add"), QStringLiteral("selftest_renamed"), &l_oRenameErr) &&
               l_pDoc->f_IsImage(QStringLiteral("selftest_renamed")) && !l_pDoc->f_IsImage(QStringLiteral("selftest_add"));
    l_pDoc->f_UndoStack()->undo();
    l_bOkImages = l_bOkImages && l_pDoc->f_IsImage(QStringLiteral("selftest_add"));
    l_pDoc->f_UndoStack()->undo();
    f_WaitForBuild(l_pDoc);
    l_bOkImages = l_bOkImages && !l_pDoc->f_IsImage(QStringLiteral("selftest_add")) && l_pDoc->f_Project().m_mapImages.size() == l_nImagesBefore &&
               !l_pDoc->f_Snapshot()->f_Region(QStringLiteral("selftest_add")) && l_pDoc->f_IsDirty() == l_bConverted;
    std::printf("selftest: add image/rename/undo %s\n", l_bOkImages ? "ok" : "FAILED");

    // Export Copy writes the .plist even when the project does not list it, into a folder of
    // our choice; the project itself is untouched.
    const QString l_oExportDir = QDir(l_oOutDir).filePath(QStringLiteral("export"));
    bool l_bExportOk = false;
    {
        QEventLoop l_oLoop;
        QObject::connect(l_pDoc, &AtlasDocument::f_ExportFinished, &l_oLoop, [&](bool l_bOk, const QString&) { l_bExportOk = l_bOk; l_oLoop.quit(); });
        QTimer::singleShot(30000, &l_oLoop, &QEventLoop::quit);
        l_pDoc->f_ExportCopy(l_oExportDir, true);
        l_oLoop.exec();
    }
    const QString l_oBase = QString::fromStdString(l_pDoc->f_Project().f_OutputName(l_pDoc->f_Project().m_oOutput));
    bool l_bOkExport = l_bExportOk && QFileInfo::exists(QDir(l_oExportDir).filePath(l_oBase + QStringLiteral(".plist"))) &&
                    QFileInfo::exists(QDir(l_oExportDir).filePath(l_oBase + QStringLiteral(".png")));
    for (const atlas::Variant& l_oV : l_pDoc->f_Project().m_vVariants) {
        const QString l_oVdir = QDir(l_oExportDir).filePath(QString::fromStdString(l_oV.m_strId));
        const QString l_oVname = QString::fromStdString(l_pDoc->f_Project().f_OutputName(l_oV.m_oOutput));
        l_bOkExport = l_bOkExport && QFileInfo::exists(QDir(l_oVdir).filePath(l_oVname + QStringLiteral(".plist")));
    }
    l_bOkExport = l_bOkExport && l_pDoc->f_IsDirty() == l_bConverted;

    // Save As into outDir/saveas. Variant outputs that point elsewhere are moved under outDir
    // first (in memory only), so the test never writes next to the original project.
    const QString l_oSaveDir = QDir(l_oOutDir).filePath(QStringLiteral("saveas"));
    QDir(l_oSaveDir).removeRecursively();
    l_pDoc->f_Edit(QStringLiteral("selftest variant outputs"), [&](atlas::Project& l_oP) {
        for (atlas::Variant& l_oV : l_oP.m_vVariants)
            l_oV.m_oOutput.m_strDir = QDir(l_oSaveDir).filePath(QStringLiteral("variants/") + QString::fromStdString(l_oV.m_strId)).toStdString();
    });
    const QString l_oSavePath = QDir(l_oSaveDir).filePath(QString::fromStdString(l_pDoc->f_Project().m_strName) + QStringLiteral(".atlasproj"));
    const size_t l_nImageCount = l_pDoc->f_Project().m_mapImages.size();
    std::vector<size_t> l_vVariantCounts;
    for (const atlas::Variant& l_oV : l_pDoc->f_Project().m_vVariants) l_vVariantCounts.push_back(l_oV.m_mapImages.size());
    // Reference folders must still point at the same places after the move.
    auto l_fnExistingRefs = [l_pDoc] {
        QStringList l_oOut;
        for (const atlas::SourceFolder& l_oF : l_pDoc->f_Project().m_vReferences) {
            const QFileInfo l_oFi(l_pDoc->f_ResolvePath(QString::fromStdString(l_oF.m_strPath)));
            if (l_oFi.isDir()) l_oOut << l_oFi.canonicalFilePath();
        }
        return l_oOut;
    };
    const QStringList l_oRefsBefore = l_fnExistingRefs();
    SaveReport l_oReport;
    QString l_oSaveErr;
    bool l_bOkSave = l_pDoc->f_Save(l_oSavePath, &l_oReport, &l_oSaveErr);
    if (!l_bOkSave) std::fprintf(stderr, "selftest: save: %s\n", l_oSaveErr.toUtf8().constData());
    std::printf("selftest: save wrote %d file(s)\n", int(l_oReport.m_oWritten.size()));
    l_bOkSave = l_bOkSave && !l_pDoc->f_IsDirty() && QFileInfo(l_pDoc->f_FilePath()) == QFileInfo(l_oSavePath);
    if (l_fnExistingRefs() != l_oRefsBefore) {
        std::fprintf(stderr, "selftest: reference folders moved on Save As\n");
        l_bOkSave = false;
    }
    const QString l_oStore = l_pDoc->f_StoreFile();
    const QString l_oStoreBase = QFileInfo(l_oStore).completeBaseName();
    l_bOkSave = l_bOkSave && QFileInfo::exists(l_oSavePath) && QFileInfo::exists(l_oStore) &&
             QFileInfo::exists(QDir(QFileInfo(l_oStore).absolutePath()).filePath(l_oStoreBase + QStringLiteral(".png"))) &&
             QFileInfo::exists(QDir(QFileInfo(l_oStore).absolutePath()).filePath(l_oStoreBase + QStringLiteral(".plist"))) &&
             QFileInfo(l_oStore).absolutePath() == QFileInfo(l_oSavePath).absolutePath();
    for (const atlas::Variant& l_oV : l_pDoc->f_Project().m_vVariants)
        l_bOkSave = l_bOkSave && QFileInfo::exists(l_pDoc->f_StoreFile(QString::fromStdString(l_oV.m_strId)));
    // Reopen what was saved: every image comes back out of the packed atlas.
    atlas::Project l_oReopened;
    std::string l_strOpenErr;
    l_bOkSave = l_bOkSave && atlas::f_OpenProject(l_oSavePath.toStdString(), l_oReopened, &l_strOpenErr) && l_oReopened.m_bEmbedded &&
             l_oReopened.m_mapImages.size() == l_nImageCount && l_oReopened.m_vVariants.size() == l_vVariantCounts.size();
    for (size_t l_nI = 0; l_bOkSave && l_nI < l_vVariantCounts.size(); l_nI++) l_bOkSave = l_oReopened.m_vVariants[l_nI].m_mapImages.size() == l_vVariantCounts[l_nI];
    if (!l_strOpenErr.empty()) std::fprintf(stderr, "selftest: reopen: %s\n", l_strOpenErr.c_str());
    std::printf("selftest: embedded save/reopen %s (%zu image(s) in %s)\n", l_bOkSave ? "ok" : "FAILED", l_oReopened.m_mapImages.size(),
                QDir::toNativeSeparators(l_oSavePath).toUtf8().constData());

    // Backup copy: a whole project + packed atlas in its own folder (written in the background);
    // the project's own packed atlas is not touched.
    bool l_bOkBackup = false;
    {
        const QDateTime l_oStoreTime = QFileInfo(l_oStore).lastModified();
        const QString l_oBdir = QDir(l_oOutDir).filePath(QStringLiteral("backup"));
        QDir(l_oBdir).removeRecursively();
        QString l_oBerr;
        l_bOkBackup = l_pDoc->f_WriteBackup(QDir(l_oBdir).filePath(QStringLiteral("proj_1.atlasproj")), &l_oBerr);
        const QString l_oBproj = QDir(QDir(l_oBdir).filePath(QStringLiteral("proj_1"))).filePath(QString::fromStdString(l_pDoc->f_Project().m_strName) +
                                                                                           QStringLiteral(".atlasproj"));
        atlas::Project l_oBack;
        QElapsedTimer l_oT;
        l_oT.start();
        bool l_bLoaded = false;
        while (l_bOkBackup && !l_bLoaded && l_oT.elapsed() < 30000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(50);
            atlas::Project l_oP;
            std::string l_strE;
            l_bLoaded = QFileInfo::exists(l_oBproj) && atlas::f_OpenProject(l_oBproj.toStdString(), l_oP, &l_strE) && l_oP.m_mapImages.size() == l_nImageCount;
            if (l_bLoaded) l_oBack = std::move(l_oP);
        }
        QThread::msleep(300);   // the rest of the background save
        l_bOkBackup = l_bOkBackup && l_bLoaded && QFileInfo(l_oStore).lastModified() == l_oStoreTime;
        std::printf("selftest: backup copy %s (%zu image(s), the project's own atlas untouched)\n", l_bOkBackup ? "ok" : "FAILED",
                    l_oBack.m_mapImages.size());
    }

    l_oW.f_CanvasPanel()->f_EnterEditMode(l_oPick);
    f_WaitForBuild(l_pDoc);
    for (const SpriteInfo& l_oS : l_spSnap->m_vSprites)
        if (l_oS.m_bChild && l_oS.m_oParent == l_oPick) {
            l_pDoc->f_SetSelection({l_oS.m_oName});
            break;
        }
    QCoreApplication::processEvents();
    const bool l_bOkEdit = l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("edit.png")));
    l_oW.f_CanvasPanel()->f_ExitEditMode();

    // A variant shown, with its Properties section.
    if (!l_pDoc->f_Project().m_vVariants.empty()) {
        l_pDoc->f_SetVariant(QString::fromStdString(l_pDoc->f_Project().m_vVariants.front().m_strId));
        f_WaitForBuild(l_pDoc);
        l_pDoc->f_SetSelection({l_oPick});
        QCoreApplication::processEvents();
        l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("variant.png")));
        l_pDoc->f_SetVariant(QString());
        f_WaitForBuild(l_pDoc);
    }

    // The Properties dock scrolled down to the Atlas output and References sections.
    if (auto* l_pProps = l_oW.findChild<QDockWidget*>(QStringLiteral("PropertiesDock")))
        if (auto* l_pScroll = l_pProps->findChild<QScrollArea*>()) {
            l_pScroll->verticalScrollBar()->setValue(l_pScroll->verticalScrollBar()->maximum());
            QCoreApplication::processEvents();
            l_pProps->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("properties.png")));
            l_pScroll->verticalScrollBar()->setValue(0);
        }

    // Light theme, with the Animations dock in front.
    if (auto* l_pAnim = l_oW.findChild<QDockWidget*>(QStringLiteral("AnimationsDock"))) l_pAnim->raise();
    Theme::f_Apply(Theme::Mode::Light);
    QCoreApplication::processEvents();
    const bool l_bOkLight = l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("light.png")));
    Theme::f_Apply(Theme::Mode::Dark);

    // The Import From References dialog over the window.
    {
        ReferencesDialog l_oDlg(l_pDoc, &l_oW);
        l_oDlg.show();
        QCoreApplication::processEvents();
        l_oDlg.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("references.png")));
        std::printf("selftest: references dialog lists %d image(s) to import\n", int(l_oDlg.f_Chosen().size()));
    }

    // English / 中文 on the real window: menus, docks and toolbar change in place, and back.
    // TOMS_LANGUAGE set: the switch is not saved (the user's own choice stays).
    qputenv("TOMS_LANGUAGE", "en");
    auto l_fnMenuTitle = [&](int l_iI) {
        const QList<QAction*> l_oActs = l_oW.menuBar()->actions();
        return l_iI < l_oActs.size() ? l_oActs[l_iI]->text() : QString();
    };
    auto l_fnDockTitle = [&](const char* l_pszName) {
        for (QDockWidget* l_pD : l_oW.findChildren<QDockWidget*>())
            if (l_pD->windowTitle() == QString::fromUtf8(l_pszName)) return true;
        return false;
    };
    Language::f_Set(Language::Lang::Chinese);
    QCoreApplication::processEvents();
    const bool l_bOkZh = l_fnMenuTitle(0) == QStringLiteral("檔案(&F)") && l_fnMenuTitle(1) == QStringLiteral("編輯(&E)") &&
                      l_fnDockTitle("精靈圖") && l_fnDockTitle("屬性");
    l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("chinese.png")));
    Language::f_Set(Language::Lang::English);
    QCoreApplication::processEvents();
    const bool l_bOkEn = l_fnMenuTitle(0) == QStringLiteral("&File") && l_fnDockTitle("Sprites") && l_fnDockTitle("Properties");
    std::printf("selftest: language 中文 %s, back to English %s\n", l_bOkZh ? "ok" : "FAILED", l_bOkEn ? "ok" : "FAILED");
    if (!l_bOkZh) std::printf("selftest: menus after the switch: '%s' '%s'\n", l_fnMenuTitle(0).toUtf8().constData(),
                           l_fnMenuTitle(1).toUtf8().constData());

    std::printf("selftest: edit/undo %s\n", l_bOkEdits ? "ok" : "FAILED");
    std::printf("selftest: export copy (+plist) %s\n", l_bOkExport ? "ok" : "FAILED");
    std::printf("selftest: %zu sprite(s), %zu page(s), %d error(s), %d warning(s), build %lld ms\n", l_spSnap->m_oResult.m_vRegions.size(),
                l_spSnap->m_oResult.m_vPages.size(), l_spSnap->m_oResult.f_ErrorCount(), l_spSnap->m_oResult.f_WarningCount(), (long long)l_spSnap->m_i64ElapsedMs);
    return l_bOkMain && l_bOkEdit && l_bOkLight && l_bOkEdits && l_bOkImages && l_bOkExport && l_bOkSave && l_bOkBackup && l_bOkZh && l_bOkEn ? 0 : 1;
}

}  // namespace

#if TOMS_ATLAS_GAME_RENDERER
// Right-drag on a canvas: it must pan (the content moves with the mouse), as middle-drag does.
static bool f_RightDragPans(CanvasView* l_pView)
{
    const QPointF l_oBefore = l_pView->f_ContentToWidget(QPointF(0, 0));
    const QPointF l_oA(l_pView->width() / 2.0, l_pView->height() / 2.0), l_oB = l_oA + QPointF(60, 25);
    auto l_fnSend = [&](QEvent::Type l_eType, const QPointF& l_oAt, Qt::MouseButton l_eButton, Qt::MouseButtons l_oButtons) {
        QMouseEvent l_oE(l_eType, l_oAt, l_pView->mapToGlobal(l_oAt), l_eButton, l_oButtons, Qt::NoModifier);
        QApplication::sendEvent(l_pView, &l_oE);
    };
    l_fnSend(QEvent::MouseButtonPress, l_oA, Qt::RightButton, Qt::RightButton);
    for (int l_iI = 1; l_iI <= 4; l_iI++) l_fnSend(QEvent::MouseMove, l_oA + (l_oB - l_oA) * (l_iI / 4.0), Qt::NoButton, Qt::RightButton);
    l_fnSend(QEvent::MouseButtonRelease, l_oB, Qt::RightButton, Qt::NoButton);
    QCoreApplication::processEvents();
    const QPointF l_oMoved = l_pView->f_ContentToWidget(QPointF(0, 0)) - l_oBefore;
    return std::fabs(l_oMoved.x() - 60) < 1 && std::fabs(l_oMoved.y() - 25) < 1;
}

// The canvases with the game renderer (on screen): the page, then sprite edit mode (bgfx moves to
// the other canvas of the stack), then the page again; a bgfx screenshot of each.
int f_RunGpuSelfTest(MainWindow& l_oW, const QString& l_oProject, const QString& l_oOutDir)
{
    QDir().mkpath(l_oOutDir);
    AtlasDocument* l_pDoc = l_oW.f_Document();
    QString l_oErr;
    if (!l_pDoc->f_Open(l_oProject, &l_oErr)) {
        std::fprintf(stderr, "selftest: %s\n", l_oErr.toUtf8().constData());
        return 2;
    }
    f_WaitForBuild(l_pDoc);
    auto l_fnPump = [](int l_iMs) {
        QElapsedTimer l_oT;
        l_oT.start();
        while (l_oT.elapsed() < l_iMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    };
    auto l_fnShot = [&](GameCanvasView* l_pView, const char* l_pszName) {
        const int l_iBefore = toms::next::f_BgfxHostScreenshotsWritten();
        l_pView->f_SaveGameRendererShot(QDir(l_oOutDir).filePath(QString::fromLatin1(l_pszName)));
        QElapsedTimer l_oT;
        l_oT.start();
        while (toms::next::f_BgfxHostScreenshotsWritten() == l_iBefore && l_oT.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        return toms::next::f_BgfxHostScreenshotsWritten() > l_iBefore;
    };
    bool l_bOk = true;
    auto l_fnCheck = [&](bool l_bGood, const char* l_pszWhat) {
        std::printf("selftest: %-60s %s\n", l_pszWhat, l_bGood ? "ok" : "FAILED");
        l_bOk &= l_bGood;
    };
    CanvasPanel* l_pPanel = l_oW.f_CanvasPanel();
    l_fnPump(600);
    l_fnCheck(l_pPanel->f_AtlasCanvas()->f_UsingGameRenderer(), "the atlas canvas runs on the game renderer");
    std::printf("selftest:   %s\n", l_pPanel->f_AtlasCanvas()->f_RendererName().toUtf8().constData());
    if (!l_pPanel->f_AtlasCanvas()->f_UsingGameRenderer()) return 1;
    l_fnCheck(l_fnShot(l_pPanel->f_AtlasCanvas(), "atlas_gpu.png"), "atlas_gpu.png (the page, bgfx screenshot)");
    l_fnCheck(f_RightDragPans(l_pPanel->f_AtlasCanvas()), "right-drag pans the page canvas");
    const QString l_oSprite = QString::fromStdString(l_pDoc->f_Snapshot()->m_oResult.m_vRegions.front().m_strName);
    l_pPanel->f_EnterEditMode(l_oSprite);
    l_fnPump(600);
    l_fnCheck(l_pPanel->f_EditCanvas()->f_UsingGameRenderer() && l_pPanel->f_EditCanvas()->f_FramesDrawn() > 0,
          "sprite edit mode: bgfx moved to the edit canvas");
    l_fnCheck(l_fnShot(l_pPanel->f_EditCanvas(), "edit_gpu.png"), "edit_gpu.png (the sprite)");
    l_fnCheck(f_RightDragPans(l_pPanel->f_EditCanvas()), "right-drag pans the sprite edit canvas");
    l_pPanel->f_ExitEditMode();
    const int l_iBefore = l_pPanel->f_AtlasCanvas()->f_FramesDrawn();
    l_fnPump(600);
    l_fnCheck(l_pPanel->f_AtlasCanvas()->f_FramesDrawn() > l_iBefore, "back to the page: bgfx draws the atlas canvas again");
    l_fnCheck(l_fnShot(l_pPanel->f_AtlasCanvas(), "atlas_back.png"), "atlas_back.png");
    std::printf("selftest: %s\n", l_bOk ? "ok" : "FAILED");
    return l_bOk ? 0 : 1;
}
#endif

int main(int l_iArgc, char** l_pArgv)
{
    const std::vector<std::string> l_vArgs = f_Utf8CommandLine(l_iArgc, l_pArgv);
    if (!l_vArgs.empty() && l_vArgs[0] == "--headless") {
        Console::f_AttachParent();
        const int l_iCode = f_AtlasCliMain(std::vector<std::string>(l_vArgs.begin() + 1, l_vArgs.end()));
        std::fflush(stdout);
        std::fflush(stderr);
        return l_iCode;
    }

    QApplication l_oApp(l_iArgc, l_pArgv);
    QApplication::setOrganizationName(QStringLiteral("TOMS"));
    QApplication::setApplicationName(QStringLiteral("AtlasEditor"));
    QApplication::setApplicationDisplayName(QStringLiteral("Atlas Editor"));
    QApplication::setWindowIcon(Icons::f_Icon(Icons::Id::Grid));
    Theme::f_Apply(Theme::Mode::Dark);
    Language::f_Init();   // English / 中文 (tools/studio_common/Language.h), before any window

    MainWindow l_oW;
    // QApplication has removed its own options (-platform ...) from arguments().
    const QStringList l_oQargs = QCoreApplication::arguments().mid(1);
#if TOMS_ATLAS_GAME_RENDERER
    if (l_oQargs.size() == 3 && l_oQargs[0] == QLatin1String("--selftest-gpu")) {   // on screen: the game renderer
        l_oW.resize(1440, 900);
        l_oW.show();
        return f_RunGpuSelfTest(l_oW, l_oQargs[1], l_oQargs[2]);
    }
#endif
    if (l_oQargs.size() == 3 && l_oQargs[0] == QLatin1String("--selftest")) {
        l_oW.resize(1440, 900);
        l_oW.show();
        Theme::f_Apply(Theme::Mode::Dark);
        return f_RunSelfTest(l_oW, l_oQargs[1], l_oQargs[2]);
    }
    l_oW.show();
    if (!l_oQargs.isEmpty()) l_oW.f_OpenProject(l_oQargs.first());
    return l_oApp.exec();
}
