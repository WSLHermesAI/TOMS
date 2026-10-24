// uied_window.h -- ui_editor: the game's UI files (assets/media/ui/*.rml, *.rcss) beside the running game, which
// shows every edit as it is typed (docs/08_RMLUI.md "The UI editor").
//
//   * Files (left): the UI folder's documents; ● = changed, not saved.
//   * The text (middle): colours, line numbers. A pause in typing (0.3 s) previews it: the changed files are handed to
//     RmlUi from memory (RmlUi::setVirtualFile) and the documents reload. Disk changes only on Save.
//   * The game (right): BgfxViewport, the real game. Preview picks what it shows (the title, or any test scenario:
//     a battle, a dialogue, the store, ...); Language shows the text in another language (not saved in settings).
//   * Problems (bottom): RmlUi's warnings and errors since the last preview; double-click opens the place it names.
//   * Inspect (Ctrl+I): a click in the game picks the UI element under it instead of pressing it: its .rml line opens,
//     and the Inspector lists the style rules that name its id, classes or tag.
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
    explicit UiEditorWindow(QWidget* parent = nullptr);
    ~UiEditorWindow() override;

    // ---- for the self-test and the smoke test ----
    void setQuiet(bool on) { quiet_ = on; }            // no message boxes (automated runs)
    bool openFile(const QString& name);                // a file of the UI folder, by name
    QString currentFile() const { return current_; }
    QString text(const QString& name) const;           // the editor's text of an open file
    void setText(const QString& name, const QString& text);   // as if typed (previews after the pause)
    bool isModified(const QString& name) const;
    bool save(const QString& name);
    void revert(const QString& name);
    void previewNow();                                  // the pause is over: preview now
    int fileCount() const;
    int problemCount() const;
    int problemAt(const QString& file) const;          // the line of the first problem naming this file (0 = none)
    BgfxViewport* viewport() const { return view_; }
    QComboBox* previewBox() const { return preview_; }
    void startPreviewAt(int index);                     // as picking it in the Preview list
    QString uiDir() const { return uiDir_; }
    void inspectNow();                                  // as a click in inspect mode
    QString inspectSummary() const;
    int inspectRuleCount() const;
    // Help: the tutorial (docs/37_UI_EDITOR.md) and its practice screen (docs/examples/ui_tutorial/practice.rml), which
    // the game itself never shows: Preview "Practice screen" loads it over the map of a new game.
    void openTutorial();
    void openPracticeScreen();
    bool practiceShown() const { return practiceDoc_ != nullptr; }

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    struct Buffer {
        QTextDocument* doc = nullptr;
        UiHighlighter* highlighter = nullptr;
        QString disk;        // the text as it is on disk
        bool crlf = false;   // the file's line ends, kept on save
    };

    void buildUi();
    void fillPreviewList();
    void fillLanguages();
    void startPreview(int index);
    void edited(const QString& name);
    void refreshFileList();
    void addProblem(int level, const QString& message);
    void openProblem(int row);
    void openRule(int row);
    void diskChanged(const QString& path);
    bool saveAll();
    bool askToSave();
    QString readDisk(const QString& name, bool* crlf = nullptr) const;
    QString pathOf(const QString& name) const;   // the UI folder's file, or the tutorial's
    void loadPractice();                         // the practice screen into the running game's UI (when it is up)

    QString uiDir_, scenariosDir_, tutorialDir_, docsDir_;
    QStringList tutorialFiles_;   // practice.rml, practice.rcss
    bool practiceOn_ = false;     // Preview is the practice screen
    Rml::ElementDocument* practiceDoc_ = nullptr;
    Rml::Context* practiceCtx_ = nullptr;   // the context it was loaded into (a restarted game has a new one)
    QTimer practiceTimer_;
    QMap<QString, Buffer> buffers_;
    QString current_;
    bool quiet_ = false;
    bool loading_ = false;      // a buffer is being filled from disk: not an edit
    QTimer previewTimer_;
    QTimer langTimer_;
    QStringList previewPaths_;  // the Preview list's scenarios ("" = the title)
    QStringList ruleTargets_;   // the Inspector rows: "file:line"
    QStringList problemTargets_;

    QListWidget* files_ = nullptr;
    UiCodeEdit* edit_ = nullptr;
    BgfxViewport* view_ = nullptr;
    QListWidget* problems_ = nullptr;
    QLabel* inspectLabel_ = nullptr;
    QListWidget* rules_ = nullptr;
    QComboBox* preview_ = nullptr;
    QComboBox* lang_ = nullptr;
    QFileSystemWatcher* watcher_ = nullptr;
    QString ownWrite_;          // the file this window is writing (its watcher event is ours)
};
