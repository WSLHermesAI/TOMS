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

    explicit SpriteTreeModel(AtlasDocument* doc, QObject* parent = nullptr);
    ~SpriteTreeModel() override;

    QModelIndex indexForSprite(const QString& name) const;

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

signals:
    void renameRequested(const QString& from, const QString& to);

private:
    struct Node;

    void refresh();
    QModelIndex indexOf(const Node* n) const;
    void emitDataChanged(const Node* n);

    AtlasDocument* m_doc;
    std::unique_ptr<Node> m_root;
    QHash<QString, Node*> m_bySprite;
    QString m_signature;                    // structure of the current tree
    mutable QHash<QString, QIcon> m_icons;  // per build
};
