#pragma once

#include <QList>
#include <QObject>
#include <QSettings>

class AnimClipsDock;
class AutoBackup;
class AnimDocument;
class AnimEventsDock;
class AnimKeyListDock;
class AnimNodesDock;
class AnimProblemsDock;
class AnimPropertiesDock;
class AnimSpritesDock;
class AnimTransport;
class AnimViewport;
class QAction;
class QActionGroup;
class QDockWidget;
class QMainWindow;
class QMenu;
class QToolBar;

// The animation editor as a unit a host window installs: it owns the document and creates the
// central viewport, the docks, the menus / actions and the toolbars. The host (AnimMainWindow
// today, the studio app's plugin host in Phase 2) only provides the QMainWindow, the theme and
// window settings, and asks maybeSave() before closing.
class AnimEditor : public QObject
{
    Q_OBJECT

public:
    explicit AnimEditor(QMainWindow* l_pHost);

    // Puts the viewport, docks, menus and toolbars on the host.
    void f_Install();

    QString f_Title() const;   // "name.anim[*] - Anim Editor" style, without the app name
    QList<QMenu*> f_Menus() const { return m_oMenus; }
    QMenu* f_ViewMenu() const { return m_pViewMenu; }
    QList<QDockWidget*> f_Docks() const;

    AnimDocument* f_Document() const { return m_pDoc; }
    AnimViewport* f_Viewport() const { return m_pViewport; }
    QAction* f_ShowSkinAction() const { return m_pShowSkinAct; }
    // Edit > Cut / Copy / Paste / Paste as Sibling (nodes; the selftest).
    QList<QAction*> f_NodeClipboardActions() const { return {m_pCutAct, m_pCopyAct, m_pPasteAct, m_pPasteSiblingAct}; }
    QAction* f_ObjectModeAction() const { return m_pObjectModeAct; }
    QAction* f_BoneModeAction() const { return m_pBoneModeAct; }
    QAction* f_LatticeModeAction() const { return m_pLatticeModeAct; }
    QAction* f_WeightModeAction() const { return m_pWeightModeAct; }
    QAction* f_AutoWeightsAction() const { return m_pAutoWeightsAct; }
    QList<QAction*> f_TutorialActions() const { return {m_pTutorialAct, m_pTutorialStartAct, m_pTutorialDoneAct}; }
    // A file of the TOMS repo the editor runs from (Build/<preset>/bin or Editor/bin, a few folders up), "" = not
    // found (this copy of the editor is outside the repo).
    static QString f_RepoFile(const QString& l_oRelative);
    AnimNodesDock* f_NodesDock() const { return m_pNodes; }
    AnimSpritesDock* f_SpritesDock() const { return m_pSprites; }
    AnimPropertiesDock* f_PropertiesDock() const { return m_pProps; }
    AnimKeyListDock* f_KeyListDock() const { return m_pKeys; }
    AnimEventsDock* f_EventsDock() const { return m_pEvents; }
    AnimProblemsDock* f_ProblemsDock() const { return m_pProblems; }
    AnimTransport* f_Transport() const { return m_pTransport; }
    AnimClipsDock* f_ClipsDock() const { return m_pClips; }

    bool f_OpenFile(const QString& l_oPath, bool l_bAskForAtlas = true);
    bool f_MaybeSave();
    // Convert > MPDI Folder: the old engine's .mpdi (and their .pi) under a folder -> .anim (+ atlases).
    void f_ConvertMpdiFolder();
    bool f_Save();
    bool f_SaveAs(const QString& l_oStartDir = QString());   // the dialog opens in startDir when given
    bool f_SaveTo(const QString& l_oPath);
    // Adds an atlas (one undo step). An untitled file is saved first (Save As, starting in the
    // atlas's folder): atlas paths are stored relative to the .anim. False = not added.
    bool f_AddAtlasFile(const QString& l_oPath);
    // Sprites dropped on a time area: asks for the time gap (Yes / No), then one sprite key per
    // reference on the selected node at time, time + gap, ... (one undo step). False = nothing inserted.
    bool f_InsertSpriteKeys(const QStringList& l_oRefs, float l_fTime);
    // Key > Motion Recipe: picks a recipe for the selected node (anim_recipes.h), its start, period and strength.
    void f_MotionRecipeDialog();

    static QSettings f_Settings();
    void f_ReadSettings();
    void f_WriteSettings() const;

signals:
    void f_TitleChanged();

private:
    void f_CreateActions();
    void f_CreateDocks();
    void f_CreateMenus();
    void f_CreateToolBars();
    void f_AddRecentFile(const QString& l_oPath);
    void f_RebuildRecentMenu();
    QString f_LastDir() const;
    void f_OnAddAtlas();
    void f_AddSpriteNode();
    void f_MoveSelected(int l_iDelta);
    void f_UpdateActions();

    QMainWindow* m_pHost;
    AnimDocument* m_pDoc;
    AnimViewport* m_pViewport;
    AnimClipsDock* m_pClips = nullptr;
    QDockWidget* m_pHistory = nullptr;
    AutoBackup* m_pBackup = nullptr;
    AnimNodesDock* m_pNodes = nullptr;
    AnimSpritesDock* m_pSprites = nullptr;
    AnimPropertiesDock* m_pProps = nullptr;
    AnimKeyListDock* m_pKeys = nullptr;
    AnimEventsDock* m_pEvents = nullptr;
    AnimProblemsDock* m_pProblems = nullptr;
    QDockWidget* m_pTimeline = nullptr;
    AnimTransport* m_pTransport = nullptr;

    QList<QMenu*> m_oMenus;
    QMenu* m_pRecentMenu = nullptr;
    QMenu* m_pViewMenu = nullptr;
    QToolBar* m_pMainBar = nullptr;
    QAction *m_pNewAct, *m_pOpenAct, *m_pSaveAct, *m_pSaveAsAct, *m_pAddAtlasAct, *m_pReloadAtlasAct, *m_pQuitAct;
    QAction *m_pUndoAct, *m_pRedoAct;
    QAction* m_pShowSkinAct = nullptr;   // View > Show Skin (AnimViewport::setShowSkin)
    QAction *m_pObjectModeAct = nullptr, *m_pBoneModeAct = nullptr, *m_pLatticeModeAct = nullptr;   // AnimViewport::EditMode
    QAction* m_pWeightModeAct = nullptr;
    // Weights mode: the brush ([ ] size; the tool bar's Brush / Strength), and back to automatic weights.
    QAction *m_pBrushSmallerAct = nullptr, *m_pBrushLargerAct = nullptr, *m_pAutoWeightsAct = nullptr;
    QList<QAction*> m_oBrushBarActions;   // the tool bar's brush controls: shown in Weights mode only
    void f_ResetWeights();
    QAction *m_pCutAct = nullptr, *m_pCopyAct = nullptr, *m_pPasteAct = nullptr, *m_pPasteSiblingAct = nullptr;   // nodes
    void f_CopyNode(bool l_bCut);
    void f_PasteNode(bool l_bSibling);
    QAction *m_pAddChildAct, *m_pAddSiblingAct, *m_pAddSpriteAct, *m_pDupAct, *m_pDeleteAct, *m_pRenameAct, *m_pUpAct, *m_pDownAct;
    QAction *m_pMoveAct, *m_pRotateAct, *m_pScaleAct, *m_pAutoKeyAct, *m_pSnapAct;
    QAction *m_pInsertKeyAct, *m_pQualifyAct, *m_pFitAct, *m_pZoomInAct, *m_pZoomOutAct, *m_pFrameAct;
    QAction *m_pHelpCliAct, *m_pAboutAct;
    // Help > Skinning Tutorial (docs/engine/17_SKINNING_TUTORIAL.md) and its example files (docs/examples/tutorial/).
    QAction *m_pTutorialAct = nullptr, *m_pTutorialStartAct = nullptr, *m_pTutorialDoneAct = nullptr;
};
