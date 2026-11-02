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
    explicit InspectorDock(ParticleDocument* l_pDoc, QWidget* l_pParent = nullptr);

    // For the selftest and the editor.
    QDoubleSpinBox* f_RateSpin() const { return m_pRate; }
    RangeEdit* f_SizeEdit() const { return m_pSize; }
    CurveEdit* f_SizeCurve() const { return m_pSizeCurve; }
    GradientEdit* f_ColorCurve() const { return m_pColorCurve; }
    QComboBox* f_BlendCombo() const { return m_pBlend; }
    QComboBox* f_ShapeCombo() const { return m_pShapeType; }
    QListWidget* f_FramesList() const { return m_pFrames; }
    QTableWidget* f_BurstsTable() const { return m_pBursts; }
    QComboBox* f_SpriteCombo() const { return m_pSprite; }
    QComboBox* f_SimulationCombo() const { return m_pSimulation; }

private:
    QWidget* f_Section(QVBoxLayout* l_pInto, const QString& l_oTitle, QFormLayout** l_pForm);
    QString f_MergeKey(const QString& l_oField);
    void f_Refresh();
    void f_RefreshSprites();
    void f_UpdateShapeRows();
    void f_CommitBursts();
    void f_CommitFrames();

    ParticleDocument* m_pDoc;
    bool m_bUpdating = false;
    QString m_oLastField;
    int m_iSerial = 0;
    QElapsedTimer m_oFieldClock;

    QWidget *m_pEmitterSections = nullptr, *m_pEffectSection = nullptr;
    QLabel* m_pNone;
    // effect
    QLineEdit* m_pEffectName;
    QDoubleSpinBox *m_pDuration, *m_pPrewarm;
    QCheckBox* m_pLoop;
    QSpinBox* m_pSeed;
    // emitter
    QLineEdit* m_pEmitterName;
    Vec2Edit* m_pOffset;
    QDoubleSpinBox *m_pStart, *m_pStop;
    QComboBox* m_pSpace;
    QSpinBox* m_pMax;
    // emission
    QDoubleSpinBox* m_pRate;
    QTableWidget* m_pBursts;
    // shape
    QFormLayout* m_pShapeForm = nullptr;
    QComboBox* m_pShapeType;
    QDoubleSpinBox *m_pLength, *m_pAngle, *m_pWidth, *m_pHeight, *m_pRadius, *m_pThickness, *m_pArcFrom, *m_pArcTo;
    QCheckBox *m_pEdge, *m_pOutward;
    // start values
    RangeEdit *m_pLife, *m_pSpeed, *m_pSize, *m_pRotation, *m_pSpin;
    QDoubleSpinBox *m_pDirection, *m_pSpread;
    ColorButton *m_pColor, *m_pColor2;
    QCheckBox* m_pRandomColor;
    QComboBox* m_pSprite;
    // over life
    GradientEdit* m_pColorCurve;
    CurveEdit *m_pSizeCurve, *m_pSpeedCurve, *m_pSpinCurve;
    // forces
    Vec2Edit* m_pGravity;
    QDoubleSpinBox* m_pDrag;
    RangeEdit *m_pRadial, *m_pTangential;
    // flipbook
    QListWidget* m_pFrames;
    QComboBox* m_pFrameMode;
    QDoubleSpinBox* m_pFps;
    QCheckBox* m_pRandomFrame;
    // render
    QComboBox *m_pBlend, *m_pSrc, *m_pDst;
    QSpinBox* m_pOrder;
    QCheckBox *m_pAlign, *m_pOldestOnTop;
    QComboBox* m_pSimulation;   // where the game simulates it: auto / CPU / GPU
};
