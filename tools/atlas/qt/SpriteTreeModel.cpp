#include "SpriteTreeModel.h"

#include "AtlasDocument.h"
#include "Icons.h"

#include <QApplication>
#include <QCollator>
#include <QDir>
#include <QFont>
#include <QPalette>
#include <QSet>

#include <algorithm>
#include <functional>

struct SpriteTreeModel::Node {
    bool folder = true;
    QString key;     // NodeKeyRole
    QString label;   // shown text
    QString name;    // sprites: full name; folders: path
    Node* parent = nullptr;
    int row = 0;
    std::vector<std::unique_ptr<Node>> children;
    // Sprites only.
    bool child = false;
    bool variantArt = false;
    int childCount = 0;
};

SpriteTreeModel::SpriteTreeModel(AtlasDocument* doc, QObject* parent)
    : QAbstractItemModel(parent)
    , m_doc(doc)
    , m_root(std::make_unique<Node>())
{
    connect(doc, &AtlasDocument::buildFinished, this, &SpriteTreeModel::refresh);
    connect(doc, &AtlasDocument::projectReset, this, &SpriteTreeModel::refresh);
    // Pin flags live in the project: repaint as soon as they change, not one build later.
    connect(doc, &AtlasDocument::projectChanged, this, [this] {
        m_icons.clear();
        emitDataChanged(m_root.get());
    });
}

SpriteTreeModel::~SpriteTreeModel() = default;

void SpriteTreeModel::refresh()
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    m_icons.clear();
    QString sig;
    if (snap)
        for (const SpriteInfo& s : snap->sprites)
            sig += s.name + QChar(1) + s.parent + (s.child ? QChar(3) : QChar(2)) + (s.variantArt ? QChar(5) : QChar(4));
    if (sig == m_signature && m_root) {
        emitDataChanged(m_root.get());
        return;
    }

    beginResetModel();
    m_signature = sig;
    m_root = std::make_unique<Node>();
    m_bySprite.clear();
    if (snap) {
        QHash<QString, Node*> folders;
        folders.insert(QString(), m_root.get());
        std::function<Node*(const QString&)> folderFor = [&](const QString& path) -> Node* {
            if (Node* f = folders.value(path)) return f;
            Node* parent = folderFor(path.section(QLatin1Char('/'), 0, -2));
            auto n = std::make_unique<Node>();
            n->key = QStringLiteral("d:") + path;
            n->label = path.section(QLatin1Char('/'), -1);
            n->name = path;
            n->parent = parent;
            Node* raw = n.get();
            parent->children.push_back(std::move(n));
            folders.insert(path, raw);
            return raw;
        };
        // All sprite nodes first, then hang each under its parent sprite or its folder.
        std::vector<std::unique_ptr<Node>> nodes;
        for (const SpriteInfo& s : snap->sprites) {
            auto n = std::make_unique<Node>();
            n->folder = false;
            n->key = QStringLiteral("s:") + s.name;
            n->label = s.name.section(QLatin1Char('/'), -1);
            n->name = s.name;
            n->child = s.child;
            n->variantArt = s.variantArt;
            m_bySprite.insert(s.name, n.get());
            nodes.push_back(std::move(n));
        }
        auto parentNode = [&](const SpriteInfo& s) -> Node* {
            // A missing parent or a parent loop (both reported in Problems) falls back to the folder.
            QSet<QString> seen{s.name};
            for (QString p = s.parent; !p.isEmpty();) {
                if (seen.contains(p) || !m_bySprite.contains(p)) return nullptr;
                seen.insert(p);
                const SpriteInfo* ps = snap->sprite(p);
                p = ps ? ps->parent : QString();
            }
            return s.child ? m_bySprite.value(s.parent) : nullptr;
        };
        for (size_t i = 0; i < nodes.size(); i++) {
            const SpriteInfo& s = snap->sprites[i];
            Node* parent = parentNode(s);
            if (parent) parent->childCount++;
            else parent = folderFor(s.name.section(QLatin1Char('/'), 0, -2));
            nodes[i]->parent = parent;
            parent->children.push_back(std::move(nodes[i]));
        }
        // Folders first, then natural order ("run_2" before "run_10").
        QCollator coll;
        coll.setNumericMode(true);
        coll.setCaseSensitivity(Qt::CaseInsensitive);
        std::function<void(Node*)> sortNode = [&](Node* n) {
            std::stable_sort(n->children.begin(), n->children.end(), [&](const auto& a, const auto& b) {
                if (a->folder != b->folder) return a->folder;
                return coll.compare(a->label, b->label) < 0;
            });
            for (size_t i = 0; i < n->children.size(); i++) {
                n->children[i]->row = int(i);
                sortNode(n->children[i].get());
            }
        };
        sortNode(m_root.get());
    }
    endResetModel();
}

void SpriteTreeModel::emitDataChanged(const Node* n)
{
    if (n->children.empty()) return;
    emit dataChanged(index(0, 0, indexOf(n)), index(int(n->children.size()) - 1, 0, indexOf(n)));
    for (const auto& c : n->children) emitDataChanged(c.get());
}

QModelIndex SpriteTreeModel::indexOf(const Node* n) const
{
    if (!n || n == m_root.get()) return QModelIndex();
    return createIndex(n->row, 0, const_cast<Node*>(n));
}

QModelIndex SpriteTreeModel::indexForSprite(const QString& name) const { return indexOf(m_bySprite.value(name)); }

QModelIndex SpriteTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    const Node* p = parent.isValid() ? static_cast<Node*>(parent.internalPointer()) : m_root.get();
    if (column != 0 || row < 0 || row >= int(p->children.size())) return QModelIndex();
    return createIndex(row, 0, p->children[size_t(row)].get());
}

QModelIndex SpriteTreeModel::parent(const QModelIndex& index) const
{
    if (!index.isValid()) return QModelIndex();
    return indexOf(static_cast<Node*>(index.internalPointer())->parent);
}

int SpriteTreeModel::rowCount(const QModelIndex& parent) const
{
    const Node* p = parent.isValid() ? static_cast<Node*>(parent.internalPointer()) : m_root.get();
    return int(p->children.size());
}

int SpriteTreeModel::columnCount(const QModelIndex&) const { return 1; }

QVariant SpriteTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return QVariant();
    const Node* n = static_cast<Node*>(index.internalPointer());
    const BuildSnapshotPtr snap = m_doc->snapshot();
    switch (role) {
    case Qt::DisplayRole:
        return n->label;
    case Qt::EditRole:
        return n->name;
    case SpriteNameRole:
        return n->folder ? QString() : n->name;
    case NodeKeyRole:
        return n->key;
    case ChildCountRole:
        return n->childCount;
    case FilterTextRole:
        return n->name;
    case Qt::DecorationRole: {
        if (n->folder) return Icons::icon(Icons::Id::Folder);
        auto it = m_icons.find(n->name);
        if (it != m_icons.end()) return *it;
        const atlas::SpriteDef d = m_doc->spriteDef(n->name);
        const QImage thumb = snap ? snap->thumbnails.value(n->name) : QImage();
        QIcon icon = thumb.isNull() && !n->child ? Icons::icon(Icons::Id::Image)
                                                 : Icons::spriteIcon(thumb, n->child && !d.bake, d.pinned, false);
        m_icons.insert(n->name, icon);
        return icon;
    }
    case Qt::ToolTipRole: {
        if (n->folder) return n->name;
        QString t = QStringLiteral("<b>%1</b>").arg(n->name.toHtmlEscaped());
        if (n->child) {
            const atlas::SpriteDef d = m_doc->spriteDef(n->name);
            t += QStringLiteral("<br>") + tr("child of %1  (%2, %3  %4 × %5)%6")
                                              .arg(QString::fromStdString(d.parent).toHtmlEscaped())
                                              .arg(d.rect.x).arg(d.rect.y).arg(d.rect.w).arg(d.rect.h)
                                              .arg(d.bake ? tr(", baked") : QString());
        } else {
            const QString variant = snap ? snap->variant : QString();
            t += QStringLiteral("<br>") + (n->variantArt ? tr("image, with %1 art").arg(variant.toHtmlEscaped())
                                           : variant.isEmpty() ? tr("image") : tr("image (base art: %1 has none of its own)").arg(variant.toHtmlEscaped()));
            if (const atlas::Region* r = snap ? snap->region(n->name) : nullptr)
                t += QStringLiteral("  %1 × %2").arg(r->origW).arg(r->origH);
        }
        if (n->childCount) t += QStringLiteral("<br>") + tr("%n child sprite(s)", nullptr, n->childCount);
        if (snap)
            for (const atlas::Diagnostic& d : snap->result.diagnostics)
                if (QString::fromStdString(d.sprite) == n->name)
                    t += QStringLiteral("<br><i>%1</i>").arg(QString::fromStdString(d.message).toHtmlEscaped());
        return t;
    }
    case Qt::ForegroundRole: {
        if (n->folder) return QVariant();
        if (snap)
            for (const atlas::Diagnostic& d : snap->result.diagnostics)
                if (d.level == atlas::Diagnostic::Error && QString::fromStdString(d.sprite) == n->name) return QColor(0xf0, 0x4a, 0x5a);
        return QVariant();
    }
    case Qt::FontRole: {
        // The shown variant's own art in bold, so its coverage is visible at a glance.
        if (n->folder || !n->variantArt) return QVariant();
        QFont f;
        f.setBold(true);
        return f;
    }
    default:
        return QVariant();
    }
}

bool SpriteTreeModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) return false;
    const Node* n = static_cast<Node*>(index.internalPointer());
    const QString to = value.toString().trimmed();
    if (n->folder || to == n->name) return false;
    // The document renames; the tree follows with the next build.
    emit renameRequested(n->name, to);
    return false;
}

Qt::ItemFlags SpriteTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    const Node* n = static_cast<Node*>(index.internalPointer());
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!n->folder) f |= Qt::ItemIsEditable;
    return f;
}
