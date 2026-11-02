#include "AnimEditor.h"
#include "AnimExport.h"
#include <QMimeData>
#include <QGuiApplication>
#include <QClipboard>

#include "AnimAtlasesPanel.h"
#include "AnimClipsDock.h"
#include "AutoBackup.h"
#include "AnimDocument.h"
#include "AnimKeyListDock.h"
#include "AnimNodesDock.h"
#include "AnimProblemsDock.h"
#include "AnimPropertiesDock.h"
#include "AnimSpriteDrop.h"
#include "AnimSpritesDock.h"
#include "AnimTransport.h"
#include "AnimViewport.h"
#include "Icons.h"
#include "Language.h"
#include "anim_recipes.h"
#include "mpdi_convert.h"

#include <QAction>
#include <QActionGroup>
#include <QToolButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

using toms::anim::Node;

namespace {

constexpr int g_iMaxRecent = 10;

QAction* f_MakeAction(QObject* l_pParent, const QString& l_oText, const QKeySequence& l_oKey = QKeySequence(), int l_iIcon = -1)
{
    auto* l_pA = new QAction(l_oText, l_pParent);
    if (!l_oKey.isEmpty()) l_pA->setShortcut(l_oKey);
    if (l_iIcon >= 0) l_pA->setIcon(Icons::f_Icon(Icons::Id(l_iIcon)));
    return l_pA;
}

bool f_SelftestRunning() { return qApp && qApp->property("toms.selftest").toBool(); }

}  // namespace

QSettings AnimEditor::f_Settings() { return QSettings(QStringLiteral("TOMS"), QStringLiteral("AnimEditor")); }

AnimEditor::AnimEditor(QMainWindow* l_pHost)
    : QObject(l_pHost)
    , m_pHost(l_pHost)
    , m_pDoc(new AnimDocument(this))
    , m_pViewport(new AnimViewport(m_pDoc, GameCanvasView::f_GameRendererWanted(QStringLiteral("AnimEditor")), l_pHost))
{
    m_pTransport = new AnimTransport(m_pDoc, l_pHost);
    f_CreateActions();
    f_CreateDocks();
    f_CreateMenus();
    f_CreateToolBars();
    m_pKeys->f_SetSpriteSource([this] { return m_pSprites->f_SelectedSprites(); });

    connect(m_pDoc, &AnimDocument::f_DirtyChanged, this, &AnimEditor::f_TitleChanged);
    connect(m_pDoc, &AnimDocument::f_FilePathChanged, this, &AnimEditor::f_TitleChanged);
    connect(m_pDoc, &AnimDocument::f_SelectionChanged, this, &AnimEditor::f_UpdateActions);
    connect(m_pDoc, &AnimDocument::f_FileChanged, this, &AnimEditor::f_UpdateActions);
    connect(m_pDoc, &AnimDocument::f_AtlasChanged, this, &AnimEditor::f_UpdateActions);
    connect(m_pDoc, &AnimDocument::f_Message, this, [this](const QString& l_oM) { m_pHost->statusBar()->showMessage(l_oM, 6000); });
    connect(m_pTransport, &AnimTransport::f_EventsFired, this, [this](const QStringList& l_oNames, float l_fT) {
        const QString l_oText = tr("event %1 at %2 s").arg(l_oNames.join(QStringLiteral(", "))).arg(l_fT, 0, 'f', 3);
        m_pHost->statusBar()->showMessage(l_oText, 3000);
        m_pProblems->f_AppendLog(l_oText);
    });
    // Editing while the clip plays: pause at the frame on screen. Otherwise every drag step (Auto-Key on) keys a new
    // time as the playhead moves on, and the clip fills with keys.
    auto l_fnPauseForEdit = [this] {
        if (!m_pTransport->f_IsPlaying()) return;
        m_pTransport->f_Pause();
        m_pHost->statusBar()->showMessage(tr("Playback paused for the edit: keys go to the frame on screen"), 4000);
    };
    connect(m_pViewport, &AnimViewport::f_DragStarted, this, l_fnPauseForEdit);
    connect(m_pDoc, &AnimDocument::f_AboutToEdit, this, l_fnPauseForEdit);
    connect(m_pViewport, &AnimViewport::f_ToolChanged, this, [this](AnimViewport::Tool l_eT) {
        (l_eT == AnimViewport::Tool::Move ? m_pMoveAct : l_eT == AnimViewport::Tool::Rotate ? m_pRotateAct : m_pScaleAct)->setChecked(true);
    });
    connect(m_pDoc, &AnimDocument::f_AutoKeyChanged, m_pAutoKeyAct, &QAction::setChecked);
    f_UpdateActions();
}

QString AnimEditor::f_Title() const { return m_pDoc->f_DisplayName() + QStringLiteral("[*]"); }

QList<QDockWidget*> AnimEditor::f_Docks() const
{
    return {m_pClips, m_pNodes, m_pSprites, m_pProps, m_pHistory, m_pKeys, m_pEvents, m_pTimeline, m_pProblems};
}

// ---- construction -------------------------------------------------------------------------------

void AnimEditor::f_CreateActions()
{
    using Id = Icons::Id;
    m_pNewAct = f_MakeAction(this, tr("&New"), QKeySequence::New, int(Id::New));
    m_pOpenAct = f_MakeAction(this, tr("&Open…"), QKeySequence::Open, int(Id::Open));
    m_pSaveAct = f_MakeAction(this, tr("&Save"), QKeySequence::Save, int(Id::Save));
    m_pSaveAsAct = f_MakeAction(this, tr("Save &As…"), QKeySequence::SaveAs);
    m_pAddAtlasAct = f_MakeAction(this, tr("Add &Atlas…"), QKeySequence(), int(Id::Grid));
    m_pAddAtlasAct->setToolTip(tr("Add an .atlas the sprites come from (Properties > Atlases; stored relative to the .anim)"));
    m_pReloadAtlasAct = f_MakeAction(this, tr("&Reload Atlases"), QKeySequence(tr("Ctrl+Shift+R")));
    m_pQuitAct = f_MakeAction(this, tr("&Quit"), QKeySequence::Quit);
    connect(m_pNewAct, &QAction::triggered, this, [this] {
        if (f_MaybeSave()) m_pDoc->f_NewFile();
    });
    connect(m_pOpenAct, &QAction::triggered, this, [this] {
        if (!f_MaybeSave()) return;
        const QString l_oPath = QFileDialog::getOpenFileName(m_pHost, tr("Open Animation"), f_LastDir(), tr("Animations (*.anim);;All files (*)"));
        if (!l_oPath.isEmpty()) f_OpenFile(l_oPath);
    });
    connect(m_pSaveAct, &QAction::triggered, this, &AnimEditor::f_Save);
    connect(m_pSaveAsAct, &QAction::triggered, this, [this] { f_SaveAs(); });
    connect(m_pAddAtlasAct, &QAction::triggered, this, &AnimEditor::f_OnAddAtlas);
    connect(m_pReloadAtlasAct, &QAction::triggered, m_pDoc, &AnimDocument::f_ReloadAtlases);
    connect(m_pQuitAct, &QAction::triggered, m_pHost, &QWidget::close);

    m_pUndoAct = m_pDoc->f_UndoStack()->createUndoAction(this, tr("&Undo"));
    m_pUndoAct->setShortcut(QKeySequence::Undo);
    m_pUndoAct->setIcon(Icons::f_Icon(Id::Back));
    m_pRedoAct = m_pDoc->f_UndoStack()->createRedoAction(this, tr("&Redo"));
    m_pRedoAct->setShortcut(QKeySequence::Redo);
    m_pRedoAct->setIcon(Icons::f_Icon(Id::Forward));

    m_pAddChildAct = f_MakeAction(this, tr("Add &Child Node"), QKeySequence(tr("Ctrl+Shift+N")), int(Id::Node));
    m_pAddChildAct->setToolTip(tr("A group node under the selected node"));
    m_pAddSiblingAct = f_MakeAction(this, tr("Add &Sibling Node"), QKeySequence(), int(Id::Add));
    m_pAddSpriteAct = f_MakeAction(this, tr("Add S&prite Node"), QKeySequence(tr("Ctrl+Shift+A")), int(Id::SpriteNode));
    m_pAddSpriteAct->setToolTip(tr("A node with the sprite picked in the Sprites dock, under the selected node"));
    m_pDupAct = f_MakeAction(this, tr("&Duplicate Node"), QKeySequence(tr("Ctrl+D")), int(Id::Duplicate));
    m_pDeleteAct = f_MakeAction(this, tr("D&elete Node"), QKeySequence::Delete, int(Id::Remove));
    m_pRenameAct = f_MakeAction(this, tr("Re&name Node"), QKeySequence(Qt::Key_F2), int(Id::Rename));
    m_pUpAct = f_MakeAction(this, tr("Move &Up"), QKeySequence(tr("Ctrl+Up")), int(Id::Up));
    m_pDownAct = f_MakeAction(this, tr("Move Do&wn"), QKeySequence(tr("Ctrl+Down")), int(Id::Down));
    connect(m_pAddChildAct, &QAction::triggered, this, [this] {
        Node l_oN;
        l_oN.m_strName = "node";
        m_pDoc->f_AddNode(m_pDoc->f_SelectedPath(), l_oN, tr("Add node"));
    });
    connect(m_pAddSiblingAct, &QAction::triggered, this, [this] {
        NodePath l_vP = m_pDoc->f_SelectedPath();
        if (!l_vP.empty()) l_vP.pop_back();
        Node l_oN;
        l_oN.m_strName = "node";
        m_pDoc->f_AddNode(l_vP, l_oN, tr("Add node"));
    });
    connect(m_pAddSpriteAct, &QAction::triggered, this, &AnimEditor::f_AddSpriteNode);
    connect(m_pDupAct, &QAction::triggered, this, [this] { m_pDoc->f_DuplicateNode(m_pDoc->f_SelectedPath()); });
    // Copy / paste nodes: the subtree as text on the system clipboard (another editor window or a text file can take it).
    m_pCutAct = f_MakeAction(this, tr("Cu&t Node"), QKeySequence::Cut, int(Id::Remove));
    m_pCutAct->setToolTip(tr("Copy the selected node with its children, keys and skin, then delete it"));
    m_pCopyAct = f_MakeAction(this, tr("&Copy Node"), QKeySequence::Copy, int(Id::Duplicate));
    m_pCopyAct->setToolTip(tr("Copy the selected node with its children, keys and skin (as text: it pastes into another editor window too)"));
    m_pPasteAct = f_MakeAction(this, tr("&Paste Node"), QKeySequence::Paste, int(Id::Add));
    m_pPasteAct->setToolTip(tr("Paste a copied node under the selected node. Names the clip already has get _2, _3, ...; "
                              "a pasted skin follows its own pasted bones"));
    m_pPasteSiblingAct = f_MakeAction(this, tr("Paste Node as &Sibling"), QKeySequence(tr("Ctrl+Shift+V")), int(Id::Add));
    m_pPasteSiblingAct->setToolTip(tr("Paste a copied node right after the selected node, under the same parent"));
    connect(m_pCutAct, &QAction::triggered, this, [this] { f_CopyNode(true); });
    connect(m_pCopyAct, &QAction::triggered, this, [this] { f_CopyNode(false); });
    connect(m_pPasteAct, &QAction::triggered, this, [this] { f_PasteNode(false); });
    connect(m_pPasteSiblingAct, &QAction::triggered, this, [this] { f_PasteNode(true); });
    connect(m_pDeleteAct, &QAction::triggered, this, [this] { m_pDoc->f_DeleteNode(m_pDoc->f_SelectedPath()); });
    connect(m_pUpAct, &QAction::triggered, this, [this] { f_MoveSelected(-1); });
    connect(m_pDownAct, &QAction::triggered, this, [this] { f_MoveSelected(1); });

    m_pMoveAct = f_MakeAction(this, tr("&Move Tool"), QKeySequence(Qt::Key_W), int(Id::Move));
    m_pRotateAct = f_MakeAction(this, tr("&Rotate Tool"), QKeySequence(Qt::Key_E), int(Id::Rotate));
    m_pScaleAct = f_MakeAction(this, tr("&Scale Tool"), QKeySequence(Qt::Key_R), int(Id::Scale));
    auto* l_pTools = new QActionGroup(this);
    for (QAction* l_pA : {m_pMoveAct, m_pRotateAct, m_pScaleAct}) {
        l_pA->setCheckable(true);
        l_pTools->addAction(l_pA);
    }
    m_pMoveAct->setChecked(true);
    m_pMoveAct->setToolTip(tr("Move (W): drag the arrows, the centre box or a sprite; Ctrl snaps to pixels"));
    m_pRotateAct->setToolTip(tr("Rotate (E): drag the ring; Shift snaps to 15°"));
    m_pScaleAct->setToolTip(tr("Scale (R): drag the axis handles (Shift keeps proportions) or the centre box"));
    connect(m_pMoveAct, &QAction::triggered, this, [this] { m_pViewport->f_SetTool(AnimViewport::Tool::Move); });
    connect(m_pRotateAct, &QAction::triggered, this, [this] { m_pViewport->f_SetTool(AnimViewport::Tool::Rotate); });
    connect(m_pScaleAct, &QAction::triggered, this, [this] { m_pViewport->f_SetTool(AnimViewport::Tool::Scale); });
    m_pAutoKeyAct = f_MakeAction(this, tr("&Auto-Key"), QKeySequence(Qt::Key_N), int(Id::AutoKey));
    m_pAutoKeyAct->setCheckable(true);
    m_pAutoKeyAct->setChecked(m_pDoc->f_AutoKey());
    m_pAutoKeyAct->setToolTip(tr("Auto-key (N): edits write a key at the playhead; off, they change the rest values"));
    connect(m_pAutoKeyAct, &QAction::toggled, m_pDoc, &AnimDocument::f_SetAutoKey);
    m_pSnapAct = f_MakeAction(this, tr("Pixel S&nap"), QKeySequence(), int(Id::Snap));
    m_pSnapAct->setCheckable(true);
    m_pSnapAct->setToolTip(tr("Snap moves to whole pixels, rotation to degrees, scale to 0.05"));
    connect(m_pSnapAct, &QAction::toggled, m_pViewport, &AnimViewport::f_SetSnap);
    m_pShowSkinAct = new QAction(tr("Show &Skin"), this);
    m_pShowSkinAct->setCheckable(true);
    m_pShowSkinAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_K));
    m_pShowSkinAct->setToolTip(tr("Over every skinned node: its mesh, its points coloured by the bones they follow, and its "
                                 "bones. Select a bone to see how much each point follows it (bright = fully, dark = not)."));
    connect(m_pShowSkinAct, &QAction::toggled, m_pViewport, &AnimViewport::f_SetShowSkin);
    // Object / Bones: what a click in the viewport picks (docs/engine/16_MESH_ANIMATION.md).
    m_pObjectModeAct = f_MakeAction(this, tr("&Object Mode"), QKeySequence(Qt::Key_O), int(Id::SpriteNode));
    m_pObjectModeAct->setToolTip(tr("Object mode (O): a click picks a sprite; moving a skinned sprite moves its mesh and the "
                                   "bones under it"));
    m_pBoneModeAct = f_MakeAction(this, tr("&Bones Mode"), QKeySequence(Qt::Key_B), int(Id::Node));
    m_pBoneModeAct->setToolTip(tr("Bones mode (B): the skin is shown and a click picks a bone, never a sprite. "
                                 "Auto-Key on: pose the bone (keys); off: place it in the sprite (its rest pose)"));
    m_pLatticeModeAct = f_MakeAction(this, tr("&Lattice Mode"), QKeySequence(Qt::Key_L), int(Id::Grid));
    m_pLatticeModeAct->setToolTip(tr("Lattice mode (L): the selected sprite's deform lattice; drag a point to key the deform at "
                                    "the playhead (squash, stretch, breathe). Properties > Lattice sets its size"));
    m_pWeightModeAct = f_MakeAction(this, tr("&Weights Mode"), QKeySequence(Qt::Key_P), int(Id::Brush));
    m_pWeightModeAct->setToolTip(tr("Weights mode (P): the skins the selected bone is in, coloured by how much each point "
                                   "follows it (blue = not, red = fully). Drag to paint it on, Ctrl+drag to take it off, "
                                   "Shift+drag to smooth; [ ] change the brush size; a click on a joint picks that bone"));
    m_pBrushSmallerAct = f_MakeAction(this, tr("Brush S&maller"), QKeySequence(Qt::Key_BracketLeft));
    m_pBrushLargerAct = f_MakeAction(this, tr("Brush &Larger"), QKeySequence(Qt::Key_BracketRight));
    connect(m_pBrushSmallerAct, &QAction::triggered, this, [this] { m_pViewport->f_SetBrushRadius(m_pViewport->f_BrushRadius() / 1.25); });
    connect(m_pBrushLargerAct, &QAction::triggered, this, [this] { m_pViewport->f_SetBrushRadius(m_pViewport->f_BrushRadius() * 1.25); });
    m_pAutoWeightsAct = f_MakeAction(this, tr("Reset to &Automatic Weights"));
    m_pAutoWeightsAct->setToolTip(tr("Drop the painted weights of the selected skinned sprite (or of the skins the selected "
                                    "bone is in): the weights are made from the bones' rest positions again"));
    connect(m_pAutoWeightsAct, &QAction::triggered, this, &AnimEditor::f_ResetWeights);
    {
        auto* l_pGroup = new QActionGroup(this);
        for (QAction* l_pA : {m_pObjectModeAct, m_pBoneModeAct, m_pLatticeModeAct, m_pWeightModeAct}) {
            l_pA->setCheckable(true);
            l_pGroup->addAction(l_pA);
        }
        m_pObjectModeAct->setChecked(true);
        connect(m_pObjectModeAct, &QAction::toggled, this, [this](bool l_bOn) {
            if (l_bOn) m_pViewport->f_SetEditMode(AnimViewport::EditMode::Object);
        });
        connect(m_pBoneModeAct, &QAction::toggled, this, [this](bool l_bOn) {
            if (l_bOn) m_pViewport->f_SetEditMode(AnimViewport::EditMode::Bones);
        });
        connect(m_pLatticeModeAct, &QAction::toggled, this, [this](bool l_bOn) {
            if (l_bOn) m_pViewport->f_SetEditMode(AnimViewport::EditMode::Lattice);
        });
        connect(m_pWeightModeAct, &QAction::toggled, this, [this](bool l_bOn) {
            if (l_bOn) m_pViewport->f_SetEditMode(AnimViewport::EditMode::Weights);
            for (QAction* l_pA : m_oBrushBarActions) l_pA->setVisible(l_bOn);
        });
    }
    m_pInsertKeyAct = f_MakeAction(this, tr("&Insert Key at Playhead"), QKeySequence(Qt::Key_K), int(Id::KeyOn));
    m_pQualifyAct = f_MakeAction(this, tr("&Qualify Sprite References"));
    m_pQualifyAct->setToolTip(tr("Every bare sprite name in the file (every clip) -> id:name with the atlas the lookup picks now"));
    m_pQualifyAct->setStatusTip(m_pQualifyAct->toolTip());
    connect(m_pQualifyAct, &QAction::triggered, m_pDoc, &AnimDocument::f_QualifySpriteReferences);

    m_pFitAct = f_MakeAction(this, tr("&Fit"), QKeySequence(Qt::Key_F), int(Id::Fit));
    m_pZoomInAct = f_MakeAction(this, tr("Zoom &In"), QKeySequence::ZoomIn, int(Id::ZoomIn));
    m_pZoomOutAct = f_MakeAction(this, tr("Zoom &Out"), QKeySequence::ZoomOut, int(Id::ZoomOut));
    m_pFrameAct = f_MakeAction(this, tr("Frame &Clip"), QKeySequence(tr("Shift+F")));
    m_pFrameAct->setToolTip(tr("Fit every pose of the clip"));
    connect(m_pFitAct, &QAction::triggered, m_pViewport, &CanvasView::f_FitToView);
    connect(m_pZoomInAct, &QAction::triggered, m_pViewport, &CanvasView::f_ZoomIn);
    connect(m_pZoomOutAct, &QAction::triggered, m_pViewport, &CanvasView::f_ZoomOut);
    connect(m_pFrameAct, &QAction::triggered, m_pViewport, &AnimViewport::f_FrameClip);

    m_pHelpCliAct = f_MakeAction(this, tr("&Command Line"));
    m_pAboutAct = f_MakeAction(this, tr("&About Anim Editor"));
    m_pTutorialAct = f_MakeAction(this, tr("&Skinning Tutorial"), QKeySequence(Qt::Key_F1));
    m_pTutorialAct->setToolTip(tr("Step by step: bones, skin, weight painting and the lattice on the golem (opens the "
                                 "tutorial in the program that shows .md files)"));
    m_pTutorialStartAct = f_MakeAction(this, tr("Open Tutorial &Start File"));
    m_pTutorialStartAct->setToolTip(tr("The golem the tutorial starts from: the sprite only. Save it under another "
                                      "name first (File > Save As), so the start file stays as it is"));
    m_pTutorialDoneAct = f_MakeAction(this, tr("Open Tutorial &Finished File"));
    m_pTutorialDoneAct->setToolTip(tr("The golem as the tutorial ends: bones, skin, painted weights, sway, nod and breath"));
    connect(m_pTutorialAct, &QAction::triggered, this, [this] {
        const QString l_oDoc = f_RepoFile(Language::f_Current() == Language::Lang::Chinese ? QStringLiteral("docs/engine/zh_TW/17_SKINNING_TUTORIAL.md")
                                                                                     : QStringLiteral("docs/engine/17_SKINNING_TUTORIAL.md"));
        if (l_oDoc.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(l_oDoc)))
            QMessageBox::information(m_pHost, tr("Skinning Tutorial"),
                                     tr("The tutorial is docs/engine/17_SKINNING_TUTORIAL.md in the TOMS repo. This editor runs "
                                        "outside the repo, so it cannot open it."));
    });
    for (auto [l_pAct, l_oName] : {std::pair{m_pTutorialStartAct, QStringLiteral("golem_start.anim")},
                             std::pair{m_pTutorialDoneAct, QStringLiteral("golem_done.anim")}}) {
        connect(l_pAct, &QAction::triggered, this, [this, name = l_oName] {
            const QString l_oFile = f_RepoFile(QStringLiteral("docs/examples/tutorial/") + name);
            if (l_oFile.isEmpty()) {
                QMessageBox::information(m_pHost, tr("Skinning Tutorial"),
                                         tr("The tutorial's files are in docs/examples/tutorial of the TOMS repo. This "
                                            "editor runs outside the repo, so it cannot find them."));
                return;
            }
            f_OpenFile(l_oFile, false);
        });
    }
    connect(m_pHelpCliAct, &QAction::triggered, this, [this] {
        QMessageBox::information(m_pHost, tr("Command Line"),
                                 tr("anim_editor [file.anim]\n"
                                    "anim_editor --headless check <file.anim> [--atlas [<id>=]<file.atlas>]...\n"
                                    "    parse, check sprite names against the atlases and the keys; exit 0 = ok, 2 = errors\n"
                                    "    (--atlas, repeatable, replaces the file's atlas list; id = the file name unless given)\n"
                                    "anim_editor --headless convert-mpdi <folder> [--out <dir>]\n"
                                    "    every .mpdi under the folder -> .anim, their .pi -> atlases (Convert menu)\n"
                                    "anim_editor --selftest <file.anim> <outdir>\n"
                                    "    automated check (run with -platform offscreen)"));
    });
    connect(m_pAboutAct, &QAction::triggered, this, [this] {
        QMessageBox::about(m_pHost, tr("Anim Editor"),
                           tr("<b>Anim Editor</b><p>Node animations (.anim) on the packed sprite atlas for TOMS. "
                              "The preview uses the game's own evaluate() and appendQuads().</p><p>docs/engine/05_ANIMATION.md</p>"));
    });
}

void AnimEditor::f_CreateDocks()
{
    m_pClips = new AnimClipsDock(m_pDoc, m_pHost);
    connect(m_pClips, &AnimClipsDock::f_ClipActivated, this, [this] {   // double-click: play the clip from its start
        m_pTransport->f_Stop();
        m_pTransport->f_Play();
    });
    m_pNodes = new AnimNodesDock(m_pDoc, m_pHost);
    m_pNodes->f_SetContextActions({m_pCutAct, m_pCopyAct, m_pPasteAct, m_pPasteSiblingAct, nullptr, m_pDupAct, m_pRenameAct,
                                nullptr, m_pDeleteAct});
    m_pSprites = new AnimSpritesDock(m_pDoc, m_pHost);
    m_pProps = new AnimPropertiesDock(m_pDoc, m_pHost);
    m_pKeys = new AnimKeyListDock(m_pDoc, m_pHost);
    m_pEvents = new AnimEventsDock(m_pDoc, m_pHost);
    m_pProblems = new AnimProblemsDock(m_pDoc, m_pHost);
    m_pHistory = f_CreateHistoryDock(m_pDoc->f_UndoStack(), m_pHost);
    m_pBackup = new AutoBackup(m_pDoc->f_UndoStack(), QStringLiteral(".anim"), [this] { return m_pDoc->f_FilePath(); },
                              [this](const QString& l_oP, QString* l_pE) { return m_pDoc->f_WriteBackup(l_oP, l_pE); }, this);
    connect(m_pBackup, &AutoBackup::f_Message, this, [this](const QString& l_oT) { m_pHost->statusBar()->showMessage(l_oT, 5000); });
    m_pTimeline = new QDockWidget(tr("Timeline"), m_pHost);
    m_pTimeline->setObjectName(QStringLiteral("AnimTimelineDock"));
    auto* l_pPlaceholder = new QLabel(tr("The multi-track timeline comes in Phase 4.\n"
                                      "Until then: the scrubber in the transport bar, and the Keys list.\n\n"
                                      "Drop sprites here (or on the scrubber / the Keys list) to insert them\n"
                                      "as sprite keys, one after another, on the selected node."),
                                   m_pTimeline);
    l_pPlaceholder->setAlignment(Qt::AlignCenter);
    l_pPlaceholder->setForegroundRole(QPalette::PlaceholderText);
    m_pTimeline->setWidget(l_pPlaceholder);

    // Several sprites dragged onto a time area: a sprite sequence (MPDI style).
    const auto l_fnOnDrop = [this](const QStringList& l_oRefs, float l_fT) { f_InsertSpriteKeys(l_oRefs, l_fT); };
    connect(new AnimSpriteDrop(l_pPlaceholder, [this](const QPoint&) { return m_pDoc->f_Time(); }),
            &AnimSpriteDrop::f_Dropped, this, l_fnOnDrop);
    AnimScrubber* l_pScrubber = m_pTransport->f_Scrubber();
    connect(new AnimSpriteDrop(l_pScrubber, [l_pScrubber](const QPoint& l_oP) { return l_pScrubber->f_TimeAt(l_oP.x()); }),
            &AnimSpriteDrop::f_Dropped, this, l_fnOnDrop);
    QTableWidget* l_pKeyTable = m_pKeys->f_Table();
    connect(new AnimSpriteDrop(l_pKeyTable->viewport(), [this, l_pKeyTable](const QPoint& l_oP) {
                const int l_iRow = l_pKeyTable->rowAt(l_oP.y());
                const std::vector<float> l_vTimes = m_pKeys->f_RowTimes();
                return l_iRow >= 0 && l_iRow < int(l_vTimes.size()) ? l_vTimes[size_t(l_iRow)] : m_pDoc->f_Time();
            }),
            &AnimSpriteDrop::f_Dropped, this, l_fnOnDrop);

    QToolBar* l_pNt = m_pNodes->f_ToolBar();
    for (QAction* l_pA : {m_pAddChildAct, m_pAddSpriteAct, m_pDupAct, m_pDeleteAct, m_pUpAct, m_pDownAct}) l_pNt->addAction(l_pA);
    connect(m_pNodes, &AnimNodesDock::f_AddSpriteNodeRequested, this, &AnimEditor::f_AddSpriteNode);
    connect(m_pProps->f_AtlasesPanel(), &AnimAtlasesPanel::f_AddRequested, this, &AnimEditor::f_OnAddAtlas);
    connect(m_pRenameAct, &QAction::triggered, m_pNodes, &AnimNodesDock::f_BeginRename);
    connect(m_pInsertKeyAct, &QAction::triggered, m_pKeys, &AnimKeyListDock::f_InsertKey);
    // Delete / F2 mean "this node" only where nodes have the focus; the key list has its own Delete.
    for (QAction* l_pA : {m_pDeleteAct, m_pRenameAct}) {
        l_pA->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_pNodes->addAction(l_pA);
        m_pViewport->addAction(l_pA);
    }
    auto* l_pDelKeys = new QAction(tr("Delete Keys"), m_pKeys);
    l_pDelKeys->setShortcut(QKeySequence::Delete);
    l_pDelKeys->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(l_pDelKeys, &QAction::triggered, m_pKeys, [this] { m_pKeys->f_DeleteRows(false); });
    m_pKeys->addAction(l_pDelKeys);
}

void AnimEditor::f_CreateMenus()
{
    QMenu* l_pFile = new QMenu(tr("&File"), m_pHost);
    l_pFile->addAction(m_pNewAct);
    l_pFile->addAction(m_pOpenAct);
    m_pRecentMenu = l_pFile->addMenu(tr("Open &Recent"));
    connect(m_pRecentMenu, &QMenu::aboutToShow, this, &AnimEditor::f_RebuildRecentMenu);
    l_pFile->addAction(m_pSaveAct);
    l_pFile->addAction(m_pSaveAsAct);
    {   // clips as GIF / animated PNG / PNG frames, drawn by the game renderer (AnimExport.h)
        QAction* l_pExp = l_pFile->addAction(tr("&Export Clips as Animation..."));
        l_pExp->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
        connect(l_pExp, &QAction::triggered, this, [this] {
            if (m_pDoc->f_File().m_vClips.empty()) {
                QMessageBox::information(m_pHost, tr("Export Clips as Animation"), tr("This file has no clips."));
                return;
            }
            animexport::ExportDialog l_oDlg(m_pDoc, m_pViewport, m_pHost);
            l_oDlg.exec();
        });
    }
    l_pFile->addSeparator();
    l_pFile->addAction(m_pAddAtlasAct);
    l_pFile->addAction(m_pReloadAtlasAct);
    l_pFile->addSeparator();
    m_pBackup->f_AddMenu(l_pFile, m_pHost);
    l_pFile->addSeparator();
    l_pFile->addAction(m_pQuitAct);

    QMenu* l_pEdit = new QMenu(tr("&Edit"), m_pHost);
    l_pEdit->addAction(m_pUndoAct);
    l_pEdit->addAction(m_pRedoAct);
    l_pEdit->addSeparator();
    for (QAction* l_pA : {m_pCutAct, m_pCopyAct, m_pPasteAct, m_pPasteSiblingAct}) l_pEdit->addAction(l_pA);
    l_pEdit->addSeparator();
    l_pEdit->addAction(m_pObjectModeAct);
    l_pEdit->addAction(m_pBoneModeAct);
    l_pEdit->addAction(m_pLatticeModeAct);
    l_pEdit->addAction(m_pWeightModeAct);
    l_pEdit->addSeparator();
    l_pEdit->addAction(m_pBrushSmallerAct);
    l_pEdit->addAction(m_pBrushLargerAct);
    l_pEdit->addAction(m_pAutoWeightsAct);
    l_pEdit->addSeparator();
    l_pEdit->addAction(m_pMoveAct);
    l_pEdit->addAction(m_pRotateAct);
    l_pEdit->addAction(m_pScaleAct);
    l_pEdit->addSeparator();
    l_pEdit->addAction(m_pAutoKeyAct);
    l_pEdit->addAction(m_pSnapAct);

    QMenu* l_pNode = new QMenu(tr("&Node"), m_pHost);
    for (QAction* l_pA : {m_pAddChildAct, m_pAddSiblingAct, m_pAddSpriteAct, m_pDupAct}) l_pNode->addAction(l_pA);
    l_pNode->addSeparator();
    l_pNode->addAction(m_pRenameAct);
    l_pNode->addAction(m_pUpAct);
    l_pNode->addAction(m_pDownAct);
    l_pNode->addSeparator();
    l_pNode->addAction(m_pDeleteAct);

    QMenu* l_pKey = new QMenu(tr("&Key"), m_pHost);
    l_pKey->addAction(m_pInsertKeyAct);
    l_pKey->addAction(tr("Delete Selected Rows"), m_pKeys, [this] { m_pKeys->f_DeleteRows(false); });
    l_pKey->addAction(tr("Delete Current Column's Keys"), m_pKeys, [this] { m_pKeys->f_DeleteRows(true); });
    l_pKey->addSeparator();
    l_pKey->addAction(tr("Set Time…"), m_pKeys, &AnimKeyListDock::f_SetTimeDialog);
    l_pKey->addAction(tr("Even Spacing"), m_pKeys, &AnimKeyListDock::f_EvenSpacing);
    l_pKey->addAction(tr("Rescale…"), m_pKeys, &AnimKeyListDock::f_RescaleDialog);
    l_pKey->addAction(tr("Ramp…"), m_pKeys, &AnimKeyListDock::f_RampDialog);
    l_pKey->addAction(tr("Fade In"), m_pKeys, &AnimKeyListDock::f_FadeIn);
    l_pKey->addAction(tr("Fade Out"), m_pKeys, &AnimKeyListDock::f_FadeOut);
    l_pKey->addAction(tr("Apply Sprite Sequence"), m_pKeys, &AnimKeyListDock::f_SpriteSequence);
    l_pKey->addAction(tr("Set Ease…"), m_pKeys, &AnimKeyListDock::f_SetEaseDialog);
    l_pKey->addAction(tr("Motion Recipe…"), this, &AnimEditor::f_MotionRecipeDialog);
    l_pKey->addSeparator();
    l_pKey->addAction(m_pQualifyAct);

    QMenu* l_pPlay = new QMenu(tr("&Playback"), m_pHost);
    l_pPlay->addAction(m_pTransport->f_PlayAction());
    l_pPlay->addAction(m_pTransport->f_StopAction());
    l_pPlay->addAction(m_pTransport->f_LoopAction());
    l_pPlay->addSeparator();
    l_pPlay->addAction(tr("Previous Key"), QKeySequence(Qt::Key_Comma), m_pTransport, [this] { m_pTransport->f_StepKey(-1); });
    l_pPlay->addAction(tr("Next Key"), QKeySequence(Qt::Key_Period), m_pTransport, [this] { m_pTransport->f_StepKey(1); });

    m_pViewMenu = new QMenu(tr("&View"), m_pHost);
    {   // the viewport's renderer: the game's (bgfx, the same pixels as toms_game) or QPainter
        QAction* l_pA = m_pViewMenu->addAction(tr("Preview with the Game Renderer (after a restart)"));
        l_pA->setCheckable(true);
        l_pA->setChecked(f_Settings().value(QStringLiteral("preview/gameRenderer"), true).toBool());
        l_pA->setToolTip(tr("Draw the viewport with toms_game's renderer (bgfx): exactly the game's pixels. Off: QPainter."));
        connect(l_pA, &QAction::toggled, this, [this](bool l_bOn) {
            f_Settings().setValue(QStringLiteral("preview/gameRenderer"), l_bOn);
            m_pHost->statusBar()->showMessage(tr("The renderer changes the next time the editor starts."), 8000);
        });
        m_pViewMenu->addSeparator();
    }
    m_pViewMenu->addAction(m_pShowSkinAct);
    {   // A character clip (body / hand nodes) shown as another character: the view only (docs/engine/05 "Characters").
        QMenu* l_pWho = m_pViewMenu->addMenu(tr("Preview as &Character"));
        l_pWho->setToolTipsVisible(true);
        connect(l_pWho, &QMenu::aboutToShow, this, [this, l_pWho] {
            l_pWho->clear();
            const QStringList l_oChoices = m_pDoc->f_CharacterChoices();
            if (l_oChoices.isEmpty()) {
                QAction* l_pNone = l_pWho->addAction(tr("(this clip has no body / hand nodes)"));
                l_pNone->setEnabled(false);
                return;
            }
            auto l_fnAdd = [&](const QString& l_oLabel, const QString& l_oSprite) {
                QAction* l_pA = l_pWho->addAction(l_oLabel);
                l_pA->setCheckable(true);
                l_pA->setChecked(m_pDoc->f_PreviewCharacter() == l_oSprite);
                connect(l_pA, &QAction::triggered, this, [this, l_oSprite] { m_pDoc->f_SetPreviewCharacter(l_oSprite); });
            };
            l_fnAdd(tr("As Made"), QString());
            l_pWho->addSeparator();
            for (const QString& l_oS : l_oChoices) l_fnAdd(l_oS, l_oS);
        });
    }
    {   // A held thing's clips drawn at the `hand` node (CharacterRig): a weapon file beside a character file.
        QMenu* l_pHeld = m_pViewMenu->addMenu(tr("&Held Clip"));
        l_pHeld->setToolTipsVisible(true);
        connect(l_pHeld, &QMenu::aboutToShow, this, [this, l_pHeld] {
            l_pHeld->clear();
            QAction* l_pNone = l_pHeld->addAction(tr("None"));
            l_pNone->setCheckable(true);
            l_pNone->setChecked(m_pDoc->f_HeldFile().isEmpty());
            connect(l_pNone, &QAction::triggered, this, [this] { m_pDoc->f_SetHeldFile(QString()); });
            QAction* l_pPick = l_pHeld->addAction(tr("Choose File..."));
            l_pPick->setToolTip(tr("A weapon's .anim: its clip named like this clip is drawn at the node named 'hand'"));
            connect(l_pPick, &QAction::triggered, this, [this] {
                const QString l_oDir = m_pDoc->f_FilePath().isEmpty() ? QString() : QFileInfo(m_pDoc->f_FilePath()).absolutePath();
                const QString l_oPath = QFileDialog::getOpenFileName(m_pHost, tr("Held Clip"), l_oDir, tr("Animations (*.anim)"));
                QString l_oErr;
                if (!l_oPath.isEmpty() && !m_pDoc->f_SetHeldFile(l_oPath, &l_oErr))
                    QMessageBox::warning(m_pHost, tr("Held Clip"), l_oErr);
            });
            if (!m_pDoc->f_HeldFile().isEmpty()) {
                QAction* l_pCur = l_pHeld->addAction(QFileInfo(m_pDoc->f_HeldFile()).fileName());
                l_pCur->setCheckable(true);
                l_pCur->setChecked(true);
                l_pCur->setEnabled(false);
            }
        });
    }
    m_pViewMenu->addSeparator();
    m_pViewMenu->addAction(m_pFitAct);
    m_pViewMenu->addAction(m_pFrameAct);
    m_pViewMenu->addAction(m_pZoomInAct);
    m_pViewMenu->addAction(m_pZoomOutAct);
    CanvasView::f_AddCoordinatesAction(m_pViewMenu);
    m_pViewMenu->addSeparator();
    for (QDockWidget* l_pD : f_Docks()) m_pViewMenu->addAction(l_pD->toggleViewAction());
    m_pViewMenu->addSeparator();

    QMenu* l_pConvert = new QMenu(tr("&Convert"), m_pHost);
    {
        QAction* l_pA = l_pConvert->addAction(tr("&MPDI Folder to .anim…"));
        l_pA->setToolTip(tr("Convert every .mpdi under a folder (the old engine's animations) to .anim, and the .pi sprite "
                         "sheets they use to atlases (.atlasproj + .atlas + .png)"));
        l_pA->setStatusTip(l_pA->toolTip());
        connect(l_pA, &QAction::triggered, this, &AnimEditor::f_ConvertMpdiFolder);
    }

    QMenu* l_pHelp = new QMenu(tr("&Help"), m_pHost);
    l_pHelp->addAction(m_pTutorialAct);
    l_pHelp->addAction(m_pTutorialStartAct);
    l_pHelp->addAction(m_pTutorialDoneAct);
    l_pHelp->addSeparator();
    l_pHelp->addAction(m_pHelpCliAct);
    l_pHelp->addAction(m_pAboutAct);
    m_oMenus = {l_pFile, l_pEdit, l_pNode, l_pKey, l_pPlay, m_pViewMenu, l_pConvert, l_pHelp};
}

void AnimEditor::f_CreateToolBars()
{
    m_pMainBar = new QToolBar(tr("Main"), m_pHost);
    m_pMainBar->setObjectName(QStringLiteral("AnimMainToolBar"));
    m_pMainBar->setIconSize(QSize(20, 20));
    m_pMainBar->setMovable(false);
    m_pMainBar->addAction(m_pNewAct);
    m_pMainBar->addAction(m_pOpenAct);
    m_pMainBar->addAction(m_pSaveAct);
    m_pMainBar->addSeparator();
    m_pMainBar->addAction(m_pUndoAct);
    m_pMainBar->addAction(m_pRedoAct);
    m_pMainBar->addSeparator();
    m_pMainBar->addAction(m_pObjectModeAct);   // shown with text: the mode is the first thing to know
    m_pMainBar->addAction(m_pBoneModeAct);
    m_pMainBar->addAction(m_pLatticeModeAct);
    m_pMainBar->addAction(m_pWeightModeAct);
    for (QAction* l_pA : {m_pObjectModeAct, m_pBoneModeAct, m_pLatticeModeAct, m_pWeightModeAct})
        if (auto* l_pB = qobject_cast<QToolButton*>(m_pMainBar->widgetForAction(l_pA))) l_pB->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    {   // Weights mode: the brush
        auto* l_pRadius = new QSpinBox(m_pMainBar);
        l_pRadius->setRange(4, 400);
        l_pRadius->setSuffix(tr(" px"));
        l_pRadius->setToolTip(tr("Brush size on screen ([ and ] change it)"));
        l_pRadius->setValue(int(std::lround(m_pViewport->f_BrushRadius())));
        auto* l_pStrength = new QDoubleSpinBox(m_pMainBar);
        l_pStrength->setRange(0.01, 1.0);
        l_pStrength->setSingleStep(0.05);
        l_pStrength->setDecimals(2);
        l_pStrength->setToolTip(tr("How far one dab moves a point at the brush's centre towards fully (or not) following the bone"));
        l_pStrength->setValue(m_pViewport->f_BrushStrength());
        connect(l_pRadius, qOverload<int>(&QSpinBox::valueChanged), this, [this](int l_iV) { m_pViewport->f_SetBrushRadius(l_iV); });
        connect(l_pStrength, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double l_dV) { m_pViewport->f_SetBrushStrength(l_dV); });
        connect(m_pViewport, &AnimViewport::f_BrushChanged, this, [this, l_pRadius, l_pStrength] {
            const QSignalBlocker l_oB1(l_pRadius), l_oB2(l_pStrength);
            l_pRadius->setValue(int(std::lround(m_pViewport->f_BrushRadius())));
            l_pStrength->setValue(m_pViewport->f_BrushStrength());
        });
        m_oBrushBarActions << m_pMainBar->addSeparator();
        m_oBrushBarActions << m_pMainBar->addWidget(new QLabel(tr(" Brush "), m_pMainBar));
        m_oBrushBarActions << m_pMainBar->addWidget(l_pRadius);
        m_oBrushBarActions << m_pMainBar->addWidget(new QLabel(tr(" Strength "), m_pMainBar));
        m_oBrushBarActions << m_pMainBar->addWidget(l_pStrength);
        for (QAction* l_pA : m_oBrushBarActions) l_pA->setVisible(false);
    }
    m_pMainBar->addSeparator();
    m_pMainBar->addAction(m_pMoveAct);
    m_pMainBar->addAction(m_pRotateAct);
    m_pMainBar->addAction(m_pScaleAct);
    m_pMainBar->addSeparator();
    m_pMainBar->addAction(m_pAutoKeyAct);
    if (auto* l_pB = m_pMainBar->widgetForAction(m_pAutoKeyAct)) l_pB->setObjectName(QStringLiteral("AutoKeyButton"));
    m_pMainBar->addAction(m_pSnapAct);
    m_pMainBar->addAction(m_pInsertKeyAct);
    m_pMainBar->addSeparator();
    m_pMainBar->addAction(m_pZoomOutAct);
    m_pMainBar->addAction(m_pZoomInAct);
    m_pMainBar->addAction(m_pFitAct);
    m_pMainBar->addSeparator();
    m_pMainBar->addAction(m_pAddAtlasAct);
}

void AnimEditor::f_Install()
{
    m_pHost->setCentralWidget(m_pViewport);
    m_pHost->setDockNestingEnabled(true);
    for (QMenu* l_pM : m_oMenus) m_pHost->menuBar()->addMenu(l_pM);
    m_pHost->addToolBar(Qt::TopToolBarArea, m_pMainBar);
    m_pHost->addToolBarBreak(Qt::TopToolBarArea);
    m_pHost->addToolBar(Qt::TopToolBarArea, m_pTransport->f_ToolBar());

    m_pHost->addDockWidget(Qt::LeftDockWidgetArea, m_pClips);
    m_pHost->splitDockWidget(m_pClips, m_pNodes, Qt::Vertical);
    m_pHost->splitDockWidget(m_pNodes, m_pSprites, Qt::Vertical);
    m_pHost->addDockWidget(Qt::RightDockWidgetArea, m_pProps);
    m_pHost->tabifyDockWidget(m_pProps, m_pHistory);
    m_pProps->raise();
    m_pHost->addDockWidget(Qt::BottomDockWidgetArea, m_pKeys);
    m_pHost->tabifyDockWidget(m_pKeys, m_pEvents);
    m_pHost->tabifyDockWidget(m_pEvents, m_pTimeline);
    m_pHost->tabifyDockWidget(m_pTimeline, m_pProblems);
    m_pKeys->raise();
    m_pHost->resizeDocks({m_pClips, m_pProps}, {260, 300}, Qt::Horizontal);
    m_pHost->resizeDocks({m_pClips, m_pNodes, m_pSprites}, {150, 260, 260}, Qt::Vertical);
    m_pHost->resizeDocks({m_pKeys}, {230}, Qt::Vertical);
    emit f_TitleChanged();
}

// ---- settings / recent files --------------------------------------------------------------------

void AnimEditor::f_ReadSettings()
{
    QSettings l_oS = f_Settings();
    m_pDoc->f_SetAutoKey(l_oS.value(QStringLiteral("autoKey"), true).toBool());
    m_pAutoKeyAct->setChecked(m_pDoc->f_AutoKey());
    m_pSnapAct->setChecked(l_oS.value(QStringLiteral("snap"), false).toBool());
    m_pShowSkinAct->setChecked(l_oS.value(QStringLiteral("showSkin"), false).toBool());
    m_pViewport->f_SetBrushRadius(l_oS.value(QStringLiteral("brushRadius"), 40.0).toDouble());
    m_pViewport->f_SetBrushStrength(l_oS.value(QStringLiteral("brushStrength"), 0.5).toDouble());
    {
        const QString l_oMode = l_oS.value(QStringLiteral("editMode")).toString();
        (l_oMode == QLatin1String("bones") || l_oS.value(QStringLiteral("boneMode"), false).toBool() ? m_pBoneModeAct
         : l_oMode == QLatin1String("lattice") ? m_pLatticeModeAct
         : l_oMode == QLatin1String("weights") ? m_pWeightModeAct : m_pObjectModeAct)->setChecked(true);
    }
    m_pTransport->f_LoopAction()->setChecked(l_oS.value(QStringLiteral("loopPreview"), false).toBool());
}

QString AnimEditor::f_RepoFile(const QString& l_oRelative)
{
    QDir l_oDir(QCoreApplication::applicationDirPath());
    for (int l_iUp = 0; l_iUp < 6; l_iUp++) {   // Build/<preset>/bin -> the repo is 3 up; Editor/bin -> 2 up
        const QString l_oPath = l_oDir.filePath(l_oRelative);
        if (QFileInfo::exists(l_oPath)) return QFileInfo(l_oPath).absoluteFilePath();
        if (!l_oDir.cdUp()) break;
    }
    return QString();
}

// Edit > Reset to Automatic Weights: the selected skinned node, and every skin the selected node is a bone of.
void AnimEditor::f_ResetWeights()
{
    const toms::anim::Clip* l_pClip = m_pDoc->f_Clip();
    const toms::anim::Node* l_pSel = m_pDoc->f_SelectedNode();
    if (!l_pClip || !l_pSel) return;
    std::vector<NodePath> l_vPaths;
    NodePath l_vPath;
    std::function<void(const toms::anim::Node&)> l_fnWalk = [&](const toms::anim::Node& l_oN) {
        const bool l_bMine = &l_oN == l_pSel || std::find(l_oN.m_vSkinBones.begin(), l_oN.m_vSkinBones.end(), l_pSel->m_strName) != l_oN.m_vSkinBones.end();
        if (l_oN.f_Skinned() && !l_oN.m_vSkinWeights.empty() && l_bMine) l_vPaths.push_back(l_vPath);
        for (size_t l_nI = 0; l_nI < l_oN.m_vChildren.size(); l_nI++) {
            l_vPath.push_back(int(l_nI));
            l_fnWalk(l_oN.m_vChildren[l_nI]);
            l_vPath.pop_back();
        }
    };
    l_fnWalk(l_pClip->m_oRoot);
    if (l_vPaths.empty()) {
        emit m_pDoc->f_Message(tr("No painted weights here: select a skinned sprite or one of its bones."));
        return;
    }
    const QString l_oKey = QStringLiteral("autoWeights%1").arg(QDateTime::currentMSecsSinceEpoch());   // one undo step
    for (const NodePath& l_vP : l_vPaths)
        m_pDoc->f_EditNode(l_vP, tr("Automatic Weights"), [](toms::anim::Node& l_oN) { l_oN.m_vSkinWeights.clear(); }, l_oKey);
}

void AnimEditor::f_WriteSettings() const
{
    QSettings l_oS = f_Settings();
    l_oS.setValue(QStringLiteral("autoKey"), m_pDoc->f_AutoKey());
    l_oS.setValue(QStringLiteral("snap"), m_pSnapAct->isChecked());
    l_oS.setValue(QStringLiteral("showSkin"), m_pShowSkinAct->isChecked());
    l_oS.setValue(QStringLiteral("editMode"), m_pBoneModeAct->isChecked() ? QStringLiteral("bones")
                                           : m_pLatticeModeAct->isChecked() ? QStringLiteral("lattice")
                                           : m_pWeightModeAct->isChecked() ? QStringLiteral("weights") : QStringLiteral("object"));
    l_oS.setValue(QStringLiteral("brushRadius"), m_pViewport->f_BrushRadius());
    l_oS.setValue(QStringLiteral("brushStrength"), m_pViewport->f_BrushStrength());
    l_oS.remove(QStringLiteral("boneMode"));
    l_oS.setValue(QStringLiteral("loopPreview"), m_pTransport->f_LoopAction()->isChecked());
}

QString AnimEditor::f_LastDir() const
{
    if (!m_pDoc->f_FilePath().isEmpty()) return QFileInfo(m_pDoc->f_FilePath()).absolutePath();
    return f_Settings().value(QStringLiteral("lastDir"), QDir::homePath()).toString();
}

void AnimEditor::f_AddRecentFile(const QString& l_oPath)
{
    if (f_SelftestRunning()) return;   // the selftest's scratch files do not belong in the list
    QSettings l_oS = f_Settings();
    QStringList l_oRecent = l_oS.value(QStringLiteral("recentFiles")).toStringList();
    const QString l_oAbs = QFileInfo(l_oPath).absoluteFilePath();
    l_oRecent.removeAll(l_oAbs);
    l_oRecent.prepend(l_oAbs);
    while (l_oRecent.size() > g_iMaxRecent) l_oRecent.removeLast();
    l_oS.setValue(QStringLiteral("recentFiles"), l_oRecent);
    l_oS.setValue(QStringLiteral("lastDir"), QFileInfo(l_oAbs).absolutePath());
}

void AnimEditor::f_RebuildRecentMenu()
{
    m_pRecentMenu->clear();
    const QStringList l_oRecent = f_Settings().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString& l_oPath : l_oRecent) {
        QAction* l_pA = m_pRecentMenu->addAction(QDir::toNativeSeparators(l_oPath));
        l_pA->setEnabled(QFileInfo::exists(l_oPath));
        connect(l_pA, &QAction::triggered, this, [this, l_oPath] {
            if (f_MaybeSave()) f_OpenFile(l_oPath);
        });
    }
    if (l_oRecent.isEmpty()) m_pRecentMenu->addAction(tr("(none)"))->setEnabled(false);
    else {
        m_pRecentMenu->addSeparator();
        m_pRecentMenu->addAction(tr("Clear List"), this, [] { f_Settings().remove(QStringLiteral("recentFiles")); });
    }
}

// ---- files --------------------------------------------------------------------------------------

bool AnimEditor::f_OpenFile(const QString& l_oPath, bool l_bAskForAtlas)
{
    QString l_oErr;
    if (!m_pDoc->f_Open(l_oPath, &l_oErr)) {
        m_pProblems->f_AppendLog(tr("Could not open %1: %2").arg(QDir::toNativeSeparators(l_oPath), l_oErr));
        if (!f_SelftestRunning())
            QMessageBox::warning(m_pHost, tr("Open Animation"), tr("Could not open %1:\n%2").arg(QDir::toNativeSeparators(l_oPath), l_oErr));
        return false;
    }
    QStringList l_oAtlases, l_oFailed;
    for (int l_iI = 0; l_iI < m_pDoc->f_AtlasCount(); l_iI++) {
        l_oAtlases << QDir::toNativeSeparators(m_pDoc->f_AtlasAt(l_iI).m_oPath);
        if (!m_pDoc->f_AtlasAt(l_iI).m_bOk) l_oFailed << m_pDoc->f_AtlasAt(l_iI).m_oError;
    }
    m_pProblems->f_AppendLog(tr("Opened %1 (%2 clip(s), atlases: %3)")
                              .arg(QDir::toNativeSeparators(l_oPath))
                              .arg(m_pDoc->f_File().m_vClips.size())
                              .arg(l_oAtlases.isEmpty() ? tr("none") : l_oAtlases.join(QStringLiteral(", "))));
    f_AddRecentFile(l_oPath);
    if (l_bAskForAtlas && !f_SelftestRunning()) {
        if (m_pDoc->f_AtlasCount() == 0) {
            if (QMessageBox::question(m_pHost, tr("Atlases"), tr("The file names no atlas.\n\nAdd the .atlas its sprites come from now?")) ==
                QMessageBox::Yes)
                f_OnAddAtlas();
        } else if (!l_oFailed.isEmpty()) {
            QMessageBox::warning(m_pHost, tr("Atlases"),
                                 tr("%n atlas(es) could not be loaded:\n\n%1\n\nFix the list under Properties > Atlases "
                                    "(Reload, or Remove and Add).", nullptr, int(l_oFailed.size()))
                                     .arg(l_oFailed.join(QStringLiteral("\n"))));
        }
    }
    return true;
}

bool AnimEditor::f_MaybeSave()
{
    if (!m_pDoc->f_IsDirty()) return true;
    const auto l_eR = QMessageBox::warning(m_pHost, tr("Unsaved Changes"), tr("%1 has unsaved changes. Save them?").arg(m_pDoc->f_DisplayName()),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (l_eR == QMessageBox::Save) return f_Save();
    return l_eR == QMessageBox::Discard;
}

bool AnimEditor::f_Save()
{
    if (m_pDoc->f_FilePath().isEmpty()) return f_SaveAs();
    return f_SaveTo(m_pDoc->f_FilePath());
}

bool AnimEditor::f_SaveAs(const QString& l_oStartDir)
{
    QString l_oStart = m_pDoc->f_FilePath().isEmpty() ? QDir(f_LastDir()).filePath(QStringLiteral("untitled.anim")) : m_pDoc->f_FilePath();
    if (!l_oStartDir.isEmpty()) l_oStart = QDir(l_oStartDir).filePath(QFileInfo(l_oStart).fileName());
    QString l_oPath = QFileDialog::getSaveFileName(m_pHost, tr("Save Animation As"), l_oStart, tr("Animations (*.anim)"));
    if (l_oPath.isEmpty()) return false;
    if (QFileInfo(l_oPath).suffix().isEmpty()) l_oPath += QStringLiteral(".anim");
    return f_SaveTo(l_oPath);
}

bool AnimEditor::f_SaveTo(const QString& l_oPath)
{
    QString l_oErr;
    if (!m_pDoc->f_Save(l_oPath, &l_oErr)) {
        m_pProblems->f_AppendLog(tr("Save failed: %1").arg(l_oErr));
        if (!f_SelftestRunning()) QMessageBox::warning(m_pHost, tr("Save"), tr("Could not save %1:\n\n%2").arg(QDir::toNativeSeparators(l_oPath), l_oErr));
        return false;
    }
    f_AddRecentFile(l_oPath);
    return true;
}

void AnimEditor::f_OnAddAtlas()
{
    const QString l_oStart = m_pDoc->f_AtlasCount() ? m_pDoc->f_AtlasAt(m_pDoc->f_AtlasCount() - 1).m_oPath : f_LastDir();
    const QStringList l_oPaths =
        QFileDialog::getOpenFileNames(m_pHost, tr("Add Atlas"), l_oStart, tr("Sprite atlases (*.atlas);;All files (*)"));
    for (const QString& l_oPath : l_oPaths) {
        const QString l_oAbs = QFileInfo(l_oPath).absoluteFilePath();
        bool l_bHave = false;
        for (int l_iA = 0; l_iA < m_pDoc->f_AtlasCount() && !l_bHave; l_iA++)
            l_bHave = QFileInfo(m_pDoc->f_AtlasAt(l_iA).m_oPath).absoluteFilePath().compare(l_oAbs, Qt::CaseInsensitive) == 0;
        if (l_bHave) {
            m_pProblems->f_AppendLog(tr("Add atlas %1: already added").arg(QDir::toNativeSeparators(l_oPath)));
            continue;
        }
        // The first one may ask to save an untitled file; cancelling that stops the rest too.
        if (!f_AddAtlasFile(l_oPath) && m_pDoc->f_FilePath().isEmpty()) break;
    }
}

bool AnimEditor::f_InsertSpriteKeys(const QStringList& l_oRefs, float l_fTime)
{
    if (l_oRefs.isEmpty()) return false;
    const toms::anim::Clip* l_pClip = m_pDoc->f_Clip();
    const Node* l_pNode = m_pDoc->f_SelectedNode();
    if (!l_pClip || !l_pNode) {
        m_pDoc->f_Message(tr("Select a node first: dropped sprites become sprite keys of the selected node."));
        return false;
    }
    QSettings l_oS = f_Settings();
    double l_dGap = f_SelftestRunning() ? 0.1 : l_oS.value(QStringLiteral("spriteKeyGap"), 0.1).toDouble();
    double l_dStart = std::max(0.0f, l_fTime);
    bool l_bExtend = true;
    if (!f_SelftestRunning()) {
        QDialog l_oDlg(m_pHost);
        l_oDlg.setWindowTitle(tr("Insert Sprite Keys"));
        auto* l_pIntro = new QLabel(tr("Insert %n sprite key(s) on node '%1'?", nullptr, int(l_oRefs.size()))
                                     .arg(QString::fromStdString(l_pNode->m_strName)), &l_oDlg);
        auto* l_pStartSpin = new QDoubleSpinBox(&l_oDlg);
        l_pStartSpin->setRange(0, 100000);
        l_pStartSpin->setDecimals(3);
        l_pStartSpin->setSingleStep(0.1);
        l_pStartSpin->setSuffix(tr(" s"));
        l_pStartSpin->setValue(l_dStart);
        auto* l_pGapSpin = new QDoubleSpinBox(&l_oDlg);
        l_pGapSpin->setRange(0.001, 1000);
        l_pGapSpin->setDecimals(3);
        l_pGapSpin->setSingleStep(0.01);
        l_pGapSpin->setSuffix(tr(" s"));
        l_pGapSpin->setValue(l_dGap);
        auto* l_pSummary = new QLabel(&l_oDlg);
        auto* l_pExtendBox = new QCheckBox(&l_oDlg);
        l_pExtendBox->setChecked(true);
        const auto l_fnUpdate = [&] {
            const double l_dFirst = l_pStartSpin->value(), l_dStep = l_pGapSpin->value();
            const double l_dLast = l_dFirst + l_dStep * double(l_oRefs.size() - 1);
            int l_iReplaced = 0;
            for (float l_fK : animed::f_KeyTimes(*l_pNode, animed::Channel::Sprite))
                for (int l_iI = 0; l_iI < l_oRefs.size(); l_iI++)
                    if (std::fabs(l_fK - (l_dFirst + l_dStep * l_iI)) < 1e-4) l_iReplaced++;
            QString l_oText = tr("Keys at %1 ... %2 s (%3 per second).")
                               .arg(l_dFirst, 0, 'f', 3)
                               .arg(l_dLast, 0, 'f', 3)
                               .arg(1.0 / l_dStep, 0, 'g', 4);
            if (l_iReplaced)
                l_oText += QStringLiteral("\n") + tr("%n existing sprite key(s) at those times will be replaced.", nullptr, l_iReplaced);
            l_pSummary->setText(l_oText);
            l_pExtendBox->setVisible(l_pClip->m_fLength > 0 && l_dLast > l_pClip->m_fLength + 1e-4);
            l_pExtendBox->setText(tr("Extend the clip length from %1 to %2 s").arg(l_pClip->m_fLength, 0, 'f', 3).arg(l_dLast, 0, 'f', 3));
        };
        connect(l_pStartSpin, &QDoubleSpinBox::valueChanged, &l_oDlg, l_fnUpdate);
        connect(l_pGapSpin, &QDoubleSpinBox::valueChanged, &l_oDlg, l_fnUpdate);
        l_fnUpdate();
        auto* l_pButtons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, &l_oDlg);
        connect(l_pButtons, &QDialogButtonBox::accepted, &l_oDlg, &QDialog::accept);
        connect(l_pButtons, &QDialogButtonBox::rejected, &l_oDlg, &QDialog::reject);
        auto* l_pForm = new QFormLayout;
        l_pForm->addRow(tr("Start time"), l_pStartSpin);
        l_pForm->addRow(tr("Time gap per key"), l_pGapSpin);
        auto* l_pL = new QVBoxLayout(&l_oDlg);
        l_pL->addWidget(l_pIntro);
        l_pL->addLayout(l_pForm);
        l_pL->addWidget(l_pSummary);
        l_pL->addWidget(l_pExtendBox);
        l_pL->addWidget(l_pButtons);
        l_pGapSpin->setFocus();
        l_pGapSpin->selectAll();
        if (l_oDlg.exec() != QDialog::Accepted) return false;
        l_dStart = l_pStartSpin->value();
        l_dGap = l_pGapSpin->value();
        l_bExtend = l_pExtendBox->isChecked();
        l_oS.setValue(QStringLiteral("spriteKeyGap"), l_dGap);
    }
    if (!m_pDoc->f_InsertSpriteSequence(float(l_dStart), float(l_dGap), l_oRefs, l_bExtend)) return false;
    m_pDoc->f_SetTime(float(l_dStart));
    return true;
}

bool AnimEditor::f_AddAtlasFile(const QString& l_oPath)
{
    if (m_pDoc->f_FilePath().isEmpty()) {
        // Atlas paths are stored relative to the .anim: it needs a place first.
        if (f_SelftestRunning()) {
            m_pProblems->f_AppendLog(tr("Add atlas: save the animation first (atlas paths are stored relative to the .anim file)"));
            return false;
        }
        if (QMessageBox::information(m_pHost, tr("Add Atlas"),
                                     tr("Save the animation first: atlas paths are stored relative to the .anim file."),
                                     QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) != QMessageBox::Ok ||
            !f_SaveAs(QFileInfo(l_oPath).absolutePath()))
            return false;
    }
    // The id its sprites are stored with: the file name, unless another atlas has that already
    // (a style's game.atlas next to the original): then ask, suggesting the style folder's name.
    const QString l_oDefaultId = QString::fromStdString(toms::anim::f_DefaultAtlasId(QFileInfo(l_oPath).fileName().toStdString()));
    QString l_oId = m_pDoc->f_SuggestAtlasId(l_oPath);
    if (l_oId != l_oDefaultId) {
        if (f_SelftestRunning()) {
            m_pProblems->f_AppendLog(tr("Add atlas %1: the id '%2' is taken, using '%3'").arg(QDir::toNativeSeparators(l_oPath), l_oDefaultId, l_oId));
        } else {
            for (;;) {
                bool l_bOk = false;
                l_oId = QInputDialog::getText(m_pHost, tr("Add Atlas"),
                                           tr("Another atlas has the id '%1' already. Sprites from %2 are stored as <id>:name, "
                                              "so it needs its own id:").arg(l_oDefaultId, QDir::toNativeSeparators(l_oPath)),
                                           QLineEdit::Normal, l_oId, &l_bOk).trimmed();
                if (!l_bOk) return false;
                QString l_oWhy;
                if (m_pDoc->f_AtlasIdUsable(l_oId, -1, &l_oWhy)) break;
                QMessageBox::warning(m_pHost, tr("Add Atlas"), l_oWhy);
            }
        }
    }
    QString l_oErr;
    if (!m_pDoc->f_AddAtlas(l_oPath, l_oId, &l_oErr)) {
        m_pProblems->f_AppendLog(tr("Add atlas %1: %2").arg(QDir::toNativeSeparators(l_oPath), l_oErr));
        if (!f_SelftestRunning())
            QMessageBox::warning(m_pHost, tr("Add Atlas"), tr("Could not add %1:\n\n%2").arg(QDir::toNativeSeparators(l_oPath), l_oErr));
        return false;
    }
    return true;
}

// ---- node helpers -------------------------------------------------------------------------------

void AnimEditor::f_AddSpriteNode()
{
    QString l_oSprite = m_pSprites->f_CurrentSprite();   // "id:name"
    if (l_oSprite.isEmpty()) {
        if (m_pDoc->f_SpriteChoices().isEmpty()) {
            m_pDoc->f_Message(tr("No sprites: add an atlas under Properties > Atlases first."));
            return;
        }
        bool l_bOk = false;
        const QString l_oPicked = QInputDialog::getItem(m_pHost, tr("Add Sprite Node"), tr("Sprite"), m_pDoc->f_SpriteChoices(), 0, true, &l_bOk);
        l_oSprite = QString::fromStdString(m_pDoc->f_SpriteFromText(l_oPicked));
        if (!l_bOk || l_oSprite.isEmpty()) return;
    }
    m_pDoc->f_AddSpriteNode(m_pDoc->f_SelectedNode() ? m_pDoc->f_SelectedPath() : NodePath(), l_oSprite, glm::vec2(0, 0));
}

void AnimEditor::f_MoveSelected(int l_iDelta)
{
    const NodePath l_vP = m_pDoc->f_SelectedPath();
    if (l_vP.empty()) return;
    const NodePath l_vParent(l_vP.begin(), l_vP.end() - 1);
    // moveNode takes the index before the node is taken out: one further when moving down.
    const int l_iIndex = l_iDelta < 0 ? l_vP.back() - 1 : l_vP.back() + 2;
    if (l_iIndex < 0) return;
    m_pDoc->f_MoveNode(l_vP, l_vParent, l_iIndex);
}

void AnimEditor::f_UpdateActions()
{
    const bool l_bNode = m_pDoc->f_SelectedNode() != nullptr;
    const bool l_bChild = l_bNode && !m_pDoc->f_SelectedPath().empty();
    for (QAction* l_pA : {m_pAddChildAct, m_pAddSiblingAct, m_pAddSpriteAct, m_pRenameAct, m_pInsertKeyAct}) l_pA->setEnabled(l_bNode);
    m_pQualifyAct->setEnabled(m_pDoc->f_BareSpriteReferenceCount() > 0);
    for (QAction* l_pA : {m_pDupAct, m_pDeleteAct, m_pUpAct, m_pDownAct, m_pCutAct}) l_pA->setEnabled(l_bChild);
    for (QAction* l_pA : {m_pCopyAct, m_pPasteAct}) l_pA->setEnabled(l_bNode);
    m_pPasteSiblingAct->setEnabled(l_bChild);
}

void AnimEditor::f_CopyNode(bool l_bCut)
{
    const NodePath l_vP = m_pDoc->f_SelectedPath();
    const QString l_oText = m_pDoc->f_CopyNodeText(l_vP);
    if (l_oText.isEmpty()) return;
    auto* l_pMime = new QMimeData;
    l_pMime->setText(l_oText);
    l_pMime->setData(QStringLiteral("application/x-toms-anim-node"), l_oText.toUtf8());
    QGuiApplication::clipboard()->setMimeData(l_pMime);
    const Node* l_pN = m_pDoc->f_SelectedNode();
    m_pHost->statusBar()->showMessage(tr("Copied node '%1'").arg(l_pN ? QString::fromStdString(l_pN->m_strName) : QString()), 4000);
    if (l_bCut && !l_vP.empty()) m_pDoc->f_DeleteNode(l_vP);
}

void AnimEditor::f_PasteNode(bool l_bSibling)
{
    const QMimeData* l_pMime = QGuiApplication::clipboard()->mimeData();
    const QString l_oText = !l_pMime ? QString()
                         : l_pMime->hasFormat(QStringLiteral("application/x-toms-anim-node"))
                             ? QString::fromUtf8(l_pMime->data(QStringLiteral("application/x-toms-anim-node")))
                             : l_pMime->text();
    const NodePath l_vSel = m_pDoc->f_SelectedPath();
    if (!m_pDoc->f_SelectedNode()) return;
    if (l_bSibling && !l_vSel.empty()) m_pDoc->f_PasteNode(NodePath(l_vSel.begin(), l_vSel.end() - 1), l_vSel.back() + 1, l_oText);
    else m_pDoc->f_PasteNode(l_vSel, -1, l_oText);
}

void AnimEditor::f_ConvertMpdiFolder()
{
    const QString l_oFolder = QFileDialog::getExistingDirectory(m_pHost, tr("Convert MPDI: the folder with the .mpdi and .pi files"),
                                                             f_Settings().value(QStringLiteral("convert/mpdiFolder")).toString());
    if (l_oFolder.isEmpty()) return;
    f_Settings().setValue(QStringLiteral("convert/mpdiFolder"), l_oFolder);
    // Where the results go: next to the old files, or a folder of their own (subfolders kept).
    QMessageBox l_oWhere(QMessageBox::Question, tr("Convert MPDI"),
                      tr("Write the .anim files and the converted atlases next to the .mpdi files, or into another folder?"),
                      QMessageBox::NoButton, m_pHost);
    QPushButton* l_pNextTo = l_oWhere.addButton(tr("Next to Them"), QMessageBox::AcceptRole);
    QPushButton* l_pOther = l_oWhere.addButton(tr("Another Folder…"), QMessageBox::ActionRole);
    l_oWhere.addButton(QMessageBox::Cancel);
    l_oWhere.exec();
    QString l_oOut;
    if (l_oWhere.clickedButton() == l_pOther) {
        l_oOut = QFileDialog::getExistingDirectory(m_pHost, tr("Convert MPDI: the output folder"),
                                                f_Settings().value(QStringLiteral("convert/mpdiOut"), l_oFolder).toString());
        if (l_oOut.isEmpty()) return;
        f_Settings().setValue(QStringLiteral("convert/mpdiOut"), l_oOut);
    } else if (l_oWhere.clickedButton() != l_pNextTo) {
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const animed::MpdiConvertReport l_oR = animed::f_ConvertMpdiFolder(l_oFolder.toStdString(), l_oOut.toStdString());
    QApplication::restoreOverrideCursor();

    QString l_oSummary = tr("%1 .mpdi found: %2 .anim written (%3 clips, %4 nodes), %5 .pi converted to atlases.")
                          .arg(l_oR.m_iMpdiFiles).arg(l_oR.m_iAnimFiles).arg(l_oR.m_iClips).arg(l_oR.m_iNodes).arg(l_oR.m_iPiFiles);
    if (!l_oR.m_vErrors.empty()) l_oSummary += tr("\n%1 file(s) could not be converted.").arg(l_oR.m_vErrors.size());
    if (!l_oR.m_vWarnings.empty()) l_oSummary += tr("\n%1 warning(s): what was approximated or left out.").arg(l_oR.m_vWarnings.size());
    QStringList l_oDetails;
    for (const std::string& l_strE : l_oR.m_vErrors) l_oDetails << QStringLiteral("error: ") + QString::fromStdString(l_strE);
    for (const std::string& l_strW : l_oR.m_vWarnings) l_oDetails << QStringLiteral("warning: ") + QString::fromStdString(l_strW);
    l_oDetails << QString();
    for (const std::string& l_strF : l_oR.m_vWritten) l_oDetails << QString::fromStdString(l_strF);
    QMessageBox l_oBox(l_oR.m_vErrors.empty() ? QMessageBox::Information : QMessageBox::Warning, tr("Convert MPDI"), l_oSummary,
                    QMessageBox::Ok, m_pHost);
    l_oBox.setDetailedText(l_oDetails.join(QLatin1Char('\n')));
    QString l_oFirstAnim;
    for (const std::string& l_strF : l_oR.m_vWritten)
        if (QFileInfo(QString::fromStdString(l_strF)).suffix() == QLatin1String("anim")) { l_oFirstAnim = QString::fromStdString(l_strF); break; }
    QPushButton* l_pOpen = l_oFirstAnim.isEmpty() ? nullptr : l_oBox.addButton(tr("Open the First .anim"), QMessageBox::ActionRole);
    l_oBox.exec();
    if (l_pOpen && l_oBox.clickedButton() == l_pOpen && f_MaybeSave()) f_OpenFile(l_oFirstAnim, false);
    m_pHost->statusBar()->showMessage(l_oSummary.section(QLatin1Char('\n'), 0, 0), 10000);
}

void AnimEditor::f_MotionRecipeDialog()
{
    const toms::anim::Node* l_pN = m_pDoc->f_SelectedNode();
    if (!l_pN) {
        QMessageBox::information(m_pHost, tr("Motion Recipe"), tr("Select the node the motion is for first."));
        return;
    }
    QDialog l_oDlg(m_pHost);
    l_oDlg.setWindowTitle(tr("Motion Recipe: %1").arg(QString::fromStdString(l_pN->m_strName)));
    auto* l_pForm = new QFormLayout(&l_oDlg);
    auto* l_pRecipe = new QComboBox(&l_oDlg);
    for (const animed::MotionRecipe& l_oR : animed::f_MotionRecipes()) {
        const QString l_oTitle = tr(l_oR.m_strTitle.c_str()), l_oDescription = tr(l_oR.m_strDescription.c_str());   // anim.json has them
        l_pRecipe->addItem(l_oR.m_bBones ? tr("%1 (bones)").arg(l_oTitle) : l_oTitle, QString::fromStdString(l_oR.m_strId));
        l_pRecipe->setItemData(l_pRecipe->count() - 1, l_oDescription, Qt::ToolTipRole);
    }
    auto* l_pAbout = new QLabel(&l_oDlg);
    l_pAbout->setWordWrap(true);
    l_pAbout->setForegroundRole(QPalette::PlaceholderText);
    auto* l_pStart = new QDoubleSpinBox(&l_oDlg);
    l_pStart->setRange(0, 3600);
    l_pStart->setDecimals(3);
    l_pStart->setSingleStep(0.1);
    l_pStart->setSuffix(tr(" s"));
    l_pStart->setValue(m_pDoc->f_Time());
    auto* l_pPeriod = new QDoubleSpinBox(&l_oDlg);
    l_pPeriod->setRange(0.05, 60);
    l_pPeriod->setDecimals(2);
    l_pPeriod->setSingleStep(0.1);
    l_pPeriod->setSuffix(tr(" s"));
    auto* l_pStrength = new QDoubleSpinBox(&l_oDlg);
    l_pStrength->setRange(0.1, 4);
    l_pStrength->setDecimals(2);
    l_pStrength->setSingleStep(0.1);
    l_pStrength->setValue(1.0);
    l_pStrength->setToolTip(tr("1 = the recipe's normal size; 0.5 subtle, 2 strong"));
    auto* l_pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &l_oDlg);
    auto l_fnUpdate = [&] {
        const animed::MotionRecipe* l_pR = animed::f_FindMotionRecipe(l_pRecipe->currentData().toString().toStdString());
        if (!l_pR) return;
        l_pPeriod->setValue(l_pR->m_fPeriod);
        const bool l_bFits = !l_pR->m_bBones || l_pN->f_Skinned();
        l_pAbout->setText(tr(l_pR->m_strDescription.c_str()) +
                       (l_bFits ? QString() : QStringLiteral("\n") + tr("This node has no skin: give it one in Properties > Skin first.")));
        l_pButtons->button(QDialogButtonBox::Ok)->setEnabled(l_bFits);
    };
    connect(l_pRecipe, &QComboBox::currentIndexChanged, &l_oDlg, l_fnUpdate);
    l_pForm->addRow(tr("Recipe"), l_pRecipe);
    l_pForm->addRow(QString(), l_pAbout);
    l_pForm->addRow(tr("Start"), l_pStart);
    l_pForm->addRow(tr("Period"), l_pPeriod);
    l_pForm->addRow(tr("Strength"), l_pStrength);
    l_pForm->addRow(new QLabel(tr("It replaces the node's keys of that kind (deform, or its bones' rotations) between the\n"
                               "start and the end. Loop the clip (or the node) for an idle that repeats."), &l_oDlg));
    l_pForm->addRow(l_pButtons);
    connect(l_pButtons, &QDialogButtonBox::accepted, &l_oDlg, &QDialog::accept);
    connect(l_pButtons, &QDialogButtonBox::rejected, &l_oDlg, &QDialog::reject);
    l_fnUpdate();
    if (l_oDlg.exec() != QDialog::Accepted) return;
    if (m_pDoc->f_ApplyMotionRecipe(l_pRecipe->currentData().toString().toStdString(), float(l_pStart->value()), float(l_pPeriod->value()),
                                 float(l_pStrength->value())))
        m_pHost->statusBar()->showMessage(tr("Motion recipe written: %1").arg(l_pRecipe->currentText()), 4000);
}
