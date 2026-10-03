#pragma once

#include <QDockWidget>
#include <QElapsedTimer>

class ColorButton;
class CurveEdit;
class GradientEdit;
class ParticleDocument;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTableWidget;
class QToolButton;
class QVBoxLayout;
class RangeEdit;
class Vec2Edit;

// Every setting of the selected effect and emitter, one collapsible section per module:
// Effect, Emitter, Emission, Shape, Start values, Over life, Forces, Flipbook, Render. Each change
// is one undo step (changes to the same field within a second merge, e.g. a held spin arrow).
class InspectorDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit InspectorDock(ParticleDocument* doc, QWidget* parent = nullptr);

    // For the selftest and the editor.
    QDoubleSpinBox* rateSpin() const { return m_rate; }
    RangeEdit* sizeEdit() const { return m_size; }
    CurveEdit* sizeCurve() const { return m_sizeCurve; }
    GradientEdit* colorCurve() const { return m_colorCurve; }
    QComboBox* blendCombo() const { return m_blend; }
    QComboBox* shapeCombo() const { return m_shapeType; }
    QListWidget* framesList() const { return m_frames; }
    QTableWidget* burstsTable() const { return m_bursts; }
    QComboBox* spriteCombo() const { return m_sprite; }
    QComboBox* simulationCombo() const { return m_simulation; }

private:
    QWidget* section(QVBoxLayout* into, const QString& title, QFormLayout** form);
    QString mergeKey(const QString& field);
    void refresh();
    void refreshSprites();
    void updateShapeRows();
    void commitBursts();
    void commitFrames();

    ParticleDocument* m_doc;
    bool m_updating = false;
    QString m_lastField;
    int m_serial = 0;
    QElapsedTimer m_fieldClock;

    QWidget *m_emitterSections = nullptr, *m_effectSection = nullptr;
    QLabel* m_none;
    // effect
    QLineEdit* m_effectName;
    QDoubleSpinBox *m_duration, *m_prewarm;
    QCheckBox* m_loop;
    QSpinBox* m_seed;
    // emitter
    QLineEdit* m_emitterName;
    Vec2Edit* m_offset;
    QDoubleSpinBox *m_start, *m_stop;
    QComboBox* m_space;
    QSpinBox* m_max;
    // emission
    QDoubleSpinBox* m_rate;
    QTableWidget* m_bursts;
    // shape
    QFormLayout* m_shapeForm = nullptr;
    QComboBox* m_shapeType;
    QDoubleSpinBox *m_length, *m_angle, *m_width, *m_height, *m_radius, *m_thickness, *m_arcFrom, *m_arcTo;
    QCheckBox *m_edge, *m_outward;
    // start values
    RangeEdit *m_life, *m_speed, *m_size, *m_rotation, *m_spin;
    QDoubleSpinBox *m_direction, *m_spread;
    ColorButton *m_color, *m_color2;
    QCheckBox* m_randomColor;
    QComboBox* m_sprite;
    // over life
    GradientEdit* m_colorCurve;
    CurveEdit *m_sizeCurve, *m_speedCurve, *m_spinCurve;
    // forces
    Vec2Edit* m_gravity;
    QDoubleSpinBox* m_drag;
    RangeEdit *m_radial, *m_tangential;
    // flipbook
    QListWidget* m_frames;
    QComboBox* m_frameMode;
    QDoubleSpinBox* m_fps;
    QCheckBox* m_randomFrame;
    // render
    QComboBox *m_blend, *m_src, *m_dst;
    QSpinBox* m_order;
    QCheckBox *m_align, *m_oldestOnTop;
    QComboBox* m_simulation;   // where the game simulates it: auto / CPU / GPU
};
