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
    using Writer = std::function<bool(const QString& l_oPath, QString* l_pError)>;

    struct Settings {
        bool m_bEnabled = true;
        int m_iMinutes = 5;
        int m_iEdits = 20;
        int m_iKeep = 20;
    };
    static Settings f_Settings();
    static void f_SetSettings(const Settings& l_oS);   // every AutoBackup of the process follows

    // currentFile: the document's path ("" while untitled). extension: with the dot (".anim").
    AutoBackup(QUndoStack* l_pStack, const QString& l_oExtension, std::function<QString()> l_fnCurrentFile, Writer l_fnWrite,
               QObject* l_pParent = nullptr);
    ~AutoBackup() override;

    static QString f_Folder();                       // the backups folder of this editor
    QString f_BackupNow(QString* l_pError = nullptr);   // the written path, "" on failure
    int f_PendingEdits() const { return m_iPending; }

    // File > Backups: Back Up Now, Open Backup Folder, Settings...
    QMenu* f_AddMenu(QMenu* l_pFileMenu, QWidget* l_pDialogParent);

signals:
    void f_Message(const QString& l_oText);   // for the status bar

private:
    void f_ApplySettings();
    void f_Edited();
    void f_Prune(const QString& l_oDir, const QString& l_oStem) const;

    QUndoStack* m_pStack;
    QString m_oExt;
    std::function<QString()> m_fnFile;
    Writer m_fnWrite;
    QTimer m_oTimer;
    int m_iPending = 0;
};

// The undo history as a dock: every step of the document, click one to go back (or forward) to it.
QDockWidget* f_CreateHistoryDock(QUndoStack* l_pStack, QWidget* l_pParent);
