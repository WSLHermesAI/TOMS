// particle_editor -- the Qt editor for .particle effects (docs/17_PARTICLES.md):
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

QString qs(const std::string& s) { return QString::fromStdString(s); }

// ---- headless ------------------------------------------------------------------------------------

bool readFile(const QString& path, std::string& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    out = f.readAll().toStdString();
    return true;
}

int headlessCheck(const QString& path)
{
    std::string text, err;
    toms::fx::ParticleFile file;
    if (!readFile(path, text)) {
        std::fprintf(stderr, "%s: cannot read\n", path.toUtf8().constData());
        return 2;
    }
    if (!toms::fx::parseParticles(text, file, &err)) {
        std::fprintf(stderr, "%s: %s\n", path.toUtf8().constData(), err.c_str());
        return 2;
    }
    std::vector<std::unique_ptr<toms::AtlasFile>> atlases;
    toms::anim::AtlasSet set;
    int errors = 0, warnings = 0;
    const QDir dir = QFileInfo(path).absoluteDir();
    for (const toms::anim::AtlasRef& r : file.atlases) {
        auto a = std::make_unique<toms::AtlasFile>();
        std::string atext;
        if (!readFile(dir.absoluteFilePath(qs(r.path)), atext) || !toms::parseAtlas(atext, *a, &err)) {
            std::printf("error: atlas %s (%s) does not load\n", r.path.c_str(), r.id.c_str());
            errors++;
            continue;
        }
        set.add(*a, r.id);
        atlases.push_back(std::move(a));
    }
    for (const toms::fx::Problem& p : toms::fx::checkParticles(file, &set)) {
        std::printf("%s: %s: %s\n", p.warning ? "warning" : "error", p.where.c_str(), p.text.c_str());
        (p.warning ? warnings : errors)++;
    }
    std::printf("%s: %zu effect(s), %zu atlas(es), %d error(s), %d warning(s)\n", path.toUtf8().constData(), file.effects.size(),
                file.atlases.size(), errors, warnings);
    return errors ? 2 : 0;
}

// A contact sheet: the effect at `frames` moments, `every` seconds apart, seed 1, one 256 px cell each.
int headlessRender(const QString& target, const QString& out, int frames, float every)
{
    const int hash = int(target.lastIndexOf(QLatin1Char('#')));
    if (hash < 0) {
        std::fprintf(stderr, "render: expected file.particle#effect\n");
        return 3;
    }
    ParticleDocument doc;
    QString err;
    if (!doc.open(target.left(hash), &err)) {
        std::fprintf(stderr, "render: %s\n", err.toUtf8().constData());
        return 2;
    }
    const toms::fx::Effect* e = doc.file().find(target.mid(hash + 1).toStdString());
    if (!e) {
        std::fprintf(stderr, "render: no effect '%s'\n", target.mid(hash + 1).toUtf8().constData());
        return 2;
    }
    const int cell = 256, cols = std::min(frames, 4), rows = (frames + cols - 1) / cols;
    QImage sheet(cols * cell, rows * cell, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QColor(24, 24, 30));
    QPainter p(&sheet);
    toms::fx::EffectInstance fx;
    fx.play(e, 1);
    std::vector<Quad> quads;
    for (int i = 0; i < frames; i++) {
        const float t = i * every;
        fx.seek(t);
        quads.clear();
        fx.appendQuads(doc.atlasSet(), glm::mat3(1.0f), nullptr, quads);
        const QPointF origin((i % cols) * cell + cell / 2.0, (i / cols) * cell + cell / 2.0);
        p.setClipRect(QRectF((i % cols) * cell, (i / cols) * cell, cell, cell));
        ParticleViewport::drawQuads(p, quads, doc, QTransform::fromTranslate(origin.x(), origin.y()));
        p.setClipping(false);
        p.setPen(QColor(150, 150, 160));
        p.drawRect(QRectF((i % cols) * cell, (i / cols) * cell, cell - 1, cell - 1));
        p.drawText(QPointF((i % cols) * cell + 6, (i / cols) * cell + 16), QStringLiteral("t=%1 s  %2 live").arg(t, 0, 'f', 2).arg(fx.liveCount()));
    }
    p.end();
    if (!sheet.save(out)) {
        std::fprintf(stderr, "render: cannot write %s\n", out.toUtf8().constData());
        return 2;
    }
    std::printf("%s: %d frame(s) of %s\n", out.toUtf8().constData(), frames, target.toUtf8().constData());
    return 0;
}

// ---- selftest ------------------------------------------------------------------------------------

void processEvents()
{
    for (int i = 0; i < 3; i++) QCoreApplication::processEvents();
}

bool check(bool ok, const char* what)
{
    std::printf("selftest: %-64s %s\n", what, ok ? "ok" : "FAILED");
    return ok;
}

void mouse(QWidget* w, QEvent::Type type, const QPointF& at, Qt::MouseButton button, Qt::MouseButtons buttons,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent e(type, at, w->mapToGlobal(at), button, buttons, mods);
    QApplication::sendEvent(w, &e);
}

void drag(QWidget* w, const QPointF& from, const QPointF& to, Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    mouse(w, QEvent::MouseButtonPress, from, button, button, mods);
    for (int i = 1; i <= 4; i++) mouse(w, QEvent::MouseMove, from + (to - from) * (i / 4.0), Qt::NoButton, button, mods);
    mouse(w, QEvent::MouseButtonRelease, to, button, Qt::NoButton, mods);
    processEvents();
}

bool selectEmitter(ParticleDocument* doc, const char* effect, const char* emitter)
{
    const auto& es = doc->file().effects;
    for (int i = 0; i < int(es.size()); i++)
        if (es[size_t(i)].name == effect)
            for (int j = 0; j < int(es[size_t(i)].emitters.size()); j++)
                if (es[size_t(i)].emitters[size_t(j)].name == emitter) {
                    doc->select(i, j);
                    processEvents();
                    return true;
                }
    return false;
}

int runSelfTest(ParticleMainWindow& w, const QString& file, const QString& outDir)
{
    qApp->setProperty("toms.selftest", true);
    QDir().mkpath(outDir);
    ParticleEditor* ed = w.editor();
    ParticleDocument* doc = ed->document();
    ParticlePlayback* play = ed->playback();
    ParticleViewport* view = ed->viewport();
    InspectorDock* insp = ed->inspector();
    bool ok = true;

    ok &= check(ed->openFile(file), "open the recipes");
    processEvents();
    ok &= check(doc->file().effects.size() >= 11 && doc->atlasCount() == 2, "11 effects, 2 atlases");
    ok &= check(doc->problems().empty() && ed->problemsList()->count() >= 1, "no problems (the list shows only the log line)");
    ok &= check(ed->effectsDock()->tree()->topLevelItemCount() == int(doc->file().effects.size()), "Effects dock lists every effect");

    // The preview: torch_fire's flames, paused at 1 s.
    ok &= check(selectEmitter(doc, "torch_fire", "flames"), "select torch_fire / flames");
    play->pause();
    play->seek(1.0f);
    w.grab();   // paints the viewport
    ok &= check(play->instance().liveCount() > 20 && !view->lastQuads().empty() && std::fabs(play->time() - 1.0f) < 0.02f,
                "paused at 1 s: particles live and drawn");
    const std::vector<Quad> first = view->lastQuads();
    play->seek(1.0f);
    w.grab();
    bool same = first.size() == view->lastQuads().size();
    for (size_t i = 0; same && i < first.size(); i++) same = std::fabs(first[i].corners[0] - view->lastQuads()[i].corners[0]) < 1e-4f;
    ok &= check(same, "seek(1) twice: the same particles (seeded)");
    ok &= check(w.grab().save(QDir(outDir).filePath(QStringLiteral("main.png"))), "main.png");

    // Inspector edits: one undo step each.
    const int base = doc->undoStack()->index();
    insp->rateSpin()->setValue(80);
    processEvents();
    ok &= check(doc->emitter()->rate == 80 && doc->undoStack()->index() == base + 1, "Inspector: rate 80 (1 step)");
    ok &= check(std::fabs(play->time() - 1.0f) < 0.02f, "the preview stays at 1 s after an edit");
    insp->sizeEdit()->minSpin()->setValue(30);
    processEvents();
    ok &= check(doc->emitter()->size.min == 30, "Inspector: size min 30");
    insp->sizeCurve()->addKey(0.5f, 2.0f);
    processEvents();
    ok &= check(doc->emitter()->sizeOverLife.keys.size() == 3, "size curve: a key added (3 keys)");
    insp->colorCurve()->addStop(0.4f);
    processEvents();
    ok &= check(doc->emitter()->colorOverLife.keys.size() == 5, "colour gradient: a stop added (5)");
    insp->blendCombo()->setCurrentIndex(2);
    emit insp->blendCombo()->activated(2);
    processEvents();
    ok &= check(doc->emitter()->blend == toms::fx::Blend::multiply(), "Blend: multiply");
    insp->simulationCombo()->setCurrentIndex(2);
    processEvents();
    ok &= check(doc->emitter()->simulation == toms::fx::Emitter::Simulation::Gpu &&
                    doc->toJson().contains(QStringLiteral("\"simulation\": \"gpu\"")),
                "Simulation: GPU (stored in the file)");
    insp->shapeCombo()->setCurrentIndex(int(toms::fx::Shape::Box));
    processEvents();
    ok &= check(doc->emitter()->shape.type == toms::fx::Shape::Box && doc->emitter()->shape.width > 0, "Shape: box (with a size)");
    ok &= check(w.grab().save(QDir(outDir).filePath(QStringLiteral("inspector.png"))), "inspector.png");

    // Gizmo: drag the move square 40 px right, the arrow to straight right.
    w.grab();
    const glm::vec2 off0 = doc->emitter()->offset;
    const int before = doc->undoStack()->index();
    const QPointF mv = view->handlePos(ParticleViewport::Handle::Move);
    drag(view, mv, mv + QPointF(40, 0));
    const float moved = doc->emitter()->offset.x - off0.x;
    ok &= check(moved > 5 && std::fabs(doc->emitter()->offset.y - off0.y) < 1 && doc->undoStack()->index() == before + 1,
                "gizmo: drag the centre moves the emitter (1 step)");
    w.grab();
    const QPointF dir = view->handlePos(ParticleViewport::Handle::Direction);
    const QPointF centre = view->handlePos(ParticleViewport::Handle::Move);
    drag(view, dir, centre + QPointF(80, 0));
    ok &= check(std::fabs(doc->emitter()->direction) < 1.5f, "gizmo: drag the arrow to the right: direction 0");
    w.grab();
    const QPointF spread = view->handlePos(ParticleViewport::Handle::SpreadB);
    drag(view, spread, centre + QPointF(0, 80));
    ok &= check(std::fabs(doc->emitter()->spread - 90) < 2, "gizmo: drag a spread end straight down: spread 90");

    // Right-drag pans (no context menu); a click fires the effect there.
    w.grab();
    const QPointF h0 = view->handlePos(ParticleViewport::Handle::Move);
    drag(view, QPointF(50, 50), QPointF(110, 70), Qt::RightButton);
    w.grab();
    const QPointF h1 = view->handlePos(ParticleViewport::Handle::Move);
    ok &= check(std::fabs(h1.x() - h0.x() - 60) < 1 && std::fabs(h1.y() - h0.y() - 20) < 1, "right-drag pans the view");
    const QPointF o0 = play->origin();
    mouse(view, QEvent::MouseButtonPress, QPointF(60, 400), Qt::LeftButton, Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, QPointF(60, 400), Qt::LeftButton, Qt::NoButton);
    processEvents();
    ok &= check(play->origin() != o0 && play->playing(), "click: the effect fires there");
    play->setOrigin(QPointF(0, 0), false);
    play->pause();

    // Timeline: click the ruler at the middle = seek there, paused.
    TimelineView* tl = ed->timeline()->view();
    const float span = tl->span();
    mouse(tl, QEvent::MouseButtonPress, QPointF(tl->xOf(span / 2), 8), Qt::LeftButton, Qt::LeftButton);
    mouse(tl, QEvent::MouseButtonRelease, QPointF(tl->xOf(span / 2), 8), Qt::LeftButton, Qt::NoButton);
    processEvents();
    ok &= check(!play->playing() && std::fabs(play->time() - span / 2) < 0.05f, "timeline ruler: seek to the middle, paused");
    // Drag the selected emitter's bar end to 1.5 s.
    const int lane = doc->emitterIndex();
    const QRectF bar = tl->barRect(lane);
    drag(tl, QPointF(bar.right() - 1, bar.center().y()), QPointF(tl->xOf(1.5f), bar.center().y()));
    ok &= check(std::fabs(doc->emitter()->stop - 1.5f) < 0.03f, "timeline: drag the bar's end: stop 1.5 s");

    // Undo everything since the file opened.
    while (doc->undoStack()->index() > base) doc->undoStack()->undo();
    processEvents();
    ok &= check(!doc->isDirty() && doc->emitter()->rate == 40 && doc->emitter()->stop == 0, "undo restores the file");

    // Effects dock: emitters added / duplicated / deleted.
    const size_t n = doc->effect()->emitters.size();
    ed->effectsDock()->addEmitter();
    ed->effectsDock()->duplicateSelected();
    processEvents();
    ok &= check(doc->effect()->emitters.size() == n + 2, "add + duplicate an emitter");
    ed->effectsDock()->deleteSelected();
    processEvents();
    ok &= check(doc->effect()->emitters.size() == n + 1, "delete one");
    // Sprites dropped into the viewport: a flipbook emitter there.
    {
        auto mime = std::make_unique<QMimeData>();
        mime->setData(QStringLiteral("application/x-toms-sprite"), QByteArrayLiteral("fx:flame_0\nfx:flame_1\nfx:flame_3"));
        const QPointF at = view->rect().center();
        QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &enter);
        QDropEvent drop(at, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &drop);
        processEvents();
        ok &= check(doc->emitter() && doc->emitter()->frames.size() == 3 && doc->emitter()->frames[2] == "fx:flame_3",
                    "3 sprites dropped in the viewport: a flipbook emitter");
    }
    // A sprite double-clicked in the Sprites dock: the emitter draws it.
    if (FxSpriteList* list = ed->spritesDock()->listFor(0)) {
        QListWidgetItem* star = nullptr;
        for (int i = 0; i < list->count(); i++)
            if (list->item(i)->text() == QLatin1String("star")) star = list->item(i);
        if (star) emit list->itemDoubleClicked(star);
        processEvents();
        ok &= check(star && doc->emitter()->sprite == "fx:star" && doc->emitter()->frames.empty(), "Sprites dock: double-click star");
    }
    while (doc->undoStack()->index() > base) doc->undoStack()->undo();
    processEvents();

    // A preset into a new file: the atlases it needs come along.
    doc->newFile();
    processEvents();
    ok &= check(ed->addPreset(QStringLiteral("coin_burst")) && doc->atlasIndexOf(QStringLiteral("game")) >= 0 &&
                    doc->atlasIndexOf(QStringLiteral("fx")) >= 0 && doc->problems().empty(),
                "preset coin_burst: game + fx atlases added, no problems");
    play->restart();
    play->pause();
    play->seek(0.3f);
    w.grab();
    ok &= check(!view->lastQuads().empty(), "coin_burst draws at 0.3 s");
    ok &= check(w.grab().save(QDir(outDir).filePath(QStringLiteral("preset.png"))), "preset.png");

    // Save As next to the atlases' drive, reopen: the same JSON, relative atlas paths.
    const QString saved = QDir(outDir).filePath(QStringLiteral("saved.particle"));
    ok &= check(ed->saveTo(saved) && !doc->isDirty(), "save as");
    const QString json = doc->toJson();
    ok &= check(json.contains(QStringLiteral("\"../")) || json.contains(QStringLiteral("\"..")) || !json.contains(QStringLiteral(":/")),
                "atlas paths stored relative");
    ok &= check(ed->openFile(saved) && doc->toJson() == json, "reopen: the same file");

    std::printf("selftest: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}

// The preview with the game renderer (bgfx, on screen: a real window). Waits real time for frames.
void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

bool waitShot(int before, int ms)
{
    QElapsedTimer t;
    t.start();
    while (toms::next::bgfxHostScreenshotsWritten() == before && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return toms::next::bgfxHostScreenshotsWritten() > before;
}

int runGpuSelfTest(ParticleMainWindow& w, const QString& file, const QString& outDir)
{
    qApp->setProperty("toms.selftest", true);
    QDir().mkpath(outDir);
    ParticleEditor* ed = w.editor();
    ParticleDocument* doc = ed->document();
    ParticlePlayback* play = ed->playback();
    ParticleViewport* view = ed->viewport();
    bool ok = true;
    pump(500);
    ok &= check(view->usingGameRenderer(), "the preview runs on the game renderer");
    std::printf("selftest:   %s\n", view->rendererName().toUtf8().constData());
    if (!view->usingGameRenderer()) return 1;
    ok &= check(ed->openFile(file) && selectEmitter(doc, "torch_fire", "flames"), "open the recipes, torch_fire");

    // Every emitter on the GPU (threshold 1), as the game would with that setting.
    ed->setGpuThreshold(1);
    play->restart();
    const int f0 = view->framesDrawn();
    pump(1500);
    const int frames = view->framesDrawn() - f0;
    std::printf("selftest:   %d frames in 1.5 s\n", frames);
    ok &= check(frames > 20, "frames are drawn (bgfx, vsync)");
    ok &= check(play->instance().gpuEmitters() == 3 && play->instance().liveCount() > 0, "threshold 1: all 3 emitters on the GPU, particles live");
    play->pause();
    play->seek(1.0f);
    pump(200);
    int before = toms::next::bgfxHostScreenshotsWritten();
    view->saveGameRendererShot(QDir(outDir).filePath(QStringLiteral("gpu_torch.png")));
    ok &= check(waitShot(before, 3000), "gpu_torch.png (bgfx screenshot)");

    // The gizmo still edits through the native window.
    const glm::vec2 off0 = doc->emitter()->offset;
    const QPointF mv = view->handlePos(ParticleViewport::Handle::Move);
    drag(view, mv, mv + QPointF(40, 0));
    ok &= check(doc->emitter()->offset.x > off0.x + 5, "gizmo drag moves the emitter (game renderer)");
    doc->undoStack()->undo();
    pump(100);

    // The same moment simulated on the CPU (threshold 0): it must look the same.
    ed->setGpuThreshold(0);
    play->seek(1.0f);
    pump(200);
    ok &= check(play->instance().gpuEmitters() == 0, "threshold 0: on the CPU");
    before = toms::next::bgfxHostScreenshotsWritten();
    view->saveGameRendererShot(QDir(outDir).filePath(QStringLiteral("cpu_torch.png")));
    ok &= check(waitShot(before, 3000), "cpu_torch.png (bgfx screenshot)");
    // More effects, GPU and CPU at the same moment, for comparing (screenshots only).
    for (const char* name : {"magic_circle", "hit_sparks", "snow"}) {
        const auto& es = doc->file().effects;
        for (int i = 0; i < int(es.size()); i++)
            if (es[size_t(i)].name == name) doc->select(i, 0);
        pump(50);
        for (int gpu = 1; gpu >= 0; gpu--) {
            ed->setGpuThreshold(gpu ? 1 : 0);
            play->seek(std::string(name) == "hit_sparks" ? 0.15f : 1.0f);
            pump(200);
            before = toms::next::bgfxHostScreenshotsWritten();
            view->saveGameRendererShot(QDir(outDir).filePath(QStringLiteral("%1_%2.png").arg(QLatin1String(name), gpu ? "gpu" : "cpu")));
            waitShot(before, 3000);
        }
    }
    ed->setGpuThreshold(5000);
    std::printf("selftest: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args = Console::utf8Args(argc, argv);
    if (!args.empty() && args[0] == "--headless") {
        Console::attachParent();
        int code = 3;
        if (args.size() == 3 && args[1] == "check") {
            code = headlessCheck(QString::fromStdString(args[2]));
        } else if ((args.size() >= 4 && args.size() <= 6) && args[1] == "render") {
            // No window is opened: the default platform plugin (the one deployed next to the exe) is enough.
            QGuiApplication app(argc, argv);
            code = headlessRender(QString::fromStdString(args[2]), QString::fromStdString(args[3]),
                                  args.size() > 4 ? std::max(1, std::atoi(args[4].c_str())) : 8,
                                  args.size() > 5 ? float(std::atof(args[5].c_str())) : 0.15f);
        } else {
            std::fprintf(stderr, "usage: particle_editor --headless check file.particle\n"
                                 "       particle_editor --headless render file.particle#effect out.png [frames] [every]\n");
        }
        std::fflush(stdout);
        std::fflush(stderr);
        return code;
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TOMS"));
    QApplication::setApplicationName(QStringLiteral("ParticleEditor"));
    QApplication::setApplicationDisplayName(QStringLiteral("Particle Editor"));
    QApplication::setWindowIcon(Icons::icon(Icons::Id::Play));
    Theme::apply(Theme::Mode::Dark);

    const QStringList qargs = QCoreApplication::arguments().mid(1);
    if (qargs.size() == 3 && qargs[0] == QLatin1String("--selftest")) {
        Console::attachParent();
        qApp->setProperty("toms.selftest", true);
        ParticleMainWindow w;
        w.setDarkTheme(true);
        w.resize(1500, 920);
        w.show();
        const int code = runSelfTest(w, qargs[1], qargs[2]);
        std::fflush(stdout);
        return code;
    }
    if (qargs.size() == 3 && qargs[0] == QLatin1String("--selftest-gpu")) {   // on screen: the game renderer
        Console::attachParent();
        qApp->setProperty("toms.selftest", true);
        ParticleMainWindow w;
        w.setDarkTheme(true);
        w.resize(1500, 920);
        w.show();
        const int code = runGpuSelfTest(w, qargs[1], qargs[2]);
        std::fflush(stdout);
        return code;
    }
    ParticleMainWindow w;
    w.show();
    if (!qargs.isEmpty()) w.editor()->openFile(qargs.first());
    return app.exec();
}
