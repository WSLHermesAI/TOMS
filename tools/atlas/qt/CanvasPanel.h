#pragma once

#include <QWidget>

class AtlasCanvas;
class AtlasDocument;
class CanvasView;
class QLabel;
class QStackedWidget;
class QTabBar;
class SpriteEditCanvas;

// The central area: page tabs + the atlas canvas, or (in sprite edit mode) a breadcrumb bar +
// the sprite edit canvas.
class CanvasPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasPanel(AtlasDocument* doc, QWidget* parent = nullptr);

    AtlasCanvas* atlasCanvas() const { return m_atlas; }
    SpriteEditCanvas* editCanvas() const { return m_edit; }
    CanvasView* currentView() const;
    bool isEditMode() const;

public slots:
    void enterEditMode(const QString& sprite);
    void exitEditMode();

signals:
    void editModeChanged(bool editMode);
    void zoomChanged(double zoom);

private:
    void refreshTabs();
    void refreshBreadcrumb();

    AtlasDocument* m_doc;
    QTabBar* m_tabs;
    QWidget* m_editBar;
    QLabel* m_breadcrumb;
    QStackedWidget* m_stack;
    AtlasCanvas* m_atlas;
    SpriteEditCanvas* m_edit;
};
