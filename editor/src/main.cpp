// main.cpp — Qt6 stage editor entry point.
#include <QApplication>
#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("魔法塔 關卡編輯器");
    MainWindow w;
    w.show();
    return app.exec();
}
