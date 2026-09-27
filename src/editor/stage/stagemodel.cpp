// stagemodel.cpp — editor data model implementation + game-JSON import/export.
#include "stagemodel.h"
#include "catalog.h"
#include <QVector>
#include <QJsonDocument>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>

StageModel::StageModel(QObject* parent) : QObject(parent) {
    layers.append({ "地形 Terrain", true, false });
    layers.append({ "物件 Objects", true, false });
    terrain = QVector<QChar>(width * height, '.');
}

void StageModel::setSize(int w, int h) {
    width = w; height = h;
    terrain = QVector<QChar>(w * h, '.');
    // drop objects outside bounds
    QList<GameObject> kept;
    for (const auto& o : objects)
        if (o.x < w && o.y < h) kept.append(o);
    objects = kept;
    emit changed();
}

void StageModel::clear() {
    objects.clear();
    terrain = QVector<QChar>(width * height, '.');
    connections = Connection{};
    emit changed();
}

GameObject* StageModel::objectAt(int x, int y, int layer) {
    // search topmost visible layer first if layer==-1
    for (int li = layers.size() - 1; li >= 0; --li) {
        if (layer != -1 && li != layer) continue;
        if (!layers[li].visible) continue;
        for (int i = objects.size() - 1; i >= 0; --i)
            if (objects[i].x == x && objects[i].y == y && objects[i].layer == li)
                return &objects[i];
    }
    return nullptr;
}

void StageModel::addObject(const GameObject& o) {
    objects.append(o);
    emit changed();
}

void StageModel::removeObject(int x, int y, int layer) {
    for (int i = 0; i < objects.size(); ++i)
        if (objects[i].x == x && objects[i].y == y &&
            (layer == -1 || objects[i].layer == layer)) {
            objects.removeAt(i);
            emit changed();
            return;
        }
}

void StageModel::moveObject(int x, int y, int nx, int ny, int layer) {
    for (auto& o : objects)
        if (o.x == x && o.y == y && (layer == -1 || o.layer == layer)) {
            o.x = nx; o.y = ny;
            emit changed();
            return;
        }
}

QList<GameObject> StageModel::objectsOnLayer(int layer) const {
    QList<GameObject> out;
    for (const auto& o : objects) if (o.layer == layer) out.append(o);
    return out;
}

void StageModel::setTerrainChar(int x, int y, QChar c) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    terrain[y * width + x] = c;
    emit changed();
}

QChar StageModel::terrainChar(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return '#';
    return terrain[y * width + x];
}

// ---------- import / export ----------

QMap<QChar, QString> StageModel::buildLegend() const {
    // base legend shared with the game
    QMap<QChar, QString> leg;
    leg['#'] = "wall";   leg['.'] = "floor";  leg['@'] = "player_start";
    leg['U'] = "stairs_up"; leg['D'] = "stairs_down";
    leg['y'] = "door:yellow"; leg['b'] = "door:blue"; leg['r'] = "door:red";
    leg['Y'] = "key:yellow";  leg['B'] = "key:blue";  leg['R'] = "key:red";
    leg['s'] = "npc:sorcerer";  leg['v'] = "npc:villager"; leg['k'] = "npc:king";
    leg['p'] = "npc:princess";  leg['m'] = "npc:handmaiden";
    leg['1'] = "monster:slime";  leg['2'] = "monster:bat";  leg['3'] = "monster:golem";
    leg['4'] = "monster:skeleton"; leg['5'] = "monster:wraith"; leg['6'] = "monster:demon";
    leg['Z'] = "monster:demonlord_vorkath";
    leg['a'] = "item:gem_atk";  leg['d'] = "item:gem_def";
    leg['h'] = "item:potion_red"; leg['H'] = "item:potion_blue"; leg['c'] = "item:coin";
    return leg;
}

QMap<QString, QChar> StageModel::reverseLegend(const QMap<QChar, QString>& leg) const {
    QMap<QString, QChar> rev;
    for (auto it = leg.begin(); it != leg.end(); ++it) rev[it.value()] = it.key();
    return rev;
}

bool StageModel::exportJson(const QString& path) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    QJsonDocument doc;
    doc.setObject(QJsonDocument::fromJson(toJsonString().toUtf8()).object());
    f.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

QString StageModel::toJsonString() const {
    QJsonObject root;
    root["id"] = id;
    root["name"] = name;
    root["subtitle"] = subtitle;
    root["index"] = index;
    root["width"] = width;
    root["height"] = height;

    // legend (stable, game-compatible)
    QMap<QChar, QString> leg = buildLegend();
    QJsonObject legend;
    for (auto it = leg.begin(); it != leg.end(); ++it)
        legend[QString(it.key())] = it.value();
    root["legend"] = legend;

    // tiles: start from terrain grid, then stamp object chars on top
    // reverse legend: semantic -> char (choose the canonical char)
    QMap<QString, QChar> rev = reverseLegend(leg);
    QVector<QChar> grid = terrain;
    for (const auto& o : objects) {
        QString sem = semanticOf(o.category, o.typeId);
        if (rev.contains(sem)) grid[o.y * width + o.x] = rev[sem];
        else if (o.category == "terrain") grid[o.y * width + o.x] = (o.typeId == "wall" ? '#' : '.');
    }
    QJsonArray tiles;
    for (int y = 0; y < height; ++y) {
        QString row;
        for (int x = 0; x < width; ++x) row.append(grid[y * width + x]);
        tiles.append(row);
    }
    root["tiles"] = tiles;

    root["story_note"] = storyNote;
    QJsonObject conn;
    conn["up"]   = connections.toStage.isEmpty() ? QJsonValue() : connections.toStage;
    conn["down"] = QJsonValue(); // single-link model; extend as needed
    root["connect"] = conn;

    // also embed editor-only metadata so the node/layer state survives round-trips
    QJsonArray lay;
    for (const auto& l : layers) { QJsonObject o; o["name"]=l.name; o["visible"]=l.visible; o["locked"]=l.locked; lay.append(o); }
    root["editor_layers"] = lay;

    QJsonArray objs;
    for (const auto& o : objects) {
        QJsonObject jo; jo["x"]=o.x; jo["y"]=o.y; jo["category"]=o.category;
        jo["type"]=o.typeId; jo["layer"]=o.layer;
        QJsonObject pr; for (auto it=o.props.begin(); it!=o.props.end(); ++it) pr[it.key()]=it.value();
        jo["props"]=pr; objs.append(jo);
    }
    root["editor_objects"] = objs;

    return QString(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool StageModel::fromJsonString(const QString& text) {
    QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (doc.isNull() || !doc.isObject()) return false;
    QJsonObject root = doc.object();
    id = root["id"].toString(); name = root["name"].toString();
    subtitle = root["subtitle"].toString(); storyNote = root["story_note"].toString();
    index = root["index"].toInt(1); width = root["width"].toInt(13); height = root["height"].toInt(11);
    terrain = QVector<QChar>(width * height, '.');

    objects.clear();
    layers.clear();
    if (root.contains("editor_layers")) {
        for (const auto& v : root["editor_layers"].toArray()) {
            QJsonObject o = v.toObject();
            layers.append({ o["name"].toString(), o["visible"].toBool(true), o["locked"].toBool(false) });
        }
    }
    if (layers.isEmpty()) { layers.append({"地形 Terrain",true,false}); layers.append({"物件 Objects",true,false}); }

    // parse tiles -> terrain + entities (game-compatible path)
    QMap<QChar, QString> leg = buildLegend();
    QJsonArray tiles = root["tiles"].toArray();
    for (int y = 0; y < tiles.size() && y < height; ++y) {
        QString row = tiles[y].toString();
        for (int x = 0; x < row.size() && x < width; ++x) {
            QChar c = row[x];
            terrain[y * width + x] = c;
            if (c == '.' || c == '#' || c == '@') continue;
            QString sem = leg.value(c);
            if (sem.isEmpty()) continue;
            GameObject o; o.x = x; o.y = y;
            if (sem == "wall") { o.category="terrain"; o.typeId="wall"; o.layer=0; }
            else if (sem == "floor") { o.category="terrain"; o.typeId="floor"; o.layer=0; }
            else if (sem == "player_start") { o.category="player"; o.typeId="start"; o.layer=1; }
            else if (sem.startsWith("stairs_")) { o.category="stairs"; o.typeId=sem; o.layer=1; }
            else if (sem.startsWith("door:")) { o.category="door"; o.typeId=sem.mid(5); o.layer=1; }
            else if (sem.startsWith("key:"))  { o.category="key";  o.typeId=sem.mid(4); o.layer=1; }
            else if (sem.startsWith("npc:"))   { o.category="npc";  o.typeId=sem.mid(4); o.layer=1; }
            else if (sem.startsWith("monster:")) { o.category="monster"; o.typeId=sem.mid(8); o.layer=1; applyMonsterDefaults(o); }
            else if (sem.startsWith("item:"))  { o.category="item"; o.typeId=sem.mid(5); o.layer=1;
                                                 const auto* t=catalogByType("item",o.typeId); if(t) o.props=t->defaults; }
            objects.append(o);
        }
    }

    // if editor_objects present, prefer them (preserves layers/props exactly)
    if (root.contains("editor_objects")) {
        objects.clear();
        for (const auto& v : root["editor_objects"].toArray()) {
            QJsonObject o = v.toObject();
            GameObject go; go.x=o["x"].toInt(); go.y=o["y"].toInt();
            go.category=o["category"].toString(); go.typeId=o["type"].toString();
            go.layer=o["layer"].toInt(1);
            QJsonObject pr=o["props"].toObject(); for (auto it=pr.begin(); it!=pr.end(); ++it) go.props[it.key()]=it.value().toString();
            objects.append(go);
        }
    }

    // connections
    QJsonObject conn = root["connect"].toObject();
    connections.fromStage = id;
    connections.toStage = conn["up"].isNull() ? QString() : conn["up"].toString();
    connections.dir = "up";
    emit changed();
    return true;
}

void StageModel::applyMonsterDefaults(GameObject& o) const {
    const auto* t = catalogByType("monster", o.typeId);
    if (t) o.props = t->defaults;
}

bool StageModel::importJson(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray bytes = f.readAll();
    return fromJsonString(QString::fromUtf8(bytes));
}
