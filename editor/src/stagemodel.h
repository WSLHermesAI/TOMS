// stagemodel.h — editor data model: layers + game objects, import/export to game JSON.
#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>

struct GameObject {
    enum Kind { Terrain, Player, NPC, Monster, Item, Door, Key, Stairs };
    int x = 0, y = 0;
    QString category;   // terrain | stairs | door | key | player | npc | monster | item
    QString typeId;     // slime, villager, yellow, start, floor, wall ...
    QMap<QString, QString> props; // monster stats / item effects / door color
    int layer = 0;      // layer index this object lives on
    bool operator==(const GameObject& o) const {
        return x==o.x && y==o.y && category==o.category && typeId==o.typeId && layer==o.layer;
    }
};

// scene graph connection: this stage <-> another stage via a stairs object
struct Connection {
    QString fromStage;      // this stage id
    QString toStage;        // target stage id
    int     fromX=0, fromY=0; // stairs cell on this stage
    QString dir;            // "up" | "down"
};

struct Layer {
    QString name;
    bool visible = true;
    bool locked = false;
};

class StageModel : public QObject {
    Q_OBJECT
public:
    explicit StageModel(QObject* parent = nullptr);

    // identity / meta
    QString id, name, subtitle, storyNote;
    int index = 1, width = 13, height = 11;

    // layers
    QList<Layer> layers;
    int activeLayer = 0;

    // objects grouped by layer
    QList<GameObject> objects;        // all objects across layers
    Connection connections;           // up/down stage links

    // ---- editing API ----
    void setSize(int w, int h);
    void clear();
    GameObject* objectAt(int x, int y, int layer = -1); // -1 = any visible layer (topmost)
    void addObject(const GameObject& o);
    void removeObject(int x, int y, int layer);
    void moveObject(int x, int y, int nx, int ny, int layer);
    QList<GameObject> objectsOnLayer(int layer) const;

    // ---- persistence (game-compatible) ----
    bool importJson(const QString& path);
    bool exportJson(const QString& path) const;
    QString toJsonString() const;
    bool fromJsonString(const QString& text);

    // terrain grid (per cell char) — used for wall/floor/stairs export + minimap
    void setTerrainChar(int x, int y, QChar c);
    QChar terrainChar(int x, int y) const;
    void applyMonsterDefaults(GameObject& o) const;

signals:
    void changed();

private:
    // terrain layer stored as a char grid (wall/floor/stairs). index = y*width + x
    QVector<QChar> terrain;
    QMap<QChar, QString> buildLegend() const;
    QMap<QString, QChar> reverseLegend(const QMap<QChar, QString>& leg) const;
};
