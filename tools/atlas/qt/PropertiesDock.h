#pragma once

#include <QDockWidget>

#include <functional>
#include <vector>

class AtlasDocument;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QToolButton;
class QWidget;
namespace atlas { struct Project; struct SpriteDef; }

// Right dock. "Atlas": the project's packing settings and output. "Sprite": the selected
// sprite(s); with several selected, a field whose values differ shows blank / indeterminate and
// setting it applies to all of them. Every change is one document edit (spin boxes merge while
// you keep stepping), so everything here is undoable.
class PropertiesDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit PropertiesDock(AtlasDocument* l_pDoc, QWidget* l_pParent = nullptr);

signals:
    void f_ImportReferencesRequested();   // the Import From References dialog

private:
    QWidget* f_BuildAtlasGroup();
    QWidget* f_BuildSpriteGroup();
    QWidget* f_BuildVariantGroup();
    QWidget* f_BuildReferencesGroup();
    void f_RefreshAtlas();
    void f_RefreshVariant();
    void f_RefreshReferences();
    void f_RefreshSprite();
    void f_RefreshInfo();

    // Atlas group: binds a widget to a field of the project.
    void f_BindInt(QSpinBox* l_pSpin, const QString& l_oKey, const QString& l_oText, std::function<void(atlas::Project&, int)> l_fnSet);
    void f_BindBool(QCheckBox* l_pBox, const QString& l_oText, std::function<void(atlas::Project&, bool)> l_fnSet);
    // Sprite group: applies to the selected sprites (children only, when asked).
    void f_EditSelection(const QString& l_oText, const QString& l_oMergeKey, const std::function<void(atlas::SpriteDef&)>& l_fnChange,
                       bool l_bChildrenOnly = false);
    std::vector<atlas::SpriteDef> f_SelectedDefs() const;

    AtlasDocument* m_pDoc;
    bool m_bUpdating = false;

    // Atlas
    QLineEdit* m_pProjectName;
    QSpinBox *m_pMaxW, *m_pMaxH, *m_pMinW, *m_pMinH;
    QCheckBox *m_pPot, *m_pSquare, *m_pFixedSize;
    QSpinBox *m_pPadding, *m_pBorder, *m_pExtrude, *m_pAlphaThreshold;
    QCheckBox *m_pTrim, *m_pDedupe, *m_pPremultiply;
    QComboBox *m_pFilter, *m_pHeuristic;
    QDoubleSpinBox *m_pDefPivotX, *m_pDefPivotY;
    QLineEdit *m_pOutDir, *m_pOutName;
    std::vector<QCheckBox*> m_vFormats;

    // Sprite
    QGroupBox* m_pSpriteGroup;
    QFormLayout* m_pSpriteForm;
    QLabel* m_pNoSelection;
    QLineEdit* m_pName;
    QLabel* m_pKind;
    QComboBox* m_pParent;
    QWidget* m_pRectRow;
    QSpinBox *m_pRectX, *m_pRectY, *m_pRectW, *m_pRectH;
    QCheckBox* m_pBake;
    QWidget* m_pPivotRow;
    QDoubleSpinBox *m_pPivotX, *m_pPivotY;
    QToolButton* m_pPivotReset;
    QWidget* m_pSplitRow;
    QCheckBox* m_pSplitOn;
    QSpinBox* m_arrSplit[4];
    QComboBox* m_pTrimOverride;
    QWidget* m_pPinRow;
    QCheckBox* m_pPinned;
    QSpinBox *m_pPinPage, *m_pPinX, *m_pPinY;
    QLineEdit* m_pTags;
    QLabel *m_pFrame, *m_pOrig, *m_pOffset, *m_pSource;

    // Variant (shown while the canvas shows one)
    QGroupBox* m_pVariantGroup;
    QLineEdit* m_pVariantRef;
    QLabel *m_pVariantArt, *m_pVariantStore;

    // References
    QListWidget* m_pRefList;
    QToolButton* m_pRefRemove;
};
