// language_test -- the editors' English / 中文 switch (Language.h), offscreen.
// Run with TOMS_LANGUAGE=en (CTest sets it): the choice starts English and is never saved.
#include "Language.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <cstdio>

namespace {

int g_failed = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "ok" : "FAILED", what);
    if (!ok) g_failed++;
}

QString tr(const char* s) { return QCoreApplication::translate("LanguageTest", s); }

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    Language::init();
    check(Language::current() == Language::Lang::English, "starts in English (TOMS_LANGUAGE=en)");
    check(Language::dictionarySize() > 1000, "the Chinese dictionaries are built in (> 1000 entries)");
    check(Language::chinese(QStringLiteral("&File")) == QStringLiteral("檔案(&F)") &&
              Language::english(QStringLiteral("檔案(&F)")) == QStringLiteral("&File"),
          "a word and back: &File <-> 檔案(&F)");

    // A window built in English, the way the editors build theirs
    QMainWindow w;
    QMenu* file = w.menuBar()->addMenu(tr("&File"));
    QAction* save = file->addAction(tr("&Save"));
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    auto* label = new QLabel(tr("Name"));
    auto* combo = new QComboBox;
    combo->addItems({tr("Checker"), tr("Black"), QStringLiteral("my_sprite")});
    auto* spin = new QSpinBox;
    spin->setRange(-1, 10);
    spin->setSpecialValueText(tr("auto"));
    spin->setValue(-1);
    auto* table = new QTableWidget(0, 2);
    table->setHorizontalHeaderLabels({tr("Picture"), tr("Character")});
    label->setToolTip(tr("Delete"));
    for (QWidget* x : {(QWidget*)label, (QWidget*)combo, (QWidget*)spin, (QWidget*)table}) layout->addWidget(x);
    w.setCentralWidget(central);
    w.setWindowTitle(tr("TOMS Font Tool"));
    w.show();
    QApplication::processEvents();
    auto* toggle = qobject_cast<QToolButton*>(w.menuBar()->cornerWidget(Qt::TopRightCorner));
    check(toggle && toggle->text() == QStringLiteral("中文"), "every main window gets the toggle, showing 中文");
    check(toggle && toggle->isVisible() && toggle->width() > 0 && toggle->geometry().right() <= w.menuBar()->width(),
          "the toggle is visible, inside the menu bar");

    toggle->click();
    check(Language::current() == Language::Lang::Chinese, "the toggle switches to Chinese");
    check(file->title() == QStringLiteral("檔案(&F)") && save->text() == QStringLiteral("儲存(&S)"), "menus and actions");
    check(label->text() == QStringLiteral("名稱") && label->toolTip() == QStringLiteral("刪除"), "labels and tool tips");
    check(combo->itemText(0) == QStringLiteral("棋盤格") && combo->itemText(1) == QStringLiteral("黑") &&
              combo->itemText(2) == QStringLiteral("my_sprite"),
          "combo items (the user's own words stay)");
    check(spin->specialValueText() == QStringLiteral("自動"), "spin box special text");
    check(table->horizontalHeaderItem(0)->text() == QStringLiteral("圖片") &&
              table->horizontalHeaderItem(1)->text() == QStringLiteral("字元"),
          "table headers");
    check(w.windowTitle() == QStringLiteral("TOMS 字型工具"), "window title");
    check(tr("Name") == QStringLiteral("名稱"), "tr() now answers in Chinese (text built after the switch)");
    check(toggle->text() == QStringLiteral("English"), "the toggle now offers English");

    toggle->click();
    check(Language::current() == Language::Lang::English && file->title() == QStringLiteral("&File") &&
              label->text() == QStringLiteral("Name") && combo->itemText(0) == QStringLiteral("Checker") &&
              spin->specialValueText() == QStringLiteral("auto") && w.windowTitle() == QStringLiteral("TOMS Font Tool") &&
              tr("Name") == QStringLiteral("Name"),
          "and back to English, everything as it was");

    std::printf("language_test: %d failed\n", g_failed);
    return g_failed ? 1 : 0;
}
