// uied_window.h -- ui_editor: the game's UI files (assets/media/ui/*.rml, *.rcss) beside the running game, which
// shows every edit as it is typed (docs/gameplay/01_RMLUI.md "The UI editor").
//
//   * Files (left): the UI folder's documents; ● = changed, not saved. "In this preview" lists the documents the game
//     shows now and the style sheets they link (followed live: a battle, then its victory dialog, ...).
//   * The text (middle): colours, line numbers. A pause in typing (0.3 s) previews it: the changed files are handed to
//     RmlUi from memory (RmlUi::setVirtualFile) and the documents reload. Disk changes only on Save.
//   * The game (right): BgfxViewport, the real game. Preview picks what it shows (the title, or any test scenario:
//     a battle, a dialogue, the store, ...); Language shows the text in another language (not saved in settings).
//   * Problems (bottom): RmlUi's warnings and errors since the last preview; double-click opens the place it names.
//   * Inspect (Ctrl+I): a click in the game picks the UI element under it instead of pressing it: its .rml line opens,
//     and the Inspector lists the style rules that name its id, classes or tag.
//   * The hint: what the text cursor is on (an .rml element, an .rcss rule) glows in the game, a pulsing gold frame
//     over each element it is, so you see what an edit there changes (Preview > Glow What the Cursor Is On).
#pragma once

#include <QMainWindow>
#include <QMap>
#include <QString>
#include <QTimer>

namespace Rml { class Context; class ElementDocument; }

class BgfxViewport;
class QComboBox;
class QFileSystemWatcher;
class QLabel;
class QListWidget;
class QTextDocument;
class UiCodeEdit;
class UiHighlighter;

class UiEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit UiEditorWindow(QWidget* l_pParent = nullptr);
    ~UiEditorWindow() override;

    // ---- for the self-test and the smoke test ----
    void f_SetQuiet(bool l_bOn) { m_bQuiet = l_bOn; }            // no message boxes (automated runs)
    bool f_OpenFile(const QString& l_oName);                // a file of the UI folder, by name
    QString f_CurrentFile() const { return m_oCurrent; }
    QString f_Text(const QString& l_oName) const;           // the editor's text of an open file
    void f_SetText(const QString& l_oName, const QString& l_oText);   // as if typed (previews after the pause)
    bool f_IsModified(const QString& l_oName) const;
    bool f_Save(const QString& l_oName);
    void f_Revert(const QString& l_oName);
    void f_PreviewNow();                                  // the pause is over: preview now
    int f_FileCount() const;
    int f_ProblemCount() const;
    int f_ProblemAt(const QString& l_oFile) const;          // the line of the first problem naming this file (0 = none)
    BgfxViewport* f_Viewport() const { return m_pView; }
    QComboBox* f_PreviewBox() const { return m_pPreview; }
    void f_StartPreviewAt(int l_iIndex);                     // as picking it in the Preview list
    QString f_UiDir() const { return m_oUiDir; }
    void f_InspectNow();                                  // as a click in inspect mode
    QString f_InspectSummary() const;
    int f_InspectRuleCount() const;
    // Help: the tutorial (docs/gameplay/10_UI_EDITOR.md) and its practice screen (docs/examples/ui_tutorial/practice.rml), which
    // the game itself never shows: Preview "Practice screen" loads it over the map of a new game.
    void f_OpenTutorial();
    void f_OpenPracticeScreen();
    bool f_PracticeShown() const { return m_pPracticeDoc != nullptr; }
    // The hint: the selectors the text cursor is on, and how many elements on screen glow for them.
    QStringList f_HintSelectors() const { return m_oHintSelectors; }
    int f_HintCount() const { return m_iHintCount; }
    void f_UpdateHint();                                  // places the glow (a timer does it while the cursor is on something)
    void f_PlaceCursor(int l_iPos);                          // the text cursor at this offset of the open file (as a click there)
    // The files of what the game shows now: each shown .rml, then the .rcss files it links (the file list's top group).
    QStringList f_RelatedFiles() const { return m_oRelated; }
    void f_UpdateRelated();                               // asks the game's UI which documents are shown (a timer does it)

protected:
    void closeEvent(QCloseEvent* l_pE) override;

private:
    struct Buffer {
        QTextDocument* m_pDoc = nullptr;
        UiHighlighter* m_pHighlighter = nullptr;
        QString m_oDisk;        // the text as it is on disk
        bool m_bCrlf = false;   // the file's line ends, kept on save
    };

    void f_BuildUi();
    void f_FillPreviewList();
    void f_FillLanguages();
    void f_StartPreview(int l_iIndex);
    void f_Edited(const QString& l_oName);
    void f_RefreshFileList();
    void f_AddProblem(int l_iLevel, const QString& l_oMessage);
    void f_OpenProblem(int l_iRow);
    void f_OpenRule(int l_iRow);
    void f_DiskChanged(const QString& l_oPath);
    bool f_SaveAll();
    bool f_AskToSave();
    QString f_ReadDisk(const QString& l_oName, bool* l_pCrlf = nullptr) const;
    QString f_PathOf(const QString& l_oName) const;   // the UI folder's file, or the tutorial's
    void f_LoadPractice();                         // the practice screen into the running game's UI (when it is up)

    QString m_oUiDir, m_oScenariosDir, m_oTutorialDir, m_oDocsDir;
    QStringList m_oTutorialFiles;   // practice.rml, practice.rcss
    bool m_bPracticeOn = false;     // Preview is the practice screen
    Rml::ElementDocument* m_pPracticeDoc = nullptr;
    Rml::Context* m_pPracticeCtx = nullptr;   // the context it was loaded into (a restarted game has a new one)
    QTimer m_oPracticeTimer;
    QMap<QString, Buffer> m_oBuffers;
    QString m_oCurrent;
    bool m_bQuiet = false;
    bool m_bLoading = false;      // a buffer is being filled from disk: not an edit
    QTimer m_oPreviewTimer;
    QTimer m_oLangTimer;
    QStringList m_oPreviewPaths;  // the Preview list's scenarios ("" = the title)
    QStringList m_oRelated;       // relatedFiles()
    QMap<QString, QString> m_oRelatedWhy;   // file -> "shown" or the .rml that links it (the tooltip)
    QTimer m_oRelatedTimer;
    // the hint (updateHint): a document of our own over the game's, a frame per element
    void f_CursorMoved();
    void f_DropHint();                                    // closes it (before a reload; a new context)
    QStringList m_oHintSelectors;
    QString m_oHintFile;                                  // an .rml hint looks only in documents loaded from this file
    bool m_bHintOn = true;
    int m_iHintCount = 0;
    Rml::ElementDocument* m_pHintDoc = nullptr;
    Rml::Context* m_pHintCtx = nullptr;
    QTimer m_oHintTimer;
    bool m_bOpenRelated = false;  // a new preview: open its first document once it shows
    QStringList m_oRuleTargets;   // the Inspector rows: "file:line"
    QStringList m_oProblemTargets;

    QListWidget* m_pFiles = nullptr;
    UiCodeEdit* m_pEdit = nullptr;
    BgfxViewport* m_pView = nullptr;
    QListWidget* m_pProblems = nullptr;
    QLabel* m_pInspectLabel = nullptr;
    QListWidget* m_pRules = nullptr;
    QComboBox* m_pPreview = nullptr;
    QComboBox* m_pLang = nullptr;
    QFileSystemWatcher* m_pWatcher = nullptr;
    QString m_oOwnWrite;          // the file this window is writing (its watcher event is ours)
};
