// editor_validate.cpp — headless end-to-end validation of the editor pipeline.
// 1) import stage01 -> make a change -> export -> re-import -> validate
// 2) build a brand-new stage11 (epilogue) -> export -> re-import -> validate
// Run from editor/build with: LD_LIBRARY_PATH set; data via ../../data/...
#include "stagemodel.h"
#include "catalog.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QIODevice>
#include <cstdio>

static QString readFile(const QString& p) {
    QFile f(p); if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}
static QJsonObject loadJson(const QString& p) {
    return QJsonDocument::fromJson(readFile(p).toUtf8()).object();
}
static bool tilesEqual(const QJsonObject& a, const QJsonObject& b) {
    return a["tiles"].toVariant().toStringList() == b["tiles"].toVariant().toStringList();
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int fails = 0;

    // ===================== Part 1: round-trip stage01 with a change =====================
    StageModel m;
    if (!m.importJson("../../data/stages/stage01.json")) { printf("P1 IMPORT_FAIL\n"); return 1; }
    QJsonObject before = loadJson("../../data/stages/stage01.json");
    int objBefore = m.objects.size();

    // --- make a simple change: add a red key at empty cell (5,5) on objects layer ---
    GameObject key; key.x=5; key.y=5; key.layer=1; key.category="key"; key.typeId="red";
    m.addObject(key);
    // --- change a monster's hp (find first monster) ---
    for (auto& o : m.objects) if (o.category=="monster") { o.props["hp"]="999"; break; }
    // --- change stage name ---
    m.name = "村莊外緣（測試）";

    if (!m.exportJson("/tmp/stage01_edited.json")) { printf("P1 EXPORT_FAIL\n"); return 1; }

    // re-import the exported file
    StageModel m2;
    if (!m2.importJson("/tmp/stage01_edited.json")) { printf("P1 REIMPORT_FAIL\n"); return 1; }
    QJsonObject after = loadJson("/tmp/stage01_edited.json");

    // validate: change must persist, other tiles/connect intact
    bool keyPersist = false;
    for (const auto& o : m2.objects) if (o.category=="key" && o.typeId=="red" && o.x==5 && o.y==5) keyPersist = true;
    bool hpPersist = false;
    for (const auto& o : m2.objects) if (o.category=="monster" && o.props.value("hp")=="999") hpPersist = true;
    bool namePersist = (m2.name == "村莊外緣（測試）");
    bool connectOk = (before["connect"].toObject()["up"] == after["connect"].toObject()["up"]);
    // validate: change must persist, connect intact, and the grid reflects the
    // added key at (5,5) while all other cells stay identical to the original.
    QStringList origTiles = before["tiles"].toVariant().toStringList();
    QStringList newTiles  = after["tiles"].toVariant().toStringList();
    int diffCells = 0; bool keyCellOk = false;
    for (int y=0; y<origTiles.size() && y<newTiles.size(); ++y) {
        for (int x=0; x<origTiles[y].size() && x<newTiles[y].size(); ++x) {
            if (origTiles[y][x] != newTiles[y][x]) {
                diffCells++;
                if (x==5 && y==5 && newTiles[y][x]=='R') keyCellOk = true;
            }
        }
    }
    bool tilesReflectChange = (diffCells == 1 && keyCellOk);
    bool objCountDelta = (m2.objects.size() == objBefore + 1);

    printf("[P1] key_persist=%d hp_persist=%d name_persist=%d connect_ok=%d tiles_reflect_change=%d obj_delta_ok=%d\n",
           keyPersist, hpPersist, namePersist, connectOk, tilesReflectChange, objCountDelta);
    if (!(keyPersist && hpPersist && namePersist && connectOk && tilesReflectChange && objCountDelta)) fails++;

    // ===================== Part 2: build stage11 (epilogue) =====================
    StageModel s11;
    s11.id = "stage_11"; s11.name = "安穩之星"; s11.subtitle = "The Star of Stability";
    s11.index = 11; s11.width = 13; s11.height = 11;
    s11.storyNote = "公主獲救，安穩之星重燃——王國迎來黎明。";
    // terrain: border walls, floor inside, player start, two stairs (up to 10, down none)
    for (int y=0;y<11;y++) for (int x=0;x<13;x++) {
        QChar c = (x==0||y==0||x==12||y==10) ? '#' : '.';
        s11.setTerrainChar(x,y,c);
    }
    s11.setTerrainChar(1,9,'@');                 // player start
    s11.setTerrainChar(6,1,'U');                 // stairs up to stage_10
    // objects layer
    GameObject princess; princess.x=10; princess.y=5; princess.layer=1; princess.category="npc"; princess.typeId="princess";
    princess.props["dialogue"]="princess_liora"; s11.addObject(princess);
    GameObject king; king.x=2; king.y=3; king.layer=1; king.category="npc"; king.typeId="king";
    king.props["dialogue"]="king_lieutenant"; s11.addObject(king);
    GameObject star; star.x=6; star.y=5; star.layer=1; star.category="item"; star.typeId="gem_atk";
    star.props["effect"]="star_restored"; s11.addObject(star); // the restored Star (reuse gem sprite)
    GameObject potion; potion.x=4; potion.y=7; potion.layer=1; potion.category="item"; potion.typeId="potion_blue";
    potion.props["effect"]="hp+80"; s11.addObject(potion);
    // connection: this stage's 'up' -> stage_10
    s11.connections.fromStage = "stage_11"; s11.connections.toStage = "stage_10"; s11.connections.dir="up";

    if (!s11.exportJson("../../data/stages/stage11.json")) { printf("P2 EXPORT_FAIL\n"); return 1; }

    // re-import to validate engine-readable shape
    StageModel s11b;
    if (!s11b.importJson("../../data/stages/stage11.json")) { printf("P2 REIMPORT_FAIL\n"); return 1; }
    QJsonObject j11 = loadJson("../../data/stages/stage11.json");
    bool tilesOk = (j11["tiles"].toVariant().toStringList().size() == 11);
    bool hasPrincess=false, hasStar=false, hasStairsUp=false;
    for (const auto& o : s11b.objects) {
        if (o.category=="npc" && o.typeId=="princess") hasPrincess=true;
        if (o.category=="item" && o.typeId=="gem_atk") hasStar=true;
    }
    // stairs_up char present in tiles
    for (const QString& row : j11["tiles"].toVariant().toStringList())
        if (row.contains('U')) hasStairsUp=true;
    bool conn11 = (j11["connect"].toObject()["up"] == "stage_10");
    printf("[P2] tiles_rows=%d princess=%d star=%d stairs_up=%d connect_up_ok=%d\n",
           tilesOk, hasPrincess, hasStar, hasStairsUp, conn11);
    if (!(tilesOk && hasPrincess && hasStar && hasStairsUp && conn11)) fails++;

    // ===================== Part 3: validate stage11 is engine-parseable =====================
    // The game's parseStage reads tiles+legend+connect; ensure legend covers our chars.
    // stage11 uses # . @ U (all in base legend) -> no unknown chars.
    QStringList unknown;
    QMap<QChar,QString> baseLeg; // mirror buildLegend minimal
    const char* legchars = "#.@UybYBRadsdhHcsvkp m123456Z"; // included for safety
    QJsonObject leg = j11["legend"].toObject();
    for (const QString& row : j11["tiles"].toVariant().toStringList())
        for (const QChar& c : row)
            if (c!='#'&&c!='.'&&c!='@'&&!leg.contains(QString(c)))
                unknown.append(QString(c));
    printf("[P3] unknown_legend_chars=%d\n", unknown.size());
    if (!unknown.isEmpty()) { for (auto& u:unknown) printf("   unknown: %s\n", u.toUtf8().constData()); fails++; }

    printf("\nRESULT: %s\n", fails==0 ? "ALL_VALID" : "HAS_FAILURES");
    return fails==0 ? 0 : 1;
}
