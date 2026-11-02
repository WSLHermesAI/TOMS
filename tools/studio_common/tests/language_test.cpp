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

int g_iFailed = 0;

void f_Check(bool l_bOk, const char* l_pszWhat) {
    std::printf("[%s] %s\n", l_bOk ? "ok" : "FAILED", l_pszWhat);
    if (!l_bOk) g_iFailed++;
}

QString f_Tr(const char* l_pszS) { return QCoreApplication::translate("LanguageTest", l_pszS); }

}  // namespace

int main(int l_iArgc, char** l_pArgv) {
    QApplication l_oApp(l_iArgc, l_pArgv);
    Language::f_Init();
    f_Check(Language::f_Current() == Language::Lang::English, "starts in English (TOMS_LANGUAGE=en)");
    f_Check(Language::f_DictionarySize() > 1000, "the Chinese dictionaries are built in (> 1000 entries)");
    f_Check(Language::f_Chinese(QStringLiteral("&File")) == QStringLiteral("檔案(&F)") &&
              Language::f_English(QStringLiteral("檔案(&F)")) == QStringLiteral("&File"),
          "a word and back: &File <-> 檔案(&F)");

    // A window built in English, the way the editors build theirs
    QMainWindow l_oW;
    QMenu* l_pFile = l_oW.menuBar()->addMenu(f_Tr("&File"));
    QAction* l_pSave = l_pFile->addAction(f_Tr("&Save"));
    auto* l_pCentral = new QWidget;
    auto* l_pLayout = new QVBoxLayout(l_pCentral);
    auto* l_pLabel = new QLabel(f_Tr("Name"));
    auto* l_pCombo = new QComboBox;
    l_pCombo->addItems({f_Tr("Checker"), f_Tr("Black"), QStringLiteral("my_sprite")});
    auto* l_pSpin = new QSpinBox;
    l_pSpin->setRange(-1, 10);
    l_pSpin->setSpecialValueText(f_Tr("auto"));
    l_pSpin->setValue(-1);
    auto* l_pTable = new QTableWidget(0, 2);
    l_pTable->setHorizontalHeaderLabels({f_Tr("Picture"), f_Tr("Character")});
    l_pLabel->setToolTip(f_Tr("Delete"));
    for (QWidget* l_pX : {(QWidget*)l_pLabel, (QWidget*)l_pCombo, (QWidget*)l_pSpin, (QWidget*)l_pTable}) l_pLayout->addWidget(l_pX);
    l_oW.setCentralWidget(l_pCentral);
    l_oW.setWindowTitle(f_Tr("TOMS Font Tool"));
    l_oW.show();
    QApplication::processEvents();
    auto* l_pToggle = qobject_cast<QToolButton*>(l_oW.menuBar()->cornerWidget(Qt::TopRightCorner));
    f_Check(l_pToggle && l_pToggle->text() == QStringLiteral("中文"), "every main window gets the toggle, showing 中文");
    f_Check(l_pToggle && l_pToggle->isVisible() && l_pToggle->width() > 0 && l_pToggle->geometry().right() <= l_oW.menuBar()->width(),
          "the toggle is visible, inside the menu bar");

    l_pToggle->click();
    f_Check(Language::f_Current() == Language::Lang::Chinese, "the toggle switches to Chinese");
    f_Check(l_pFile->title() == QStringLiteral("檔案(&F)") && l_pSave->text() == QStringLiteral("儲存(&S)"), "menus and actions");
    f_Check(l_pLabel->text() == QStringLiteral("名稱") && l_pLabel->toolTip() == QStringLiteral("刪除"), "labels and tool tips");
    f_Check(l_pCombo->itemText(0) == QStringLiteral("棋盤格") && l_pCombo->itemText(1) == QStringLiteral("黑") &&
              l_pCombo->itemText(2) == QStringLiteral("my_sprite"),
          "combo items (the user's own words stay)");
    f_Check(l_pSpin->specialValueText() == QStringLiteral("自動"), "spin box special text");
    f_Check(l_pTable->horizontalHeaderItem(0)->text() == QStringLiteral("圖片") &&
              l_pTable->horizontalHeaderItem(1)->text() == QStringLiteral("字元"),
          "table headers");
    f_Check(l_oW.windowTitle() == QStringLiteral("TOMS 字型工具"), "window title");
    f_Check(f_Tr("Name") == QStringLiteral("名稱"), "tr() now answers in Chinese (text built after the switch)");
    f_Check(l_pToggle->text() == QStringLiteral("English"), "the toggle now offers English");

    l_pToggle->click();
    f_Check(Language::f_Current() == Language::Lang::English && l_pFile->title() == QStringLiteral("&File") &&
              l_pLabel->text() == QStringLiteral("Name") && l_pCombo->itemText(0) == QStringLiteral("Checker") &&
              l_pSpin->specialValueText() == QStringLiteral("auto") && l_oW.windowTitle() == QStringLiteral("TOMS Font Tool") &&
              f_Tr("Name") == QStringLiteral("Name"),
          "and back to English, everything as it was");

    std::printf("language_test: %d failed\n", g_iFailed);
    return g_iFailed ? 1 : 0;
}
