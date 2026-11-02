#pragma once

#include <QObject>
#include <QString>

class QMenuBar;
class QWidget;

// English / Traditional Chinese for the TOMS editors (tools/studio_common). The editors write their UI in
// English through tr(); Chinese comes from i18n/zh_TW/*.json (English -> 中文, built into the editors as a Qt
// resource), installed as a QTranslator. Switching at run time re-translates every open window in place:
// menus, actions, labels, buttons, tool tips, combo items, table headers, spin box texts, dock titles. Text an
// editor builds with numbers in it (tr() + arg(n)) changes the next time the editor redraws it.
//
// The choice is shared by all the editors (QSettings "TOMS"/"studio", language) and kept between runs.
// --selftest / --headless runs always start in English (their checks read UI text); TOMS_LANGUAGE=en|zh
// overrides the saved choice.
// i18n/i18n_check.py lists tr() strings that have no translation yet (CTest studio.i18n_complete).
namespace Language {

enum class Lang { English, Chinese };

// Once, after QApplication and before any window is made: the saved choice, its translator.
void f_Init();
// Before QApplication goes (end of main): the translators and the English <-> 中文 tables are freed (docs/engine/15).
void f_Shutdown();
Lang f_Current();
// Switches, saves the choice and re-translates every open window.
void f_Set(Lang l_eLang);
void f_Toggle();

// The EN / 中文 toggle button in the menu bar's top-right corner (QMenuBar::setCornerWidget).
void f_AddToggle(QMenuBar* l_pBar);

// The Chinese of an English tr() text ("" when there is none), and the reverse.
QString f_Chinese(const QString& l_oEnglish);
QString f_English(const QString& l_oChinese);
int f_DictionarySize();

// Re-translates one widget tree to the current language (set() does every top-level window).
void f_Retranslate(QWidget* l_pRoot);

// Emits changed() after set(): for windows that rebuild text with numbers in it.
class Notifier : public QObject {
    Q_OBJECT
public:
    static Notifier* f_Instance();
signals:
    void f_Changed();
};

}  // namespace Language
