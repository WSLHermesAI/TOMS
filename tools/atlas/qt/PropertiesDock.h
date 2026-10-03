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
    explicit PropertiesDock(AtlasDocument* doc, QWidget* parent = nullptr);

signals:
    void importReferencesRequested();   // the Import From References dialog

private:
    QWidget* buildAtlasGroup();
    QWidget* buildSpriteGroup();
    QWidget* buildVariantGroup();
    QWidget* buildReferencesGroup();
    void refreshAtlas();
    void refreshVariant();
    void refreshReferences();
    void refreshSprite();
    void refreshInfo();

    // Atlas group: binds a widget to a field of the project.
    void bindInt(QSpinBox* spin, const QString& key, const QString& text, std::function<void(atlas::Project&, int)> set);
    void bindBool(QCheckBox* box, const QString& text, std::function<void(atlas::Project&, bool)> set);
    // Sprite group: applies to the selected sprites (children only, when asked).
    void editSelection(const QString& text, const QString& mergeKey, const std::function<void(atlas::SpriteDef&)>& change,
                       bool childrenOnly = false);
    std::vector<atlas::SpriteDef> selectedDefs() const;

    AtlasDocument* m_doc;
    bool m_updating = false;

    // Atlas
    QLineEdit* m_projectName;
    QSpinBox *m_maxW, *m_maxH, *m_minW, *m_minH;
    QCheckBox *m_pot, *m_square, *m_fixedSize;
    QSpinBox *m_padding, *m_border, *m_extrude, *m_alphaThreshold;
    QCheckBox *m_trim, *m_dedupe, *m_premultiply;
    QComboBox *m_filter, *m_heuristic;
    QDoubleSpinBox *m_defPivotX, *m_defPivotY;
    QLineEdit *m_outDir, *m_outName;
    std::vector<QCheckBox*> m_formats;

    // Sprite
    QGroupBox* m_spriteGroup;
    QFormLayout* m_spriteForm;
    QLabel* m_noSelection;
    QLineEdit* m_name;
    QLabel* m_kind;
    QComboBox* m_parent;
    QWidget* m_rectRow;
    QSpinBox *m_rectX, *m_rectY, *m_rectW, *m_rectH;
    QCheckBox* m_bake;
    QWidget* m_pivotRow;
    QDoubleSpinBox *m_pivotX, *m_pivotY;
    QToolButton* m_pivotReset;
    QWidget* m_splitRow;
    QCheckBox* m_splitOn;
    QSpinBox* m_split[4];
    QComboBox* m_trimOverride;
    QWidget* m_pinRow;
    QCheckBox* m_pinned;
    QSpinBox *m_pinPage, *m_pinX, *m_pinY;
    QLineEdit* m_tags;
    QLabel *m_frame, *m_orig, *m_offset, *m_source;

    // Variant (shown while the canvas shows one)
    QGroupBox* m_variantGroup;
    QLineEdit* m_variantRef;
    QLabel *m_variantArt, *m_variantStore;

    // References
    QListWidget* m_refList;
    QToolButton* m_refRemove;
};
