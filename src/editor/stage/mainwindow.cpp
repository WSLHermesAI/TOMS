// mainwindow.cpp — editor shell implementation.
#include "mainwindow.h"
#include "stagescene.h"
#include "catalog.h"
#include <QToolBar>
#include <QListWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QInputDialog>
#include <QGraphicsView>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    model = new StageModel(this);
    model->id = "stage_01"; model->name = "村莊外緣"; model->subtitle = "Village Outskirts";
    model->storyNote = "長老：『沃卡司封印了安穩之星，公主被囚塔頂。去吧，攀上巫師之塔。』";

    setWindowTitle("魔法塔 關卡編輯器 — Stage Editor");
    resize(1280, 800);

    // ---- toolbar ----
    QToolBar* tb = addToolBar("main");
    tb->addAction("新增 New", this, &MainWindow::onNewStage);
    tb->addAction("匯入 Import", this, &MainWindow::onImport);
    tb->addAction("匯出 Export", this, &MainWindow::onExport);

    // ---- central canvas ----
    scene = new StageScene(model, cellSize, this);
    view = new QGraphicsView(scene);
    view->setRenderHint(QPainter::Antialiasing);
    view->setBackgroundBrush(QBrush(QColor(30,30,36)));

    // ---- left: palette + layers ----
    QWidget* left = new QWidget;
    QVBoxLayout* leftL = new QVBoxLayout(left);
    leftL->addWidget(new QLabel("物件面板 Palette"));
    palette = new QListWidget; rebuildPalette();
    connect(palette, &QListWidget::currentTextChanged, this, &MainWindow::onObjectTypeSelected);
    leftL->addWidget(palette);
    leftL->addWidget(new QLabel("圖層 Layers"));
    layerList = new QListWidget; rebuildLayers();
    connect(layerList, &QListWidget::itemChanged, this, &MainWindow::onLayerToggled);
    connect(layerList, &QListWidget::currentRowChanged, this, &MainWindow::onActiveLayerChanged);
    leftL->addWidget(layerList);

    // ---- right: stage meta + inspector + connections ----
    QWidget* right = new QWidget;
    QVBoxLayout* rightL = new QVBoxLayout(right);
    rightL->addWidget(new QLabel("關卡屬性 Stage"));
    leId = new QLineEdit(model->id);
    leName = new QLineEdit(model->name);
    leSub = new QLineEdit(model->subtitle);
    sbIndex = new QSpinBox(); sbIndex->setValue(model->index);
    sbW = new QSpinBox(); sbW->setRange(5, 40); sbW->setValue(model->width);
    sbH = new QSpinBox(); sbH->setRange(5, 40); sbH->setValue(model->height);
    QGridLayout* meta = new QGridLayout;
    meta->addWidget(new QLabel("ID"),0,0); meta->addWidget(leId,0,1);
    meta->addWidget(new QLabel("名稱"),1,0); meta->addWidget(leName,1,1);
    meta->addWidget(new QLabel("副標"),2,0); meta->addWidget(leSub,2,1);
    meta->addWidget(new QLabel("序號"),3,0); meta->addWidget(sbIndex,3,1);
    meta->addWidget(new QLabel("寬"),4,0); meta->addWidget(sbW,4,1);
    meta->addWidget(new QLabel("高"),5,0); meta->addWidget(sbH,5,1);
    rightL->addLayout(meta);
    leStory = new QLineEdit(model->storyNote);
    rightL->addWidget(new QLabel("劇情旁白 Story note"));
    rightL->addWidget(leStory);

    rightL->addWidget(new QLabel("物件檢視 Inspector"));
    inspector = new QTreeWidget; inspector->setHeaderLabels({"屬性","值"});
    connect(inspector, &QTreeWidget::itemChanged, this, &MainWindow::onApplyProps);
    rightL->addWidget(inspector);

    rightL->addWidget(new QLabel("關卡連接 Connections (node)"));
    connView = new QTextEdit; connView->setReadOnly(true);
    rightL->addWidget(connView);
    QPushButton* addConn = new QPushButton("新增連接 Add connection (up)");
    connect(addConn, &QPushButton::clicked, this, &MainWindow::onAddConnection);
    rightL->addWidget(addConn);
    rebuildConnectionsView();

    // ---- layout ----
    QWidget* central = new QWidget; QHBoxLayout* hl = new QHBoxLayout(central);
    hl->addWidget(left, 1); hl->addWidget(view, 4); hl->addWidget(right, 2);
    setCentralWidget(central);

    status = new QLabel;
    setStatusBar(statusBar()); status->setText("就緒 Ready");

    // connect scene
    connect(scene, &StageScene::selectionChanged, this, &MainWindow::onCanvasSelection);
    connect(model, &StageModel::changed, this, [this](){
        scene->setActiveLayer(model->activeLayer);
        scene->rebuild();
    });
}

void MainWindow::rebuildPalette() {
    palette->clear();
    QString cur;
    for (const auto& t : catalogAll())
        palette->addItem(QString("%1  %2").arg(t.ch).arg(t.label));
}

void MainWindow::onObjectTypeSelected(const QString& key) {
    // parse "X  label" -> find type
    if (key.isEmpty()) return;
    QChar ch = key.trimmed().at(0);
    const ObjectType* t = catalogByChar(ch);
    if (!t) return;
    currentTypeKey = t->category + ":" + t->typeId;
    // set brush on active object layer
    scene->setActiveLayer(std::max(1, model->activeLayer));
    scene->setBrush(t->category, t->typeId);
    status->setText(QString("筆刷 Brush: %1 (%2) — 點擊網格放置").arg(t->label).arg(t->category));
}

void MainWindow::rebuildLayers() {
    layerList->blockSignals(true);
    layerList->clear();
    for (const auto& l : model->layers) {
        auto* it = new QListWidgetItem(l.name);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(l.visible ? Qt::Checked : Qt::Unchecked);
        layerList->addItem(it);
    }
    layerList->setCurrentRow(model->activeLayer);
    layerList->blockSignals(false);
}

void MainWindow::onLayerToggled(QListWidgetItem* item) {
    int i = layerList->row(item);
    if (i < 0 || i >= model->layers.size()) return;
    model->layers[i].visible = (item->checkState() == Qt::Checked);
    scene->rebuild();
}

void MainWindow::onActiveLayerChanged(int row) {
    if (row < 0) return;
    model->activeLayer = row;
    scene->setActiveLayer(row);
    status->setText(QString("作用圖層 Active layer: %1").arg(model->layers[row].name));
}

void MainWindow::onCanvasSelection(NodeItem* node) {
    Q_UNUSED(node);
    refreshInspector(scene->selectedObject());
}

void MainWindow::refreshInspector(GameObject* o) {
    inspector->blockSignals(true);
    inspector->clear();
    if (!o) { inspector->blockSignals(false); return; }
    auto* top = new QTreeWidgetItem(inspector, {"type", o->category + ":" + o->typeId});
    top->setFlags(top->flags() | Qt::ItemIsEditable);
    new QTreeWidgetItem(inspector, {"x", QString::number(o->x)});
    new QTreeWidgetItem(inspector, {"y", QString::number(o->y)});
    new QTreeWidgetItem(inspector, {"layer", QString::number(o->layer)});
    for (auto it = o->props.begin(); it != o->props.end(); ++it) {
        auto* pi = new QTreeWidgetItem(inspector, {it.key(), it.value()});
        pi->setFlags(pi->flags() | Qt::ItemIsEditable);
    }
    inspector->blockSignals(false);
}

void MainWindow::onApplyProps() {
    // find selected object and write edited values back
    GameObject* o = scene->selectedObject();
    if (!o) return;
    for (int i = 0; i < inspector->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = inspector->topLevelItem(i);
        QString k = it->text(0), v = it->text(1);
        if (k == "x") o->x = v.toInt();
        else if (k == "y") o->y = v.toInt();
        else if (k == "layer") o->layer = v.toInt();
        else if (k != "type") o->props[k] = v;
    }
    model->changed(); // triggers rebuild
    status->setText("屬性已更新 Props updated");
}

void MainWindow::onAddConnection() {
    bool ok; QString to = QInputDialog::getText(this, "連接 Connection",
        "目標關卡 ID (target stage id):", QLineEdit::Normal, model->connections.toStage, &ok);
    if (!ok) return;
    model->connections.fromStage = model->id;
    model->connections.toStage = to;
    model->connections.dir = "up";
    rebuildConnectionsView();
}

void MainWindow::rebuildConnectionsView() {
    QString txt = QString("本關 %1\n").arg(model->id);
    txt += QString("  up  -> %1\n").arg(model->connections.toStage.isEmpty() ? "(none)" : model->connections.toStage);
    connView->setPlainText(txt);
}

void MainWindow::onCellClicked(int x, int y) {
    Q_UNUSED(x); Q_UNUSED(y);
    scene->clearBrush(); // clicking empty -> select mode
}

void MainWindow::onNewStage() {
    model->clear();
    model->id = "stage_new"; model->name = "新關卡"; model->subtitle = "";
    model->index++; model->width = 13; model->height = 11;
    leId->setText(model->id); leName->setText(model->name); leSub->setText(model->subtitle);
    sbIndex->setValue(model->index); sbW->setValue(model->width); sbH->setValue(model->height);
    scene->rebuild(); rebuildLayers(); rebuildConnectionsView();
    updateTitle();
    status->setText("新增關卡 New stage");
}

void MainWindow::onImport() {
    QString path = QFileDialog::getOpenFileName(this, "匯入關卡 Import stage",
        QString(), "JSON (*.json)");
    if (path.isEmpty()) return;
    if (model->importJson(path)) {
        leId->setText(model->id); leName->setText(model->name); leSub->setText(model->subtitle);
        leStory->setText(model->storyNote);
        sbIndex->setValue(model->index); sbW->setValue(model->width); sbH->setValue(model->height);
        scene->rebuild(); rebuildLayers(); rebuildConnectionsView(); updateTitle();
        status->setText("已匯入 Imported: " + path);
    } else {
        QMessageBox::warning(this, "錯誤 Error", "無法讀取 JSON / Failed to parse JSON");
    }
}

void MainWindow::onExport() {
    // sync UI fields into model
    model->id = leId->text(); model->name = leName->text(); model->subtitle = leSub->text();
    model->storyNote = leStory->text(); model->index = sbIndex->value();
    model->setSize(sbW->value(), sbH->value());
    QString path = QFileDialog::getSaveFileName(this, "匯出關卡 Export stage",
        model->id + ".json", "JSON (*.json)");
    if (path.isEmpty()) return;
    if (model->exportJson(path)) {
        status->setText("已匯出 Exported: " + path);
        QMessageBox::information(this, "完成 Done", "關卡資料已匯出 (含 editor 層/物件欄位)\n遊戲引擎讀取 tiles/legend/connect 即可。");
    } else {
        QMessageBox::warning(this, "錯誤 Error", "寫入失敗 / Failed to write");
    }
}

void MainWindow::updateTitle() {
    setWindowTitle("魔法塔 關卡編輯器 — " + model->id);
}
