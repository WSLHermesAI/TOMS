// anim_editor -- the Qt editor for .anim node animations (docs/15_ANIMATION.md):
//
//   anim_editor                                   the editor
//   anim_editor file.anim                         the editor with that file open
//   anim_editor --headless check file.anim        parse + check (sprites vs atlas, keys); exit 0 / 2
//   anim_editor --selftest file.anim outdir       automated check (-platform offscreen); outdir must be
//                                                 on the atlas's drive (atlas paths are stored relative)
#include "AnimAtlasesPanel.h"
#include "AnimDocument.h"
#include "AnimClipsDock.h"
#include "AutoBackup.h"
#include "AnimEditor.h"
#include "AnimKeyListDock.h"
#include "AnimMainWindow.h"
#include "AnimNodesDock.h"
#include "AnimProblemsDock.h"
#include "AnimPropertiesDock.h"
#include "AnimSpritesDock.h"
#include "AnimTransport.h"
#include "AnimViewport.h"
#include "BezierDialog.h"
#include "Console.h"
#include "Icons.h"
#include "Theme.h"
#include "anim_check.h"
#include "anim_player.h"
#include "bgfx_host.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>

#include <cmath>
#include <cstdio>
#include <memory>

using namespace animed;
using toms::anim::Node;

namespace {

void processEvents()
{
    for (int i = 0; i < 3; i++) QCoreApplication::processEvents();
}

bool check(bool ok, const char* what)
{
    std::printf("selftest: %-58s %s\n", what, ok ? "ok" : "FAILED");
    return ok;
}

int rowOf(const AnimKeyListDock* keys, float t)
{
    const std::vector<float> times = keys->rowTimes();
    for (size_t i = 0; i < times.size(); i++)
        if (sameTime(times[i], t)) return int(i);
    return -1;
}

// The viewport's quads vs one appendQuads() call over the same poses, and the QPainter transform
// of each quad vs its corners: the editor draws exactly what the game would.
bool checkDrawing(AnimEditor* ed, float t)
{
    AnimDocument* doc = ed->document();
    std::vector<toms::anim::NodePose> poses;
    std::vector<AnimViewport::DrawItem> items;
    ed->viewport()->buildFrame(t, poses, items, nullptr);
    std::vector<toms::anim::NodePose> ref;
    toms::anim::evaluate(*doc->clip(), t, ref);
    std::vector<Quad> quads;
    toms::anim::appendQuads(ref, doc->atlasSet(), toms::anim::placement(0, 0), nullptr, quads);
    bool ok = items.size() == quads.size() && !quads.empty();
    double maxCorner = 0, maxMap = 0;
    for (size_t i = 0; ok && i < items.size(); i++) {
        const Quad& a = items[i].quad;
        const Quad& b = quads[i];
        for (int k = 0; k < 8; k++) maxCorner = std::max(maxCorner, double(std::fabs(a.corners[k] - b.corners[k])));
        for (int k = 0; k < 4; k++) maxCorner = std::max(maxCorner, double(std::fabs(a.tint[k] - b.tint[k]) + std::fabs(a.uv[k] - b.uv[k])));
        ok = ok && a.additive == b.additive && a.texture == b.texture;
        // The page image the viewport cuts the sprite from is the one the quad's texture names.
        int page = -1;
        ok = ok && items[i].images && items[i].images == doc->pageImages(a.texture, &page) && items[i].page == page;
        // The transform QPainter draws the sprite's pixel rect with must land on the 4 corners.
        const QTransform tr = AnimViewport::quadTransform(a, items[i].src.size());
        const QSizeF s = items[i].src.size();
        const QPointF src[4] = {QPointF(0, 0), QPointF(s.width(), 0), QPointF(s.width(), s.height()), QPointF(0, s.height())};
        for (int k = 0; k < 4; k++) {
            const QPointF m = tr.map(src[k]);
            maxMap = std::max(maxMap, std::hypot(m.x() - a.corners[k * 2], m.y() - a.corners[k * 2 + 1]));
        }
        const toms::anim::Clip* clip = doc->clip();
        const Node* n = nodeAt(clip->root, items[i].path);
        std::printf("selftest:   t=%.2f %-8s %s corners (%.2f,%.2f) (%.2f,%.2f) (%.2f,%.2f) (%.2f,%.2f) tint a=%.2f\n", t,
                    n ? n->name.c_str() : "?", a.additive ? "add " : "norm", a.corners[0], a.corners[1], a.corners[2], a.corners[3],
                    a.corners[4], a.corners[5], a.corners[6], a.corners[7], a.tint[3]);
    }
    std::printf("selftest:   %zu quads, max corner/uv/tint difference %.6f, max transform error %.6f px\n", items.size(), maxCorner, maxMap);
    return ok && maxCorner < 1e-4 && maxMap < 1e-3;
}

// The page and pixels the viewport draws the node `name` with at time t (nullptr: not drawn).
const SpriteImageCache* drawnFrom(AnimEditor* ed, const char* name, float t, QRect* src)
{
    AnimDocument* doc = ed->document();
    std::vector<toms::anim::NodePose> poses;
    std::vector<AnimViewport::DrawItem> items;
    ed->viewport()->buildFrame(t, poses, items, nullptr);
    for (const AnimViewport::DrawItem& it : items)
        if (const Node* n = nodeAt(doc->clip()->root, it.path); n && n->name == name) {
            if (src) *src = it.src;
            return it.images;
        }
    return nullptr;
}

bool hasProblemIn(AnimDocument* doc, Problem::Level level, const QString& a, const QString& b)
{
    for (const Problem& p : checkAnim(doc->file(), doc->checkAtlases())) {
        const QString m = QString::fromStdString(p.message);
        if (p.level == level && m.contains(a) && m.contains(b)) return true;
    }
    return false;
}

// (d) Sprite references with atlas ids (the file has game + extra; extra has spark and its own
// coin): every pick stores "id:name" -- the exact copy picked, also the duplicate coin of the
// later atlas -- the viewport draws that atlas's page, sprite keys can switch atlases, ids can be
// renamed (rewriting the references), bare names qualified, broken references are errors.
bool atlasReferences(AnimEditor* ed, const QString& workDir, const QString& outDir, AnimMainWindow& w)
{
    AnimDocument* doc = ed->document();
    AnimSpritesDock* sprites = ed->spritesDock();
    AnimKeyListDock* keys = ed->keyListDock();
    bool ok = true;
    const QString start = doc->toJson();
    const SpriteImageCache* gamePage = &doc->atlasAt(0).images;
    const SpriteImageCache* extraPage = &doc->atlasAt(1).images;
    const int base = doc->undoStack()->index();
    auto steps = [&] { return doc->undoStack()->index() - base; };
    auto undoAll = [&] {
        while (doc->undoStack()->index() > base) doc->undoStack()->undo();
        processEvents();
        return doc->toJson() == start;
    };
    const Node* gem = nullptr;
    auto selectGem = [&] {
        doc->selectNodeByName(QStringLiteral("gem"));
        processEvents();
        gem = doc->selectedNode();
        return gem && gem->name == "gem";
    };
    QRect src;

    // The dock: both coins are listed and pickable; the later copy is not greyed.
    QListWidgetItem* gameCoin = sprites->spriteItem(0, QStringLiteral("coin"));
    QListWidgetItem* extraCoin = sprites->spriteItem(1, QStringLiteral("coin"));
    QListWidgetItem* extraSpark = sprites->spriteItem(1, QStringLiteral("spark"));
    ok &= check(gameCoin && extraCoin && extraSpark && (extraCoin->flags() & Qt::ItemIsEnabled) &&
                    extraCoin->foreground() == gameCoin->foreground() && extraCoin->toolTip().contains(QStringLiteral("usable")),
                "(d) Sprites dock: extra's duplicate coin is listed, enabled, not greyed");
    if (!gameCoin || !extraCoin || !extraSpark || !selectGem()) return false;
    std::unique_ptr<QMimeData> mime(sprites->listFor(1)->mimeData({extraCoin}));
    ok &= check(mime && mime->text() == QLatin1String("extra:coin"), "(d) dragging extra's coin carries \"extra:coin\"");

    // 1. Double-click extra's coin (auto-key off: the rest sprite).
    doc->setAutoKey(false);
    emit sprites->listFor(1)->itemDoubleClicked(extraCoin);
    processEvents();
    selectGem();
    ok &= check(gem->sprite == "extra:coin" && steps() == 1, "(d) double-click extra's coin: gem.sprite = \"extra:coin\" (1 step)");
    ok &= check(drawnFrom(ed, "gem", 0.0f, &src) == extraPage && src == QRect(16, 0, 16, 16),
                "(d) viewport: gem drawn from extra's page (16,0 16x16)");
    ok &= check(checkDrawing(ed, 0.0f), "(d) viewport quads == appendQuads");
    ok &= check(doc->spriteDisplay(gem->sprite) == QLatin1String("coin (extra)") &&
                    doc->spriteDisplay("coin") == QLatin1String("coin (auto: game)") && doc->spriteDisplay("") == QLatin1String("(none)"),
                "(d) shown as 'coin (extra)'; a bare name as 'coin (auto: game)'");
    // The Nodes dock's Sprite column.
    bool nodesShow = false;
    if (auto* tree = ed->nodesDock()->findChild<QTreeWidget*>())
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->text(0) == QLatin1String("gem")) nodesShow = (*it)->text(1) == QLatin1String("coin (extra)");
    ok &= check(nodesShow, "(d) Nodes dock: gem's sprite column shows 'coin (extra)'");
    ok &= check(doc->addSpriteNode(NodePath(), mime->text(), glm::vec2(40, 0)) && doc->selectedNode()->sprite == "extra:coin",
                "(d) dropping it adds a node with \"extra:coin\"");
    ok &= check(undoAll(), "(d) undone");

    // 1b. Several sprites dropped on the scrubber (MPDI-style sequence): one sprite key each,
    // 0.1 s apart from the time under the cursor, in one undo step; the same on the key list.
    if (!selectGem()) return false;
    {
        auto seq = std::make_unique<QMimeData>();
        seq->setData(QLatin1String(AnimSpriteList::kMime), QByteArrayLiteral("game:coin\nextra:spark\nextra:coin"));
        AnimScrubber* scrubber = ed->transport()->scrubber();
        const QPointF at(8, scrubber->height() / 2);   // the track's left end: t = 0
        QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, seq.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(scrubber, &enter);
        QDropEvent drop(at, Qt::CopyAction, seq.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(scrubber, &drop);
        processEvents();   // the drop is handled after the drag's event loop
        gem = doc->selectedNode();
        auto keyAt = [&](size_t i, float t, const char* v) {
            return gem->spriteKeys.size() > i && std::fabs(gem->spriteKeys[i].t - t) < 1e-4f && gem->spriteKeys[i].v == v;
        };
        ok &= check(enter.isAccepted() && drop.isAccepted() && gem->spriteKeys.size() == 3 && keyAt(0, 0.0f, "game:coin") &&
                        keyAt(1, 0.1f, "extra:spark") && keyAt(2, 0.2f, "extra:coin") && steps() == 1,
                    "(d) 3 sprites dropped on the scrubber: keys at 0 / 0.1 / 0.2 (1 step)");
        ok &= check(drawnFrom(ed, "gem", 0.15f, &src) == extraPage && src == QRect(0, 0, 16, 16), "(d) t=0.15: extra's spark");
        // On the key list: the row under the cursor gives the start time (the 0.1 s row).
        QTableWidget* table = keys->table();
        processEvents();
        const int r = rowOf(keys, 0.1f);
        if (r < 0) return check(false, "(d) key list has a 0.1 s row");
        table->scrollToItem(table->item(r, 0));
        const QRect rowRect = table->visualItemRect(table->item(r, 0));
        QDragEnterEvent enter2(rowRect.center(), Qt::CopyAction, seq.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(table->viewport(), &enter2);   // a drop goes to the widget that took the enter
        QDropEvent drop2(QPointF(rowRect.center()), Qt::CopyAction, seq.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(table->viewport(), &drop2);
        processEvents();
        gem = doc->selectedNode();
        std::printf("selftest:   key list drop at row %d (%d,%d %dx%d), accepted %d, steps %d, sprite keys:", r, rowRect.x(),
                    rowRect.y(), rowRect.width(), rowRect.height(), int(drop2.isAccepted()), steps());
        for (const auto& k : gem->spriteKeys) std::printf(" %.3f=%s", k.t, k.v.c_str());
        std::printf("\n");
        ok &= check(drop2.isAccepted() && gem->spriteKeys.size() == 4 && keyAt(1, 0.1f, "game:coin") &&
                        keyAt(2, 0.2f, "extra:spark") && keyAt(3, 0.3f, "extra:coin") && steps() == 2,
                    "(d) dropped on key list row 0.1: keys replaced from 0.1, one more at 0.3 (1 step)");
        // Other drags are not taken.
        QMimeData plain;
        plain.setText(QStringLiteral("coin"));
        QDragEnterEvent other(at.toPoint(), Qt::CopyAction, &plain, Qt::LeftButton, Qt::NoModifier);
        other.ignore();
        QApplication::sendEvent(scrubber, &other);
        ok &= check(!other.isAccepted(), "(d) a plain text drag is not taken by the scrubber");
    }
    ok &= check(undoAll(), "(d) undone");

    // 2. The Properties sprite picker: "coin (extra)" from the list; a typed bare name gets the
    // atlas the lookup picks; a bare legacy name shown as "(auto: ...)" is left alone untouched.
    selectGem();
    QComboBox* combo = ed->propertiesDock()->spriteCombo();
    const int idx = combo->findText(QStringLiteral("coin (extra)"));
    ok &= check(idx > 0 && combo->findText(QStringLiteral("coin (game)")) > 0, "(d) Properties: both coins in the sprite list");
    combo->setCurrentIndex(idx);
    emit combo->activated(idx);
    processEvents();
    selectGem();
    ok &= check(gem->sprite == "extra:coin" && combo->currentText() == QLatin1String("coin (extra)"),
                "(d) Properties: picking 'coin (extra)' stores \"extra:coin\"");
    combo->setEditText(QStringLiteral("spark"));
    emit combo->lineEdit()->editingFinished();
    processEvents();
    selectGem();
    ok &= check(gem->sprite == "extra:spark", "(d) Properties: typing 'spark' stores \"extra:spark\"");
    ok &= check(undoAll(), "(d) undone");
    doc->selectNodeByName(QStringLiteral("coin"));
    processEvents();
    const int before = doc->undoStack()->index();
    emit combo->lineEdit()->editingFinished();   // untouched "coin (auto: game)": nothing written
    ok &= check(combo->currentText() == QLatin1String("coin (auto: game)") && doc->undoStack()->index() == before,
                "(d) Properties: a bare name shows 'coin (auto: game)', left alone");

    // 3. Sprite keys alternating atlases (auto-key on: a key at the playhead per double-click).
    selectGem();
    doc->setAutoKey(true);
    const float times[3] = {0.0f, 0.2f, 0.4f};
    QListWidgetItem* picks[3] = {gameCoin, extraSpark, extraCoin};
    for (int i = 0; i < 3; i++) {
        doc->setTime(times[i]);
        emit picks[i]->listWidget()->itemDoubleClicked(picks[i]);
        processEvents();
    }
    selectGem();
    ok &= check(gem->spriteKeys.size() == 3 && gem->spriteKeys[0].v == "game:coin" && gem->spriteKeys[1].v == "extra:spark" &&
                    gem->spriteKeys[2].v == "extra:coin" && steps() == 3,
                "(d) sprite keys 0 game:coin, 0.2 extra:spark, 0.4 extra:coin");
    ok &= check(drawnFrom(ed, "gem", 0.1f, &src) == gamePage && src.size() == QSize(32, 32), "(d) t=0.1: game's coin (32x32)");
    ok &= check(drawnFrom(ed, "gem", 0.3f, &src) == extraPage && src == QRect(0, 0, 16, 16), "(d) t=0.3: extra's spark");
    ok &= check(drawnFrom(ed, "gem", 0.5f, &src) == extraPage && src == QRect(16, 0, 16, 16), "(d) t=0.5: extra's coin");
    for (float t : {0.1f, 0.3f, 0.5f}) ok &= check(checkDrawing(ed, t), "(d) viewport quads == appendQuads");
    const int r1 = rowOf(keys, 0.2f), r2 = rowOf(keys, 0.4f);
    ok &= check(r1 >= 0 && r2 >= 0 && keys->table()->item(r1, AnimKeyListDock::ColSprite)->text() == QLatin1String("spark (extra)") &&
                    keys->table()->item(r2, AnimKeyListDock::ColSprite)->text() == QLatin1String("coin (extra)"),
                "(d) key list shows 'spark (extra)', 'coin (extra)'");
    ok &= check(keys->commitCell(r2, AnimKeyListDock::ColSprite, QStringLiteral("coin (game)")) && doc->selectedNode()->spriteKeys[2].v == "game:coin" &&
                    keys->commitCell(r2, AnimKeyListDock::ColSprite, QStringLiteral("spark")) && doc->selectedNode()->spriteKeys[2].v == "extra:spark",
                "(d) key list cell: 'coin (game)' -> game:coin, typed 'spark' -> extra:spark");
    doc->undoStack()->undo();
    doc->undoStack()->undo();
    processEvents();
    selectGem();
    keys->raise();
    keys->selectRows({r1});
    processEvents();
    ok &= check(w.grab().save(QDir(outDir).filePath(QStringLiteral("keylist_atlases.png"))), "keylist_atlases.png");
    ok &= check(keys->grab().save(QDir(outDir).filePath(QStringLiteral("keylist_sprites.png"))), "keylist_sprites.png");

    // 4. Rename extra's id through the Atlases list's cell: every "extra:" reference follows, one step.
    AnimAtlasesPanel* panel = ed->propertiesDock()->atlasesPanel();
    const int beforeRename = doc->undoStack()->index();
    if (QTreeWidgetItem* it = panel->list()->topLevelItem(1)) it->setText(AnimAtlasesPanel::ColId, QStringLiteral("fx"));
    processEvents();
    selectGem();
    ok &= check(doc->atlasId(1) == QLatin1String("fx") && gem->spriteKeys[1].v == "fx:spark" && gem->spriteKeys[2].v == "fx:coin" &&
                    gem->spriteKeys[0].v == "game:coin" && doc->undoStack()->index() == beforeRename + 1 &&
                    drawnFrom(ed, "gem", 0.5f, &src) == extraPage &&
                    countLevel(checkAnim(doc->file(), doc->checkAtlases()), Problem::Error) == 0,
                "(d) rename id extra -> fx: references rewritten (1 step), still drawn, no errors");
    QString why;
    ok &= check(!doc->renameAtlasId(1, QStringLiteral("a:b"), &why) && why.contains(QLatin1Char(':')) &&
                    !doc->renameAtlasId(1, QStringLiteral("game"), &why) && why.contains(QStringLiteral("unique")) &&
                    !doc->renameAtlasId(1, QString(), &why) && doc->atlasId(1) == QLatin1String("fx"),
                "(d) ids: ':' / taken / empty refused");
    doc->undoStack()->undo();
    processEvents();
    selectGem();
    ok &= check(doc->atlasId(1) == QLatin1String("extra") && gem->spriteKeys[1].v == "extra:spark" &&
                    panel->list()->topLevelItem(1)->text(AnimAtlasesPanel::ColId) == QLatin1String("extra"),
                "(d) rename undone");

    // 5. Removing extra: its references would break -- counted, and errors after.
    const int refs = doc->atlasReferenceCount(1);
    ok &= check(refs == 3, "(d) extra is used by 3 references (spark node, 2 gem keys)");
    std::printf("selftest:   atlasReferenceCount(extra) = %d\n", refs);
    ok &= check(panel->removeAtlas(1) && hasProblemIn(doc, Problem::Error, QStringLiteral("'extra:coin'"), QStringLiteral("no atlas has the id")),
                "(d) remove extra: 'extra:coin' is an error");
    doc->undoStack()->undo();
    processEvents();

    // 6. Broken references are errors: an unknown id, a name the atlas lacks.
    selectGem();
    doc->editSelected(QStringLiteral("test"), [](Node& n) { n.sprite = "nope:coin"; });
    doc->editSelected(QStringLiteral("test"), [](Node& n) { n.spriteKeys[0].v = "extra:slime"; });
    ok &= check(hasProblemIn(doc, Problem::Error, QStringLiteral("'nope:coin'"), QStringLiteral("no atlas has the id 'nope'")) &&
                    hasProblemIn(doc, Problem::Error, QStringLiteral("'extra:slime'"), QStringLiteral("has no sprite 'slime'")),
                "(d) check: unknown id and missing name are errors");
    for (const Problem& p : checkAnim(doc->file(), doc->checkAtlases()))
        if (p.level == Problem::Error) std::printf("selftest:   error: %s: %s\n", p.where.c_str(), p.message.c_str());
    doc->undoStack()->undo();
    doc->undoStack()->undo();
    processEvents();

    // 7. Qualify: every bare name gets the atlas the lookup picks now (coin -> game:coin), one step.
    const int bare = doc->bareSpriteReferenceCount();
    const int beforeQualify = doc->undoStack()->index();
    ok &= check(bare > 0 && hasProblemIn(doc, Problem::Warning, QStringLiteral("'coin'"), QStringLiteral("does not name one")),
                "(d) bare names in the file; bare 'coin' warns");
    const int qualified = doc->qualifySpriteReferences();
    std::vector<std::string> all;
    for (const toms::anim::Clip& c : doc->file().clips) collectSpriteRefs(c.root, all);
    bool allQualified = !all.empty();
    for (const std::string& r : all) allQualified = allQualified && !toms::anim::parseSpriteRef(r).atlas.empty();
    doc->selectNodeByName(QStringLiteral("coin"));
    ok &= check(qualified == bare && allQualified && doc->selectedNode()->sprite == "game:coin" &&
                    doc->undoStack()->index() == beforeQualify + 1 && doc->bareSpriteReferenceCount() == 0 &&
                    !hasProblemIn(doc, Problem::Warning, QStringLiteral("coin"), QStringLiteral("several atlases")),
                "(d) qualify: every reference names its atlas, coin -> game:coin, warning gone (1 step)");
    std::printf("selftest:   qualified %d bare reference(s)\n", qualified);
    doc->undoStack()->undo();
    processEvents();
    ok &= check(doc->bareSpriteReferenceCount() == bare, "(d) qualify undone");

    // 8. A second game.atlas (a style's): its default id is taken, so it gets the style folder's.
    {
        const QString styleDir = QDir(workDir).filePath(QStringLiteral("styles/dark16/atlas"));
        QDir().mkpath(styleDir);
        QFile::remove(QDir(styleDir).filePath(QStringLiteral("game.atlas")));
        QFile::copy(QDir(workDir).filePath(QStringLiteral("extra/extra.png")), QDir(styleDir).filePath(QStringLiteral("extra.png")));
        QFile::copy(QDir(workDir).filePath(QStringLiteral("extra/extra.atlas")), QDir(styleDir).filePath(QStringLiteral("game.atlas")));
        const QString style = QDir(styleDir).filePath(QStringLiteral("game.atlas"));
        ok &= check(doc->suggestAtlasId(style) == QLatin1String("dark16") && ed->addAtlasFile(style) && doc->atlasCount() == 3 &&
                        doc->atlasId(2) == QLatin1String("dark16"),
                    "(d) a second game.atlas gets the id 'dark16'");
        QString err;
        ok &= check(!doc->addAtlas(style, QStringLiteral("x"), &err), "(d) the same file twice is refused");
        const toms::anim::AnimFile f = doc->file();
        ok &= check(f.atlases.size() == 3 && f.atlases[2].id == "dark16" &&
                        doc->toJson().contains(QStringLiteral("\"id\": \"dark16\"")),
                    "(d) stored as {\"id\": \"dark16\", \"path\": ...}");
        doc->undoStack()->undo();
        processEvents();
    }

    // 9. Save with the alternating keys, reopen: the same file.
    const QString saved = doc->filePath();
    const QString json = doc->toJson();
    ok &= check(ed->saveTo(saved), "(d) save");
    std::string text;
    ok &= check(readTextFile(saved.toStdString(), text) && text.find("\"extra:coin\"") != std::string::npos &&
                    text.find("\"extra:spark\"") != std::string::npos,
                "(d) the file stores \"extra:coin\", \"extra:spark\"");
    ok &= check(ed->openFile(saved, false) && doc->toJson() == json && doc->atlasId(1) == QLatin1String("extra"),
                "(d) reopen: animToJson equal");
    ok &= check(selectGem() && drawnFrom(ed, "gem", 0.5f, &src) == &doc->atlasAt(1).images && src == QRect(16, 0, 16, 16),
                "(d) reopened: t=0.5 draws extra's coin");
    std::fflush(stdout);
    ok &= check(headlessMain({"check", saved.toStdString()}) == 0, "(d) --headless check on it: exit 0");
    return ok;
}

int runSelfTest(AnimMainWindow& w, const QString& file, const QString& outDir)
{
    qApp->setProperty("toms.selftest", true);
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a crash still leaves the lines before it
    AnimEditor* ed = w.editor();
    AnimDocument* doc = ed->document();
    QDir().mkpath(outDir);
    bool ok = true;
    if (!ed->openFile(file, false)) {
        std::fprintf(stderr, "selftest: cannot open %s\n", file.toUtf8().constData());
        return 2;
    }
    // Work on a copy in outDir (it must be on the atlases' drive: atlas paths are stored relative).
    const QString workDir = QDir(outDir).filePath(QStringLiteral("work"));
    QDir(workDir).removeRecursively();
    QDir().mkpath(workDir);
    const QString work = QDir(workDir).filePath(QFileInfo(file).fileName());
    if (!ed->saveTo(work) || !ed->openFile(work, false)) {
        std::fprintf(stderr, "selftest: cannot copy %s to %s (outdir must be on the same drive as the atlas)\n",
                     file.toUtf8().constData(), work.toUtf8().constData());
        return 2;
    }
    doc->setAutoKey(true);
    doc->setKeyTogether(true);
    processEvents();
    ok &= check(doc->atlasLoaded() && doc->atlasCount() == 1, "atlas loaded (the scratch copy finds it)");
    ok &= check(!doc->spriteNames().isEmpty(), "sprites listed");
    const QString original = doc->toJson();

    // 1. The viewport draws what the game draws.
    ok &= check(checkDrawing(ed, 0.76f), "viewport quads == appendQuads at t=0.76");
    ok &= check(checkDrawing(ed, 0.0f), "viewport quads == appendQuads at t=0");

    // 2. Select a node, add keys through the UI's own paths, undo them.
    ok &= check(doc->selectNodeByName(QStringLiteral("slime")), "select node 'slime'");
    processEvents();
    AnimKeyListDock* keys = ed->keyListDock();
    const int rowsBefore = keys->table()->rowCount();
    std::vector<int> tracksBefore;   // key count per interpolated channel
    for (Channel c : kInterpolated) tracksBefore.push_back(keyCount(*doc->selectedNode(), c));
    doc->setTime(0.25f);
    keys->insertKey();   // the Insert button / K
    processEvents();
    const Node* slime = doc->selectedNode();
    bool onlyAnimated = slime != nullptr;
    for (size_t i = 0; slime && i < 4; i++) {
        const Channel c = kInterpolated[i];
        const bool animated = tracksBefore[i] > 0;
        onlyAnimated &= animated ? hasKeyAt(*slime, c, 0.25f) && keyCount(*slime, c) == tracksBefore[i] + 1
                                 : keyCount(*slime, c) == 0;
    }
    ok &= check(onlyAnimated && keys->table()->rowCount() == rowsBefore + 1,
                "insert key at 0.25: keys slime's animated channels only (no new tracks)");
    const int row = rowOf(keys, 0.25f);
    ok &= check(row >= 0 && keys->commitCell(row, AnimKeyListDock::ColPos, QStringLiteral("5, -20")) &&
                    toms::anim::positionAt(*doc->selectedNode(), 0.25f) == glm::vec2(5, -20),
                "edit a key list cell (pos = 5, -20)");
    ed->propertiesDock()->posXSpin()->setValue(7.0);   // a Properties edit with auto-key
    processEvents();
    ok &= check(toms::anim::positionAt(*doc->selectedNode(), 0.25f).x == 7.0f, "properties edit writes the key (pos.x = 7)");
    int undos = 0;
    while (doc->undoStack()->canUndo() && doc->toJson() != original && undos < 10) {
        doc->undoStack()->undo();
        undos++;
    }
    processEvents();
    ok &= check(doc->toJson() == original && !doc->isDirty() && undos == 3, "undo restores the file (3 steps)");

    // A viewport drag (Move tool, auto-key): click the gem, drag it in three steps = one undo step.
    {
        AnimViewport* vp = ed->viewport();
        doc->setTime(0.5f);
        processEvents();
        std::vector<toms::anim::NodePose> poses;
        std::vector<AnimViewport::DrawItem> items;
        vp->buildFrame(0.5f, poses, items, nullptr);
        QPointF gem;
        for (const AnimViewport::DrawItem& it : items)
            if (const Node* n = nodeAt(doc->clip()->root, it.path); n && n->name == "gem")
                gem = QPointF((it.quad.corners[0] + it.quad.corners[4]) / 2, (it.quad.corners[1] + it.quad.corners[5]) / 2);
        const QPointF at = vp->widgetPos(gem);
        const int stackBefore = doc->undoStack()->index();
        auto send = [&](QEvent::Type type, const QPointF& p, Qt::MouseButtons buttons) {
            QMouseEvent e(type, p, vp->mapToGlobal(p), Qt::LeftButton, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(vp, &e);
        };
        send(QEvent::MouseButtonPress, at, Qt::LeftButton);
        for (int i = 1; i <= 3; i++) send(QEvent::MouseMove, at + QPointF(10 * i, 0), Qt::LeftButton);
        send(QEvent::MouseButtonRelease, at + QPointF(30, 0), Qt::NoButton);
        const Node* g = doc->selectedNode();
        ok &= check(g && g->name == "gem" && hasKeyAt(*g, Channel::Pos, 0.5f) && doc->undoStack()->index() == stackBefore + 1,
                    "viewport: click selects gem, drag keys pos (1 undo step)");
        if (g) std::printf("selftest:   gem pos key at 0.5: %.3f, %.3f\n", toms::anim::positionAt(*g, 0.5f).x, toms::anim::positionAt(*g, 0.5f).y);
        doc->undoStack()->undo();
        ok &= check(doc->toJson() == original, "drag undone");
        doc->selectNodeByName(QStringLiteral("slime"));
        processEvents();
    }

    // 3. Key list tools, each undone.
    keys->selectRows({0, 1, 2});
    keys->fadeIn();
    slime = doc->selectedNode();
    ok &= check(slime && slime->colorKeys.size() == 3 && slime->colorKeys[0].v.a == 0.0f && std::fabs(slime->colorKeys[1].v.a - 0.5f) < 1e-6f &&
                    slime->colorKeys[2].v.a == 1.0f,
                "fade in over 3 rows (alpha 0, 0.5, 1)");
    doc->undoStack()->undo();
    ed->spritesDock()->selectSprites({QStringLiteral("bat"), QStringLiteral("slime")});
    keys->selectRows({0, 1, 2});
    keys->spriteSequence();
    slime = doc->selectedNode();
    ok &= check(slime && slime->spriteKeys.size() == 3 && slime->spriteKeys[0].v == "game:bat" && slime->spriteKeys[1].v == "game:slime",
                "sprite sequence (bat, slime, bat), stored with the atlas");
    doc->undoStack()->undo();
    QString conflict;
    ok &= check(!doc->moveRows({0.5f}, {1.0f}, &conflict) && !conflict.isEmpty(), "set time refuses a clash");
    ok &= check(doc->moveRows({0.5f}, {0.4f}, &conflict), "set time 0.5 -> 0.4");
    doc->undoStack()->undo();
    ok &= check(doc->toJson() == original, "tools undone");

    // 4. Play from 0 to 0.5 (AnimPlayer semantics; the 'hit' event at 0.5 fires).
    QStringList fired;
    QObject::connect(ed->transport(), &AnimTransport::eventsFired, ed, [&](const QStringList& n, float) { fired << n; });
    doc->setTime(0);
    ed->transport()->play();
    ed->transport()->advance(500);
    ed->transport()->pause();
    processEvents();
    ok &= check(std::fabs(doc->time() - 0.5f) < 1e-4f && fired.contains(QStringLiteral("hit")), "play to t=0.5 fires 'hit'");

    // 4b. A looping node keeps playing after the clip's end: the playhead stays at the end (edits go
    // there), the viewport draws a later time; pausing goes back to the playhead.
    {
        const QString before = doc->toJson();
        doc->editClip(QStringLiteral("loop root"), [](toms::anim::Clip& c) { c.root.loop = true; c.playCount = 1; });
        const bool clipLoop = ed->transport()->loopAction()->isChecked();
        ed->transport()->loopAction()->setChecked(false);   // the clip plays once, its node loops
        const float d = doc->duration();
        doc->setTime(0);
        ed->transport()->play();
        ed->transport()->advance(int(d * 1000) + 700);
        processEvents();
        ok &= check(ed->transport()->isPlaying() && std::fabs(doc->time() - d) < 1e-4f && doc->previewTime() > d + 0.6f,
                    "loop node: playback runs on past the end (playhead at the end, preview later)");
        ed->transport()->pause();
        ok &= check(doc->previewTime() == doc->time(), "pause: the preview is the playhead again");
        doc->undoStack()->undo();
        ok &= check(doc->toJson() == before, "loop undone");
        ed->transport()->loopAction()->setChecked(clipLoop);
    }

    // 4c. Clips dock: double-click opens a clip; a changed clip asks to discard its changes first.
    {
        AnimClipsDock* clips = ed->clipsDock();
        const int start = doc->undoStack()->index();
        const int home = doc->clipIndex();
        const size_t clipCount = doc->file().clips.size();
        doc->addClip(QStringLiteral("other"));
        const int other = doc->clipIndex();
        ok &= check(!doc->clipModified() && clips->openClip(home) && doc->clipIndex() == home,
                    "open a clip: an unchanged clip switches without asking");
        doc->editClip(QStringLiteral("len"), [](toms::anim::Clip& c) { c.length = 2.5f; });
        ok &= check(doc->clipModified(), "an edit marks the clip changed");
        ok &= check(!clips->openClip(other, 0) && doc->clipIndex() == home && doc->clipModified(),
                    "No: stays on the changed clip, changes kept");
        ok &= check(clips->openClip(other, 1) && doc->clipIndex() == other, "Yes: opens the other clip");
        ok &= check(doc->file().clips[size_t(home)].length != 2.5f, "Yes: the changes to the first clip are discarded");
        doc->undoStack()->undo();
        ok &= check(doc->file().clips[size_t(home)].length == 2.5f, "undo brings the discarded changes back");
        doc->setClipIndex(home);
        doc->deleteClip(other);
        ok &= check(doc->clipIndex() == home, "deleting another clip keeps the open one");
        doc->undoStack()->setIndex(start);
        doc->setClipIndex(home);
        ok &= check(doc->file().clips.size() == clipCount && doc->clipIndex() == home, "clips test undone");
    }

    // 4d. Automatic backups: after `edits` edits a copy goes to the backups folder; only `keep` stay;
    // the copy opens (absolute atlas paths). The user's settings are put back afterwards.
    {
        const AutoBackup::Settings saved = AutoBackup::settings();
        AutoBackup::setSettings({true, 60, 3, 2});
        const QString dir = QDir(AutoBackup::folder()).filePath(QFileInfo(doc->filePath()).completeBaseName());
        QDir(dir).removeRecursively();
        auto count = [&] { return int(QDir(dir).entryList({QStringLiteral("*.anim")}, QDir::Files).size()); };
        const int start = doc->undoStack()->index();
        for (int i = 1; i <= 2; i++) doc->editClip(QStringLiteral("b"), [i](toms::anim::Clip& c) { c.length = 1.0f + 0.1f * float(i); });
        ok &= check(count() == 0, "backup: not before 3 edits");
        doc->editClip(QStringLiteral("b"), [](toms::anim::Clip& c) { c.length = 1.3f; });
        ok &= check(count() == 1, "backup: written after 3 edits");
        for (int i = 4; i <= 9; i++) doc->editClip(QStringLiteral("b"), [i](toms::anim::Clip& c) { c.length = 1.0f + 0.1f * float(i); });
        ok &= check(count() == 2, "backup: only the newest 2 are kept");
        const QStringList files = QDir(dir).entryList({QStringLiteral("*.anim")}, QDir::Files, QDir::Name);
        toms::anim::AnimFile back;
        std::string err;
        QFile f(QDir(dir).filePath(files.isEmpty() ? QString() : files.last()));
        ok &= check(f.open(QIODevice::ReadOnly) && toms::anim::parseAnim(f.readAll().toStdString(), back, &err) &&
                        !back.atlases.empty() && QDir::isAbsolutePath(QString::fromStdString(back.atlases[0].path)) &&
                        std::fabs(back.clips[size_t(doc->clipIndex())].length - 1.9f) < 1e-4f,
                    "backup: the newest copy has the last edit and absolute atlas paths");
        f.close();
        doc->undoStack()->setIndex(start);
        QDir(dir).removeRecursively();
        AutoBackup::setSettings(saved);
    }

    // 5. Screenshots.
    processEvents();
    ok &= check(w.grab().save(QDir(outDir).filePath(QStringLiteral("main.png"))), "main.png");
    keys->raise();
    keys->selectRows({1});
    processEvents();
    ok &= check(w.grab().save(QDir(outDir).filePath(QStringLiteral("keylist.png"))), "keylist.png");
    if (auto* problems = w.findChild<QDockWidget*>(QStringLiteral("AnimProblemsDock"))) {
        problems->raise();
        processEvents();
        ok &= check(problems->grab().save(QDir(outDir).filePath(QStringLiteral("problems.png"))), "problems.png");
        keys->raise();
    }
    {
        toms::anim::Ease e;
        e.kind = toms::anim::Ease::kBezier;
        const float b[4] = {0.34f, 1.56f, 0.64f, 1.0f};
        std::copy(b, b + 4, e.bezier);
        BezierDialog dlg(e, &w);
        dlg.show();
        processEvents();
        ok &= check(dlg.grab().save(QDir(outDir).filePath(QStringLiteral("bezier.png"))), "bezier.png");
    }
    w.setDarkTheme(false);
    processEvents();
    ok &= check(w.grab().save(QDir(outDir).filePath(QStringLiteral("light.png"))), "light.png");
    w.setDarkTheme(true);

    // 6. Save As into outDir and reopen: the same file (the atlas paths are rebased).
    const QString saveDir = QDir(outDir).filePath(QStringLiteral("saveas"));
    QDir(saveDir).removeRecursively();
    QDir().mkpath(saveDir);
    const QString saved = QDir(saveDir).filePath(QFileInfo(file).fileName());
    ok &= check(ed->saveTo(saved) && !doc->isDirty(), "save as");
    const QString before = doc->toJson();
    const QStringList atlasesBefore = doc->atlasPaths();
    ok &= check(ed->openFile(saved, false) && doc->toJson() == before && doc->atlasLoaded() && doc->atlasPaths() == atlasesBefore,
                "reopen: animToJson equal, atlas found");
    std::string text;
    toms::anim::AnimFile reparsed;
    ok &= check(readTextFile(saved.toStdString(), text) && toms::anim::parseAnim(text, reparsed) &&
                    QString::fromStdString(toms::anim::animToJson(reparsed)) == before,
                "saved file parses back to the same JSON");
    std::printf("selftest: saved %s (atlases %s)\n", QDir::toNativeSeparators(saved).toUtf8().constData(),
                reparsed.atlases.empty() ? "none" : reparsed.atlases.front().path.c_str());

    // The file on disk: an "atlases" array of `expected` relative paths that exist, no "atlas".
    auto storedRelative = [](const QString& path, int expected) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonArray a = o.value(QStringLiteral("atlases")).toArray();
        if (o.contains(QStringLiteral("atlas")) || a.size() != expected) return false;
        for (const QJsonValue& v : a) {
            const QString p = v.toString();
            if (p.isEmpty() || QDir::isAbsolutePath(p) || isAbsoluteAtlasPath(p.toStdString()) ||
                !QFileInfo::exists(QFileInfo(path).absoluteDir().filePath(p)))
                return false;
        }
        return true;
    };
    auto writeFile = [](const QString& path, const QByteArray& bytes) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
    };
    auto hasProblem = [&](Problem::Level level, const QString& a, const QString& b) {
        for (const Problem& p : checkAnim(doc->file(), doc->checkAtlases())) {
            const QString m = QString::fromStdString(p.message);
            if (p.level == level && m.contains(a) && m.contains(b)) return true;
        }
        return false;
    };
    const QString gameAtlas = doc->atlasPaths().value(0);
    ok &= check(storedRelative(saved, 1), "(a) saved file: \"atlases\" with relative paths only");

    // (b) Save As onto another drive than the atlas: refused, nothing written.
    {
        const QString other = QDir(QDir::tempPath()).filePath(QStringLiteral("toms_anim_selftest_other_drive.anim"));
        QFile::remove(other);
        if (QDir::isAbsolutePath(QDir(QDir::tempPath()).relativeFilePath(gameAtlas))) {
            QString err;
            ok &= check(!doc->save(other, &err) && !QFileInfo::exists(other) && doc->filePath() == QFileInfo(saved).absoluteFilePath() &&
                            err.contains(QStringLiteral("game.atlas")),
                        "(b) Save As to another drive is refused");
            std::printf("selftest:   \"%s\"\n", err.section(QLatin1Char('\n'), 0, 0).toUtf8().constData());
            ok &= check(!ed->saveTo(other) && !QFileInfo::exists(other), "(b) ... through the editor too");
        } else {
            std::printf("selftest: (b) skipped: the temp folder is on the atlas's drive\n");
        }
    }

    // An older file's single "atlas" opens and saves as "atlases"; an absolute path is warned
    // about and stored relative on save.
    {
        QFile in(saved);
        in.open(QIODevice::ReadOnly);
        QJsonObject o = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
        const QString rel = o.value(QStringLiteral("atlases")).toArray().at(0).toString();
        o.remove(QStringLiteral("atlases"));
        o.insert(QStringLiteral("atlas"), rel);
        const QString legacy = QDir(workDir).filePath(QStringLiteral("legacy.anim"));
        writeFile(legacy, QJsonDocument(o).toJson());
        ok &= check(ed->openFile(legacy, false) && doc->atlasCount() == 1 && doc->atlasLoaded(), "old file (\"atlas\": \"...\") opens");
        ok &= check(ed->saveTo(legacy) && storedRelative(legacy, 1), "... and saves \"atlases\"");
        o.remove(QStringLiteral("atlas"));
        o.insert(QStringLiteral("atlases"), QJsonArray{gameAtlas});
        const QString absFile = QDir(workDir).filePath(QStringLiteral("absolute.anim"));
        writeFile(absFile, QJsonDocument(o).toJson());
        ok &= check(ed->openFile(absFile, false) && doc->atlasLoaded() && doc->atlasPathIsAbsoluteInFile(0),
                    "absolute atlas path: the file opens, the atlas loads");
        ok &= check(hasProblem(Problem::Warning, QStringLiteral("absolute atlas path"), QStringLiteral("relative")),
                    "... Problems: 'absolute atlas path' warning");
        ok &= check(ed->saveTo(absFile) && storedRelative(absFile, 1) && !doc->atlasPathIsAbsoluteInFile(0) &&
                        !hasProblem(Problem::Warning, QStringLiteral("absolute atlas path"), QString()),
                    "... stored relative on save, warning gone");
    }

    // (c) Two atlases: a small second one with a sprite game.atlas lacks (spark) and a name it
    // has too (coin).
    {
        ok &= check(ed->openFile(work, false) && doc->atlasCount() == 1, "reopen the scratch copy");
        const QString extraDir = QDir(workDir).filePath(QStringLiteral("extra"));
        QDir().mkpath(extraDir);
        QImage page(32, 16, QImage::Format_ARGB32);
        page.fill(Qt::transparent);
        {
            QPainter p(&page);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(QColor(90, 210, 255));
            p.setPen(Qt::NoPen);
            p.drawPolygon(QPolygonF({QPointF(8, 0), QPointF(10, 6), QPointF(16, 8), QPointF(10, 10), QPointF(8, 16), QPointF(6, 10),
                                     QPointF(0, 8), QPointF(6, 6)}));
            p.fillRect(QRect(18, 2, 12, 12), QColor(255, 60, 200));
        }
        const QString extra = QDir(extraDir).filePath(QStringLiteral("extra.atlas"));
        ok &= check(page.save(QDir(extraDir).filePath(QStringLiteral("extra.png"))) &&
                        writeFile(extra, "extra.png\nsize: 32, 16\nformat: RGBA8888\nfilter: Nearest, Nearest\nrepeat: none\n"
                                         "spark\n  bounds: 0, 0, 16, 16\ncoin\n  bounds: 16, 0, 16, 16\n"),
                    "(c) write extra.atlas + extra.png");
        const int stack = doc->undoStack()->index();
        ok &= check(ed->addAtlasFile(extra) && doc->atlasCount() == 2 && doc->atlasLoaded() && doc->undoStack()->index() == stack + 1,
                    "(c) add it as the second atlas (1 undo step)");
        int ai = -1, bi = -1;
        ok &= check(doc->findSprite("coin", &ai) && ai == 0 && doc->findSprite("spark", &bi) && bi == 1,
                    "(c) lookup order: coin from game.atlas, spark from extra.atlas");
        ok &= check(doc->spriteNames().contains(QStringLiteral("spark")) && doc->spriteNames().count(QStringLiteral("coin")) == 1,
                    "(c) Sprites: names of both atlases");
        ok &= check(doc->atlasId(0) == QLatin1String("game") && doc->atlasId(1) == QLatin1String("extra"), "(c) atlas ids: game, extra");
        ok &= check(doc->addSpriteNode(NodePath(), QStringLiteral("extra:spark"), glm::vec2(0, 72)) && doc->selectedNode() &&
                        doc->selectedNode()->name == "spark",
                    "(c) add a node with 'extra:spark' (named spark)");
        std::vector<toms::anim::NodePose> poses;
        std::vector<AnimViewport::DrawItem> items;
        ed->viewport()->buildFrame(0.0f, poses, items, nullptr);
        bool spark = false, coin = false;
        for (const AnimViewport::DrawItem& it : items) {
            const Node* n = nodeAt(doc->clip()->root, it.path);
            if (n && n->name == "spark") spark = it.images == &doc->atlasAt(1).images && it.src == QRect(0, 0, 16, 16);
            if (n && n->name == "coin") coin = it.images == &doc->atlasAt(0).images && it.src.size() == QSize(32, 32);
        }
        ok &= check(spark && coin, "(c) viewport: spark from extra.atlas's page, coin from game.atlas's");
        ok &= check(checkDrawing(ed, 0.0f), "(c) viewport quads == appendQuads over both atlases");
        ok &= check(hasProblem(Problem::Warning, QStringLiteral("'coin'"), QStringLiteral("several atlases")) &&
                        countLevel(checkAnim(doc->file(), doc->checkAtlases()), Problem::Error) == 0,
                    "(c) check: 'coin' in two atlases is a warning, no errors");
        ok &= check(ed->saveTo(work) && storedRelative(work, 2), "(c) saved: two relative atlas paths");
        std::fflush(stdout);
        ok &= check(headlessMain({"check", work.toStdString()}) == 0, "(c) --headless check on it: exit 0 (warnings only)");
        ok &= check(doc->moveAtlas(1, -1) && doc->findSprite("coin", &ai) && ai == 0 &&
                        QFileInfo(doc->atlasAt(0).path).fileName() == QLatin1String("extra.atlas"),
                    "(c) move extra.atlas up: coin now comes from it");
        doc->undoStack()->undo();
        ok &= check(QFileInfo(doc->atlasAt(0).path).fileName() == QLatin1String("game.atlas") && doc->atlasAt(1).ok, "(c) undo the move");

        ok &= check(atlasReferences(ed, workDir, outDir, w), "(d) sprite references store their atlas");

        // Screenshots: the Atlases list, the Sprites dock grouped by atlas.
        ed->propertiesDock()->raise();
        ed->propertiesDock()->atlasesPanel()->setCurrentIndex(1);
        ed->spritesDock()->selectSprites({QStringLiteral("spark")});
        if (QListWidget* list = ed->spritesDock()->list()) list->scrollToBottom();
        processEvents();
        ok &= check(w.grab().save(QDir(outDir).filePath(QStringLiteral("atlases.png"))), "atlases.png");
        ok &= check(ed->propertiesDock()->grab().save(QDir(outDir).filePath(QStringLiteral("properties.png"))), "properties.png");
        ok &= check(ed->spritesDock()->grab().save(QDir(outDir).filePath(QStringLiteral("sprites.png"))), "sprites.png");
        if (auto* filter = ed->spritesDock()->findChild<QLineEdit*>()) {
            filter->setText(QStringLiteral("coin"));
            if (QListWidget* list = ed->spritesDock()->list()) list->scrollToTop();
            processEvents();
            // Floating and taller, so both coins (game's and extra's) show with their badges.
            QDockWidget* dock = ed->spritesDock();
            dock->setFloating(true);
            dock->resize(340, 240);
            processEvents();
            ok &= check(dock->grab().save(QDir(outDir).filePath(QStringLiteral("sprites_dup.png"))), "sprites_dup.png");
            dock->setFloating(false);
            processEvents();
            filter->clear();
        }
        {   // One tab per atlas: the second tab (extra) in front.
            AnimSpritesDock* sd = ed->spritesDock();
            ok &= check(sd->tabs()->count() == doc->atlasCount() && sd->tabs()->tabText(0).startsWith(doc->atlasId(0)) &&
                            sd->tabs()->tabText(1).startsWith(doc->atlasId(1)),
                        "(c) Sprites dock: one tab per atlas, titled with its id");
            sd->tabs()->setCurrentIndex(1);
            processEvents();
            ok &= check(sd->list() == sd->listFor(1) && sd->list()->count() == 2, "(c) the extra tab lists its 2 sprites");
            ok &= check(sd->grab().save(QDir(outDir).filePath(QStringLiteral("sprites_tabs.png"))), "sprites_tabs.png");
            sd->tabs()->setCurrentIndex(0);
        }
        if (auto* problems = w.findChild<QDockWidget*>(QStringLiteral("AnimProblemsDock"))) {
            problems->raise();
            processEvents();
            ok &= check(problems->grab().save(QDir(outDir).filePath(QStringLiteral("problems_atlases.png"))), "problems_atlases.png");
        }

        ok &= check(doc->removeAtlas(1) && doc->atlasCount() == 1 && !doc->findSprite("spark") &&
                        hasProblem(Problem::Error, QStringLiteral("'extra:spark'"), QStringLiteral("no atlas has the id 'extra'")),
                    "(c) remove it: 'extra:spark' names an unknown atlas (error)");
        doc->undoStack()->undo();
        ok &= check(doc->atlasCount() == 2 && doc->findSprite("spark"), "(c) undo the remove");

        // An untitled file may hold atlases only in memory: adding one asks to save first.
        doc->newFile();
        processEvents();
        ok &= check(doc->filePath().isEmpty() && doc->atlasCount() == 2 && !ed->addAtlasFile(extra) &&
                        ed->problemsDock()->logText().contains(QStringLiteral("save the animation first")),
                    "untitled: adding an atlas asks to save the .anim first");
    }
    std::printf("selftest: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}

// Right-drag on a canvas: it must pan (the content moves with the mouse), as middle-drag does.
static bool rightDragPans(CanvasView* view)
{
    const QPointF before = view->contentToWidget(QPointF(0, 0));
    const QPointF a(view->width() / 2.0, view->height() / 2.0), b = a + QPointF(60, 25);
    auto send = [&](QEvent::Type type, const QPointF& at, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(type, at, view->mapToGlobal(at), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(view, &e);
    };
    send(QEvent::MouseButtonPress, a, Qt::RightButton, Qt::RightButton);
    for (int i = 1; i <= 4; i++) send(QEvent::MouseMove, a + (b - a) * (i / 4.0), Qt::NoButton, Qt::RightButton);
    send(QEvent::MouseButtonRelease, b, Qt::RightButton, Qt::NoButton);
    QCoreApplication::processEvents();
    const QPointF moved = view->contentToWidget(QPointF(0, 0)) - before;
    return std::fabs(moved.x() - 60) < 1 && std::fabs(moved.y() - 25) < 1;
}

// The viewport with the game renderer (on screen): it starts, draws, and a bgfx screenshot of the
// clip at a fixed time is saved for comparing with toms_game --anim.
int runGpuSelfTest(AnimMainWindow& w, const QString& file, const QString& outDir)
{
    qApp->setProperty("toms.selftest", true);
    QDir().mkpath(outDir);
    AnimEditor* ed = w.editor();
    auto pump = [](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    };
    bool ok = check(ed->openFile(file, false), "open the clip");
    pump(500);
    AnimViewport* view = ed->viewport();
    ok &= check(view->usingGameRenderer(), "the viewport runs on the game renderer");
    std::printf("selftest:   %s\n", view->rendererName().toUtf8().constData());
    if (!view->usingGameRenderer()) return 1;
    ed->document()->setTime(0.5f);
    const int f0 = view->framesDrawn();
    pump(600);
    ok &= check(view->framesDrawn() > f0, "frames are drawn");
    ok &= check(rightDragPans(view), "right-drag pans the viewport (game renderer window)");
    const int before = toms::next::bgfxHostScreenshotsWritten();
    view->saveGameRendererShot(QDir(outDir).filePath(QStringLiteral("anim_gpu.png")));
    QElapsedTimer t;
    t.start();
    while (toms::next::bgfxHostScreenshotsWritten() == before && t.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    ok &= check(toms::next::bgfxHostScreenshotsWritten() > before, "anim_gpu.png (bgfx screenshot)");
    auto shot = [&](const char* name) {
        const int n = toms::next::bgfxHostScreenshotsWritten();
        view->saveGameRendererShot(QDir(outDir).filePath(QString::fromLatin1(name)));
        QElapsedTimer st;
        st.start();
        while (toms::next::bgfxHostScreenshotsWritten() == n && st.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        return toms::next::bgfxHostScreenshotsWritten() > n;
    };
    // Double-click another clip in the Clips dock (real mouse events): the viewport shows that clip.
    if (ed->document()->file().clips.size() > 1) {
        auto* list = ed->clipsDock()->findChild<QListWidget*>();
        ed->clipsDock()->show();
        ed->clipsDock()->raise();
        pump(200);
        const QPoint at = list->visualItemRect(list->item(1)).center();
        for (QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease}) {
            QMouseEvent e(type, QPointF(at), list->viewport()->mapToGlobal(QPointF(at)), Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(list->viewport(), &e);
        }
        const int f1 = view->framesDrawn();
        pump(600);
        ok &= check(ed->document()->clipIndex() == 1, "double-click opens clip 2");
        ok &= check(view->framesDrawn() > f1, "the viewport redraws after the switch");
        ok &= check(shot("anim_clip2.png"), "anim_clip2.png (the second clip)");
    }
    // Solo the first child of the root's first child (in anim_child_timing's second clip: the coin
    // and the gem it carries): the player is not drawn, the coin and gem are, in their place.
    {
        AnimDocument* doc = ed->document();
        const toms::anim::Clip* c = doc->clip();
        if (c && !c->root.children.empty() && !c->root.children[0].children.empty()) {
            const NodePath solo{0, 0};
            doc->setSolo(solo);
            const toms::anim::Node& parent = c->root.children[0];
            const toms::anim::Node& kid = parent.children[0];
            ok &= check(doc->soloShows(&kid) && !doc->soloShows(&parent) && !doc->soloShows(&c->root) &&
                            (kid.children.empty() || doc->soloShows(&kid.children[0])),
                        "solo: the node and its children are drawn, its parents not");
            pump(300);
            ok &= check(shot("anim_solo.png"), "anim_solo.png (solo)");
            doc->setClipIndex(0);
            ok &= check(!doc->solo(), "another clip ends the solo");
        }
    }
    std::printf("selftest: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args = Console::utf8Args(argc, argv);
    if (!args.empty() && args[0] == "--headless") {
        Console::attachParent();
        const int code = animed::headlessMain(std::vector<std::string>(args.begin() + 1, args.end()));
        std::fflush(stdout);
        std::fflush(stderr);
        return code;
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TOMS"));
    QApplication::setApplicationName(QStringLiteral("AnimEditor"));
    QApplication::setApplicationDisplayName(QStringLiteral("Anim Editor"));
    QApplication::setWindowIcon(Icons::icon(Icons::Id::Rotate));
    Theme::apply(Theme::Mode::Dark);

    const QStringList qargs = QCoreApplication::arguments().mid(1);
    if (qargs.size() == 3 && qargs[0] == QLatin1String("--selftest")) {
        Console::attachParent();
        AnimMainWindow w;
        w.setDarkTheme(true);
        w.resize(1440, 900);
        w.show();
        const int code = runSelfTest(w, qargs[1], qargs[2]);
        std::fflush(stdout);
        return code;
    }
    if (qargs.size() == 3 && qargs[0] == QLatin1String("--selftest-gpu")) {   // on screen: the game renderer
        Console::attachParent();
        qApp->setProperty("toms.selftest", true);
        AnimMainWindow w;
        w.setDarkTheme(true);
        w.resize(1440, 900);
        w.show();
        const int code = runGpuSelfTest(w, qargs[1], qargs[2]);
        std::fflush(stdout);
        return code;
    }
    AnimMainWindow w;
    w.show();
    if (!qargs.isEmpty()) w.editor()->openFile(qargs.first());
    return app.exec();
}
