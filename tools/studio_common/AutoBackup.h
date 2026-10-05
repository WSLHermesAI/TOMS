#pragma once

#include <QObject>
#include <QTimer>

#include <functional>

class QDockWidget;
class QMenu;
class QUndoStack;
class QWidget;

// Automatic backups for an editor's document (tools/studio_common: atlas, anim and particle
// editors). A backup is written
//   - every `minutes` minutes, when there were edits since the last backup, and
//   - after every `edits` edits (undo / redo steps count too),
// into  <local app data>/TOMS/<Editor>/backups/<file name>/<file name>_<yyyyMMdd-HHmmss-zzz>.<ext>
// (never next to the file: the asset folders stay clean). The newest `keep` per file name are kept.
// Saving resets the count. A backup opens like any file of its kind (File > Open); Save As puts it
// back. The settings are per editor (File > Backups > Settings...).
class AutoBackup : public QObject
{
    Q_OBJECT

public:
    // Writes the document to `path` (references stored so the copy opens from there, e.g. absolute
    // atlas paths). May write a folder named after `path` instead (the atlas editor: a project and
    // its packed atlas). False + error on failure.
    using Writer = std::function<bool(const QString& path, QString* error)>;

    struct Settings {
        bool enabled = true;
        int minutes = 5;
        int edits = 20;
        int keep = 20;
    };
    static Settings settings();
    static void setSettings(const Settings& s);   // every AutoBackup of the process follows

    // currentFile: the document's path ("" while untitled). extension: with the dot (".anim").
    AutoBackup(QUndoStack* stack, const QString& extension, std::function<QString()> currentFile, Writer write,
               QObject* parent = nullptr);
    ~AutoBackup() override;

    static QString folder();                       // the backups folder of this editor
    QString backupNow(QString* error = nullptr);   // the written path, "" on failure
    int pendingEdits() const { return m_pending; }

    // File > Backups: Back Up Now, Open Backup Folder, Settings...
    QMenu* addMenu(QMenu* fileMenu, QWidget* dialogParent);

signals:
    void message(const QString& text);   // for the status bar

private:
    void applySettings();
    void edited();
    void prune(const QString& dir, const QString& stem) const;

    QUndoStack* m_stack;
    QString m_ext;
    std::function<QString()> m_file;
    Writer m_write;
    QTimer m_timer;
    int m_pending = 0;
};

// The undo history as a dock: every step of the document, click one to go back (or forward) to it.
QDockWidget* createHistoryDock(QUndoStack* stack, QWidget* parent);
