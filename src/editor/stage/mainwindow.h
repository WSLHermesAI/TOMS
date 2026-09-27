// mainwindow.h — editor shell: toolbar, palette, layer panel, canvas, inspector, connections.
#pragma once
#include <QMainWindow>
#include <QGraphicsView>
#include "stagemodel.h"

class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QLineEdit;
class QLabel;
class QComboBox;
class QSpinBox;
class QTextEdit;
class StageScene;
class NodeItem;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onImport();
    void onExport();
    void onNewStage();
    void onObjectTypeSelected(const QString& key);
    void onLayerToggled(QListWidgetItem* item);
    void onActiveLayerChanged(int row);
    void onApplyProps();
    void onAddConnection();
    void onCellClicked(int x, int y);
    void onCanvasSelection(NodeItem* node);
    void updateTitle();

private:
    StageModel* model = nullptr;
    StageScene* scene = nullptr;
    QGraphicsView* view = nullptr;

    QListWidget* palette = nullptr;
    QListWidget* layerList = nullptr;
    QLineEdit* leId = nullptr, * leName = nullptr, * leSub = nullptr, * leStory = nullptr;
    QSpinBox* sbW = nullptr, * sbH = nullptr, * sbIndex = nullptr;
    QLineEdit* leConnTo = nullptr;
    QTreeWidget* inspector = nullptr;
    QLabel* status = nullptr;
    QTextEdit* connView = nullptr;

    QString currentTypeKey;   // selected palette type "category:typeId"
    int cellSize = 40;

    void rebuildPalette();
    void rebuildLayers();
    void rebuildConnectionsView();
    void refreshInspector(GameObject* o);
};
