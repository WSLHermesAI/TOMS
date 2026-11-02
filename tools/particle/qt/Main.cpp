// particle_editor -- the Qt editor for .particle effects (docs/engine/07_PARTICLES.md):
//
//   particle_editor                                    the editor
//   particle_editor file.particle                      the editor with that file open
//   particle_editor --headless check file.particle     parse + check (sprites vs atlases, curves, pools); exit 0 / 2
//   particle_editor --headless render file.particle#effect out.png [frames] [every]
//                                                      a contact sheet: `frames` moments (8), `every` s apart (0.15)
//   particle_editor --selftest file.particle outdir    automated check (-platform offscreen; QPainter preview)
//   particle_editor --selftest-gpu file.particle outdir  the same for the game-renderer preview (on screen)
#include "Console.h"
#include "EffectsDock.h"
#include "FxSpritesDock.h"
#include "Icons.h"
#include "Language.h"
#include "InspectorDock.h"
#include "ParticleDocument.h"
#include "ParticleEditor.h"
#include "ParticleMainWindow.h"
#include "ParticlePlayback.h"
#include "ParticleViewport.h"
#include "ParticleWidgets.h"
#include "Theme.h"
#include "TimelineDock.h"
#include "bgfx_host.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTreeWidget>
#include <QUndoStack>

#include <cmath>
#include <cstdio>

using toms::fx::Emitter;

namespace {

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }

// ---- headless ------------------------------------------------------------------------------------

bool f_ReadFile(const QString& l_oPath, std::string& l_strOut)
{
    QFile l_oF(l_oPath);
    if (!l_oF.open(QIODevice::ReadOnly)) return false;
    l_strOut = l_oF.readAll().toStdString();
    return true;
}

int f_HeadlessCheck(const QString& l_oPath)
{
    std::string l_strText, l_strErr;
    toms::fx::ParticleFile l_oFile;
    if (!f_ReadFile(l_oPath, l_strText)) {
        std::fprintf(stderr, "%s: cannot read\n", l_oPath.toUtf8().constData());
        return 2;
    }
    if (!toms::fx::f_ParseParticles(l_strText, l_oFile, &l_strErr)) {
        std::fprintf(stderr, "%s: %s\n", l_oPath.toUtf8().constData(), l_strErr.c_str());
        return 2;
    }
    std::vector<std::unique_ptr<toms::AtlasFile>> l_vAtlases;
    toms::anim::AtlasSet l_oSet;
    int l_iErrors = 0, l_iWarnings = 0;
    const QDir l_oDir = QFileInfo(l_oPath).absoluteDir();
    for (const toms::anim::AtlasRef& l_oR : l_oFile.m_vAtlases) {
        auto l_upA = std::make_unique<toms::AtlasFile>();
        std::string l_strAtext;
        if (!f_ReadFile(l_oDir.absoluteFilePath(f_Qs(l_oR.m_strPath)), l_strAtext) || !toms::f_ParseAtlas(l_strAtext, *l_upA, &l_strErr)) {
            std::printf("error: atlas %s (%s) does not load\n", l_oR.m_strPath.c_str(), l_oR.m_strId.c_str());
            l_iErrors++;
            continue;
        }
        l_oSet.f_Add(*l_upA, l_oR.m_strId);
        l_vAtlases.push_back(std::move(l_upA));
    }
    for (const toms::fx::Problem& l_oP : toms::fx::f_CheckParticles(l_oFile, &l_oSet)) {
        std::printf("%s: %s: %s\n", l_oP.m_bWarning ? "warning" : "error", l_oP.m_strWhere.c_str(), l_oP.m_strText.c_str());
        (l_oP.m_bWarning ? l_iWarnings : l_iErrors)++;
    }
    std::printf("%s: %zu effect(s), %zu atlas(es), %d error(s), %d warning(s)\n", l_oPath.toUtf8().constData(), l_oFile.m_vEffects.size(),
                l_oFile.m_vAtlases.size(), l_iErrors, l_iWarnings);
    return l_iErrors ? 2 : 0;
}

// A contact sheet: the effect at `frames` moments, `every` seconds apart, seed 1, one 256 px cell each.
int f_HeadlessRender(const QString& l_oTarget, const QString& l_oOut, int l_iFrames, float l_fEvery)
{
    const int l_iHash = int(l_oTarget.lastIndexOf(QLatin1Char('#')));
    if (l_iHash < 0) {
        std::fprintf(stderr, "render: expected file.particle#effect\n");
        return 3;
    }
    ParticleDocument l_oDoc;
    QString l_oErr;
    if (!l_oDoc.f_Open(l_oTarget.left(l_iHash), &l_oErr)) {
        std::fprintf(stderr, "render: %s\n", l_oErr.toUtf8().constData());
        return 2;
    }
    const toms::fx::Effect* l_pE = l_oDoc.f_File().f_Find(l_oTarget.mid(l_iHash + 1).toStdString());
    if (!l_pE) {
        std::fprintf(stderr, "render: no effect '%s'\n", l_oTarget.mid(l_iHash + 1).toUtf8().constData());
        return 2;
    }
    const int l_iCell = 256, l_iCols = std::min(l_iFrames, 4), l_iRows = (l_iFrames + l_iCols - 1) / l_iCols;
    QImage l_oSheet(l_iCols * l_iCell, l_iRows * l_iCell, QImage::Format_ARGB32_Premultiplied);
    l_oSheet.fill(QColor(24, 24, 30));
    QPainter l_oP(&l_oSheet);
    toms::fx::EffectInstance l_oFx;
    l_oFx.f_Play(l_pE, 1);
    std::vector<Quad> l_vQuads;
    for (int l_iI = 0; l_iI < l_iFrames; l_iI++) {
        const float l_fT = l_iI * l_fEvery;
        l_oFx.f_Seek(l_fT);
        l_vQuads.clear();
        l_oFx.f_AppendQuads(l_oDoc.f_AtlasSet(), glm::mat3(1.0f), nullptr, l_vQuads);
        const QPointF l_oOrigin((l_iI % l_iCols) * l_iCell + l_iCell / 2.0, (l_iI / l_iCols) * l_iCell + l_iCell / 2.0);
        l_oP.setClipRect(QRectF((l_iI % l_iCols) * l_iCell, (l_iI / l_iCols) * l_iCell, l_iCell, l_iCell));
        ParticleViewport::f_DrawQuads(l_oP, l_vQuads, l_oDoc, QTransform::fromTranslate(l_oOrigin.x(), l_oOrigin.y()));
        l_oP.setClipping(false);
        l_oP.setPen(QColor(150, 150, 160));
        l_oP.drawRect(QRectF((l_iI % l_iCols) * l_iCell, (l_iI / l_iCols) * l_iCell, l_iCell - 1, l_iCell - 1));
        l_oP.drawText(QPointF((l_iI % l_iCols) * l_iCell + 6, (l_iI / l_iCols) * l_iCell + 16), QStringLiteral("t=%1 s  %2 live").arg(l_fT, 0, 'f', 2).arg(l_oFx.f_LiveCount()));
    }
    l_oP.end();
    if (!l_oSheet.save(l_oOut)) {
        std::fprintf(stderr, "render: cannot write %s\n", l_oOut.toUtf8().constData());
        return 2;
    }
    std::printf("%s: %d frame(s) of %s\n", l_oOut.toUtf8().constData(), l_iFrames, l_oTarget.toUtf8().constData());
    return 0;
}

// ---- selftest ------------------------------------------------------------------------------------

void f_ProcessEvents()
{
    for (int l_iI = 0; l_iI < 3; l_iI++) QCoreApplication::processEvents();
}

bool f_Check(bool l_bOk, const char* l_pszWhat)
{
    std::printf("selftest: %-64s %s\n", l_pszWhat, l_bOk ? "ok" : "FAILED");
    return l_bOk;
}

void f_Mouse(QWidget* l_pW, QEvent::Type l_eType, const QPointF& l_oAt, Qt::MouseButton l_eButton, Qt::MouseButtons l_oButtons,
           Qt::KeyboardModifiers l_oMods = Qt::NoModifier)
{
    QMouseEvent l_oE(l_eType, l_oAt, l_pW->mapToGlobal(l_oAt), l_eButton, l_oButtons, l_oMods);
    QApplication::sendEvent(l_pW, &l_oE);
}

void f_Drag(QWidget* l_pW, const QPointF& l_oFrom, const QPointF& l_oTo, Qt::MouseButton l_eButton = Qt::LeftButton, Qt::KeyboardModifiers l_oMods = Qt::NoModifier)
{
    f_Mouse(l_pW, QEvent::MouseButtonPress, l_oFrom, l_eButton, l_eButton, l_oMods);
    for (int l_iI = 1; l_iI <= 4; l_iI++) f_Mouse(l_pW, QEvent::MouseMove, l_oFrom + (l_oTo - l_oFrom) * (l_iI / 4.0), Qt::NoButton, l_eButton, l_oMods);
    f_Mouse(l_pW, QEvent::MouseButtonRelease, l_oTo, l_eButton, Qt::NoButton, l_oMods);
    f_ProcessEvents();
}

bool f_SelectEmitter(ParticleDocument* l_pDoc, const char* l_pszEffect, const char* l_pszEmitter)
{
    const auto& l_vEs = l_pDoc->f_File().m_vEffects;
    for (int l_iI = 0; l_iI < int(l_vEs.size()); l_iI++)
        if (l_vEs[size_t(l_iI)].m_strName == l_pszEffect)
            for (int l_iJ = 0; l_iJ < int(l_vEs[size_t(l_iI)].m_vEmitters.size()); l_iJ++)
                if (l_vEs[size_t(l_iI)].m_vEmitters[size_t(l_iJ)].m_strName == l_pszEmitter) {
                    l_pDoc->f_Select(l_iI, l_iJ);
                    f_ProcessEvents();
                    return true;
                }
    return false;
}

int f_RunSelfTest(ParticleMainWindow& l_oW, const QString& l_oFile, const QString& l_oOutDir)
{
    qApp->setProperty("toms.selftest", true);
    QDir().mkpath(l_oOutDir);
    ParticleEditor* l_pEd = l_oW.f_Editor();
    ParticleDocument* l_pDoc = l_pEd->f_Document();
    ParticlePlayback* l_pPlay = l_pEd->f_Playback();
    ParticleViewport* l_pView = l_pEd->f_Viewport();
    InspectorDock* l_pInsp = l_pEd->f_Inspector();
    bool l_bOk = true;

    l_bOk &= f_Check(l_pEd->f_OpenFile(l_oFile), "open the recipes");
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_File().m_vEffects.size() >= 11 && l_pDoc->f_AtlasCount() == 2, "11 effects, 2 atlases");
    l_bOk &= f_Check(l_pDoc->f_Problems().empty() && l_pEd->f_ProblemsList()->count() >= 1, "no problems (the list shows only the log line)");
    l_bOk &= f_Check(l_pEd->f_EffectsDock()->f_Tree()->topLevelItemCount() == int(l_pDoc->f_File().m_vEffects.size()), "Effects dock lists every effect");

    // The preview: torch_fire's flames, paused at 1 s.
    l_bOk &= f_Check(f_SelectEmitter(l_pDoc, "torch_fire", "flames"), "select torch_fire / flames");
    l_pPlay->f_Pause();
    l_pPlay->f_Seek(1.0f);
    l_oW.grab();   // paints the viewport
    l_bOk &= f_Check(l_pPlay->f_Instance().f_LiveCount() > 20 && !l_pView->f_LastQuads().empty() && std::fabs(l_pPlay->f_Time() - 1.0f) < 0.02f,
                "paused at 1 s: particles live and drawn");
    const std::vector<Quad> l_vFirst = l_pView->f_LastQuads();
    l_pPlay->f_Seek(1.0f);
    l_oW.grab();
    bool l_bSame = l_vFirst.size() == l_pView->f_LastQuads().size();
    for (size_t l_nI = 0; l_bSame && l_nI < l_vFirst.size(); l_nI++) l_bSame = std::fabs(l_vFirst[l_nI].m_arrCorners[0] - l_pView->f_LastQuads()[l_nI].m_arrCorners[0]) < 1e-4f;
    l_bOk &= f_Check(l_bSame, "seek(1) twice: the same particles (seeded)");
    l_bOk &= f_Check(l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("main.png"))), "main.png");

    // Inspector edits: one undo step each.
    const int l_iBase = l_pDoc->f_UndoStack()->index();
    l_pInsp->f_RateSpin()->setValue(80);
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_Emitter()->m_fRate == 80 && l_pDoc->f_UndoStack()->index() == l_iBase + 1, "Inspector: rate 80 (1 step)");
    l_bOk &= f_Check(std::fabs(l_pPlay->f_Time() - 1.0f) < 0.02f, "the preview stays at 1 s after an edit");
    l_pInsp->f_SizeEdit()->f_MinSpin()->setValue(30);
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_Emitter()->m_oSize.m_fMin == 30, "Inspector: size min 30");
    l_pInsp->f_SizeCurve()->f_AddKey(0.5f, 2.0f);
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_Emitter()->m_oSizeOverLife.m_vKeys.size() == 3, "size curve: a key added (3 keys)");
    l_pInsp->f_ColorCurve()->f_AddStop(0.4f);
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_Emitter()->m_oColorOverLife.m_vKeys.size() == 5, "colour gradient: a stop added (5)");
    l_pInsp->f_BlendCombo()->setCurrentIndex(2);
    emit l_pInsp->f_BlendCombo()->activated(2);
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_Emitter()->m_oBlend == toms::fx::Blend::f_Multiply(), "Blend: multiply");
    l_pInsp->f_SimulationCombo()->setCurrentIndex(2);
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_Emitter()->m_eSimulation == toms::fx::Emitter::Simulation::Gpu &&
                    l_pDoc->f_ToJson().contains(QStringLiteral("\"simulation\": \"gpu\"")),
                "Simulation: GPU (stored in the file)");
    l_pInsp->f_ShapeCombo()->setCurrentIndex(int(toms::fx::Shape::Box));
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_Emitter()->m_oShape.m_eType == toms::fx::Shape::Box && l_pDoc->f_Emitter()->m_oShape.m_fWidth > 0, "Shape: box (with a size)");
    l_bOk &= f_Check(l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("inspector.png"))), "inspector.png");

    // Gizmo: drag the move square 40 px right, the arrow to straight right.
    l_oW.grab();
    const glm::vec2 l_v2Off0 = l_pDoc->f_Emitter()->m_v2Offset;
    const int l_iBefore = l_pDoc->f_UndoStack()->index();
    const QPointF l_oMv = l_pView->f_HandlePos(ParticleViewport::Handle::Move);
    f_Drag(l_pView, l_oMv, l_oMv + QPointF(40, 0));
    const float l_fMoved = l_pDoc->f_Emitter()->m_v2Offset.x - l_v2Off0.x;
    l_bOk &= f_Check(l_fMoved > 5 && std::fabs(l_pDoc->f_Emitter()->m_v2Offset.y - l_v2Off0.y) < 1 && l_pDoc->f_UndoStack()->index() == l_iBefore + 1,
                "gizmo: drag the centre moves the emitter (1 step)");
    l_oW.grab();
    const QPointF l_oDir = l_pView->f_HandlePos(ParticleViewport::Handle::Direction);
    const QPointF l_oCentre = l_pView->f_HandlePos(ParticleViewport::Handle::Move);
    f_Drag(l_pView, l_oDir, l_oCentre + QPointF(80, 0));
    l_bOk &= f_Check(std::fabs(l_pDoc->f_Emitter()->m_fDirection) < 1.5f, "gizmo: drag the arrow to the right: direction 0");
    l_oW.grab();
    const QPointF l_oSpread = l_pView->f_HandlePos(ParticleViewport::Handle::SpreadB);
    f_Drag(l_pView, l_oSpread, l_oCentre + QPointF(0, 80));
    l_bOk &= f_Check(std::fabs(l_pDoc->f_Emitter()->m_fSpread - 90) < 2, "gizmo: drag a spread end straight down: spread 90");

    // Right-drag pans (no context menu); a click fires the effect there.
    l_oW.grab();
    const QPointF l_oH0 = l_pView->f_HandlePos(ParticleViewport::Handle::Move);
    f_Drag(l_pView, QPointF(50, 50), QPointF(110, 70), Qt::RightButton);
    l_oW.grab();
    const QPointF l_oH1 = l_pView->f_HandlePos(ParticleViewport::Handle::Move);
    l_bOk &= f_Check(std::fabs(l_oH1.x() - l_oH0.x() - 60) < 1 && std::fabs(l_oH1.y() - l_oH0.y() - 20) < 1, "right-drag pans the view");
    const QPointF l_oO0 = l_pPlay->f_Origin();
    f_Mouse(l_pView, QEvent::MouseButtonPress, QPointF(60, 400), Qt::LeftButton, Qt::LeftButton);
    f_Mouse(l_pView, QEvent::MouseButtonRelease, QPointF(60, 400), Qt::LeftButton, Qt::NoButton);
    f_ProcessEvents();
    l_bOk &= f_Check(l_pPlay->f_Origin() != l_oO0 && l_pPlay->f_Playing(), "click: the effect fires there");
    l_pPlay->f_SetOrigin(QPointF(0, 0), false);
    l_pPlay->f_Pause();

    // Timeline: click the ruler at the middle = seek there, paused.
    TimelineView* l_pTl = l_pEd->f_Timeline()->f_View();
    const float l_fSpan = l_pTl->f_Span();
    f_Mouse(l_pTl, QEvent::MouseButtonPress, QPointF(l_pTl->f_XOf(l_fSpan / 2), 8), Qt::LeftButton, Qt::LeftButton);
    f_Mouse(l_pTl, QEvent::MouseButtonRelease, QPointF(l_pTl->f_XOf(l_fSpan / 2), 8), Qt::LeftButton, Qt::NoButton);
    f_ProcessEvents();
    l_bOk &= f_Check(!l_pPlay->f_Playing() && std::fabs(l_pPlay->f_Time() - l_fSpan / 2) < 0.05f, "timeline ruler: seek to the middle, paused");
    // Drag the selected emitter's bar end to 1.5 s.
    const int l_iLane = l_pDoc->f_EmitterIndex();
    const QRectF l_oBar = l_pTl->f_BarRect(l_iLane);
    f_Drag(l_pTl, QPointF(l_oBar.right() - 1, l_oBar.center().y()), QPointF(l_pTl->f_XOf(1.5f), l_oBar.center().y()));
    l_bOk &= f_Check(std::fabs(l_pDoc->f_Emitter()->m_fStop - 1.5f) < 0.03f, "timeline: drag the bar's end: stop 1.5 s");

    // Undo everything since the file opened.
    while (l_pDoc->f_UndoStack()->index() > l_iBase) l_pDoc->f_UndoStack()->undo();
    f_ProcessEvents();
    l_bOk &= f_Check(!l_pDoc->f_IsDirty() && l_pDoc->f_Emitter()->m_fRate == 40 && l_pDoc->f_Emitter()->m_fStop == 0, "undo restores the file");

    // Effects dock: emitters added / duplicated / deleted.
    const size_t l_nN = l_pDoc->f_Effect()->m_vEmitters.size();
    l_pEd->f_EffectsDock()->f_AddEmitter();
    l_pEd->f_EffectsDock()->f_DuplicateSelected();
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_Effect()->m_vEmitters.size() == l_nN + 2, "add + duplicate an emitter");
    l_pEd->f_EffectsDock()->f_DeleteSelected();
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_Effect()->m_vEmitters.size() == l_nN + 1, "delete one");
    // Sprites dropped into the viewport: a flipbook emitter there.
    {
        auto l_upMime = std::make_unique<QMimeData>();
        l_upMime->setData(QStringLiteral("application/x-toms-sprite"), QByteArrayLiteral("fx:flame_0\nfx:flame_1\nfx:flame_3"));
        const QPointF l_oAt = l_pView->rect().center();
        QDragEnterEvent l_oEnter(l_oAt.toPoint(), Qt::CopyAction, l_upMime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(l_pView, &l_oEnter);
        QDropEvent l_oDrop(l_oAt, Qt::CopyAction, l_upMime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(l_pView, &l_oDrop);
        f_ProcessEvents();
        l_bOk &= f_Check(l_pDoc->f_Emitter() && l_pDoc->f_Emitter()->m_vFrames.size() == 3 && l_pDoc->f_Emitter()->m_vFrames[2] == "fx:flame_3",
                    "3 sprites dropped in the viewport: a flipbook emitter");
    }
    // A sprite double-clicked in the Sprites dock: the emitter draws it.
    if (FxSpriteList* l_pList = l_pEd->f_SpritesDock()->f_ListFor(0)) {
        QListWidgetItem* l_pStar = nullptr;
        for (int l_iI = 0; l_iI < l_pList->count(); l_iI++)
            if (l_pList->item(l_iI)->text() == QLatin1String("star")) l_pStar = l_pList->item(l_iI);
        if (l_pStar) emit l_pList->itemDoubleClicked(l_pStar);
        f_ProcessEvents();
        l_bOk &= f_Check(l_pStar && l_pDoc->f_Emitter()->m_strSprite == "fx:star" && l_pDoc->f_Emitter()->m_vFrames.empty(), "Sprites dock: double-click star");
    }
    while (l_pDoc->f_UndoStack()->index() > l_iBase) l_pDoc->f_UndoStack()->undo();
    f_ProcessEvents();

    // A preset into a new file: the atlases it needs come along.
    l_pDoc->f_NewFile();
    f_ProcessEvents();
    l_bOk &= f_Check(l_pEd->f_AddPreset(QStringLiteral("coin_burst")) && l_pDoc->f_AtlasIndexOf(QStringLiteral("game")) >= 0 &&
                    l_pDoc->f_AtlasIndexOf(QStringLiteral("fx")) >= 0 && l_pDoc->f_Problems().empty(),
                "preset coin_burst: game + fx atlases added, no problems");
    l_pPlay->f_Restart();
    l_pPlay->f_Pause();
    l_pPlay->f_Seek(0.3f);
    l_oW.grab();
    l_bOk &= f_Check(!l_pView->f_LastQuads().empty(), "coin_burst draws at 0.3 s");
    l_bOk &= f_Check(l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("preset.png"))), "preset.png");

    // Save As next to the atlases' drive, reopen: the same JSON, relative atlas paths.
    const QString l_oSaved = QDir(l_oOutDir).filePath(QStringLiteral("saved.particle"));
    l_bOk &= f_Check(l_pEd->f_SaveTo(l_oSaved) && !l_pDoc->f_IsDirty(), "save as");
    const QString l_oJson = l_pDoc->f_ToJson();
    l_bOk &= f_Check(l_oJson.contains(QStringLiteral("\"../")) || l_oJson.contains(QStringLiteral("\"..")) || !l_oJson.contains(QStringLiteral(":/")),
                "atlas paths stored relative");
    l_bOk &= f_Check(l_pEd->f_OpenFile(l_oSaved) && l_pDoc->f_ToJson() == l_oJson, "reopen: the same file");

    // Backup copy: opens from anywhere (absolute atlas paths), the document is unchanged.
    {
        const QString l_oBackup = QDir(l_oOutDir).filePath(QStringLiteral("backup_1.particle"));
        const size_t l_nEffects = l_pDoc->f_File().m_vEffects.size();
        QString l_oBerr;
        l_bOk &= f_Check(l_pDoc->f_WriteBackup(l_oBackup, &l_oBerr) && !l_pDoc->f_IsDirty() && l_pDoc->f_FilePath() == QFileInfo(l_oSaved).absoluteFilePath(),
                    "backup copy written, the document unchanged");
        l_bOk &= f_Check(l_pEd->f_OpenFile(l_oBackup) && l_pDoc->f_File().m_vEffects.size() == l_nEffects && l_pDoc->f_AtlasCount() == 2 && l_pDoc->f_Problems().empty(),
                    "the backup opens: same effects, both atlases found");
    }

    std::printf("selftest: %s\n", l_bOk ? "ok" : "FAILED");
    return l_bOk ? 0 : 1;
}

// The preview with the game renderer (bgfx, on screen: a real window). Waits real time for frames.
void f_Pump(int l_iMs)
{
    QElapsedTimer l_oT;
    l_oT.start();
    while (l_oT.elapsed() < l_iMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

bool f_WaitShot(int l_iBefore, int l_iMs)
{
    QElapsedTimer l_oT;
    l_oT.start();
    while (toms::next::f_BgfxHostScreenshotsWritten() == l_iBefore && l_oT.elapsed() < l_iMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return toms::next::f_BgfxHostScreenshotsWritten() > l_iBefore;
}

int f_RunGpuSelfTest(ParticleMainWindow& l_oW, const QString& l_oFile, const QString& l_oOutDir)
{
    qApp->setProperty("toms.selftest", true);
    QDir().mkpath(l_oOutDir);
    ParticleEditor* l_pEd = l_oW.f_Editor();
    ParticleDocument* l_pDoc = l_pEd->f_Document();
    ParticlePlayback* l_pPlay = l_pEd->f_Playback();
    ParticleViewport* l_pView = l_pEd->f_Viewport();
    bool l_bOk = true;
    f_Pump(500);
    l_bOk &= f_Check(l_pView->f_UsingGameRenderer(), "the preview runs on the game renderer");
    std::printf("selftest:   %s\n", l_pView->f_RendererName().toUtf8().constData());
    if (!l_pView->f_UsingGameRenderer()) return 1;
    l_bOk &= f_Check(l_pEd->f_OpenFile(l_oFile) && f_SelectEmitter(l_pDoc, "torch_fire", "flames"), "open the recipes, torch_fire");

    // Every emitter on the GPU (threshold 1), as the game would with that setting.
    l_pEd->f_SetGpuThreshold(1);
    l_pPlay->f_Restart();
    const int l_iF0 = l_pView->f_FramesDrawn();
    f_Pump(1500);
    const int l_iFrames = l_pView->f_FramesDrawn() - l_iF0;
    std::printf("selftest:   %d frames in 1.5 s\n", l_iFrames);
    l_bOk &= f_Check(l_iFrames > 20, "frames are drawn (bgfx, vsync)");
    l_bOk &= f_Check(l_pPlay->f_Instance().f_GpuEmitters() == 3 && l_pPlay->f_Instance().f_LiveCount() > 0, "threshold 1: all 3 emitters on the GPU, particles live");
    l_pPlay->f_Pause();
    l_pPlay->f_Seek(1.0f);
    f_Pump(200);
    int l_iBefore = toms::next::f_BgfxHostScreenshotsWritten();
    l_pView->f_SaveGameRendererShot(QDir(l_oOutDir).filePath(QStringLiteral("gpu_torch.png")));
    l_bOk &= f_Check(f_WaitShot(l_iBefore, 3000), "gpu_torch.png (bgfx screenshot)");

    // The gizmo still edits through the native window.
    const glm::vec2 l_v2Off0 = l_pDoc->f_Emitter()->m_v2Offset;
    const QPointF l_oMv = l_pView->f_HandlePos(ParticleViewport::Handle::Move);
    f_Drag(l_pView, l_oMv, l_oMv + QPointF(40, 0));
    l_bOk &= f_Check(l_pDoc->f_Emitter()->m_v2Offset.x > l_v2Off0.x + 5, "gizmo drag moves the emitter (game renderer)");
    l_pDoc->f_UndoStack()->undo();
    f_Pump(100);

    // The same moment simulated on the CPU (threshold 0): it must look the same.
    l_pEd->f_SetGpuThreshold(0);
    l_pPlay->f_Seek(1.0f);
    f_Pump(200);
    l_bOk &= f_Check(l_pPlay->f_Instance().f_GpuEmitters() == 0, "threshold 0: on the CPU");
    l_iBefore = toms::next::f_BgfxHostScreenshotsWritten();
    l_pView->f_SaveGameRendererShot(QDir(l_oOutDir).filePath(QStringLiteral("cpu_torch.png")));
    l_bOk &= f_Check(f_WaitShot(l_iBefore, 3000), "cpu_torch.png (bgfx screenshot)");
    // More effects, GPU and CPU at the same moment, for comparing (screenshots only).
    for (const char* l_pszName : {"magic_circle", "hit_sparks", "snow"}) {
        const auto& l_vEs = l_pDoc->f_File().m_vEffects;
        for (int l_iI = 0; l_iI < int(l_vEs.size()); l_iI++)
            if (l_vEs[size_t(l_iI)].m_strName == l_pszName) l_pDoc->f_Select(l_iI, 0);
        f_Pump(50);
        for (int l_iGpu = 1; l_iGpu >= 0; l_iGpu--) {
            l_pEd->f_SetGpuThreshold(l_iGpu ? 1 : 0);
            l_pPlay->f_Seek(std::string(l_pszName) == "hit_sparks" ? 0.15f : 1.0f);
            f_Pump(200);
            l_iBefore = toms::next::f_BgfxHostScreenshotsWritten();
            l_pView->f_SaveGameRendererShot(QDir(l_oOutDir).filePath(QStringLiteral("%1_%2.png").arg(QLatin1String(l_pszName), l_iGpu ? "gpu" : "cpu")));
            f_WaitShot(l_iBefore, 3000);
        }
    }
    l_pEd->f_SetGpuThreshold(5000);
    std::printf("selftest: %s\n", l_bOk ? "ok" : "FAILED");
    return l_bOk ? 0 : 1;
}

}  // namespace

int main(int l_iArgc, char** l_pArgv)
{
    const std::vector<std::string> l_vArgs = Console::f_Utf8Args(l_iArgc, l_pArgv);
    if (!l_vArgs.empty() && l_vArgs[0] == "--headless") {
        Console::f_AttachParent();
        int l_iCode = 3;
        if (l_vArgs.size() == 3 && l_vArgs[1] == "check") {
            l_iCode = f_HeadlessCheck(QString::fromStdString(l_vArgs[2]));
        } else if ((l_vArgs.size() >= 4 && l_vArgs.size() <= 6) && l_vArgs[1] == "render") {
            // No window is opened: the default platform plugin (the one deployed next to the exe) is enough.
            QGuiApplication l_oApp(l_iArgc, l_pArgv);
            l_iCode = f_HeadlessRender(QString::fromStdString(l_vArgs[2]), QString::fromStdString(l_vArgs[3]),
                                  l_vArgs.size() > 4 ? std::max(1, std::atoi(l_vArgs[4].c_str())) : 8,
                                  l_vArgs.size() > 5 ? float(std::atof(l_vArgs[5].c_str())) : 0.15f);
        } else {
            std::fprintf(stderr, "usage: particle_editor --headless check file.particle\n"
                                 "       particle_editor --headless render file.particle#effect out.png [frames] [every]\n");
        }
        std::fflush(stdout);
        std::fflush(stderr);
        return l_iCode;
    }

    QApplication l_oApp(l_iArgc, l_pArgv);
    QApplication::setOrganizationName(QStringLiteral("TOMS"));
    QApplication::setApplicationName(QStringLiteral("ParticleEditor"));
    QApplication::setApplicationDisplayName(QStringLiteral("Particle Editor"));
    QApplication::setWindowIcon(Icons::f_Icon(Icons::Id::Play));
    Theme::f_Apply(Theme::Mode::Dark);
    Language::f_Init();   // English / 中文 (tools/studio_common/Language.h), before any window

    const QStringList l_oQargs = QCoreApplication::arguments().mid(1);
    if (l_oQargs.size() == 3 && l_oQargs[0] == QLatin1String("--selftest")) {
        Console::f_AttachParent();
        qApp->setProperty("toms.selftest", true);
        ParticleMainWindow l_oW;
        l_oW.f_SetDarkTheme(true);
        l_oW.resize(1500, 920);
        l_oW.show();
        const int l_iCode = f_RunSelfTest(l_oW, l_oQargs[1], l_oQargs[2]);
        std::fflush(stdout);
        return l_iCode;
    }
    if (l_oQargs.size() == 3 && l_oQargs[0] == QLatin1String("--selftest-gpu")) {   // on screen: the game renderer
        Console::f_AttachParent();
        qApp->setProperty("toms.selftest", true);
        ParticleMainWindow l_oW;
        l_oW.f_SetDarkTheme(true);
        l_oW.resize(1500, 920);
        l_oW.show();
        const int l_iCode = f_RunGpuSelfTest(l_oW, l_oQargs[1], l_oQargs[2]);
        std::fflush(stdout);
        return l_iCode;
    }
    ParticleMainWindow l_oW;
    l_oW.show();
    if (!l_oQargs.isEmpty()) l_oW.f_Editor()->f_OpenFile(l_oQargs.first());
    return l_oApp.exec();
}
