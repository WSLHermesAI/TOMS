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
    bool m_bFolder = true;
    QString m_oKey;     // NodeKeyRole
    QString m_oLabel;   // shown text
    QString m_oName;    // sprites: full name; folders: path
    Node* m_pParent = nullptr;
    int m_iRow = 0;
    std::vector<std::unique_ptr<Node>> m_vChildren;
    // Sprites only.
    bool m_bChild = false;
    bool m_bVariantArt = false;
    int m_iChildCount = 0;
};

SpriteTreeModel::SpriteTreeModel(AtlasDocument* l_pDoc, QObject* l_pParent)
    : QAbstractItemModel(l_pParent)
    , m_pDoc(l_pDoc)
    , m_upRoot(std::make_unique<Node>())
{
    connect(l_pDoc, &AtlasDocument::f_BuildFinished, this, &SpriteTreeModel::f_Refresh);
    connect(l_pDoc, &AtlasDocument::f_ProjectReset, this, &SpriteTreeModel::f_Refresh);
    // Pin flags live in the project: repaint as soon as they change, not one build later.
    connect(l_pDoc, &AtlasDocument::f_ProjectChanged, this, [this] {
        m_oIcons.clear();
        f_EmitDataChanged(m_upRoot.get());
    });
}

SpriteTreeModel::~SpriteTreeModel() = default;

void SpriteTreeModel::f_Refresh()
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    m_oIcons.clear();
    QString l_oSig;
    if (l_spSnap)
        for (const SpriteInfo& l_oS : l_spSnap->m_vSprites)
            l_oSig += l_oS.m_oName + QChar(1) + l_oS.m_oParent + (l_oS.m_bChild ? QChar(3) : QChar(2)) + (l_oS.m_bVariantArt ? QChar(5) : QChar(4));
    if (l_oSig == m_oSignature && m_upRoot) {
        f_EmitDataChanged(m_upRoot.get());
        return;
    }

    beginResetModel();
    m_oSignature = l_oSig;
    m_upRoot = std::make_unique<Node>();
    m_oBySprite.clear();
    if (l_spSnap) {
        QHash<QString, Node*> l_oFolders;
        l_oFolders.insert(QString(), m_upRoot.get());
        std::function<Node*(const QString&)> l_fnFolderFor = [&](const QString& l_oPath) -> Node* {
            if (Node* l_pF = l_oFolders.value(l_oPath)) return l_pF;
            Node* l_pParent = l_fnFolderFor(l_oPath.section(QLatin1Char('/'), 0, -2));
            auto l_upN = std::make_unique<Node>();
            l_upN->m_oKey = QStringLiteral("d:") + l_oPath;
            l_upN->m_oLabel = l_oPath.section(QLatin1Char('/'), -1);
            l_upN->m_oName = l_oPath;
            l_upN->m_pParent = l_pParent;
            Node* l_pRaw = l_upN.get();
            l_pParent->m_vChildren.push_back(std::move(l_upN));
            l_oFolders.insert(l_oPath, l_pRaw);
            return l_pRaw;
        };
        // All sprite nodes first, then hang each under its parent sprite or its folder.
        std::vector<std::unique_ptr<Node>> l_vNodes;
        for (const SpriteInfo& l_oS : l_spSnap->m_vSprites) {
            auto l_upN = std::make_unique<Node>();
            l_upN->m_bFolder = false;
            l_upN->m_oKey = QStringLiteral("s:") + l_oS.m_oName;
            l_upN->m_oLabel = l_oS.m_oName.section(QLatin1Char('/'), -1);
            l_upN->m_oName = l_oS.m_oName;
            l_upN->m_bChild = l_oS.m_bChild;
            l_upN->m_bVariantArt = l_oS.m_bVariantArt;
            m_oBySprite.insert(l_oS.m_oName, l_upN.get());
            l_vNodes.push_back(std::move(l_upN));
        }
        auto l_fnParentNode = [&](const SpriteInfo& l_oS) -> Node* {
            // A missing parent or a parent loop (both reported in Problems) falls back to the folder.
            QSet<QString> l_oSeen{l_oS.m_oName};
            for (QString l_oP = l_oS.m_oParent; !l_oP.isEmpty();) {
                if (l_oSeen.contains(l_oP) || !m_oBySprite.contains(l_oP)) return nullptr;
                l_oSeen.insert(l_oP);
                const SpriteInfo* l_pPs = l_spSnap->f_Sprite(l_oP);
                l_oP = l_pPs ? l_pPs->m_oParent : QString();
            }
            return l_oS.m_bChild ? m_oBySprite.value(l_oS.m_oParent) : nullptr;
        };
        for (size_t l_nI = 0; l_nI < l_vNodes.size(); l_nI++) {
            const SpriteInfo& l_oS = l_spSnap->m_vSprites[l_nI];
            Node* l_pParent = l_fnParentNode(l_oS);
            if (l_pParent) l_pParent->m_iChildCount++;
            else l_pParent = l_fnFolderFor(l_oS.m_oName.section(QLatin1Char('/'), 0, -2));
            l_vNodes[l_nI]->m_pParent = l_pParent;
            l_pParent->m_vChildren.push_back(std::move(l_vNodes[l_nI]));
        }
        // Folders first, then natural order ("run_2" before "run_10").
        QCollator l_oColl;
        l_oColl.setNumericMode(true);
        l_oColl.setCaseSensitivity(Qt::CaseInsensitive);
        std::function<void(Node*)> l_fnSortNode = [&](Node* l_pN) {
            std::stable_sort(l_pN->m_vChildren.begin(), l_pN->m_vChildren.end(), [&](const auto& l_oA, const auto& l_oB) {
                if (l_oA->m_bFolder != l_oB->m_bFolder) return l_oA->m_bFolder;
                return l_oColl.compare(l_oA->m_oLabel, l_oB->m_oLabel) < 0;
            });
            for (size_t l_nI = 0; l_nI < l_pN->m_vChildren.size(); l_nI++) {
                l_pN->m_vChildren[l_nI]->m_iRow = int(l_nI);
                l_fnSortNode(l_pN->m_vChildren[l_nI].get());
            }
        };
        l_fnSortNode(m_upRoot.get());
    }
    endResetModel();
}

void SpriteTreeModel::f_EmitDataChanged(const Node* l_pN)
{
    if (l_pN->m_vChildren.empty()) return;
    emit dataChanged(index(0, 0, f_IndexOf(l_pN)), index(int(l_pN->m_vChildren.size()) - 1, 0, f_IndexOf(l_pN)));
    for (const auto& l_upC : l_pN->m_vChildren) f_EmitDataChanged(l_upC.get());
}

QModelIndex SpriteTreeModel::f_IndexOf(const Node* l_pN) const
{
    if (!l_pN || l_pN == m_upRoot.get()) return QModelIndex();
    return createIndex(l_pN->m_iRow, 0, const_cast<Node*>(l_pN));
}

QModelIndex SpriteTreeModel::f_IndexForSprite(const QString& l_oName) const { return f_IndexOf(m_oBySprite.value(l_oName)); }

QModelIndex SpriteTreeModel::index(int l_iRow, int l_iColumn, const QModelIndex& l_oParent) const
{
    const Node* l_pP = l_oParent.isValid() ? static_cast<Node*>(l_oParent.internalPointer()) : m_upRoot.get();
    if (l_iColumn != 0 || l_iRow < 0 || l_iRow >= int(l_pP->m_vChildren.size())) return QModelIndex();
    return createIndex(l_iRow, 0, l_pP->m_vChildren[size_t(l_iRow)].get());
}

QModelIndex SpriteTreeModel::parent(const QModelIndex& l_oIndex) const
{
    if (!l_oIndex.isValid()) return QModelIndex();
    return f_IndexOf(static_cast<Node*>(l_oIndex.internalPointer())->m_pParent);
}

int SpriteTreeModel::rowCount(const QModelIndex& l_oParent) const
{
    const Node* l_pP = l_oParent.isValid() ? static_cast<Node*>(l_oParent.internalPointer()) : m_upRoot.get();
    return int(l_pP->m_vChildren.size());
}

int SpriteTreeModel::columnCount(const QModelIndex&) const { return 1; }

QVariant SpriteTreeModel::data(const QModelIndex& l_oIndex, int l_iRole) const
{
    if (!l_oIndex.isValid()) return QVariant();
    const Node* l_pN = static_cast<Node*>(l_oIndex.internalPointer());
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    switch (l_iRole) {
    case Qt::DisplayRole:
        return l_pN->m_oLabel;
    case Qt::EditRole:
        return l_pN->m_oName;
    case SpriteNameRole:
        return l_pN->m_bFolder ? QString() : l_pN->m_oName;
    case NodeKeyRole:
        return l_pN->m_oKey;
    case ChildCountRole:
        return l_pN->m_iChildCount;
    case FilterTextRole:
        return l_pN->m_oName;
    case Qt::DecorationRole: {
        if (l_pN->m_bFolder) return Icons::f_Icon(Icons::Id::Folder);
        auto l_oIt = m_oIcons.find(l_pN->m_oName);
        if (l_oIt != m_oIcons.end()) return *l_oIt;
        const atlas::SpriteDef l_oD = m_pDoc->f_SpriteDef(l_pN->m_oName);
        const QImage l_oThumb = l_spSnap ? l_spSnap->m_oThumbnails.value(l_pN->m_oName) : QImage();
        QIcon l_oIcon = l_oThumb.isNull() && !l_pN->m_bChild ? Icons::f_Icon(Icons::Id::Image)
                                                 : Icons::f_SpriteIcon(l_oThumb, l_pN->m_bChild && !l_oD.m_bBake, l_oD.m_bPinned, false);
        m_oIcons.insert(l_pN->m_oName, l_oIcon);
        return l_oIcon;
    }
    case Qt::ToolTipRole: {
        if (l_pN->m_bFolder) return l_pN->m_oName;
        QString l_oT = QStringLiteral("<b>%1</b>").arg(l_pN->m_oName.toHtmlEscaped());
        if (l_pN->m_bChild) {
            const atlas::SpriteDef l_oD = m_pDoc->f_SpriteDef(l_pN->m_oName);
            l_oT += QStringLiteral("<br>") + tr("child of %1  (%2, %3  %4 × %5)%6")
                                              .arg(QString::fromStdString(l_oD.m_strParent).toHtmlEscaped())
                                              .arg(l_oD.m_oRect.m_iX).arg(l_oD.m_oRect.m_iY).arg(l_oD.m_oRect.m_iW).arg(l_oD.m_oRect.m_iH)
                                              .arg(l_oD.m_bBake ? tr(", baked") : QString());
        } else {
            const QString l_oVariant = l_spSnap ? l_spSnap->m_oVariant : QString();
            l_oT += QStringLiteral("<br>") + (l_pN->m_bVariantArt ? tr("image, with %1 art").arg(l_oVariant.toHtmlEscaped())
                                           : l_oVariant.isEmpty() ? tr("image") : tr("image (base art: %1 has none of its own)").arg(l_oVariant.toHtmlEscaped()));
            if (const atlas::Region* l_pR = l_spSnap ? l_spSnap->f_Region(l_pN->m_oName) : nullptr)
                l_oT += QStringLiteral("  %1 × %2").arg(l_pR->m_iOrigW).arg(l_pR->m_iOrigH);
        }
        if (l_pN->m_iChildCount) l_oT += QStringLiteral("<br>") + tr("%n child sprite(s)", nullptr, l_pN->m_iChildCount);
        if (l_spSnap)
            for (const atlas::Diagnostic& l_oD : l_spSnap->m_oResult.m_vDiagnostics)
                if (QString::fromStdString(l_oD.m_strSprite) == l_pN->m_oName)
                    l_oT += QStringLiteral("<br><i>%1</i>").arg(QString::fromStdString(l_oD.m_strMessage).toHtmlEscaped());
        return l_oT;
    }
    case Qt::ForegroundRole: {
        if (l_pN->m_bFolder) return QVariant();
        if (l_spSnap)
            for (const atlas::Diagnostic& l_oD : l_spSnap->m_oResult.m_vDiagnostics)
                if (l_oD.m_eLevel == atlas::Diagnostic::Error && QString::fromStdString(l_oD.m_strSprite) == l_pN->m_oName) return QColor(0xf0, 0x4a, 0x5a);
        return QVariant();
    }
    case Qt::FontRole: {
        // The shown variant's own art in bold, so its coverage is visible at a glance.
        if (l_pN->m_bFolder || !l_pN->m_bVariantArt) return QVariant();
        QFont l_oF;
        l_oF.setBold(true);
        return l_oF;
    }
    default:
        return QVariant();
    }
}

bool SpriteTreeModel::setData(const QModelIndex& l_oIndex, const QVariant& l_oValue, int l_iRole)
{
    if (!l_oIndex.isValid() || l_iRole != Qt::EditRole) return false;
    const Node* l_pN = static_cast<Node*>(l_oIndex.internalPointer());
    const QString l_oTo = l_oValue.toString().trimmed();
    if (l_pN->m_bFolder || l_oTo == l_pN->m_oName) return false;
    // The document renames; the tree follows with the next build.
    emit f_RenameRequested(l_pN->m_oName, l_oTo);
    return false;
}

Qt::ItemFlags SpriteTreeModel::flags(const QModelIndex& l_oIndex) const
{
    if (!l_oIndex.isValid()) return Qt::NoItemFlags;
    const Node* l_pN = static_cast<Node*>(l_oIndex.internalPointer());
    Qt::ItemFlags l_oF = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!l_pN->m_bFolder) l_oF |= Qt::ItemIsEditable;
    return l_oF;
}
