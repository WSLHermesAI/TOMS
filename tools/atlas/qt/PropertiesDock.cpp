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

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string u8(const QString& s) { return s.toStdString(); }

const char* kMixed = "–";

// Spin boxes that can show "mixed": one step below the real minimum is the blank state.
QSpinBox* mixedSpin(int lo, int hi, QWidget* parent)
{
    auto* s = new QSpinBox(parent);
    s->setRange(lo - 1, hi);
    s->setSpecialValueText(QString::fromUtf8(kMixed));
    s->setKeyboardTracking(false);   // commit on Enter / focus out / arrow, not per keystroke
    s->setMinimumWidth(56);
    return s;
}

QDoubleSpinBox* mixedDoubleSpin(double lo, double hi, QWidget* parent)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setDecimals(3);
    s->setSingleStep(0.05);
    s->setRange(lo - 0.001, hi);
    s->setSpecialValueText(QString::fromUtf8(kMixed));
    s->setKeyboardTracking(false);
    s->setMinimumWidth(64);
    return s;
}

bool isMixed(const QSpinBox* s) { return s->value() == s->minimum(); }
bool isMixed(const QDoubleSpinBox* s) { return s->value() <= s->minimum(); }

void showValue(QSpinBox* s, bool same, int v) { s->setValue(same ? v : s->minimum()); }
void showValue(QDoubleSpinBox* s, bool same, double v) { s->setValue(same ? v : s->minimum()); }

void showValue(QCheckBox* b, bool same, bool v)
{
    b->setTristate(!same);
    if (same) b->setChecked(v);
    else b->setCheckState(Qt::PartiallyChecked);
}

void showValue(QLineEdit* e, bool same, const QString& v)
{
    e->setText(same ? v : QString());
    e->setPlaceholderText(same ? QString() : QObject::tr("(mixed)"));
}

// True when get() gives the same value for every def; `out` gets the first one.
template <class T, class F>
bool common(const std::vector<SpriteDef>& defs, F get, T& out)
{
    out = get(defs.front());
    for (const SpriteDef& d : defs)
        if (!(get(d) == out)) return false;
    return true;
}

// Shows the value get() has for every def, or "mixed" when they differ.
template <class W, class F>
void showCommon(W* w, const std::vector<SpriteDef>& defs, F get)
{
    decltype(get(defs.front())) v{};
    const bool same = common(defs, get, v);
    showValue(w, same, v);
}

QSpinBox* plainSpin(int lo, int hi, QWidget* parent)
{
    auto* s = new QSpinBox(parent);
    s->setRange(lo, hi);
    s->setKeyboardTracking(false);
    s->setMinimumWidth(64);
    return s;
}

// A row of labelled fields: "x [ ] y [ ] ...".
QWidget* fieldRow(QWidget* parent, const QList<QPair<QString, QWidget*>>& fields, QWidget* trailing = nullptr)
{
    auto* row = new QWidget(parent);
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    for (const auto& f : fields) {
        if (!f.first.isEmpty()) {
            auto* lab = new QLabel(f.first, row);
            lab->setForegroundRole(QPalette::PlaceholderText);
            l->addWidget(lab);
        }
        l->addWidget(f.second, 1);
    }
    if (trailing) l->addWidget(trailing);
    return row;
}

QLabel* infoLabel(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Long paths must not widen the dock: clip, the tooltip has the full text.
    l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return l;
}

const atlas::Heuristic kHeuristics[] = {atlas::Heuristic::Auto,        atlas::Heuristic::BestShortSideFit,
                                        atlas::Heuristic::BestLongSideFit, atlas::Heuristic::BestAreaFit,
                                        atlas::Heuristic::BottomLeft,  atlas::Heuristic::ContactPoint,
                                        atlas::Heuristic::Shelf};

}  // namespace

PropertiesDock::PropertiesDock(AtlasDocument* doc, QWidget* parent)
    : QDockWidget(tr("Properties"), parent)
    , m_doc(doc)
{
    setObjectName(QStringLiteral("PropertiesDock"));
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // Rows are laid out to fit the dock; a sideways scroll bar would only hide the right column.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);
    layout->addWidget(buildSpriteGroup());
    layout->addWidget(buildVariantGroup());
    layout->addWidget(buildAtlasGroup());
    layout->addWidget(buildReferencesGroup());
    layout->addStretch(1);
    scroll->setWidget(body);
    setWidget(scroll);

    connect(doc, &AtlasDocument::projectChanged, this, [this] {
        refreshAtlas();
        refreshSprite();
        refreshVariant();
        refreshReferences();
    });
    connect(doc, &AtlasDocument::variantChanged, this, [this] {
        refreshVariant();
        refreshSprite();
    });
    // Relative paths are shown as stored; their tooltips follow the project file.
    connect(doc, &AtlasDocument::filePathChanged, this, &PropertiesDock::refreshReferences);
    connect(doc, &AtlasDocument::selectionChanged, this, &PropertiesDock::refreshSprite);
    connect(doc, &AtlasDocument::buildFinished, this, &PropertiesDock::refreshInfo);
    refreshAtlas();
    refreshSprite();
    refreshVariant();
    refreshReferences();
}

// ---- Atlas group --------------------------------------------------------------------------------

void PropertiesDock::bindInt(QSpinBox* spin, const QString& key, const QString& text, std::function<void(Project&, int)> set)
{
    connect(spin, &QSpinBox::valueChanged, this, [this, key, text, set](int v) {
        if (m_updating) return;
        m_doc->edit(text, [&](Project& p) { set(p, v); }, QStringLiteral("atlas.") + key);
    });
}

void PropertiesDock::bindBool(QCheckBox* box, const QString& text, std::function<void(Project&, bool)> set)
{
    connect(box, &QCheckBox::toggled, this, [this, text, set](bool v) {
        if (m_updating) return;
        m_doc->edit(text, [&](Project& p) { set(p, v); });
    });
}

QWidget* PropertiesDock::buildAtlasGroup()
{
    auto* group = new QGroupBox(tr("Atlas"));
    auto* form = new QFormLayout(group);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_projectName = new QLineEdit(group);
    form->addRow(tr("Name"), m_projectName);
    connect(m_projectName, &QLineEdit::editingFinished, this, [this] {
        const std::string n = u8(m_projectName->text().trimmed());
        if (m_updating || n.empty() || n == m_doc->project().name) return;
        m_doc->edit(tr("Rename atlas"), [&](Project& p) { p.name = n; });
    });

    m_maxW = plainSpin(16, 16384, group);
    m_maxH = plainSpin(16, 16384, group);
    m_minW = plainSpin(1, 16384, group);
    m_minH = plainSpin(1, 16384, group);
    form->addRow(tr("Max size"), fieldRow(group, {{QString(), m_maxW}, {QStringLiteral("×"), m_maxH}}));
    form->addRow(tr("Min size"), fieldRow(group, {{QString(), m_minW}, {QStringLiteral("×"), m_minH}}));
    bindInt(m_maxW, QStringLiteral("maxW"), tr("Max width"), [](Project& p, int v) { p.settings.maxWidth = v; });
    bindInt(m_maxH, QStringLiteral("maxH"), tr("Max height"), [](Project& p, int v) { p.settings.maxHeight = v; });
    bindInt(m_minW, QStringLiteral("minW"), tr("Min width"), [](Project& p, int v) { p.settings.minWidth = v; });
    bindInt(m_minH, QStringLiteral("minH"), tr("Min height"), [](Project& p, int v) { p.settings.minHeight = v; });

    m_pot = new QCheckBox(tr("Power of two"), group);
    m_square = new QCheckBox(tr("Square"), group);
    m_fixedSize = new QCheckBox(tr("Fixed size"), group);
    m_fixedSize->setToolTip(tr("Always use the maximum size instead of shrinking the page to fit"));
    auto* sizeFlags = new QWidget(group);
    auto* sl = new QGridLayout(sizeFlags);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->addWidget(m_pot, 0, 0);
    sl->addWidget(m_square, 0, 1);
    sl->addWidget(m_fixedSize, 1, 0);
    form->addRow(QString(), sizeFlags);
    bindBool(m_pot, tr("Power of two"), [](Project& p, bool v) { p.settings.powerOfTwo = v; });
    bindBool(m_square, tr("Square pages"), [](Project& p, bool v) { p.settings.square = v; });
    bindBool(m_fixedSize, tr("Fixed size"), [](Project& p, bool v) { p.settings.fixedSize = v; });

    m_padding = plainSpin(0, 64, group);
    m_border = plainSpin(0, 64, group);
    m_extrude = plainSpin(0, 16, group);
    m_padding->setToolTip(tr("Empty pixels between sprites"));
    m_border->setToolTip(tr("Empty pixels along the page edges"));
    m_extrude->setToolTip(tr("Edge pixels repeated around each sprite (stops filtering seams)"));
    form->addRow(tr("Padding"), m_padding);
    form->addRow(tr("Border"), m_border);
    form->addRow(tr("Extrude"), m_extrude);
    bindInt(m_padding, QStringLiteral("padding"), tr("Padding"), [](Project& p, int v) { p.settings.padding = v; });
    bindInt(m_border, QStringLiteral("border"), tr("Border"), [](Project& p, int v) { p.settings.border = v; });
    bindInt(m_extrude, QStringLiteral("extrude"), tr("Extrude"), [](Project& p, int v) { p.settings.extrude = v; });

    m_trim = new QCheckBox(tr("Trim transparent edges"), group);
    m_alphaThreshold = plainSpin(0, 254, group);
    m_alphaThreshold->setToolTip(tr("Alpha at or below this counts as transparent when trimming"));
    form->addRow(QString(), m_trim);
    form->addRow(tr("Alpha ≤"), m_alphaThreshold);
    bindBool(m_trim, tr("Trim"), [](Project& p, bool v) { p.settings.trim = v; });
    bindInt(m_alphaThreshold, QStringLiteral("alpha"), tr("Alpha threshold"), [](Project& p, int v) { p.settings.alphaThreshold = v; });

    m_dedupe = new QCheckBox(tr("Merge identical images"), group);
    m_premultiply = new QCheckBox(tr("Premultiply alpha"), group);
    form->addRow(QString(), m_dedupe);
    form->addRow(QString(), m_premultiply);
    bindBool(m_dedupe, tr("Dedupe"), [](Project& p, bool v) { p.settings.dedupe = v; });
    bindBool(m_premultiply, tr("Premultiply alpha"), [](Project& p, bool v) { p.settings.premultiplyAlpha = v; });

    m_filter = new QComboBox(group);
    m_filter->addItems({QStringLiteral("Nearest"), QStringLiteral("Linear")});
    form->addRow(tr("Filter"), m_filter);
    connect(m_filter, &QComboBox::activated, this, [this] {
        const std::string f = u8(m_filter->currentText());
        m_doc->edit(tr("Filter"), [&](Project& p) { p.settings.filter = f; });
    });

    m_heuristic = new QComboBox(group);
    for (atlas::Heuristic h : kHeuristics) m_heuristic->addItem(QString::fromLatin1(atlas::heuristicName(h)));
    m_heuristic->setToolTip(tr("Placement rule; Auto tries them all and keeps the smallest result"));
    form->addRow(tr("Heuristic"), m_heuristic);
    connect(m_heuristic, &QComboBox::activated, this, [this](int i) {
        m_doc->edit(tr("Heuristic"), [&](Project& p) { p.settings.heuristic = kHeuristics[i]; });
    });

    m_defPivotX = mixedDoubleSpin(-10, 10, group);
    m_defPivotY = mixedDoubleSpin(-10, 10, group);
    form->addRow(tr("Default pivot"), fieldRow(group, {{QStringLiteral("x"), m_defPivotX}, {QStringLiteral("y"), m_defPivotY}}));
    for (int axis = 0; axis < 2; axis++) {
        QDoubleSpinBox* s = axis ? m_defPivotY : m_defPivotX;
        connect(s, &QDoubleSpinBox::valueChanged, this, [this, axis](double v) {
            if (m_updating) return;
            m_doc->edit(tr("Default pivot"), [&](Project& p) { p.settings.defaultPivot[axis] = float(v); },
                        QStringLiteral("atlas.pivot%1").arg(axis));
        });
    }

    // Output.
    auto* outLabel = new QLabel(tr("<b>Output</b>"), group);
    form->addRow(outLabel);
    m_outDir = new QLineEdit(group);
    auto* browse = new QToolButton(group);
    browse->setText(QStringLiteral("…"));
    browse->setToolTip(tr("Choose the output folder"));
    m_outDir->setToolTip(tr("Where Save writes the packed atlas (it holds the images) and the output formats; "
                            "\".\" = next to the project"));
    form->addRow(tr("Folder"), fieldRow(group, {{QString(), m_outDir}}, browse));
    connect(m_outDir, &QLineEdit::editingFinished, this, [this] {
        const std::string d = u8(QDir::fromNativeSeparators(m_outDir->text().trimmed()));
        if (m_updating || d == m_doc->project().output.dir) return;
        m_doc->edit(tr("Output folder"), [&](Project& p) { p.output.dir = d.empty() ? "." : d; });
    });
    connect(browse, &QToolButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Output Folder"),
                                                              m_doc->resolvePath(qs(m_doc->project().output.dir)));
        if (dir.isEmpty()) return;
        const std::string d = u8(m_doc->toProjectPath(dir));
        m_doc->edit(tr("Output folder"), [&](Project& p) { p.output.dir = d; });
    });

    m_outName = new QLineEdit(group);
    form->addRow(tr("File name"), m_outName);
    connect(m_outName, &QLineEdit::editingFinished, this, [this] {
        const std::string n = u8(m_outName->text().trimmed());
        if (m_updating || n == m_doc->project().output.name) return;
        m_doc->edit(tr("Output name"), [&](Project& p) { p.output.name = n; });
    });

    auto* formats = new QWidget(group);
    auto* grid = new QGridLayout(formats);
    grid->setContentsMargins(0, 0, 0, 0);
    const std::vector<std::string>& all = atlas::exportFormats();
    for (size_t i = 0; i < all.size(); i++) {
        auto* box = new QCheckBox(qs(all[i]), formats);
        if (all[i] == kAlwaysExported) {
            box->setEnabled(false);
            box->setToolTip(tr("Save always writes the .plist (Cocos Creator Sprite Atlas)"));
        } else if (all[i] == "atlas") {
            box->setEnabled(false);
            box->setToolTip(tr("Save always writes the .atlas: the packed atlas holds the project's images"));
        }
        grid->addWidget(box, int(i / 2), int(i % 2));
        m_formats.push_back(box);
        connect(box, &QCheckBox::toggled, this, [this] {
            if (m_updating) return;
            std::vector<std::string> f;
            for (QCheckBox* b : m_formats)
                if (b->isChecked()) f.push_back(u8(b->text()));
            m_doc->edit(tr("Export formats"), [&](Project& p) { p.output.formats = f; });
        });
    }
    form->addRow(tr("Formats"), formats);
    return group;
}

void PropertiesDock::refreshAtlas()
{
    m_updating = true;
    const Project& p = m_doc->project();
    const atlas::Settings& s = p.settings;
    m_projectName->setText(qs(p.name));
    m_maxW->setValue(s.maxWidth);
    m_maxH->setValue(s.maxHeight);
    m_minW->setValue(s.minWidth);
    m_minH->setValue(s.minHeight);
    m_pot->setChecked(s.powerOfTwo);
    m_square->setChecked(s.square);
    m_fixedSize->setChecked(s.fixedSize);
    m_padding->setValue(s.padding);
    m_border->setValue(s.border);
    m_extrude->setValue(s.extrude);
    m_trim->setChecked(s.trim);
    m_alphaThreshold->setValue(s.alphaThreshold);
    m_alphaThreshold->setEnabled(s.trim);
    m_dedupe->setChecked(s.dedupe);
    m_premultiply->setChecked(s.premultiplyAlpha);
    m_filter->setCurrentIndex(std::max(0, m_filter->findText(qs(s.filter), Qt::MatchFixedString)));
    for (int i = 0; i < int(std::size(kHeuristics)); i++)
        if (kHeuristics[i] == s.heuristic) m_heuristic->setCurrentIndex(i);
    m_defPivotX->setValue(s.defaultPivot[0]);
    m_defPivotY->setValue(s.defaultPivot[1]);
    m_outDir->setText(QDir::toNativeSeparators(qs(p.output.dir)));
    m_outName->setText(qs(p.output.name));
    m_outName->setPlaceholderText(qs(p.name));
    for (QCheckBox* b : m_formats)
        b->setChecked(u8(b->text()) == kAlwaysExported || b->text() == QLatin1String("atlas") ||
                      std::find(p.output.formats.begin(), p.output.formats.end(), u8(b->text())) != p.output.formats.end());
    m_updating = false;
}

// ---- Sprite group -------------------------------------------------------------------------------

std::vector<SpriteDef> PropertiesDock::selectedDefs() const
{
    std::vector<SpriteDef> defs;
    for (const QString& n : m_doc->selection())
        if (m_doc->spriteExists(n)) defs.push_back(m_doc->spriteDef(n));
    return defs;
}

void PropertiesDock::editSelection(const QString& text, const QString& mergeKey, const std::function<void(SpriteDef&)>& change,
                                   bool childrenOnly)
{
    if (m_updating) return;
    QStringList names;
    for (const SpriteDef& d : selectedDefs())
        if (!childrenOnly || d.isChild()) names << qs(d.name);
    m_doc->editSprites(names, text, change, mergeKey.isEmpty() ? QString() : QStringLiteral("sprite.") + mergeKey);
}

QWidget* PropertiesDock::buildSpriteGroup()
{
    m_spriteGroup = new QGroupBox(tr("Sprite"));
    auto* outer = new QVBoxLayout(m_spriteGroup);
    m_noSelection = new QLabel(tr("Select a sprite in the tree or on the canvas."), m_spriteGroup);
    m_noSelection->setForegroundRole(QPalette::PlaceholderText);
    m_noSelection->setWordWrap(true);
    outer->addWidget(m_noSelection);
    auto* formHost = new QWidget(m_spriteGroup);
    outer->addWidget(formHost);
    m_spriteForm = new QFormLayout(formHost);
    m_spriteForm->setContentsMargins(0, 0, 0, 0);
    m_spriteForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    QWidget* g = formHost;

    m_name = new QLineEdit(g);
    m_spriteForm->addRow(tr("Name"), m_name);
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        const QString from = m_doc->currentSprite();
        const QString to = m_name->text().trimmed();
        if (m_updating || m_doc->selection().size() != 1 || to == from) return;
        QString err;
        if (!m_doc->renameSprite(from, to, &err)) {
            QMessageBox::warning(this, tr("Rename"), err);
            m_name->setText(from);
        }
    });

    m_kind = new QLabel(g);
    m_spriteForm->addRow(tr("Kind"), m_kind);

    m_parent = new QComboBox(g);
    m_parent->setPlaceholderText(tr("(mixed)"));
    m_spriteForm->addRow(tr("Parent"), m_parent);
    connect(m_parent, &QComboBox::activated, this, [this] {
        const std::string par = u8(m_parent->currentText());
        editSelection(tr("Change parent"), QString(), [&](SpriteDef& d) { d.parent = par; }, true);
    });

    m_rectX = mixedSpin(0, 65535, g);
    m_rectY = mixedSpin(0, 65535, g);
    m_rectW = mixedSpin(1, 65535, g);
    m_rectH = mixedSpin(1, 65535, g);
    m_rectRow = new QWidget(g);
    {
        auto* grid = new QGridLayout(m_rectRow);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(4);
        const QList<QPair<QString, QSpinBox*>> cells = {{QStringLiteral("x"), m_rectX}, {QStringLiteral("y"), m_rectY},
                                                        {QStringLiteral("w"), m_rectW}, {QStringLiteral("h"), m_rectH}};
        for (int i = 0; i < 4; i++) {
            auto* lab = new QLabel(cells[i].first, m_rectRow);
            lab->setForegroundRole(QPalette::PlaceholderText);
            grid->addWidget(lab, i / 2, (i % 2) * 2);
            grid->addWidget(cells[i].second, i / 2, (i % 2) * 2 + 1);
        }
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);
    }
    m_spriteForm->addRow(tr("Rect"), m_rectRow);
    QSpinBox* rectSpins[4] = {m_rectX, m_rectY, m_rectW, m_rectH};
    for (int i = 0; i < 4; i++) {
        connect(rectSpins[i], &QSpinBox::valueChanged, this, [this, i](int v) {
            if (isMixed(i == 0 ? m_rectX : i == 1 ? m_rectY : i == 2 ? m_rectW : m_rectH)) return;
            editSelection(tr("Child rect"), QStringLiteral("rect%1").arg(i), [&](SpriteDef& d) {
                int* f[4] = {&d.rect.x, &d.rect.y, &d.rect.w, &d.rect.h};
                *f[i] = v;
            }, true);
        });
    }

    m_bake = new QCheckBox(tr("Bake (own copy of the pixels)"), g);
    m_bake->setToolTip(tr("A baked child is packed as its own image instead of pointing into its parent"));
    m_spriteForm->addRow(QString(), m_bake);
    connect(m_bake, &QCheckBox::clicked, this, [this] {
        m_bake->setTristate(false);
        const bool v = m_bake->isChecked();
        editSelection(v ? tr("Bake") : tr("Unbake"), QString(), [&](SpriteDef& d) { d.bake = v; }, true);
    });

    m_pivotX = mixedDoubleSpin(-10, 10, g);
    m_pivotY = mixedDoubleSpin(-10, 10, g);
    m_pivotReset = new QToolButton(g);
    m_pivotReset->setText(tr("Default"));
    m_pivotReset->setToolTip(tr("Use the atlas default pivot"));
    m_pivotRow = fieldRow(g, {{QStringLiteral("x"), m_pivotX}, {QStringLiteral("y"), m_pivotY}}, m_pivotReset);
    m_spriteForm->addRow(tr("Pivot"), m_pivotRow);
    for (int axis = 0; axis < 2; axis++) {
        QDoubleSpinBox* s = axis ? m_pivotY : m_pivotX;
        connect(s, &QDoubleSpinBox::valueChanged, this, [this, s, axis](double v) {
            if (isMixed(s)) return;
            const float* def = m_doc->project().settings.defaultPivot;
            editSelection(tr("Pivot"), QStringLiteral("pivot%1").arg(axis), [&](SpriteDef& d) {
                if (!d.hasPivot) {   // start from what it showed: the default pivot
                    d.pivot[0] = def[0];
                    d.pivot[1] = def[1];
                }
                d.hasPivot = true;
                d.pivot[axis] = float(v);
            });
        });
    }
    connect(m_pivotReset, &QToolButton::clicked, this, [this] {
        editSelection(tr("Default pivot"), QString(), [](SpriteDef& d) {
            d.hasPivot = false;
            d.pivot[0] = d.pivot[1] = 0.5f;
        });
    });

    m_splitOn = new QCheckBox(tr("9-slice"), g);
    const char* sides[4] = {"l", "r", "t", "b"};
    m_splitRow = new QWidget(g);
    {
        auto* grid = new QGridLayout(m_splitRow);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(4);
        grid->addWidget(m_splitOn, 0, 0, 1, 4);
        for (int i = 0; i < 4; i++) {
            m_split[i] = mixedSpin(0, 65535, m_splitRow);
            auto* lab = new QLabel(QString::fromLatin1(sides[i]), m_splitRow);
            lab->setForegroundRole(QPalette::PlaceholderText);
            grid->addWidget(lab, 1 + i / 2, (i % 2) * 2);
            grid->addWidget(m_split[i], 1 + i / 2, (i % 2) * 2 + 1);
        }
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);
    }
    m_split[0]->setToolTip(tr("Left border (pixels)"));
    m_split[1]->setToolTip(tr("Right border (pixels)"));
    m_split[2]->setToolTip(tr("Top border (pixels)"));
    m_split[3]->setToolTip(tr("Bottom border (pixels)"));
    m_spriteForm->addRow(tr("Borders"), m_splitRow);
    connect(m_splitOn, &QCheckBox::clicked, this, [this] {
        m_splitOn->setTristate(false);
        const bool v = m_splitOn->isChecked();
        editSelection(v ? tr("Enable 9-slice") : tr("Disable 9-slice"), QString(), [&](SpriteDef& d) { d.hasSplit = v; });
    });
    for (int i = 0; i < 4; i++) {
        connect(m_split[i], &QSpinBox::valueChanged, this, [this, i](int v) {
            if (isMixed(m_split[i])) return;
            editSelection(tr("9-slice border"), QStringLiteral("split%1").arg(i), [&](SpriteDef& d) {
                d.hasSplit = true;
                d.split[i] = v;
            });
        });
    }

    m_trimOverride = new QComboBox(g);
    m_trimOverride->addItems({tr("Atlas setting"), tr("Trim"), tr("Keep full size")});
    m_trimOverride->setPlaceholderText(tr("(mixed)"));
    m_spriteForm->addRow(tr("Trim"), m_trimOverride);
    connect(m_trimOverride, &QComboBox::activated, this, [this](int i) {
        editSelection(tr("Trim"), QString(), [&](SpriteDef& d) { d.trim = i == 0 ? -1 : i == 1 ? 1 : 0; });
    });

    m_pinned = new QCheckBox(tr("Pinned"), g);
    m_pinned->setToolTip(tr("Keep a fixed place in the atlas (drag a sprite on the canvas to pin it)"));
    m_pinPage = mixedSpin(0, 64, g);
    m_pinX = mixedSpin(0, 65535, g);
    m_pinY = mixedSpin(0, 65535, g);
    m_pinRow = new QWidget(g);
    {
        auto* grid = new QGridLayout(m_pinRow);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(4);
        grid->addWidget(m_pinned, 0, 0, 1, 2);
        auto* pl = new QLabel(tr("page"), m_pinRow);
        pl->setForegroundRole(QPalette::PlaceholderText);
        grid->addWidget(pl, 0, 2);
        grid->addWidget(m_pinPage, 0, 3);
        const QList<QPair<QString, QSpinBox*>> cells = {{QStringLiteral("x"), m_pinX}, {QStringLiteral("y"), m_pinY}};
        for (int i = 0; i < 2; i++) {
            auto* lab = new QLabel(cells[i].first, m_pinRow);
            lab->setForegroundRole(QPalette::PlaceholderText);
            grid->addWidget(lab, 1, i * 2);
            grid->addWidget(cells[i].second, 1, i * 2 + 1);
        }
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);
    }
    m_spriteForm->addRow(tr("Place"), m_pinRow);
    connect(m_pinned, &QCheckBox::clicked, this, [this] {
        m_pinned->setTristate(false);
        const bool v = m_pinned->isChecked();
        const BuildSnapshotPtr snap = m_doc->snapshot();
        editSelection(v ? tr("Pin") : tr("Unpin"), QString(), [&](SpriteDef& d) {
            // Pinning keeps the sprite where the last build put it.
            if (v && !d.pinned && snap)
                if (const atlas::Region* r = snap->region(qs(d.name)); r && r->aliasOf.empty()) {
                    d.pinPage = r->page;
                    d.pinX = r->frame.x;
                    d.pinY = r->frame.y;
                }
            d.pinned = v;
        });
    });
    QSpinBox* pinSpins[3] = {m_pinPage, m_pinX, m_pinY};
    for (int i = 0; i < 3; i++) {
        QSpinBox* s = pinSpins[i];
        connect(s, &QSpinBox::valueChanged, this, [this, s, i](int v) {
            if (isMixed(s)) return;
            editSelection(tr("Pin position"), QStringLiteral("pin%1").arg(i), [&](SpriteDef& d) {
                d.pinned = true;
                (i == 0 ? d.pinPage : i == 1 ? d.pinX : d.pinY) = v;
            });
        });
    }

    m_tags = new QLineEdit(g);
    m_tags->setPlaceholderText(tr("comma separated"));
    m_spriteForm->addRow(tr("Tags"), m_tags);
    connect(m_tags, &QLineEdit::editingFinished, this, [this] {
        // Leaving a "(mixed)" field untouched must not wipe everyone's tags.
        if (m_updating || (m_tags->text().isEmpty() && m_tags->placeholderText() == tr("(mixed)"))) return;
        std::vector<std::string> tags;
        for (const QString& t : m_tags->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
            if (!t.trimmed().isEmpty()) tags.push_back(u8(t.trimmed()));
        editSelection(tr("Tags"), QString(), [&](SpriteDef& d) { d.tags = tags; });
    });

    m_frame = infoLabel(g);
    m_orig = infoLabel(g);
    m_offset = infoLabel(g);
    m_source = infoLabel(g);
    m_spriteForm->addRow(tr("Frame"), m_frame);
    m_spriteForm->addRow(tr("Original"), m_orig);
    m_spriteForm->addRow(tr("Offset"), m_offset);
    m_spriteForm->addRow(tr("Art"), m_source);
    return m_spriteGroup;
}

void PropertiesDock::refreshSprite()
{
    const std::vector<SpriteDef> defs = selectedDefs();
    const bool any = !defs.empty();
    m_noSelection->setVisible(!any);
    m_spriteForm->parentWidget()->setVisible(any);
    m_spriteGroup->setTitle(defs.size() > 1 ? tr("Sprite (%1 selected)").arg(defs.size()) : tr("Sprite"));
    if (!any) return;

    m_updating = true;
    const bool single = defs.size() == 1;
    int children = 0;
    for (const SpriteDef& d : defs) children += d.isChild();
    const bool allChildren = children == int(defs.size()), noChildren = children == 0;
    const SpriteDef& first = defs.front();

    // Name / kind.
    m_name->setEnabled(single);
    showValue(m_name, single, single ? qs(first.name) : QString());
    if (!single) m_name->setPlaceholderText(tr("%1 sprites").arg(defs.size()));
    QString kind;
    if (!single) kind = allChildren ? tr("child sprites") : noChildren ? tr("image sprites") : tr("images and children");
    else if (first.isChild()) kind = first.bake ? tr("child (baked)") : tr("child");
    else kind = m_doc->hasVariantArt(qs(first.name), m_doc->variant()) ? tr("image + %1 art").arg(m_doc->variant()) : tr("image");
    m_kind->setText(kind);

    // Child fields.
    m_spriteForm->setRowVisible(m_parent, allChildren);
    m_spriteForm->setRowVisible(m_rectRow, allChildren);
    m_spriteForm->setRowVisible(m_bake, allChildren);
    if (allChildren) {
        // A sprite cannot become a child of itself or of its own descendants.
        QSet<QString> banned;
        for (const SpriteDef& d : defs) {
            banned.insert(qs(d.name));
            for (const QString& c : m_doc->descendants(qs(d.name))) banned.insert(c);
        }
        m_parent->clear();
        if (const BuildSnapshotPtr snap = m_doc->snapshot())
            for (const SpriteInfo& s : snap->sprites)
                if (!banned.contains(s.name)) m_parent->addItem(s.name);
        std::string par;
        const bool sameParent = common(defs, [](const SpriteDef& d) { return d.parent; }, par);
        if (sameParent && m_parent->findText(qs(par)) < 0) m_parent->addItem(qs(par));   // a missing parent stays visible
        m_parent->setCurrentIndex(sameParent ? m_parent->findText(qs(par)) : -1);
        showCommon(m_rectX, defs, [](const SpriteDef& d) { return d.rect.x; });
        showCommon(m_rectY, defs, [](const SpriteDef& d) { return d.rect.y; });
        showCommon(m_rectW, defs, [](const SpriteDef& d) { return d.rect.w; });
        showCommon(m_rectH, defs, [](const SpriteDef& d) { return d.rect.h; });
        showCommon(m_bake, defs, [](const SpriteDef& d) { return d.bake; });
    }

    // Pivot (the default when the sprite has none of its own).
    const float* defPivot = m_doc->project().settings.defaultPivot;
    showCommon(m_pivotX, defs, [&](const SpriteDef& d) { return d.hasPivot ? d.pivot[0] : defPivot[0]; });
    showCommon(m_pivotY, defs, [&](const SpriteDef& d) { return d.hasPivot ? d.pivot[1] : defPivot[1]; });
    bool hasPivot = false;
    m_pivotReset->setEnabled(!common(defs, [](const SpriteDef& d) { return d.hasPivot; }, hasPivot) || hasPivot);

    // 9-slice.
    bool b = false;
    const bool sameSplit = common(defs, [](const SpriteDef& d) { return d.hasSplit; }, b);
    showValue(m_splitOn, sameSplit, b);
    for (int i = 0; i < 4; i++) {
        showCommon(m_split[i], defs, [i](const SpriteDef& d) { return d.split[i]; });
        m_split[i]->setEnabled(!sameSplit || b);
    }

    // Trim (images only).
    m_spriteForm->setRowVisible(m_trimOverride, noChildren);
    int trim = -1;
    m_trimOverride->setCurrentIndex(common(defs, [](const SpriteDef& d) { return d.trim; }, trim) ? (trim < 0 ? 0 : trim == 1 ? 1 : 2) : -1);

    // Pin (sprites with their own place: images and baked children).
    bool pinnable = true;
    for (const SpriteDef& d : defs) pinnable = pinnable && (!d.isChild() || d.bake);
    m_spriteForm->setRowVisible(m_pinRow, pinnable);
    const bool samePinned = common(defs, [](const SpriteDef& d) { return d.pinned; }, b);
    showValue(m_pinned, samePinned, b);
    showCommon(m_pinPage, defs, [](const SpriteDef& d) { return d.pinPage; });
    showCommon(m_pinX, defs, [](const SpriteDef& d) { return d.pinX; });
    showCommon(m_pinY, defs, [](const SpriteDef& d) { return d.pinY; });
    for (QSpinBox* s : {m_pinPage, m_pinX, m_pinY}) s->setEnabled(!samePinned || b);

    // Tags.
    QStringList tagList;
    for (const std::string& t : first.tags) tagList << qs(t);
    std::vector<std::string> tags;
    const bool sameTags = common(defs, [](const SpriteDef& d) { return d.tags; }, tags);
    m_tags->setText(sameTags ? tagList.join(QStringLiteral(", ")) : QString());
    m_tags->setPlaceholderText(sameTags ? tr("comma separated") : tr("(mixed)"));
    m_updating = false;
    refreshInfo();
}

void PropertiesDock::refreshInfo()
{
    const QString name = m_doc->selection().size() == 1 ? m_doc->currentSprite() : QString();
    const BuildSnapshotPtr snap = m_doc->snapshot();
    const atlas::Region* r = snap && !name.isEmpty() ? snap->region(name) : nullptr;
    for (QLabel* l : {m_frame, m_orig, m_offset, m_source}) m_spriteForm->setRowVisible(l, !name.isEmpty());
    if (name.isEmpty()) return;
    if (!r) {
        m_frame->setText(tr("not in the atlas (see Problems)"));
        m_orig->clear();
        m_offset->clear();
        m_source->clear();
        return;
    }
    QString frame = tr("%1, %2  %3 × %4  page %5").arg(r->frame.x).arg(r->frame.y).arg(r->frame.w).arg(r->frame.h).arg(r->page + 1);
    if (!r->aliasOf.empty()) frame += QStringLiteral("\n") + tr("same pixels as %1").arg(qs(r->aliasOf));
    m_frame->setText(frame);
    m_orig->setText(QStringLiteral("%1 × %2").arg(r->origW).arg(r->origH));
    m_offset->setText(r->child ? tr("%1, %2 in %3").arg(r->local.x).arg(r->local.y).arg(qs(r->parent))
                               : QStringLiteral("%1, %2%3").arg(r->offsetX).arg(r->offsetY).arg(r->trimmed() ? tr("  (trimmed)") : QString()));
    QString art;
    if (r->child) art = r->baked ? tr("baked copy from %1").arg(qs(r->parent)) : tr("inside %1").arg(qs(r->parent));
    else if (m_doc->hasVariantArt(name, m_doc->variant())) art = tr("%1 art (embedded)").arg(m_doc->variant());
    else art = m_doc->variant().isEmpty() ? tr("embedded") : tr("base art (embedded)");
    m_source->setText(art);
    m_source->setToolTip(QDir::toNativeSeparators(m_doc->storeFile(m_doc->variant())));
}

// ---- Variant group ------------------------------------------------------------------------------

QWidget* PropertiesDock::buildVariantGroup()
{
    m_variantGroup = new QGroupBox(tr("Variant"));
    auto* form = new QFormLayout(m_variantGroup);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_variantArt = infoLabel(m_variantGroup);
    form->addRow(tr("Own art"), m_variantArt);
    m_variantStore = infoLabel(m_variantGroup);
    form->addRow(tr("Packed"), m_variantStore);

    m_variantRef = new QLineEdit(m_variantGroup);
    m_variantRef->setPlaceholderText(tr("(none)"));
    auto* browse = new QToolButton(m_variantGroup);
    browse->setText(QStringLiteral("…"));
    browse->setToolTip(tr("Choose the variant's reference folder"));
    form->addRow(tr("Reference"), fieldRow(m_variantGroup, {{QString(), m_variantRef}}, browse));
    connect(m_variantRef, &QLineEdit::editingFinished, this, [this] {
        const atlas::Variant* v = m_doc->currentVariant();
        const std::string d = u8(QDir::fromNativeSeparators(m_variantRef->text().trimmed()));
        if (m_updating || !v || d == v->reference) return;
        const std::string id = v->id;
        m_doc->edit(tr("Variant reference folder"), [&](Project& p) {
            for (atlas::Variant& x : p.variants)
                if (x.id == id) x.reference = d;
        });
    });
    connect(browse, &QToolButton::clicked, this, [this] {
        const atlas::Variant* v = m_doc->currentVariant();
        if (!v) return;
        const QString start = v->reference.empty() ? QFileInfo(m_doc->filePath()).absolutePath() : m_doc->resolvePath(qs(v->reference));
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Reference Folder for %1").arg(qs(v->id)), start);
        if (!dir.isEmpty()) m_doc->setVariantReference(qs(v->id), dir);
    });

    auto* import = new QPushButton(tr("Import From Reference…"), m_variantGroup);
    form->addRow(QString(), import);
    connect(import, &QPushButton::clicked, this, &PropertiesDock::importReferencesRequested);
    return m_variantGroup;
}

void PropertiesDock::refreshVariant()
{
    const atlas::Variant* v = m_doc->currentVariant();
    m_variantGroup->setVisible(v != nullptr);
    if (!v) return;
    m_updating = true;
    m_variantGroup->setTitle(tr("Variant: %1").arg(qs(v->id)));
    m_variantArt->setText(tr("%1 of %2 image(s)").arg(v->images.size()).arg(m_doc->project().images.size()));
    const QString store = m_doc->storeFile(qs(v->id));
    m_variantStore->setText(QDir::toNativeSeparators(m_doc->toProjectPath(store)));
    m_variantStore->setToolTip(tr("The variant's packed atlas (holds its own art): %1").arg(QDir::toNativeSeparators(store)));
    m_variantRef->setText(QDir::toNativeSeparators(qs(v->reference)));
    m_variantRef->setToolTip(v->reference.empty() ? tr("Folder the variant's replacement art is imported from (<sprite name>.png)")
                                                  : QDir::toNativeSeparators(m_doc->resolvePath(qs(v->reference))));
    m_updating = false;
}

// ---- References group ---------------------------------------------------------------------------

QWidget* PropertiesDock::buildReferencesGroup()
{
    auto* group = new QGroupBox(tr("References"));
    auto* l = new QVBoxLayout(group);
    auto* hint = new QLabel(tr("Folders to import new or changed art from. Saving never reads them: "
                               "the images live in the packed atlas."), group);
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    l->addWidget(hint);

    m_refList = new QListWidget(group);
    m_refList->setMaximumHeight(96);
    m_refList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_refList->setTextElideMode(Qt::ElideMiddle);
    m_refList->setSelectionMode(QAbstractItemView::SingleSelection);
    l->addWidget(m_refList);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    auto* add = new QToolButton(group);
    add->setIcon(Icons::icon(Icons::Id::Add));
    add->setToolTip(tr("Add a reference folder"));
    m_refRemove = new QToolButton(group);
    m_refRemove->setIcon(Icons::icon(Icons::Id::Remove));
    m_refRemove->setToolTip(tr("Remove the selected reference folder (the folder itself stays)"));
    auto* import = new QPushButton(tr("Import…"), group);
    import->setToolTip(tr("Import new or changed art from the reference folders"));
    row->addWidget(add);
    row->addWidget(m_refRemove);
    row->addStretch(1);
    row->addWidget(import);
    l->addLayout(row);

    connect(add, &QToolButton::clicked, this, [this] {
        const QString start = m_doc->filePath().isEmpty() ? QString() : QFileInfo(m_doc->filePath()).absolutePath();
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Reference Folder"), start);
        if (!dir.isEmpty()) m_doc->addReference(dir);
    });
    connect(m_refRemove, &QToolButton::clicked, this, [this] { m_doc->removeReference(m_refList->currentRow()); });
    connect(m_refList, &QListWidget::currentRowChanged, this, [this](int r) {
        m_refRemove->setEnabled(r >= 0 && r < int(m_doc->project().references.size()));
    });
    connect(import, &QPushButton::clicked, this, &PropertiesDock::importReferencesRequested);
    return group;
}

void PropertiesDock::refreshReferences()
{
    const int keep = m_refList->currentRow();
    const int n = int(m_doc->project().references.size());
    const QSignalBlocker block(m_refList);
    m_refList->clear();
    for (const atlas::SourceFolder& f : m_doc->project().references) {
        const QString abs = m_doc->resolvePath(qs(f.path));
        auto* item = new QListWidgetItem(Icons::icon(Icons::Id::Folder), QDir::toNativeSeparators(qs(f.path)), m_refList);
        QString tip = QDir::toNativeSeparators(abs);
        if (!f.prefix.empty()) tip += QStringLiteral("\n") + tr("names start with \"%1\"").arg(qs(f.prefix));
        if (!f.recursive) tip += QStringLiteral("\n") + tr("this folder only (no subfolders)");
        if (!QFileInfo(abs).isDir()) {
            tip += QStringLiteral("\n") + tr("(missing)");
            item->setForeground(QColor(0xf0, 0x4a, 0x5a));
        }
        item->setToolTip(tip);
    }
    if (n == 0) {
        auto* item = new QListWidgetItem(tr("(none — add a folder to import art from)"), m_refList);
        item->setFlags(Qt::NoItemFlags);
    }
    m_refList->setCurrentRow(n == 0 ? -1 : std::min(std::max(keep, 0), n - 1));
    m_refRemove->setEnabled(n > 0 && m_refList->currentRow() >= 0);
}
