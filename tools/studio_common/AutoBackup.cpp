#include "AutoBackup.h"

#include "Icons.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QUndoStack>
#include <QUndoView>
#include <QUrl>

#include <algorithm>

namespace {
QList<AutoBackup*>& instances()
{
    static QList<AutoBackup*> list;
    return list;
}
}  // namespace

AutoBackup::Settings AutoBackup::settings()
{
    const QSettings q;
    Settings s;
    s.enabled = q.value(QStringLiteral("backup/enabled"), s.enabled).toBool();
    s.minutes = std::clamp(q.value(QStringLiteral("backup/minutes"), s.minutes).toInt(), 1, 240);
    s.edits = std::clamp(q.value(QStringLiteral("backup/edits"), s.edits).toInt(), 1, 1000);
    s.keep = std::clamp(q.value(QStringLiteral("backup/keep"), s.keep).toInt(), 1, 500);
    return s;
}

void AutoBackup::setSettings(const Settings& s)
{
    QSettings q;
    q.setValue(QStringLiteral("backup/enabled"), s.enabled);
    q.setValue(QStringLiteral("backup/minutes"), s.minutes);
    q.setValue(QStringLiteral("backup/edits"), s.edits);
    q.setValue(QStringLiteral("backup/keep"), s.keep);
    for (AutoBackup* b : instances()) b->applySettings();
}

AutoBackup::AutoBackup(QUndoStack* stack, const QString& extension, std::function<QString()> currentFile, Writer write,
                       QObject* parent)
    : QObject(parent)
    , m_stack(stack)
    , m_ext(extension)
    , m_file(std::move(currentFile))
    , m_write(std::move(write))
{
    instances().append(this);
    connect(stack, &QUndoStack::indexChanged, this, &AutoBackup::edited);
    connect(stack, &QUndoStack::cleanChanged, this, [this](bool clean) {
        if (clean) m_pending = 0;   // saved (or opened): the file on disk has it all
    });
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_pending > 0 && settings().enabled) backupNow();
    });
    applySettings();
}

AutoBackup::~AutoBackup() { instances().removeAll(this); }

void AutoBackup::applySettings()
{
    const Settings s = settings();
    m_timer.setInterval(s.minutes * 60 * 1000);
    if (s.enabled) m_timer.start();
    else m_timer.stop();
}

void AutoBackup::edited()
{
    if (m_stack->isClean()) {
        m_pending = 0;
        return;
    }
    m_pending++;
    const Settings s = settings();
    if (s.enabled && m_pending >= s.edits) backupNow();
}

QString AutoBackup::folder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("backups"));
}

QString AutoBackup::backupNow(QString* error)
{
    const QString file = m_file();
    QString stem = file.isEmpty() ? QStringLiteral("untitled") : QFileInfo(file).completeBaseName();
    stem.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    const QString dir = QDir(folder()).filePath(stem);
    QDir().mkpath(dir);
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    QString path = QDir(dir).filePath(stem + QLatin1Char('_') + time + m_ext);
    for (int i = 2; QFileInfo::exists(path) || QFileInfo::exists(path.left(path.size() - m_ext.size())); i++)
        path = QDir(dir).filePath(QStringLiteral("%1_%2-%3%4").arg(stem, time).arg(i).arg(m_ext));
    QString err;
    if (!m_write(path, &err)) {
        if (error) *error = err;
        emit message(tr("Backup failed: %1").arg(err));
        return QString();
    }
    m_pending = 0;
    prune(dir, stem);
    emit message(tr("Backed up to %1").arg(QDir::toNativeSeparators(path)));
    return path;
}

void AutoBackup::prune(const QString& dir, const QString& stem) const
{
    // Names end in a sortable time: the oldest come first.
    QDir d(dir);
    const QFileInfoList all = d.entryInfoList({stem + QStringLiteral("_*")}, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    const int keep = settings().keep;
    for (int i = 0; i + keep < all.size(); i++) {
        if (all[i].isDir()) QDir(all[i].absoluteFilePath()).removeRecursively();
        else QFile::remove(all[i].absoluteFilePath());
    }
}

QMenu* AutoBackup::addMenu(QMenu* fileMenu, QWidget* dialogParent)
{
    QMenu* m = fileMenu->addMenu(tr("Back&ups"));
    m->addAction(Icons::icon(Icons::Id::Save), tr("Back Up &Now"), this, [this] { backupNow(); });
    m->addAction(Icons::icon(Icons::Id::Folder), tr("&Open Backup Folder"), this, [] {
        QDir().mkpath(folder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder()));
    });
    m->addAction(tr("&Settings..."), this, [dialogParent] {
        Settings s = settings();
        QDialog dlg(dialogParent);
        dlg.setWindowTitle(tr("Backup Settings"));
        auto* form = new QFormLayout(&dlg);
        auto* on = new QCheckBox(tr("Back up automatically"), &dlg);
        on->setChecked(s.enabled);
        auto spin = [&dlg](int lo, int hi, int v, const QString& suffix) {
            auto* b = new QSpinBox(&dlg);
            b->setRange(lo, hi);
            b->setValue(v);
            b->setSuffix(suffix);
            return b;
        };
        QSpinBox* minutes = spin(1, 240, s.minutes, tr(" min"));
        QSpinBox* edits = spin(1, 1000, s.edits, tr(" edits"));
        QSpinBox* keep = spin(1, 500, s.keep, tr(" backups per file"));
        minutes->setToolTip(tr("A backup every this many minutes, when something changed"));
        edits->setToolTip(tr("Also a backup after this many edits (undo / redo count too)"));
        form->addRow(on);
        form->addRow(tr("Every"), minutes);
        form->addRow(tr("Or after"), edits);
        form->addRow(tr("Keep"), keep);
        form->addRow(tr("Folder"), new QLabel(QDir::toNativeSeparators(folder()), &dlg));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        setSettings({on->isChecked(), minutes->value(), edits->value(), keep->value()});
    });
    return m;
}

QDockWidget* createHistoryDock(QUndoStack* stack, QWidget* parent)
{
    auto* dock = new QDockWidget(QObject::tr("History"), parent);
    dock->setObjectName(QStringLiteral("HistoryDock"));
    auto* view = new QUndoView(stack, dock);
    view->setEmptyLabel(QObject::tr("<opened>"));
    view->setToolTip(QObject::tr("Every edit, oldest first: click one to undo or redo up to it"));
    dock->setWidget(view);
    return dock;
}
