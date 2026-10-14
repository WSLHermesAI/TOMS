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
static void initI18nResource() { Q_INIT_RESOURCE(studio_i18n); }

namespace Language {
namespace {

const char* kToggleName = "tomsLanguageToggle";

QHash<QString, QString>& toChinese() { static QHash<QString, QString> d; return d; }
QHash<QString, QString>& toEnglish() { static QHash<QString, QString> d; return d; }
Lang g_lang = Lang::English;
bool g_loaded = false;

// tr() -> the dictionary. A text it does not know stays English.
class DictTranslator : public QTranslator {
public:
    bool isEmpty() const override { return toChinese().isEmpty(); }
    QString translate(const char*, const char* source, const char*, int) const override {
        return source ? toChinese().value(QString::fromUtf8(source)) : QString();
    }
};

DictTranslator* g_dict = nullptr;
QTranslator* g_qt = nullptr;   // Qt's own dialogs (file dialog, message box buttons), when Qt ships them

void load() {
    if (g_loaded) return;
    g_loaded = true;
    initI18nResource();
    const QDir dir(QStringLiteral(":/i18n/zh_TW"));
    for (const QString& f : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
        for (auto it = o.begin(); it != o.end(); ++it) {
            const QString zh = it.value().toString();
            if (zh.isEmpty()) continue;
            toChinese().insert(it.key(), zh);
            toEnglish().insert(zh, it.key());
        }
    }
}

QString conv(const QString& s) {
    if (s.isEmpty()) return s;
    if (g_lang == Lang::Chinese) {
        auto it = toChinese().constFind(s);
        return it != toChinese().constEnd() ? *it : s;
    }
    auto it = toEnglish().constFind(s);
    return it != toEnglish().constEnd() ? *it : s;
}

// Sets a text property only when the conversion changes it (no needless signals or repaints).
template <typename Get, typename Set>
void swapText(Get get, Set set) {
    const QString from = get();
    const QString to = conv(from);
    if (to != from) set(to);
}

void retranslateAction(QAction* a) {
    swapText([&] { return a->text(); }, [&](const QString& t) { a->setText(t); });
    swapText([&] { return a->iconText(); }, [&](const QString& t) { a->setIconText(t); });
    swapText([&] { return a->toolTip(); }, [&](const QString& t) { a->setToolTip(t); });
    swapText([&] { return a->statusTip(); }, [&](const QString& t) { a->setStatusTip(t); });
}

void retranslateWidget(QWidget* w, QSet<QAction*>& done) {
    if (w->objectName() == QLatin1String(kToggleName)) return;
    swapText([&] { return w->windowTitle(); }, [&](const QString& t) { w->setWindowTitle(t); });
    swapText([&] { return w->toolTip(); }, [&](const QString& t) { w->setToolTip(t); });
    swapText([&] { return w->statusTip(); }, [&](const QString& t) { w->setStatusTip(t); });
    swapText([&] { return w->whatsThis(); }, [&](const QString& t) { w->setWhatsThis(t); });
    if (auto* b = qobject_cast<QAbstractButton*>(w))
        swapText([&] { return b->text(); }, [&](const QString& t) { b->setText(t); });
    if (auto* l = qobject_cast<QLabel*>(w))
        swapText([&] { return l->text(); }, [&](const QString& t) { l->setText(t); });
    if (auto* g = qobject_cast<QGroupBox*>(w))
        swapText([&] { return g->title(); }, [&](const QString& t) { g->setTitle(t); });
    if (auto* m = qobject_cast<QMenu*>(w))
        swapText([&] { return m->title(); }, [&](const QString& t) { m->setTitle(t); });
    if (auto* e = qobject_cast<QLineEdit*>(w))
        swapText([&] { return e->placeholderText(); }, [&](const QString& t) { e->setPlaceholderText(t); });
    if (auto* e = qobject_cast<QPlainTextEdit*>(w))
        swapText([&] { return e->placeholderText(); }, [&](const QString& t) { e->setPlaceholderText(t); });
    if (auto* e = qobject_cast<QTextEdit*>(w))
        swapText([&] { return e->placeholderText(); }, [&](const QString& t) { e->setPlaceholderText(t); });
    if (auto* c = qobject_cast<QComboBox*>(w)) {
        if (!c->isEditable())   // an editable combo holds the user's own words
            for (int i = 0; i < c->count(); i++)
                swapText([&] { return c->itemText(i); }, [&](const QString& t) { c->setItemText(i, t); });
        swapText([&] { return c->placeholderText(); }, [&](const QString& t) { c->setPlaceholderText(t); });
    }
    if (auto* tb = qobject_cast<QTabBar*>(w))
        for (int i = 0; i < tb->count(); i++) {
            swapText([&] { return tb->tabText(i); }, [&](const QString& t) { tb->setTabText(i, t); });
            swapText([&] { return tb->tabToolTip(i); }, [&](const QString& t) { tb->setTabToolTip(i, t); });
        }
    if (auto* tb = qobject_cast<QToolBox*>(w))
        for (int i = 0; i < tb->count(); i++)
            swapText([&] { return tb->itemText(i); }, [&](const QString& t) { tb->setItemText(i, t); });
    if (auto* s = qobject_cast<QAbstractSpinBox*>(w))
        swapText([&] { return s->specialValueText(); }, [&](const QString& t) { s->setSpecialValueText(t); });
    if (auto* s = qobject_cast<QSpinBox*>(w)) {
        swapText([&] { return s->prefix(); }, [&](const QString& t) { s->setPrefix(t); });
        swapText([&] { return s->suffix(); }, [&](const QString& t) { s->setSuffix(t); });
    }
    if (auto* s = qobject_cast<QDoubleSpinBox*>(w)) {
        swapText([&] { return s->prefix(); }, [&](const QString& t) { s->setPrefix(t); });
        swapText([&] { return s->suffix(); }, [&](const QString& t) { s->setSuffix(t); });
    }
    if (auto* t = qobject_cast<QTableWidget*>(w)) {
        for (int i = 0; i < t->columnCount(); i++)
            if (QTableWidgetItem* h = t->horizontalHeaderItem(i))
                swapText([&] { return h->text(); }, [&](const QString& x) { h->setText(x); });
        for (int i = 0; i < t->rowCount(); i++)
            if (QTableWidgetItem* h = t->verticalHeaderItem(i))
                swapText([&] { return h->text(); }, [&](const QString& x) { h->setText(x); });
    }
    if (auto* t = qobject_cast<QTreeWidget*>(w))
        if (QTreeWidgetItem* h = t->headerItem())
            for (int i = 0; i < h->columnCount(); i++)
                swapText([&] { return h->text(i); }, [&](const QString& x) { h->setText(i, x); });
    for (QAction* a : w->actions())
        if (!done.contains(a)) {
            done.insert(a);
            retranslateAction(a);
        }
}

void updateToggles() {
    for (QWidget* w : QApplication::allWidgets())
        if (auto* b = qobject_cast<QToolButton*>(w))
            if (b->objectName() == QLatin1String(kToggleName))
                b->setText(g_lang == Lang::Chinese ? QStringLiteral("English") : QStringLiteral("中文"));
}

void installTranslators() {
    if (!g_dict) g_dict = new DictTranslator;
    if (!g_qt) {
        g_qt = new QTranslator;
        // Qt's own texts: from the Qt install (developer machines) or a translations/ folder by the exe
        if (!g_qt->load(QStringLiteral("qtbase_zh_TW"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            (void)g_qt->load(QStringLiteral("qtbase_zh_TW"), QApplication::applicationDirPath() + QStringLiteral("/translations"));
    }
    QCoreApplication::removeTranslator(g_dict);
    QCoreApplication::removeTranslator(g_qt);
    if (g_lang == Lang::Chinese) {
        if (!g_qt->isEmpty()) QCoreApplication::installTranslator(g_qt);
        QCoreApplication::installTranslator(g_dict);
    }
}

QSettings settings() { return QSettings(QStringLiteral("TOMS"), QStringLiteral("studio")); }

// Puts the toggle on every main window's menu bar the first time it shows: no editor has to.
class ToggleInstaller : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show)
            if (auto* mw = qobject_cast<QMainWindow*>(o))
                if (QMenuBar* bar = mw->menuBar(); bar && !bar->cornerWidget(Qt::TopRightCorner)) addToggle(bar);
        return false;
    }
};

}  // namespace

void init() {
    load();
    Lang lang = settings().value(QStringLiteral("language")).toString() == QLatin1String("zh") ? Lang::Chinese : Lang::English;
    // automated runs (their checks read UI text, screenshots compare pixels) always start in English
    const QStringList args = QCoreApplication::arguments();
    for (const QString& a : args)
        if (a.startsWith(QLatin1String("--selftest")) || a == QLatin1String("--headless") ||
            a.startsWith(QLatin1String("--screenshot")) || a.startsWith(QLatin1String("--frames")))
            lang = Lang::English;
    const QByteArray env = qgetenv("TOMS_LANGUAGE").toLower();
    if (env == "zh" || env == "zh_tw") lang = Lang::Chinese;
    else if (env == "en") lang = Lang::English;
    g_lang = lang;
    installTranslators();
    static bool filterInstalled = false;
    if (!filterInstalled && qApp) {
        filterInstalled = true;
        qApp->installEventFilter(new ToggleInstaller(qApp));
    }
}

Lang current() { return g_lang; }

void retranslate(QWidget* root) {
    if (!root) return;
    QSet<QAction*> done;
    retranslateWidget(root, done);
    for (QWidget* w : root->findChildren<QWidget*>()) retranslateWidget(w, done);
}

void set(Lang lang) {
    load();
    if (lang == g_lang) return;
    g_lang = lang;
    installTranslators();
    if (qEnvironmentVariableIsEmpty("TOMS_LANGUAGE")) {   // a forced language (tests) is not saved
        QSettings s = settings();
        s.setValue(QStringLiteral("language"), lang == Lang::Chinese ? QStringLiteral("zh") : QStringLiteral("en"));
    }
    for (QWidget* w : QApplication::topLevelWidgets()) retranslate(w);
    updateToggles();
    emit Notifier::instance()->changed();
}

void toggle() { set(g_lang == Lang::Chinese ? Lang::English : Lang::Chinese); }

void addToggle(QMenuBar* bar) {
    if (!bar) return;
    auto* b = new QToolButton(bar);
    b->setObjectName(QLatin1String(kToggleName));
    b->setAutoRaise(true);
    b->setToolTip(QStringLiteral("Language / 語言: English ⇄ 繁體中文"));
    b->setText(g_lang == Lang::Chinese ? QStringLiteral("English") : QStringLiteral("中文"));
    QObject::connect(b, &QToolButton::clicked, b, [] { toggle(); });
    bar->setCornerWidget(b, Qt::TopRightCorner);
    b->show();   // added to a window that is already showing: a new child stays hidden until shown
}

QString chinese(const QString& english) { load(); return toChinese().value(english); }
QString english(const QString& chinese) { load(); return toEnglish().value(chinese); }
int dictionarySize() { load(); return toChinese().size(); }

Notifier* Notifier::instance() {
    static Notifier n;
    return &n;
}

}  // namespace Language
