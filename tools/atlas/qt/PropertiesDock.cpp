#include "PropertiesDock.h"

#include "AtlasDocument.h"
#include "Icons.h"
#include "atlas_export.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

using atlas::Project;
using atlas::SpriteDef;

namespace {

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }
std::string f_U8(const QString& l_oS) { return l_oS.toStdString(); }

const char* g_pszMixed = "–";

// Spin boxes that can show "mixed": one step below the real minimum is the blank state.
QSpinBox* f_MixedSpin(int l_iLo, int l_iHi, QWidget* l_pParent)
{
    auto* l_pS = new QSpinBox(l_pParent);
    l_pS->setRange(l_iLo - 1, l_iHi);
    l_pS->setSpecialValueText(QString::fromUtf8(g_pszMixed));
    l_pS->setKeyboardTracking(false);   // commit on Enter / focus out / arrow, not per keystroke
    l_pS->setMinimumWidth(56);
    return l_pS;
}

QDoubleSpinBox* f_MixedDoubleSpin(double l_dLo, double l_dHi, QWidget* l_pParent)
{
    auto* l_pS = new QDoubleSpinBox(l_pParent);
    l_pS->setDecimals(3);
    l_pS->setSingleStep(0.05);
    l_pS->setRange(l_dLo - 0.001, l_dHi);
    l_pS->setSpecialValueText(QString::fromUtf8(g_pszMixed));
    l_pS->setKeyboardTracking(false);
    l_pS->setMinimumWidth(64);
    return l_pS;
}

bool f_IsMixed(const QSpinBox* l_pS) { return l_pS->value() == l_pS->minimum(); }
bool f_IsMixed(const QDoubleSpinBox* l_pS) { return l_pS->value() <= l_pS->minimum(); }

void f_ShowValue(QSpinBox* l_pS, bool l_bSame, int l_iV) { l_pS->setValue(l_bSame ? l_iV : l_pS->minimum()); }
void f_ShowValue(QDoubleSpinBox* l_pS, bool l_bSame, double l_dV) { l_pS->setValue(l_bSame ? l_dV : l_pS->minimum()); }

void f_ShowValue(QCheckBox* l_pB, bool l_bSame, bool l_bV)
{
    l_pB->setTristate(!l_bSame);
    if (l_bSame) l_pB->setChecked(l_bV);
    else l_pB->setCheckState(Qt::PartiallyChecked);
}

void f_ShowValue(QLineEdit* l_pE, bool l_bSame, const QString& l_oV)
{
    l_pE->setText(l_bSame ? l_oV : QString());
    l_pE->setPlaceholderText(l_bSame ? QString() : QObject::tr("(mixed)"));
}

// True when get() gives the same value for every def; `out` gets the first one.
template <class T, class F>
bool f_Common(const std::vector<SpriteDef>& l_vDefs, F l_oGet, T& l_oOut)
{
    l_oOut = l_oGet(l_vDefs.front());
    for (const SpriteDef& l_oD : l_vDefs)
        if (!(l_oGet(l_oD) == l_oOut)) return false;
    return true;
}

// Shows the value get() has for every def, or "mixed" when they differ.
template <class W, class F>
void f_ShowCommon(W* l_pW, const std::vector<SpriteDef>& l_vDefs, F l_oGet)
{
    decltype(l_oGet(l_vDefs.front())) l_oV{};
    const bool l_bSame = f_Common(l_vDefs, l_oGet, l_oV);
    f_ShowValue(l_pW, l_bSame, l_oV);
}

QSpinBox* f_PlainSpin(int l_iLo, int l_iHi, QWidget* l_pParent)
{
    auto* l_pS = new QSpinBox(l_pParent);
    l_pS->setRange(l_iLo, l_iHi);
    l_pS->setKeyboardTracking(false);
    l_pS->setMinimumWidth(64);
    return l_pS;
}

// A row of labelled fields: "x [ ] y [ ] ...".
QWidget* f_FieldRow(QWidget* l_pParent, const QList<QPair<QString, QWidget*>>& l_oFields, QWidget* l_pTrailing = nullptr)
{
    auto* l_pRow = new QWidget(l_pParent);
    auto* l_pL = new QHBoxLayout(l_pRow);
    l_pL->setContentsMargins(0, 0, 0, 0);
    l_pL->setSpacing(4);
    for (const auto& l_pairF : l_oFields) {
        if (!l_pairF.first.isEmpty()) {
            auto* l_pLab = new QLabel(l_pairF.first, l_pRow);
            l_pLab->setForegroundRole(QPalette::PlaceholderText);
            l_pL->addWidget(l_pLab);
        }
        l_pL->addWidget(l_pairF.second, 1);
    }
    if (l_pTrailing) l_pL->addWidget(l_pTrailing);
    return l_pRow;
}

QLabel* f_InfoLabel(QWidget* l_pParent)
{
    auto* l_pL = new QLabel(l_pParent);
    l_pL->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Long paths must not widen the dock: clip, the tooltip has the full text.
    l_pL->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return l_pL;
}

const atlas::Heuristic g_arrHeuristics[] = {atlas::Heuristic::Auto,        atlas::Heuristic::BestShortSideFit,
                                        atlas::Heuristic::BestLongSideFit, atlas::Heuristic::BestAreaFit,
                                        atlas::Heuristic::BottomLeft,  atlas::Heuristic::ContactPoint,
                                        atlas::Heuristic::Shelf};

}  // namespace

PropertiesDock::PropertiesDock(AtlasDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Properties"), l_pParent)
    , m_pDoc(l_pDoc)
{
    setObjectName(QStringLiteral("PropertiesDock"));
    auto* l_pScroll = new QScrollArea(this);
    l_pScroll->setWidgetResizable(true);
    l_pScroll->setFrameShape(QFrame::NoFrame);
    // Rows are laid out to fit the dock; a sideways scroll bar would only hide the right column.
    l_pScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* l_pBody = new QWidget(l_pScroll);
    auto* l_pLayout = new QVBoxLayout(l_pBody);
    l_pLayout->setContentsMargins(6, 6, 6, 6);
    l_pLayout->setSpacing(8);
    l_pLayout->addWidget(f_BuildSpriteGroup());
    l_pLayout->addWidget(f_BuildVariantGroup());
    l_pLayout->addWidget(f_BuildAtlasGroup());
    l_pLayout->addWidget(f_BuildReferencesGroup());
    l_pLayout->addStretch(1);
    l_pScroll->setWidget(l_pBody);
    setWidget(l_pScroll);

    connect(l_pDoc, &AtlasDocument::f_ProjectChanged, this, [this] {
        f_RefreshAtlas();
        f_RefreshSprite();
        f_RefreshVariant();
        f_RefreshReferences();
    });
    connect(l_pDoc, &AtlasDocument::f_VariantChanged, this, [this] {
        f_RefreshVariant();
        f_RefreshSprite();
    });
    // Relative paths are shown as stored; their tooltips follow the project file.
    connect(l_pDoc, &AtlasDocument::f_FilePathChanged, this, &PropertiesDock::f_RefreshReferences);
    connect(l_pDoc, &AtlasDocument::f_SelectionChanged, this, &PropertiesDock::f_RefreshSprite);
    connect(l_pDoc, &AtlasDocument::f_BuildFinished, this, &PropertiesDock::f_RefreshInfo);
    f_RefreshAtlas();
    f_RefreshSprite();
    f_RefreshVariant();
    f_RefreshReferences();
}

// ---- Atlas group --------------------------------------------------------------------------------

void PropertiesDock::f_BindInt(QSpinBox* l_pSpin, const QString& l_oKey, const QString& l_oText, std::function<void(Project&, int)> l_fnSet)
{
    connect(l_pSpin, &QSpinBox::valueChanged, this, [this, l_oKey, l_oText, l_fnSet](int l_iV) {
        if (m_bUpdating) return;
        m_pDoc->f_Edit(l_oText, [&](Project& l_oP) { l_fnSet(l_oP, l_iV); }, QStringLiteral("atlas.") + l_oKey);
    });
}

void PropertiesDock::f_BindBool(QCheckBox* l_pBox, const QString& l_oText, std::function<void(Project&, bool)> l_fnSet)
{
    connect(l_pBox, &QCheckBox::toggled, this, [this, l_oText, l_fnSet](bool l_bV) {
        if (m_bUpdating) return;
        m_pDoc->f_Edit(l_oText, [&](Project& l_oP) { l_fnSet(l_oP, l_bV); });
    });
}

QWidget* PropertiesDock::f_BuildAtlasGroup()
{
    auto* l_pGroup = new QGroupBox(tr("Atlas"));
    auto* l_pForm = new QFormLayout(l_pGroup);
    l_pForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_pProjectName = new QLineEdit(l_pGroup);
    l_pForm->addRow(tr("Name"), m_pProjectName);
    connect(m_pProjectName, &QLineEdit::editingFinished, this, [this] {
        const std::string l_strN = f_U8(m_pProjectName->text().trimmed());
        if (m_bUpdating || l_strN.empty() || l_strN == m_pDoc->f_Project().m_strName) return;
        m_pDoc->f_Edit(tr("Rename atlas"), [&](Project& l_oP) { l_oP.m_strName = l_strN; });
    });

    m_pMaxW = f_PlainSpin(16, 16384, l_pGroup);
    m_pMaxH = f_PlainSpin(16, 16384, l_pGroup);
    m_pMinW = f_PlainSpin(1, 16384, l_pGroup);
    m_pMinH = f_PlainSpin(1, 16384, l_pGroup);
    l_pForm->addRow(tr("Max size"), f_FieldRow(l_pGroup, {{QString(), m_pMaxW}, {QStringLiteral("×"), m_pMaxH}}));
    l_pForm->addRow(tr("Min size"), f_FieldRow(l_pGroup, {{QString(), m_pMinW}, {QStringLiteral("×"), m_pMinH}}));
    f_BindInt(m_pMaxW, QStringLiteral("maxW"), tr("Max width"), [](Project& l_oP, int l_iV) { l_oP.m_oSettings.m_iMaxWidth = l_iV; });
    f_BindInt(m_pMaxH, QStringLiteral("maxH"), tr("Max height"), [](Project& l_oP, int l_iV) { l_oP.m_oSettings.m_iMaxHeight = l_iV; });
    f_BindInt(m_pMinW, QStringLiteral("minW"), tr("Min width"), [](Project& l_oP, int l_iV) { l_oP.m_oSettings.m_iMinWidth = l_iV; });
    f_BindInt(m_pMinH, QStringLiteral("minH"), tr("Min height"), [](Project& l_oP, int l_iV) { l_oP.m_oSettings.m_iMinHeight = l_iV; });

    m_pPot = new QCheckBox(tr("Power of two"), l_pGroup);
    m_pSquare = new QCheckBox(tr("Square"), l_pGroup);
    m_pFixedSize = new QCheckBox(tr("Fixed size"), l_pGroup);
    m_pFixedSize->setToolTip(tr("Always use the maximum size instead of shrinking the page to fit"));
    auto* l_pSizeFlags = new QWidget(l_pGroup);
    auto* l_pSl = new QGridLayout(l_pSizeFlags);
    l_pSl->setContentsMargins(0, 0, 0, 0);
    l_pSl->addWidget(m_pPot, 0, 0);
    l_pSl->addWidget(m_pSquare, 0, 1);
    l_pSl->addWidget(m_pFixedSize, 1, 0);
    l_pForm->addRow(QString(), l_pSizeFlags);
    f_BindBool(m_pPot, tr("Power of two"), [](Project& l_oP, bool l_bV) { l_oP.m_oSettings.m_bPowerOfTwo = l_bV; });
    f_BindBool(m_pSquare, tr("Square pages"), [](Project& l_oP, bool l_bV) { l_oP.m_oSettings.m_bSquare = l_bV; });
    f_BindBool(m_pFixedSize, tr("Fixed size"), [](Project& l_oP, bool l_bV) { l_oP.m_oSettings.m_bFixedSize = l_bV; });

    m_pPadding = f_PlainSpin(0, 64, l_pGroup);
    m_pBorder = f_PlainSpin(0, 64, l_pGroup);
    m_pExtrude = f_PlainSpin(0, 16, l_pGroup);
    m_pPadding->setToolTip(tr("Empty pixels between sprites"));
    m_pBorder->setToolTip(tr("Empty pixels along the page edges"));
    m_pExtrude->setToolTip(tr("Edge pixels copied outward around each sprite, so scaling or filtering never picks "
                            "up a neighbour or the gap (default 1). Raise it when sprites are drawn much smaller "
                            "(mipmaps, strong zoom-out); 0 only if every sprite is always drawn at a whole-number "
                            "scale."));
    l_pForm->addRow(tr("Padding"), m_pPadding);
    l_pForm->addRow(tr("Border"), m_pBorder);
    l_pForm->addRow(tr("Extrude"), m_pExtrude);
    f_BindInt(m_pPadding, QStringLiteral("padding"), tr("Padding"), [](Project& l_oP, int l_iV) { l_oP.m_oSettings.m_iPadding = l_iV; });
    f_BindInt(m_pBorder, QStringLiteral("border"), tr("Border"), [](Project& l_oP, int l_iV) { l_oP.m_oSettings.m_iBorder = l_iV; });
    f_BindInt(m_pExtrude, QStringLiteral("extrude"), tr("Extrude"), [](Project& l_oP, int l_iV) { l_oP.m_oSettings.m_iExtrude = l_iV; });

    m_pTrim = new QCheckBox(tr("Trim transparent edges"), l_pGroup);
    m_pAlphaThreshold = f_PlainSpin(0, 254, l_pGroup);
    m_pAlphaThreshold->setToolTip(tr("Alpha at or below this counts as transparent when trimming"));
    l_pForm->addRow(QString(), m_pTrim);
    l_pForm->addRow(tr("Alpha ≤"), m_pAlphaThreshold);
    f_BindBool(m_pTrim, tr("Trim"), [](Project& l_oP, bool l_bV) { l_oP.m_oSettings.m_bTrim = l_bV; });
    f_BindInt(m_pAlphaThreshold, QStringLiteral("alpha"), tr("Alpha threshold"), [](Project& l_oP, int l_iV) { l_oP.m_oSettings.m_iAlphaThreshold = l_iV; });

    m_pDedupe = new QCheckBox(tr("Merge identical images"), l_pGroup);
    m_pPremultiply = new QCheckBox(tr("Premultiply alpha"), l_pGroup);
    l_pForm->addRow(QString(), m_pDedupe);
    l_pForm->addRow(QString(), m_pPremultiply);
    f_BindBool(m_pDedupe, tr("Dedupe"), [](Project& l_oP, bool l_bV) { l_oP.m_oSettings.m_bDedupe = l_bV; });
    f_BindBool(m_pPremultiply, tr("Premultiply alpha"), [](Project& l_oP, bool l_bV) { l_oP.m_oSettings.m_bPremultiplyAlpha = l_bV; });

    m_pFilter = new QComboBox(l_pGroup);
    m_pFilter->addItems({QStringLiteral("Nearest"), QStringLiteral("Linear")});
    l_pForm->addRow(tr("Filter"), m_pFilter);
    connect(m_pFilter, &QComboBox::activated, this, [this] {
        const std::string l_strF = f_U8(m_pFilter->currentText());
        m_pDoc->f_Edit(tr("Filter"), [&](Project& l_oP) { l_oP.m_oSettings.m_strFilter = l_strF; });
    });

    m_pHeuristic = new QComboBox(l_pGroup);
    for (atlas::Heuristic l_eH : g_arrHeuristics) m_pHeuristic->addItem(QString::fromLatin1(atlas::f_HeuristicName(l_eH)));
    m_pHeuristic->setToolTip(tr("Placement rule; Auto tries them all and keeps the smallest result"));
    l_pForm->addRow(tr("Heuristic"), m_pHeuristic);
    connect(m_pHeuristic, &QComboBox::activated, this, [this](int l_iI) {
        m_pDoc->f_Edit(tr("Heuristic"), [&](Project& l_oP) { l_oP.m_oSettings.m_eHeuristic = g_arrHeuristics[l_iI]; });
    });

    m_pDefPivotX = f_MixedDoubleSpin(-10, 10, l_pGroup);
    m_pDefPivotY = f_MixedDoubleSpin(-10, 10, l_pGroup);
    l_pForm->addRow(tr("Default pivot"), f_FieldRow(l_pGroup, {{QStringLiteral("x"), m_pDefPivotX}, {QStringLiteral("y"), m_pDefPivotY}}));
    for (int l_iAxis = 0; l_iAxis < 2; l_iAxis++) {
        QDoubleSpinBox* l_pS = l_iAxis ? m_pDefPivotY : m_pDefPivotX;
        connect(l_pS, &QDoubleSpinBox::valueChanged, this, [this, l_iAxis](double l_dV) {
            if (m_bUpdating) return;
            m_pDoc->f_Edit(tr("Default pivot"), [&](Project& l_oP) { l_oP.m_oSettings.m_arrDefaultPivot[l_iAxis] = float(l_dV); },
                        QStringLiteral("atlas.pivot%1").arg(l_iAxis));
        });
    }

    // Output.
    auto* l_pOutLabel = new QLabel(tr("<b>Output</b>"), l_pGroup);
    l_pForm->addRow(l_pOutLabel);
    m_pOutDir = new QLineEdit(l_pGroup);
    auto* l_pBrowse = new QToolButton(l_pGroup);
    l_pBrowse->setText(QStringLiteral("…"));
    l_pBrowse->setToolTip(tr("Choose the output folder"));
    m_pOutDir->setToolTip(tr("Where Save writes the packed atlas (it holds the images) and the output formats; "
                            "\".\" = next to the project"));
    l_pForm->addRow(tr("Folder"), f_FieldRow(l_pGroup, {{QString(), m_pOutDir}}, l_pBrowse));
    connect(m_pOutDir, &QLineEdit::editingFinished, this, [this] {
        const std::string l_strD = f_U8(QDir::fromNativeSeparators(m_pOutDir->text().trimmed()));
        if (m_bUpdating || l_strD == m_pDoc->f_Project().m_oOutput.m_strDir) return;
        m_pDoc->f_Edit(tr("Output folder"), [&](Project& l_oP) { l_oP.m_oOutput.m_strDir = l_strD.empty() ? "." : l_strD; });
    });
    connect(l_pBrowse, &QToolButton::clicked, this, [this] {
        const QString l_oDir = QFileDialog::getExistingDirectory(this, tr("Output Folder"),
                                                              m_pDoc->f_ResolvePath(f_Qs(m_pDoc->f_Project().m_oOutput.m_strDir)));
        if (l_oDir.isEmpty()) return;
        const std::string l_strD = f_U8(m_pDoc->f_ToProjectPath(l_oDir));
        m_pDoc->f_Edit(tr("Output folder"), [&](Project& l_oP) { l_oP.m_oOutput.m_strDir = l_strD; });
    });

    m_pOutName = new QLineEdit(l_pGroup);
    l_pForm->addRow(tr("File name"), m_pOutName);
    connect(m_pOutName, &QLineEdit::editingFinished, this, [this] {
        const std::string l_strN = f_U8(m_pOutName->text().trimmed());
        if (m_bUpdating || l_strN == m_pDoc->f_Project().m_oOutput.m_strName) return;
        m_pDoc->f_Edit(tr("Output name"), [&](Project& l_oP) { l_oP.m_oOutput.m_strName = l_strN; });
    });

    auto* l_pFormats = new QWidget(l_pGroup);
    auto* l_pGrid = new QGridLayout(l_pFormats);
    l_pGrid->setContentsMargins(0, 0, 0, 0);
    const std::vector<std::string>& l_vAll = atlas::f_ExportFormats();
    for (size_t l_nI = 0; l_nI < l_vAll.size(); l_nI++) {
        auto* l_pBox = new QCheckBox(f_Qs(l_vAll[l_nI]), l_pFormats);
        if (l_vAll[l_nI] == g_pszAlwaysExported) {
            l_pBox->setEnabled(false);
            l_pBox->setToolTip(tr("Save always writes the .plist (Cocos Creator Sprite Atlas)"));
        } else if (l_vAll[l_nI] == "atlas") {
            l_pBox->setEnabled(false);
            l_pBox->setToolTip(tr("Save always writes the .atlas: the packed atlas holds the project's images"));
        }
        l_pGrid->addWidget(l_pBox, int(l_nI / 2), int(l_nI % 2));
        m_vFormats.push_back(l_pBox);
        connect(l_pBox, &QCheckBox::toggled, this, [this] {
            if (m_bUpdating) return;
            std::vector<std::string> l_vF;
            for (QCheckBox* l_pB : m_vFormats)
                if (l_pB->isChecked()) l_vF.push_back(f_U8(l_pB->text()));
            m_pDoc->f_Edit(tr("Export formats"), [&](Project& l_oP) { l_oP.m_oOutput.m_vFormats = l_vF; });
        });
    }
    l_pForm->addRow(tr("Formats"), l_pFormats);
    return l_pGroup;
}

void PropertiesDock::f_RefreshAtlas()
{
    m_bUpdating = true;
    const Project& l_oP = m_pDoc->f_Project();
    const atlas::Settings& l_oS = l_oP.m_oSettings;
    m_pProjectName->setText(f_Qs(l_oP.m_strName));
    m_pMaxW->setValue(l_oS.m_iMaxWidth);
    m_pMaxH->setValue(l_oS.m_iMaxHeight);
    m_pMinW->setValue(l_oS.m_iMinWidth);
    m_pMinH->setValue(l_oS.m_iMinHeight);
    m_pPot->setChecked(l_oS.m_bPowerOfTwo);
    m_pSquare->setChecked(l_oS.m_bSquare);
    m_pFixedSize->setChecked(l_oS.m_bFixedSize);
    m_pPadding->setValue(l_oS.m_iPadding);
    m_pBorder->setValue(l_oS.m_iBorder);
    m_pExtrude->setValue(l_oS.m_iExtrude);
    m_pTrim->setChecked(l_oS.m_bTrim);
    m_pAlphaThreshold->setValue(l_oS.m_iAlphaThreshold);
    m_pAlphaThreshold->setEnabled(l_oS.m_bTrim);
    m_pDedupe->setChecked(l_oS.m_bDedupe);
    m_pPremultiply->setChecked(l_oS.m_bPremultiplyAlpha);
    m_pFilter->setCurrentIndex(std::max(0, m_pFilter->findText(f_Qs(l_oS.m_strFilter), Qt::MatchFixedString)));
    for (int l_iI = 0; l_iI < int(std::size(g_arrHeuristics)); l_iI++)
        if (g_arrHeuristics[l_iI] == l_oS.m_eHeuristic) m_pHeuristic->setCurrentIndex(l_iI);
    m_pDefPivotX->setValue(l_oS.m_arrDefaultPivot[0]);
    m_pDefPivotY->setValue(l_oS.m_arrDefaultPivot[1]);
    m_pOutDir->setText(QDir::toNativeSeparators(f_Qs(l_oP.m_oOutput.m_strDir)));
    m_pOutName->setText(f_Qs(l_oP.m_oOutput.m_strName));
    m_pOutName->setPlaceholderText(f_Qs(l_oP.m_strName));
    for (QCheckBox* l_pB : m_vFormats)
        l_pB->setChecked(f_U8(l_pB->text()) == g_pszAlwaysExported || l_pB->text() == QLatin1String("atlas") ||
                      std::find(l_oP.m_oOutput.m_vFormats.begin(), l_oP.m_oOutput.m_vFormats.end(), f_U8(l_pB->text())) != l_oP.m_oOutput.m_vFormats.end());
    m_bUpdating = false;
}

// ---- Sprite group -------------------------------------------------------------------------------

std::vector<SpriteDef> PropertiesDock::f_SelectedDefs() const
{
    std::vector<SpriteDef> l_vDefs;
    for (const QString& l_oN : m_pDoc->f_Selection())
        if (m_pDoc->f_SpriteExists(l_oN)) l_vDefs.push_back(m_pDoc->f_SpriteDef(l_oN));
    return l_vDefs;
}

void PropertiesDock::f_EditSelection(const QString& l_oText, const QString& l_oMergeKey, const std::function<void(SpriteDef&)>& l_fnChange,
                                   bool l_bChildrenOnly)
{
    if (m_bUpdating) return;
    QStringList l_oNames;
    for (const SpriteDef& l_oD : f_SelectedDefs())
        if (!l_bChildrenOnly || l_oD.f_IsChild()) l_oNames << f_Qs(l_oD.m_strName);
    m_pDoc->f_EditSprites(l_oNames, l_oText, l_fnChange, l_oMergeKey.isEmpty() ? QString() : QStringLiteral("sprite.") + l_oMergeKey);
}

QWidget* PropertiesDock::f_BuildSpriteGroup()
{
    m_pSpriteGroup = new QGroupBox(tr("Sprite"));
    auto* l_pOuter = new QVBoxLayout(m_pSpriteGroup);
    m_pNoSelection = new QLabel(tr("Select a sprite in the tree or on the canvas."), m_pSpriteGroup);
    m_pNoSelection->setForegroundRole(QPalette::PlaceholderText);
    m_pNoSelection->setWordWrap(true);
    l_pOuter->addWidget(m_pNoSelection);
    auto* l_pFormHost = new QWidget(m_pSpriteGroup);
    l_pOuter->addWidget(l_pFormHost);
    m_pSpriteForm = new QFormLayout(l_pFormHost);
    m_pSpriteForm->setContentsMargins(0, 0, 0, 0);
    m_pSpriteForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    QWidget* l_pG = l_pFormHost;

    m_pName = new QLineEdit(l_pG);
    m_pSpriteForm->addRow(tr("Name"), m_pName);
    connect(m_pName, &QLineEdit::editingFinished, this, [this] {
        const QString l_oFrom = m_pDoc->f_CurrentSprite();
        const QString l_oTo = m_pName->text().trimmed();
        if (m_bUpdating || m_pDoc->f_Selection().size() != 1 || l_oTo == l_oFrom) return;
        QString l_oErr;
        if (!m_pDoc->f_RenameSprite(l_oFrom, l_oTo, &l_oErr)) {
            QMessageBox::warning(this, tr("Rename"), l_oErr);
            m_pName->setText(l_oFrom);
        }
    });

    m_pKind = new QLabel(l_pG);
    m_pSpriteForm->addRow(tr("Kind"), m_pKind);

    m_pParent = new QComboBox(l_pG);
    m_pParent->setPlaceholderText(tr("(mixed)"));
    m_pSpriteForm->addRow(tr("Parent"), m_pParent);
    connect(m_pParent, &QComboBox::activated, this, [this] {
        const std::string l_strPar = f_U8(m_pParent->currentText());
        f_EditSelection(tr("Change parent"), QString(), [&](SpriteDef& l_oD) { l_oD.m_strParent = l_strPar; }, true);
    });

    m_pRectX = f_MixedSpin(0, 65535, l_pG);
    m_pRectY = f_MixedSpin(0, 65535, l_pG);
    m_pRectW = f_MixedSpin(1, 65535, l_pG);
    m_pRectH = f_MixedSpin(1, 65535, l_pG);
    m_pRectRow = new QWidget(l_pG);
    {
        auto* l_pGrid = new QGridLayout(m_pRectRow);
        l_pGrid->setContentsMargins(0, 0, 0, 0);
        l_pGrid->setHorizontalSpacing(4);
        const QList<QPair<QString, QSpinBox*>> l_oCells = {{QStringLiteral("x"), m_pRectX}, {QStringLiteral("y"), m_pRectY},
                                                        {QStringLiteral("w"), m_pRectW}, {QStringLiteral("h"), m_pRectH}};
        for (int l_iI = 0; l_iI < 4; l_iI++) {
            auto* l_pLab = new QLabel(l_oCells[l_iI].first, m_pRectRow);
            l_pLab->setForegroundRole(QPalette::PlaceholderText);
            l_pGrid->addWidget(l_pLab, l_iI / 2, (l_iI % 2) * 2);
            l_pGrid->addWidget(l_oCells[l_iI].second, l_iI / 2, (l_iI % 2) * 2 + 1);
        }
        l_pGrid->setColumnStretch(1, 1);
        l_pGrid->setColumnStretch(3, 1);
    }
    m_pSpriteForm->addRow(tr("Rect"), m_pRectRow);
    QSpinBox* l_arrRectSpins[4] = {m_pRectX, m_pRectY, m_pRectW, m_pRectH};
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        connect(l_arrRectSpins[l_iI], &QSpinBox::valueChanged, this, [this, l_iI](int l_iV) {
            if (f_IsMixed(l_iI == 0 ? m_pRectX : l_iI == 1 ? m_pRectY : l_iI == 2 ? m_pRectW : m_pRectH)) return;
            f_EditSelection(tr("Child rect"), QStringLiteral("rect%1").arg(l_iI), [&](SpriteDef& l_oD) {
                int* l_arrF[4] = {&l_oD.m_oRect.m_iX, &l_oD.m_oRect.m_iY, &l_oD.m_oRect.m_iW, &l_oD.m_oRect.m_iH};
                *l_arrF[l_iI] = l_iV;
            }, true);
        });
    }

    m_pBake = new QCheckBox(tr("Bake (own copy of the pixels)"), l_pG);
    m_pBake->setToolTip(tr("A baked child is packed as its own image instead of pointing into its parent"));
    m_pSpriteForm->addRow(QString(), m_pBake);
    connect(m_pBake, &QCheckBox::clicked, this, [this] {
        m_pBake->setTristate(false);
        const bool l_bV = m_pBake->isChecked();
        f_EditSelection(l_bV ? tr("Bake") : tr("Unbake"), QString(), [&](SpriteDef& l_oD) { l_oD.m_bBake = l_bV; }, true);
    });

    m_pPivotX = f_MixedDoubleSpin(-10, 10, l_pG);
    m_pPivotY = f_MixedDoubleSpin(-10, 10, l_pG);
    m_pPivotReset = new QToolButton(l_pG);
    m_pPivotReset->setText(tr("Default"));
    m_pPivotReset->setToolTip(tr("Use the atlas default pivot"));
    m_pPivotRow = f_FieldRow(l_pG, {{QStringLiteral("x"), m_pPivotX}, {QStringLiteral("y"), m_pPivotY}}, m_pPivotReset);
    m_pSpriteForm->addRow(tr("Pivot"), m_pPivotRow);
    for (int l_iAxis = 0; l_iAxis < 2; l_iAxis++) {
        QDoubleSpinBox* l_pS = l_iAxis ? m_pPivotY : m_pPivotX;
        connect(l_pS, &QDoubleSpinBox::valueChanged, this, [this, l_pS, l_iAxis](double l_dV) {
            if (f_IsMixed(l_pS)) return;
            const float* l_pDef = m_pDoc->f_Project().m_oSettings.m_arrDefaultPivot;
            f_EditSelection(tr("Pivot"), QStringLiteral("pivot%1").arg(l_iAxis), [&](SpriteDef& l_oD) {
                if (!l_oD.m_bHasPivot) {   // start from what it showed: the default pivot
                    l_oD.m_arrPivot[0] = l_pDef[0];
                    l_oD.m_arrPivot[1] = l_pDef[1];
                }
                l_oD.m_bHasPivot = true;
                l_oD.m_arrPivot[l_iAxis] = float(l_dV);
            });
        });
    }
    connect(m_pPivotReset, &QToolButton::clicked, this, [this] {
        f_EditSelection(tr("Default pivot"), QString(), [](SpriteDef& l_oD) {
            l_oD.m_bHasPivot = false;
            l_oD.m_arrPivot[0] = l_oD.m_arrPivot[1] = 0.5f;
        });
    });

    m_pSplitOn = new QCheckBox(tr("9-slice"), l_pG);
    const char* l_arrSides[4] = {"l", "r", "t", "b"};
    m_pSplitRow = new QWidget(l_pG);
    {
        auto* l_pGrid = new QGridLayout(m_pSplitRow);
        l_pGrid->setContentsMargins(0, 0, 0, 0);
        l_pGrid->setHorizontalSpacing(4);
        l_pGrid->addWidget(m_pSplitOn, 0, 0, 1, 4);
        for (int l_iI = 0; l_iI < 4; l_iI++) {
            m_arrSplit[l_iI] = f_MixedSpin(0, 65535, m_pSplitRow);
            auto* l_pLab = new QLabel(QString::fromLatin1(l_arrSides[l_iI]), m_pSplitRow);
            l_pLab->setForegroundRole(QPalette::PlaceholderText);
            l_pGrid->addWidget(l_pLab, 1 + l_iI / 2, (l_iI % 2) * 2);
            l_pGrid->addWidget(m_arrSplit[l_iI], 1 + l_iI / 2, (l_iI % 2) * 2 + 1);
        }
        l_pGrid->setColumnStretch(1, 1);
        l_pGrid->setColumnStretch(3, 1);
    }
    m_arrSplit[0]->setToolTip(tr("Left border (pixels)"));
    m_arrSplit[1]->setToolTip(tr("Right border (pixels)"));
    m_arrSplit[2]->setToolTip(tr("Top border (pixels)"));
    m_arrSplit[3]->setToolTip(tr("Bottom border (pixels)"));
    m_pSpriteForm->addRow(tr("Borders"), m_pSplitRow);
    connect(m_pSplitOn, &QCheckBox::clicked, this, [this] {
        m_pSplitOn->setTristate(false);
        const bool l_bV = m_pSplitOn->isChecked();
        f_EditSelection(l_bV ? tr("Enable 9-slice") : tr("Disable 9-slice"), QString(), [&](SpriteDef& l_oD) { l_oD.m_bHasSplit = l_bV; });
    });
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        connect(m_arrSplit[l_iI], &QSpinBox::valueChanged, this, [this, l_iI](int l_iV) {
            if (f_IsMixed(m_arrSplit[l_iI])) return;
            f_EditSelection(tr("9-slice border"), QStringLiteral("split%1").arg(l_iI), [&](SpriteDef& l_oD) {
                l_oD.m_bHasSplit = true;
                l_oD.m_arrSplit[l_iI] = l_iV;
            });
        });
    }

    m_pTrimOverride = new QComboBox(l_pG);
    m_pTrimOverride->addItems({tr("Atlas setting"), tr("Trim"), tr("Keep full size")});
    m_pTrimOverride->setPlaceholderText(tr("(mixed)"));
    m_pSpriteForm->addRow(tr("Trim"), m_pTrimOverride);
    connect(m_pTrimOverride, &QComboBox::activated, this, [this](int l_iI) {
        f_EditSelection(tr("Trim"), QString(), [&](SpriteDef& l_oD) { l_oD.m_iTrim = l_iI == 0 ? -1 : l_iI == 1 ? 1 : 0; });
    });

    m_pPinned = new QCheckBox(tr("Pinned"), l_pG);
    m_pPinned->setToolTip(tr("Keep a fixed place in the atlas (drag a sprite on the canvas to pin it)"));
    m_pPinPage = f_MixedSpin(0, 64, l_pG);
    m_pPinX = f_MixedSpin(0, 65535, l_pG);
    m_pPinY = f_MixedSpin(0, 65535, l_pG);
    m_pPinRow = new QWidget(l_pG);
    {
        auto* l_pGrid = new QGridLayout(m_pPinRow);
        l_pGrid->setContentsMargins(0, 0, 0, 0);
        l_pGrid->setHorizontalSpacing(4);
        l_pGrid->addWidget(m_pPinned, 0, 0, 1, 2);
        auto* l_pPl = new QLabel(tr("page"), m_pPinRow);
        l_pPl->setForegroundRole(QPalette::PlaceholderText);
        l_pGrid->addWidget(l_pPl, 0, 2);
        l_pGrid->addWidget(m_pPinPage, 0, 3);
        const QList<QPair<QString, QSpinBox*>> l_oCells = {{QStringLiteral("x"), m_pPinX}, {QStringLiteral("y"), m_pPinY}};
        for (int l_iI = 0; l_iI < 2; l_iI++) {
            auto* l_pLab = new QLabel(l_oCells[l_iI].first, m_pPinRow);
            l_pLab->setForegroundRole(QPalette::PlaceholderText);
            l_pGrid->addWidget(l_pLab, 1, l_iI * 2);
            l_pGrid->addWidget(l_oCells[l_iI].second, 1, l_iI * 2 + 1);
        }
        l_pGrid->setColumnStretch(1, 1);
        l_pGrid->setColumnStretch(3, 1);
    }
    m_pSpriteForm->addRow(tr("Place"), m_pPinRow);
    connect(m_pPinned, &QCheckBox::clicked, this, [this] {
        m_pPinned->setTristate(false);
        const bool l_bV = m_pPinned->isChecked();
        const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
        f_EditSelection(l_bV ? tr("Pin") : tr("Unpin"), QString(), [&](SpriteDef& l_oD) {
            // Pinning keeps the sprite where the last build put it.
            if (l_bV && !l_oD.m_bPinned && l_spSnap)
                if (const atlas::Region* l_pR = l_spSnap->f_Region(f_Qs(l_oD.m_strName)); l_pR && l_pR->m_strAliasOf.empty()) {
                    l_oD.m_iPinPage = l_pR->m_iPage;
                    l_oD.m_iPinX = l_pR->m_oFrame.m_iX;
                    l_oD.m_iPinY = l_pR->m_oFrame.m_iY;
                }
            l_oD.m_bPinned = l_bV;
        });
    });
    QSpinBox* l_arrPinSpins[3] = {m_pPinPage, m_pPinX, m_pPinY};
    for (int l_iI = 0; l_iI < 3; l_iI++) {
        QSpinBox* l_pS = l_arrPinSpins[l_iI];
        connect(l_pS, &QSpinBox::valueChanged, this, [this, l_pS, l_iI](int l_iV) {
            if (f_IsMixed(l_pS)) return;
            f_EditSelection(tr("Pin position"), QStringLiteral("pin%1").arg(l_iI), [&](SpriteDef& l_oD) {
                l_oD.m_bPinned = true;
                (l_iI == 0 ? l_oD.m_iPinPage : l_iI == 1 ? l_oD.m_iPinX : l_oD.m_iPinY) = l_iV;
            });
        });
    }

    m_pTags = new QLineEdit(l_pG);
    m_pTags->setPlaceholderText(tr("comma separated"));
    m_pSpriteForm->addRow(tr("Tags"), m_pTags);
    connect(m_pTags, &QLineEdit::editingFinished, this, [this] {
        // Leaving a "(mixed)" field untouched must not wipe everyone's tags.
        if (m_bUpdating || (m_pTags->text().isEmpty() && m_pTags->placeholderText() == tr("(mixed)"))) return;
        std::vector<std::string> l_vTags;
        for (const QString& l_oT : m_pTags->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
            if (!l_oT.trimmed().isEmpty()) l_vTags.push_back(f_U8(l_oT.trimmed()));
        f_EditSelection(tr("Tags"), QString(), [&](SpriteDef& l_oD) { l_oD.m_vTags = l_vTags; });
    });

    m_pFrame = f_InfoLabel(l_pG);
    m_pOrig = f_InfoLabel(l_pG);
    m_pOffset = f_InfoLabel(l_pG);
    m_pSource = f_InfoLabel(l_pG);
    m_pSpriteForm->addRow(tr("Frame"), m_pFrame);
    m_pSpriteForm->addRow(tr("Original"), m_pOrig);
    m_pSpriteForm->addRow(tr("Offset"), m_pOffset);
    m_pSpriteForm->addRow(tr("Art"), m_pSource);
    return m_pSpriteGroup;
}

void PropertiesDock::f_RefreshSprite()
{
    const std::vector<SpriteDef> l_vDefs = f_SelectedDefs();
    const bool l_bAny = !l_vDefs.empty();
    m_pNoSelection->setVisible(!l_bAny);
    m_pSpriteForm->parentWidget()->setVisible(l_bAny);
    m_pSpriteGroup->setTitle(l_vDefs.size() > 1 ? tr("Sprite (%1 selected)").arg(l_vDefs.size()) : tr("Sprite"));
    if (!l_bAny) return;

    m_bUpdating = true;
    const bool l_bSingle = l_vDefs.size() == 1;
    int l_iChildren = 0;
    for (const SpriteDef& l_oD : l_vDefs) l_iChildren += l_oD.f_IsChild();
    const bool l_bAllChildren = l_iChildren == int(l_vDefs.size()), l_bNoChildren = l_iChildren == 0;
    const SpriteDef& l_oFirst = l_vDefs.front();

    // Name / kind.
    m_pName->setEnabled(l_bSingle);
    f_ShowValue(m_pName, l_bSingle, l_bSingle ? f_Qs(l_oFirst.m_strName) : QString());
    if (!l_bSingle) m_pName->setPlaceholderText(tr("%1 sprites").arg(l_vDefs.size()));
    QString l_oKind;
    if (!l_bSingle) l_oKind = l_bAllChildren ? tr("child sprites") : l_bNoChildren ? tr("image sprites") : tr("images and children");
    else if (l_oFirst.f_IsChild()) l_oKind = l_oFirst.m_bBake ? tr("child (baked)") : tr("child");
    else l_oKind = m_pDoc->f_HasVariantArt(f_Qs(l_oFirst.m_strName), m_pDoc->f_Variant()) ? tr("image + %1 art").arg(m_pDoc->f_Variant()) : tr("image");
    m_pKind->setText(l_oKind);

    // Child fields.
    m_pSpriteForm->setRowVisible(m_pParent, l_bAllChildren);
    m_pSpriteForm->setRowVisible(m_pRectRow, l_bAllChildren);
    m_pSpriteForm->setRowVisible(m_pBake, l_bAllChildren);
    if (l_bAllChildren) {
        // A sprite cannot become a child of itself or of its own descendants.
        QSet<QString> l_oBanned;
        for (const SpriteDef& l_oD : l_vDefs) {
            l_oBanned.insert(f_Qs(l_oD.m_strName));
            for (const QString& l_oC : m_pDoc->f_Descendants(f_Qs(l_oD.m_strName))) l_oBanned.insert(l_oC);
        }
        m_pParent->clear();
        if (const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot())
            for (const SpriteInfo& l_oS : l_spSnap->m_vSprites)
                if (!l_oBanned.contains(l_oS.m_oName)) m_pParent->addItem(l_oS.m_oName);
        std::string l_strPar;
        const bool l_bSameParent = f_Common(l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_strParent; }, l_strPar);
        if (l_bSameParent && m_pParent->findText(f_Qs(l_strPar)) < 0) m_pParent->addItem(f_Qs(l_strPar));   // a missing parent stays visible
        m_pParent->setCurrentIndex(l_bSameParent ? m_pParent->findText(f_Qs(l_strPar)) : -1);
        f_ShowCommon(m_pRectX, l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_oRect.m_iX; });
        f_ShowCommon(m_pRectY, l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_oRect.m_iY; });
        f_ShowCommon(m_pRectW, l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_oRect.m_iW; });
        f_ShowCommon(m_pRectH, l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_oRect.m_iH; });
        f_ShowCommon(m_pBake, l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_bBake; });
    }

    // Pivot (the default when the sprite has none of its own).
    const float* l_pDefPivot = m_pDoc->f_Project().m_oSettings.m_arrDefaultPivot;
    f_ShowCommon(m_pPivotX, l_vDefs, [&](const SpriteDef& l_oD) { return l_oD.m_bHasPivot ? l_oD.m_arrPivot[0] : l_pDefPivot[0]; });
    f_ShowCommon(m_pPivotY, l_vDefs, [&](const SpriteDef& l_oD) { return l_oD.m_bHasPivot ? l_oD.m_arrPivot[1] : l_pDefPivot[1]; });
    bool l_bHasPivot = false;
    m_pPivotReset->setEnabled(!f_Common(l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_bHasPivot; }, l_bHasPivot) || l_bHasPivot);

    // 9-slice.
    bool l_bB = false;
    const bool l_bSameSplit = f_Common(l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_bHasSplit; }, l_bB);
    f_ShowValue(m_pSplitOn, l_bSameSplit, l_bB);
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        f_ShowCommon(m_arrSplit[l_iI], l_vDefs, [l_iI](const SpriteDef& l_oD) { return l_oD.m_arrSplit[l_iI]; });
        m_arrSplit[l_iI]->setEnabled(!l_bSameSplit || l_bB);
    }

    // Trim (images only).
    m_pSpriteForm->setRowVisible(m_pTrimOverride, l_bNoChildren);
    int l_iTrim = -1;
    m_pTrimOverride->setCurrentIndex(f_Common(l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_iTrim; }, l_iTrim) ? (l_iTrim < 0 ? 0 : l_iTrim == 1 ? 1 : 2) : -1);

    // Pin (sprites with their own place: images and baked children).
    bool l_bPinnable = true;
    for (const SpriteDef& l_oD : l_vDefs) l_bPinnable = l_bPinnable && (!l_oD.f_IsChild() || l_oD.m_bBake);
    m_pSpriteForm->setRowVisible(m_pPinRow, l_bPinnable);
    const bool l_bSamePinned = f_Common(l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_bPinned; }, l_bB);
    f_ShowValue(m_pPinned, l_bSamePinned, l_bB);
    f_ShowCommon(m_pPinPage, l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_iPinPage; });
    f_ShowCommon(m_pPinX, l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_iPinX; });
    f_ShowCommon(m_pPinY, l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_iPinY; });
    for (QSpinBox* l_pS : {m_pPinPage, m_pPinX, m_pPinY}) l_pS->setEnabled(!l_bSamePinned || l_bB);

    // Tags.
    QStringList l_oTagList;
    for (const std::string& l_strT : l_oFirst.m_vTags) l_oTagList << f_Qs(l_strT);
    std::vector<std::string> l_vTags;
    const bool l_bSameTags = f_Common(l_vDefs, [](const SpriteDef& l_oD) { return l_oD.m_vTags; }, l_vTags);
    m_pTags->setText(l_bSameTags ? l_oTagList.join(QStringLiteral(", ")) : QString());
    m_pTags->setPlaceholderText(l_bSameTags ? tr("comma separated") : tr("(mixed)"));
    m_bUpdating = false;
    f_RefreshInfo();
}

void PropertiesDock::f_RefreshInfo()
{
    const QString l_oName = m_pDoc->f_Selection().size() == 1 ? m_pDoc->f_CurrentSprite() : QString();
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    const atlas::Region* l_pR = l_spSnap && !l_oName.isEmpty() ? l_spSnap->f_Region(l_oName) : nullptr;
    for (QLabel* l_pL : {m_pFrame, m_pOrig, m_pOffset, m_pSource}) m_pSpriteForm->setRowVisible(l_pL, !l_oName.isEmpty());
    if (l_oName.isEmpty()) return;
    if (!l_pR) {
        m_pFrame->setText(tr("not in the atlas (see Problems)"));
        m_pOrig->clear();
        m_pOffset->clear();
        m_pSource->clear();
        return;
    }
    QString l_oFrame = tr("%1, %2  %3 × %4  page %5").arg(l_pR->m_oFrame.m_iX).arg(l_pR->m_oFrame.m_iY).arg(l_pR->m_oFrame.m_iW).arg(l_pR->m_oFrame.m_iH).arg(l_pR->m_iPage + 1);
    if (!l_pR->m_strAliasOf.empty()) l_oFrame += QStringLiteral("\n") + tr("same pixels as %1").arg(f_Qs(l_pR->m_strAliasOf));
    m_pFrame->setText(l_oFrame);
    m_pOrig->setText(QStringLiteral("%1 × %2").arg(l_pR->m_iOrigW).arg(l_pR->m_iOrigH));
    m_pOffset->setText(l_pR->m_bChild ? tr("%1, %2 in %3").arg(l_pR->m_oLocal.m_iX).arg(l_pR->m_oLocal.m_iY).arg(f_Qs(l_pR->m_strParent))
                               : QStringLiteral("%1, %2%3").arg(l_pR->m_iOffsetX).arg(l_pR->m_iOffsetY).arg(l_pR->f_Trimmed() ? tr("  (trimmed)") : QString()));
    QString l_oArt;
    if (l_pR->m_bChild) l_oArt = l_pR->m_bBaked ? tr("baked copy from %1").arg(f_Qs(l_pR->m_strParent)) : tr("inside %1").arg(f_Qs(l_pR->m_strParent));
    else if (m_pDoc->f_HasVariantArt(l_oName, m_pDoc->f_Variant())) l_oArt = tr("%1 art (embedded)").arg(m_pDoc->f_Variant());
    else l_oArt = m_pDoc->f_Variant().isEmpty() ? tr("embedded") : tr("base art (embedded)");
    m_pSource->setText(l_oArt);
    m_pSource->setToolTip(QDir::toNativeSeparators(m_pDoc->f_StoreFile(m_pDoc->f_Variant())));
}

// ---- Variant group ------------------------------------------------------------------------------

QWidget* PropertiesDock::f_BuildVariantGroup()
{
    m_pVariantGroup = new QGroupBox(tr("Variant"));
    auto* l_pForm = new QFormLayout(m_pVariantGroup);
    l_pForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_pVariantArt = f_InfoLabel(m_pVariantGroup);
    l_pForm->addRow(tr("Own art"), m_pVariantArt);
    m_pVariantStore = f_InfoLabel(m_pVariantGroup);
    l_pForm->addRow(tr("Packed"), m_pVariantStore);

    m_pVariantRef = new QLineEdit(m_pVariantGroup);
    m_pVariantRef->setPlaceholderText(tr("(none)"));
    auto* l_pBrowse = new QToolButton(m_pVariantGroup);
    l_pBrowse->setText(QStringLiteral("…"));
    l_pBrowse->setToolTip(tr("Choose the variant's reference folder"));
    l_pForm->addRow(tr("Reference"), f_FieldRow(m_pVariantGroup, {{QString(), m_pVariantRef}}, l_pBrowse));
    connect(m_pVariantRef, &QLineEdit::editingFinished, this, [this] {
        const atlas::Variant* l_pV = m_pDoc->f_CurrentVariant();
        const std::string l_strD = f_U8(QDir::fromNativeSeparators(m_pVariantRef->text().trimmed()));
        if (m_bUpdating || !l_pV || l_strD == l_pV->m_strReference) return;
        const std::string l_strId = l_pV->m_strId;
        m_pDoc->f_Edit(tr("Variant reference folder"), [&](Project& l_oP) {
            for (atlas::Variant& l_oX : l_oP.m_vVariants)
                if (l_oX.m_strId == l_strId) l_oX.m_strReference = l_strD;
        });
    });
    connect(l_pBrowse, &QToolButton::clicked, this, [this] {
        const atlas::Variant* l_pV = m_pDoc->f_CurrentVariant();
        if (!l_pV) return;
        const QString l_oStart = l_pV->m_strReference.empty() ? QFileInfo(m_pDoc->f_FilePath()).absolutePath() : m_pDoc->f_ResolvePath(f_Qs(l_pV->m_strReference));
        const QString l_oDir = QFileDialog::getExistingDirectory(this, tr("Reference Folder for %1").arg(f_Qs(l_pV->m_strId)), l_oStart);
        if (!l_oDir.isEmpty()) m_pDoc->f_SetVariantReference(f_Qs(l_pV->m_strId), l_oDir);
    });

    auto* l_pImport = new QPushButton(tr("Import From Reference…"), m_pVariantGroup);
    l_pForm->addRow(QString(), l_pImport);
    connect(l_pImport, &QPushButton::clicked, this, &PropertiesDock::f_ImportReferencesRequested);
    return m_pVariantGroup;
}

void PropertiesDock::f_RefreshVariant()
{
    const atlas::Variant* l_pV = m_pDoc->f_CurrentVariant();
    m_pVariantGroup->setVisible(l_pV != nullptr);
    if (!l_pV) return;
    m_bUpdating = true;
    m_pVariantGroup->setTitle(tr("Variant: %1").arg(f_Qs(l_pV->m_strId)));
    m_pVariantArt->setText(tr("%1 of %2 image(s)").arg(l_pV->m_mapImages.size()).arg(m_pDoc->f_Project().m_mapImages.size()));
    const QString l_oStore = m_pDoc->f_StoreFile(f_Qs(l_pV->m_strId));
    m_pVariantStore->setText(QDir::toNativeSeparators(m_pDoc->f_ToProjectPath(l_oStore)));
    m_pVariantStore->setToolTip(tr("The variant's packed atlas (holds its own art): %1").arg(QDir::toNativeSeparators(l_oStore)));
    m_pVariantRef->setText(QDir::toNativeSeparators(f_Qs(l_pV->m_strReference)));
    m_pVariantRef->setToolTip(l_pV->m_strReference.empty() ? tr("Folder the variant's replacement art is imported from (<sprite name>.png)")
                                                  : QDir::toNativeSeparators(m_pDoc->f_ResolvePath(f_Qs(l_pV->m_strReference))));
    m_bUpdating = false;
}

// ---- References group ---------------------------------------------------------------------------

QWidget* PropertiesDock::f_BuildReferencesGroup()
{
    auto* l_pGroup = new QGroupBox(tr("References"));
    auto* l_pL = new QVBoxLayout(l_pGroup);
    auto* l_pHint = new QLabel(tr("Folders to import new or changed art from. Saving never reads them: "
                               "the images live in the packed atlas."), l_pGroup);
    l_pHint->setWordWrap(true);
    l_pHint->setForegroundRole(QPalette::PlaceholderText);
    l_pL->addWidget(l_pHint);

    m_pRefList = new QListWidget(l_pGroup);
    m_pRefList->setMaximumHeight(96);
    m_pRefList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pRefList->setTextElideMode(Qt::ElideMiddle);
    m_pRefList->setSelectionMode(QAbstractItemView::SingleSelection);
    l_pL->addWidget(m_pRefList);

    auto* l_pRow = new QHBoxLayout();
    l_pRow->setContentsMargins(0, 0, 0, 0);
    auto* l_pAdd = new QToolButton(l_pGroup);
    l_pAdd->setIcon(Icons::f_Icon(Icons::Id::Add));
    l_pAdd->setToolTip(tr("Add a reference folder"));
    m_pRefRemove = new QToolButton(l_pGroup);
    m_pRefRemove->setIcon(Icons::f_Icon(Icons::Id::Remove));
    m_pRefRemove->setToolTip(tr("Remove the selected reference folder (the folder itself stays)"));
    auto* l_pImport = new QPushButton(tr("Import…"), l_pGroup);
    l_pImport->setToolTip(tr("Import new or changed art from the reference folders"));
    l_pRow->addWidget(l_pAdd);
    l_pRow->addWidget(m_pRefRemove);
    l_pRow->addStretch(1);
    l_pRow->addWidget(l_pImport);
    l_pL->addLayout(l_pRow);

    connect(l_pAdd, &QToolButton::clicked, this, [this] {
        const QString l_oStart = m_pDoc->f_FilePath().isEmpty() ? QString() : QFileInfo(m_pDoc->f_FilePath()).absolutePath();
        const QString l_oDir = QFileDialog::getExistingDirectory(this, tr("Add Reference Folder"), l_oStart);
        if (!l_oDir.isEmpty()) m_pDoc->f_AddReference(l_oDir);
    });
    connect(m_pRefRemove, &QToolButton::clicked, this, [this] { m_pDoc->f_RemoveReference(m_pRefList->currentRow()); });
    connect(m_pRefList, &QListWidget::currentRowChanged, this, [this](int l_iR) {
        m_pRefRemove->setEnabled(l_iR >= 0 && l_iR < int(m_pDoc->f_Project().m_vReferences.size()));
    });
    connect(l_pImport, &QPushButton::clicked, this, &PropertiesDock::f_ImportReferencesRequested);
    return l_pGroup;
}

void PropertiesDock::f_RefreshReferences()
{
    const int l_iKeep = m_pRefList->currentRow();
    const int l_iN = int(m_pDoc->f_Project().m_vReferences.size());
    const QSignalBlocker l_oBlock(m_pRefList);
    m_pRefList->clear();
    for (const atlas::SourceFolder& l_oF : m_pDoc->f_Project().m_vReferences) {
        const QString l_oAbs = m_pDoc->f_ResolvePath(f_Qs(l_oF.m_strPath));
        auto* l_pItem = new QListWidgetItem(Icons::f_Icon(Icons::Id::Folder), QDir::toNativeSeparators(f_Qs(l_oF.m_strPath)), m_pRefList);
        QString l_oTip = QDir::toNativeSeparators(l_oAbs);
        if (!l_oF.m_strPrefix.empty()) l_oTip += QStringLiteral("\n") + tr("names start with \"%1\"").arg(f_Qs(l_oF.m_strPrefix));
        if (!l_oF.m_bRecursive) l_oTip += QStringLiteral("\n") + tr("this folder only (no subfolders)");
        if (!QFileInfo(l_oAbs).isDir()) {
            l_oTip += QStringLiteral("\n") + tr("(missing)");
            l_pItem->setForeground(QColor(0xf0, 0x4a, 0x5a));
        }
        l_pItem->setToolTip(l_oTip);
    }
    if (l_iN == 0) {
        auto* l_pItem = new QListWidgetItem(tr("(none — add a folder to import art from)"), m_pRefList);
        l_pItem->setFlags(Qt::NoItemFlags);
    }
    m_pRefList->setCurrentRow(l_iN == 0 ? -1 : std::min(std::max(l_iKeep, 0), l_iN - 1));
    m_pRefRemove->setEnabled(l_iN > 0 && m_pRefList->currentRow() >= 0);
}
