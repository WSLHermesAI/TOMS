#include "Language.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGroupBox>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QTabBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolBox>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>

// The JSON dictionaries are a Qt resource of this static library: it has to be registered by hand.
static void f_InitI18nResource() { Q_INIT_RESOURCE(studio_i18n); }

namespace Language {
namespace {

const char* g_pszToggleName = "tomsLanguageToggle";

QHash<QString, QString>& f_ToChinese() { static QHash<QString, QString> l_oD; return l_oD; }
QHash<QString, QString>& f_ToEnglish() { static QHash<QString, QString> l_oD; return l_oD; }
Lang g_eLang = Lang::English;
bool g_bLoaded = false;

// tr() -> the dictionary. A text it does not know stays English.
class DictTranslator : public QTranslator {
public:
    bool isEmpty() const override { return f_ToChinese().isEmpty(); }
    QString translate(const char*, const char* l_pszSource, const char*, int) const override {
        return l_pszSource ? f_ToChinese().value(QString::fromUtf8(l_pszSource)) : QString();
    }
};

DictTranslator* g_pDict = nullptr;
QTranslator* g_pQt = nullptr;   // Qt's own dialogs (file dialog, message box buttons), when Qt ships them

void f_Load() {
    if (g_bLoaded) return;
    g_bLoaded = true;
    f_InitI18nResource();
    const QDir l_oDir(QStringLiteral(":/i18n/zh_TW"));
    for (const QString& l_oF : l_oDir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile l_oFile(l_oDir.filePath(l_oF));
        if (!l_oFile.open(QIODevice::ReadOnly)) continue;
        const QJsonObject l_oO = QJsonDocument::fromJson(l_oFile.readAll()).object();
        for (auto l_oIt = l_oO.begin(); l_oIt != l_oO.end(); ++l_oIt) {
            const QString l_oZh = l_oIt.value().toString();
            if (l_oZh.isEmpty()) continue;
            f_ToChinese().insert(l_oIt.key(), l_oZh);
            f_ToEnglish().insert(l_oZh, l_oIt.key());
        }
    }
}

QString f_Conv(const QString& l_oS) {
    if (l_oS.isEmpty()) return l_oS;
    if (g_eLang == Lang::Chinese) {
        auto l_oIt = f_ToChinese().constFind(l_oS);
        return l_oIt != f_ToChinese().constEnd() ? *l_oIt : l_oS;
    }
    auto l_oIt = f_ToEnglish().constFind(l_oS);
    return l_oIt != f_ToEnglish().constEnd() ? *l_oIt : l_oS;
}

// Sets a text property only when the conversion changes it (no needless signals or repaints).
template <typename Get, typename Set>
void f_SwapText(Get l_oGet, Set l_oSet) {
    const QString l_oFrom = l_oGet();
    const QString l_oTo = f_Conv(l_oFrom);
    if (l_oTo != l_oFrom) l_oSet(l_oTo);
}

void f_RetranslateAction(QAction* l_pA) {
    f_SwapText([&] { return l_pA->text(); }, [&](const QString& l_oT) { l_pA->setText(l_oT); });
    f_SwapText([&] { return l_pA->iconText(); }, [&](const QString& l_oT) { l_pA->setIconText(l_oT); });
    f_SwapText([&] { return l_pA->toolTip(); }, [&](const QString& l_oT) { l_pA->setToolTip(l_oT); });
    f_SwapText([&] { return l_pA->statusTip(); }, [&](const QString& l_oT) { l_pA->setStatusTip(l_oT); });
}

void f_RetranslateWidget(QWidget* l_pW, QSet<QAction*>& l_oDone) {
    if (l_pW->objectName() == QLatin1String(g_pszToggleName)) return;
    f_SwapText([&] { return l_pW->windowTitle(); }, [&](const QString& l_oT) { l_pW->setWindowTitle(l_oT); });
    f_SwapText([&] { return l_pW->toolTip(); }, [&](const QString& l_oT) { l_pW->setToolTip(l_oT); });
    f_SwapText([&] { return l_pW->statusTip(); }, [&](const QString& l_oT) { l_pW->setStatusTip(l_oT); });
    f_SwapText([&] { return l_pW->whatsThis(); }, [&](const QString& l_oT) { l_pW->setWhatsThis(l_oT); });
    if (auto* l_pB = qobject_cast<QAbstractButton*>(l_pW))
        f_SwapText([&] { return l_pB->text(); }, [&](const QString& l_oT) { l_pB->setText(l_oT); });
    if (auto* l_pL = qobject_cast<QLabel*>(l_pW))
        f_SwapText([&] { return l_pL->text(); }, [&](const QString& l_oT) { l_pL->setText(l_oT); });
    if (auto* l_pG = qobject_cast<QGroupBox*>(l_pW))
        f_SwapText([&] { return l_pG->title(); }, [&](const QString& l_oT) { l_pG->setTitle(l_oT); });
    if (auto* l_pM = qobject_cast<QMenu*>(l_pW))
        f_SwapText([&] { return l_pM->title(); }, [&](const QString& l_oT) { l_pM->setTitle(l_oT); });
    if (auto* l_pE = qobject_cast<QLineEdit*>(l_pW))
        f_SwapText([&] { return l_pE->placeholderText(); }, [&](const QString& l_oT) { l_pE->setPlaceholderText(l_oT); });
    if (auto* l_pE = qobject_cast<QPlainTextEdit*>(l_pW))
        f_SwapText([&] { return l_pE->placeholderText(); }, [&](const QString& l_oT) { l_pE->setPlaceholderText(l_oT); });
    if (auto* l_pE = qobject_cast<QTextEdit*>(l_pW))
        f_SwapText([&] { return l_pE->placeholderText(); }, [&](const QString& l_oT) { l_pE->setPlaceholderText(l_oT); });
    if (auto* l_pC = qobject_cast<QComboBox*>(l_pW)) {
        if (!l_pC->isEditable())   // an editable combo holds the user's own words
            for (int l_iI = 0; l_iI < l_pC->count(); l_iI++)
                f_SwapText([&] { return l_pC->itemText(l_iI); }, [&](const QString& l_oT) { l_pC->setItemText(l_iI, l_oT); });
        f_SwapText([&] { return l_pC->placeholderText(); }, [&](const QString& l_oT) { l_pC->setPlaceholderText(l_oT); });
    }
    if (auto* l_pTb = qobject_cast<QTabBar*>(l_pW))
        for (int l_iI = 0; l_iI < l_pTb->count(); l_iI++) {
            f_SwapText([&] { return l_pTb->tabText(l_iI); }, [&](const QString& l_oT) { l_pTb->setTabText(l_iI, l_oT); });
            f_SwapText([&] { return l_pTb->tabToolTip(l_iI); }, [&](const QString& l_oT) { l_pTb->setTabToolTip(l_iI, l_oT); });
        }
    if (auto* l_pTb = qobject_cast<QToolBox*>(l_pW))
        for (int l_iI = 0; l_iI < l_pTb->count(); l_iI++)
            f_SwapText([&] { return l_pTb->itemText(l_iI); }, [&](const QString& l_oT) { l_pTb->setItemText(l_iI, l_oT); });
    if (auto* l_pS = qobject_cast<QAbstractSpinBox*>(l_pW))
        f_SwapText([&] { return l_pS->specialValueText(); }, [&](const QString& l_oT) { l_pS->setSpecialValueText(l_oT); });
    if (auto* l_pS = qobject_cast<QSpinBox*>(l_pW)) {
        f_SwapText([&] { return l_pS->prefix(); }, [&](const QString& l_oT) { l_pS->setPrefix(l_oT); });
        f_SwapText([&] { return l_pS->suffix(); }, [&](const QString& l_oT) { l_pS->setSuffix(l_oT); });
    }
    if (auto* l_pS = qobject_cast<QDoubleSpinBox*>(l_pW)) {
        f_SwapText([&] { return l_pS->prefix(); }, [&](const QString& l_oT) { l_pS->setPrefix(l_oT); });
        f_SwapText([&] { return l_pS->suffix(); }, [&](const QString& l_oT) { l_pS->setSuffix(l_oT); });
    }
    if (auto* l_pT = qobject_cast<QTableWidget*>(l_pW)) {
        for (int l_iI = 0; l_iI < l_pT->columnCount(); l_iI++)
            if (QTableWidgetItem* l_pH = l_pT->horizontalHeaderItem(l_iI))
                f_SwapText([&] { return l_pH->text(); }, [&](const QString& l_oX) { l_pH->setText(l_oX); });
        for (int l_iI = 0; l_iI < l_pT->rowCount(); l_iI++)
            if (QTableWidgetItem* l_pH = l_pT->verticalHeaderItem(l_iI))
                f_SwapText([&] { return l_pH->text(); }, [&](const QString& l_oX) { l_pH->setText(l_oX); });
    }
    if (auto* l_pT = qobject_cast<QTreeWidget*>(l_pW))
        if (QTreeWidgetItem* l_pH = l_pT->headerItem())
            for (int l_iI = 0; l_iI < l_pH->columnCount(); l_iI++)
                f_SwapText([&] { return l_pH->text(l_iI); }, [&](const QString& l_oX) { l_pH->setText(l_iI, l_oX); });
    for (QAction* l_pA : l_pW->actions())
        if (!l_oDone.contains(l_pA)) {
            l_oDone.insert(l_pA);
            f_RetranslateAction(l_pA);
        }
}

void f_UpdateToggles() {
    for (QWidget* l_pW : QApplication::allWidgets())
        if (auto* l_pB = qobject_cast<QToolButton*>(l_pW))
            if (l_pB->objectName() == QLatin1String(g_pszToggleName))
                l_pB->setText(g_eLang == Lang::Chinese ? QStringLiteral("English") : QStringLiteral("中文"));
}

void f_InstallTranslators() {
    if (!g_pDict) g_pDict = new DictTranslator;
    if (!g_pQt) {
        g_pQt = new QTranslator;
        // Qt's own texts: from the Qt install (developer machines) or a translations/ folder by the exe
        if (!g_pQt->load(QStringLiteral("qtbase_zh_TW"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            (void)g_pQt->load(QStringLiteral("qtbase_zh_TW"), QApplication::applicationDirPath() + QStringLiteral("/translations"));
    }
    QCoreApplication::removeTranslator(g_pDict);
    QCoreApplication::removeTranslator(g_pQt);
    if (g_eLang == Lang::Chinese) {
        if (!g_pQt->isEmpty()) QCoreApplication::installTranslator(g_pQt);
        QCoreApplication::installTranslator(g_pDict);
    }
}

QSettings f_Settings() { return QSettings(QStringLiteral("TOMS"), QStringLiteral("studio")); }

// Puts the toggle on every main window's menu bar the first time it shows: no editor has to.
class ToggleInstaller : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* l_pO, QEvent* l_pE) override {
        if (l_pE->type() == QEvent::Show)
            if (auto* l_pMw = qobject_cast<QMainWindow*>(l_pO); l_pMw && l_pMw->isWindow())   // not one hosted in a tab
                if (QMenuBar* l_pBar = l_pMw->menuBar(); l_pBar && !l_pBar->cornerWidget(Qt::TopRightCorner)) f_AddToggle(l_pBar);
        return false;
    }
};

}  // namespace

void f_Shutdown() {
    if (g_pDict) { QCoreApplication::removeTranslator(g_pDict); delete g_pDict; g_pDict = nullptr; }
    if (g_pQt) { QCoreApplication::removeTranslator(g_pQt); delete g_pQt; g_pQt = nullptr; }
    QHash<QString, QString>().swap(f_ToChinese());
    QHash<QString, QString>().swap(f_ToEnglish());
    g_bLoaded = false;
}

void f_Init() {
    f_Load();
    Lang l_eLang = f_Settings().value(QStringLiteral("language")).toString() == QLatin1String("zh") ? Lang::Chinese : Lang::English;
    // automated runs (their checks read UI text, screenshots compare pixels) always start in English
    const QStringList l_oArgs = QCoreApplication::arguments();
    for (const QString& l_oA : l_oArgs)
        if (l_oA.startsWith(QLatin1String("--selftest")) || l_oA == QLatin1String("--headless") ||
            l_oA.startsWith(QLatin1String("--screenshot")) || l_oA.startsWith(QLatin1String("--frames")))
            l_eLang = Lang::English;
    const QByteArray l_oEnv = qgetenv("TOMS_LANGUAGE").toLower();
    if (l_oEnv == "zh" || l_oEnv == "zh_tw") l_eLang = Lang::Chinese;
    else if (l_oEnv == "en") l_eLang = Lang::English;
    g_eLang = l_eLang;
    f_InstallTranslators();
    static bool l_bFilterInstalled = false;
    if (!l_bFilterInstalled && qApp) {
        l_bFilterInstalled = true;
        qApp->installEventFilter(new ToggleInstaller(qApp));
    }
}

Lang f_Current() { return g_eLang; }

void f_Retranslate(QWidget* l_pRoot) {
    if (!l_pRoot) return;
    QSet<QAction*> l_oDone;
    f_RetranslateWidget(l_pRoot, l_oDone);
    for (QWidget* l_pW : l_pRoot->findChildren<QWidget*>()) f_RetranslateWidget(l_pW, l_oDone);
}

void f_Set(Lang l_eLang) {
    f_Load();
    if (l_eLang == g_eLang) return;
    g_eLang = l_eLang;
    f_InstallTranslators();
    if (qEnvironmentVariableIsEmpty("TOMS_LANGUAGE")) {   // a forced language (tests) is not saved
        QSettings l_oS = f_Settings();
        l_oS.setValue(QStringLiteral("language"), l_eLang == Lang::Chinese ? QStringLiteral("zh") : QStringLiteral("en"));
    }
    for (QWidget* l_pW : QApplication::topLevelWidgets()) f_Retranslate(l_pW);
    f_UpdateToggles();
    emit Notifier::f_Instance()->f_Changed();
}

void f_Toggle() { f_Set(g_eLang == Lang::Chinese ? Lang::English : Lang::Chinese); }

void f_AddToggle(QMenuBar* l_pBar) {
    if (!l_pBar) return;
    auto* l_pB = new QToolButton(l_pBar);
    l_pB->setObjectName(QLatin1String(g_pszToggleName));
    l_pB->setAutoRaise(true);
    l_pB->setToolTip(QStringLiteral("Language / 語言: English ⇄ 繁體中文"));
    l_pB->setText(g_eLang == Lang::Chinese ? QStringLiteral("English") : QStringLiteral("中文"));
    QObject::connect(l_pB, &QToolButton::clicked, l_pB, [] { f_Toggle(); });
    l_pBar->setCornerWidget(l_pB, Qt::TopRightCorner);
    l_pB->show();   // added to a window that is already showing: a new child stays hidden until shown
}

QString f_Chinese(const QString& l_oEnglish) { f_Load(); return f_ToChinese().value(l_oEnglish); }
QString f_English(const QString& l_oChinese) { f_Load(); return f_ToEnglish().value(l_oChinese); }
int f_DictionarySize() { f_Load(); return f_ToChinese().size(); }

Notifier* Notifier::f_Instance() {
    static Notifier l_oN;
    return &l_oN;
}

}  // namespace Language
