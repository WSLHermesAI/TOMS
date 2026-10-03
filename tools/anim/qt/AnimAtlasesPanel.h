#pragma once

#include "AnimDocument.h"

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

// The file's atlases in lookup order (a bare sprite name resolves to the first atlas that has it):
// each row is the atlas's id (what "id:name" sprite references use; double-click to rename it --
// every reference to it is rewritten in the same undo step), the path as the file stores it
// (relative to the .anim) and its sprite count, red when it did not load. Add / Remove (asks
// first when sprites still use it) / Move up / Move down / Qualify (bare names -> "id:name") /
// Reload; each change is one undo step. Adding goes through AnimEditor (an untitled file is saved
// first, a taken id asks for another), so Add only asks for it.
class AnimAtlasesPanel : public QWidget
{
    Q_OBJECT

public:
    enum Column { ColId, ColPath, ColCount };

    explicit AnimAtlasesPanel(AnimDocument* doc, QWidget* parent = nullptr);

    QTreeWidget* list() const { return m_list; }
    int currentIndex() const;
    void setCurrentIndex(int index);
    // The Remove button: asks when sprite references still use the atlas. False = not removed.
    bool removeAtlas(int index);
    // What editing an id cell commits (also the inline editor's path).
    bool renameId(int index, const QString& id);

signals:
    void addRequested();

private:
    void rebuild();
    void updateButtons();
    void warn(const QString& title, const QString& text);

    AnimDocument* m_doc;
    QTreeWidget* m_list;
    QToolButton *m_add, *m_remove, *m_up, *m_down, *m_qualify, *m_reload;
    bool m_updating = false;
};
