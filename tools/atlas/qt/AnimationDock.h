#pragma once

#include <QDockWidget>

#include <functional>

class AnimationPreview;
class AtlasDocument;
class QCheckBox;
class QListWidget;
class QTableWidget;
class QToolButton;
namespace atlas { struct Animation; }

// Bottom dock: the project's animations (sprite + time per frame) with a live preview.
// Frames are added from the sprites selected in the tree/canvas.
class AnimationDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnimationDock(AtlasDocument* l_pDoc, QWidget* l_pParent = nullptr);

private:
    void f_Refresh();
    void f_RefreshFrames();
    void f_MarkMissingSprites();
    QString f_CurrentAnimation() const;
    // One undo step on the current animation.
    void f_EditCurrent(const QString& l_oText, const std::function<void(atlas::Animation&)>& l_fnChange, const QString& l_oMergeKey = QString());
    void f_OnAdd();
    void f_OnRemove();
    void f_OnAddFrames();
    void f_OnRemoveFrames();
    void f_OnMoveFrame(int l_iDelta);

    AtlasDocument* m_pDoc;
    QListWidget* m_pList;
    QTableWidget* m_pFrames;
    QCheckBox* m_pLoop;
    QToolButton *m_pAddFrames, *m_pRemoveFrames, *m_pUp, *m_pDown, *m_pPlay;
    AnimationPreview* m_pPreview;
    bool m_bUpdating = false;
};
