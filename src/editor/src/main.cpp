// main.cpp -- toms_editor: Qt6 editor with an embedded bgfx viewport. Desktop only, never shipped.
#include "editor_window.h"
#include "object.h"

#include <QApplication>
#include <QStringList>

int main(int argc, char** argv) {
    int rc = 0;
    {
        QApplication app(argc, argv);
        QApplication::setApplicationName(QStringLiteral("TOMS Editor"));
        QApplication::setOrganizationName(QStringLiteral("TOMS"));
        EditorWindow w;
        // Smoke test (same switches as toms_game): --frames=<n> --screenshot=<png>
        int frames = 0;
        QString png;
        for (const QString& a : QApplication::arguments()) {
            if (a.startsWith(QStringLiteral("--frames="))) frames = a.mid(9).toInt();
            else if (a.startsWith(QStringLiteral("--screenshot="))) png = a.mid(13);
        }
        if (frames > 0 && !png.isEmpty()) w.runSmokeTest(frames, png);
        w.show();
        rc = app.exec();
    }
    Object::DumpLeaks();
    return rc;
}
