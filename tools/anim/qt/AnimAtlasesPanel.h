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

    explicit AnimAtlasesPanel(AnimDocument* l_pDoc, QWidget* l_pParent = nullptr);

    QTreeWidget* f_List() const { return m_pList; }
    int f_CurrentIndex() const;
    void f_SetCurrentIndex(int l_iIndex);
    // The Remove button: asks when sprite references still use the atlas. False = not removed.
    bool f_RemoveAtlas(int l_iIndex);
    // What editing an id cell commits (also the inline editor's path).
    bool f_RenameId(int l_iIndex, const QString& l_oId);

signals:
    void f_AddRequested();

private:
    void f_Rebuild();
    void f_UpdateButtons();
    void f_Warn(const QString& l_oTitle, const QString& l_oText);

    AnimDocument* m_pDoc;
    QTreeWidget* m_pList;
    QToolButton *m_pAdd, *m_pRemove, *m_pUp, *m_pDown, *m_pQualify, *m_pReload;
    bool m_bUpdating = false;
};
