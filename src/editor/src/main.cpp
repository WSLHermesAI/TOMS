// main.cpp -- toms_editor: Qt6 editor with an embedded bgfx viewport. Desktop only, never shipped.
#include "editor_window.h"
#include "Language.h"
#include "leak_check.h"
#include "singletons.h"   // Windows Debug builds: what is still allocated at exit (docs/engine/15_LEAK_CHECK.md)
#include "object.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    // Windows Debug builds: track every heap block from here; toms_editor_leaks.log at exit. The same options as
    // toms_game: --leak-trace=<n,m,...> adds those allocations' call stacks, --leak-break=<n> stops in the debugger.
    long leakBreak = 0;
    const char* leakTrace = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--leak-break=", 13) == 0) leakBreak = std::atol(argv[i] + 13);
        if (std::strncmp(argv[i], "--leak-trace=", 13) == 0) leakTrace = argv[i] + 13;
    }
    toms::leakCheckBegin(leakBreak, leakTrace);
    int rc = 0;
    {
        QApplication app(argc, argv);
        QApplication::setApplicationName(QStringLiteral("TOMS Editor"));
        QApplication::setOrganizationName(QStringLiteral("TOMS"));
        Language::init();   // English / 中文 (tools/studio_common/Language.h), before any window
        EditorWindow w;
        // Smoke test (same switches as toms_game): --frames=<n> --screenshot=<png>
        int frames = 0;
        QString png, scenario;
        const QStringList args = QApplication::arguments();
        for (int i = 1; i < args.size(); ++i) {
            const QString& a = args[i];
            if (a.startsWith(QStringLiteral("--frames="))) frames = a.mid(9).toInt();
            else if (a.startsWith(QStringLiteral("--screenshot="))) png = a.mid(13);
            else if (a.startsWith(QStringLiteral("--scenario="))) scenario = a.mid(11);
            // a scenario folder or its scenario.json dropped on toms_editor.exe (or "Open with"): play it
            else if (!a.startsWith(QStringLiteral("--")) &&
                     (QFileInfo(a).fileName() == QStringLiteral("scenario.json") ||
                      QFileInfo::exists(QDir(a).filePath(QStringLiteral("scenario.json")))))
                scenario = a;
        }
        if (frames > 0 && !png.isEmpty()) w.runSmokeTest(frames, png);
        w.show();
        if (!scenario.isEmpty()) w.playScenario(scenario);   // test scenarios (docs/gameplay/08_TEST_SCENARIOS.md)
        rc = app.exec();
        Language::shutdown();   // the translators and tables, while QApplication is still there
    }
    toms::releaseSingletons();   // the job system and other singletons, now that the editor is gone
    Object::DumpLeaks();
    toms::leakCheckEnd("toms_editor_leaks.log");   // Windows Debug: heap blocks still allocated since main() began
    return rc;
}
