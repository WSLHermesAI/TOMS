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
    explicit AnimationDock(AtlasDocument* doc, QWidget* parent = nullptr);

private:
    void refresh();
    void refreshFrames();
    void markMissingSprites();
    QString currentAnimation() const;
    // One undo step on the current animation.
    void editCurrent(const QString& text, const std::function<void(atlas::Animation&)>& change, const QString& mergeKey = QString());
    void onAdd();
    void onRemove();
    void onAddFrames();
    void onRemoveFrames();
    void onMoveFrame(int delta);

    AtlasDocument* m_doc;
    QListWidget* m_list;
    QTableWidget* m_frames;
    QCheckBox* m_loop;
    QToolButton *m_addFrames, *m_removeFrames, *m_up, *m_down, *m_play;
    AnimationPreview* m_preview;
    bool m_updating = false;
};
