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
    explicit CanvasPanel(AtlasDocument* l_pDoc, QWidget* l_pParent = nullptr);

    AtlasCanvas* f_AtlasCanvas() const { return m_pAtlas; }
    SpriteEditCanvas* f_EditCanvas() const { return m_pEdit; }
    CanvasView* f_CurrentView() const;
    bool f_IsEditMode() const;

public slots:
    void f_EnterEditMode(const QString& l_oSprite);
    void f_ExitEditMode();

signals:
    void f_EditModeChanged(bool l_bEditMode);
    void f_ZoomChanged(double l_dZoom);

private:
    void f_RefreshTabs();
    void f_RefreshBreadcrumb();

    AtlasDocument* m_pDoc;
    QTabBar* m_pTabs;
    QWidget* m_pEditBar;
    QLabel* m_pBreadcrumb;
    QStackedWidget* m_pStack;
    AtlasCanvas* m_pAtlas;
    SpriteEditCanvas* m_pEdit;
};
