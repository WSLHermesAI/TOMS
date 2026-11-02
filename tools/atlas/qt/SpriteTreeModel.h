#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QIcon>

#include <memory>
#include <vector>

class AtlasDocument;

// The sprites of the last build as a tree: folders from the '/' in the names, child sprites
// nested under their parent. Rebuilt from the document's snapshot; the structure is only reset
// when sprites were added, removed or re-parented, otherwise the rows just repaint (thumbnails,
// pin badges), so expansion and scrolling survive every rebuild.
class SpriteTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Role {
        SpriteNameRole = Qt::UserRole + 1,   // full sprite name; empty for folders
        NodeKeyRole,                         // stable id ("d:ui/buttons" or "s:ui/buttons/ok")
        ChildCountRole,                      // number of child sprites (sprite rows)
        FilterTextRole,                      // what the filter box matches
    };

    explicit SpriteTreeModel(AtlasDocument* l_pDoc, QObject* l_pParent = nullptr);
    ~SpriteTreeModel() override;

    QModelIndex f_IndexForSprite(const QString& l_oName) const;

    QModelIndex index(int l_iRow, int l_iColumn, const QModelIndex& l_oParent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& l_oIndex) const override;
    int rowCount(const QModelIndex& l_oParent = QModelIndex()) const override;
    int columnCount(const QModelIndex& l_oParent = QModelIndex()) const override;
    QVariant data(const QModelIndex& l_oIndex, int l_iRole = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& l_oIndex, const QVariant& l_oValue, int l_iRole = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& l_oIndex) const override;

signals:
    void f_RenameRequested(const QString& l_oFrom, const QString& l_oTo);

private:
    struct Node;

    void f_Refresh();
    QModelIndex f_IndexOf(const Node* l_pN) const;
    void f_EmitDataChanged(const Node* l_pN);

    AtlasDocument* m_pDoc;
    std::unique_ptr<Node> m_upRoot;
    QHash<QString, Node*> m_oBySprite;
    QString m_oSignature;                    // structure of the current tree
    mutable QHash<QString, QIcon> m_oIcons;  // per build
};
