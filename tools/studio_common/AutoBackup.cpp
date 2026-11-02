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
QList<AutoBackup*>& f_Instances()
{
    static QList<AutoBackup*> l_oList;
    return l_oList;
}
}  // namespace

AutoBackup::Settings AutoBackup::f_Settings()
{
    const QSettings l_oQ;
    Settings l_oS;
    l_oS.m_bEnabled = l_oQ.value(QStringLiteral("backup/enabled"), l_oS.m_bEnabled).toBool();
    l_oS.m_iMinutes = std::clamp(l_oQ.value(QStringLiteral("backup/minutes"), l_oS.m_iMinutes).toInt(), 1, 240);
    l_oS.m_iEdits = std::clamp(l_oQ.value(QStringLiteral("backup/edits"), l_oS.m_iEdits).toInt(), 1, 1000);
    l_oS.m_iKeep = std::clamp(l_oQ.value(QStringLiteral("backup/keep"), l_oS.m_iKeep).toInt(), 1, 500);
    return l_oS;
}

void AutoBackup::f_SetSettings(const Settings& l_oS)
{
    QSettings l_oQ;
    l_oQ.setValue(QStringLiteral("backup/enabled"), l_oS.m_bEnabled);
    l_oQ.setValue(QStringLiteral("backup/minutes"), l_oS.m_iMinutes);
    l_oQ.setValue(QStringLiteral("backup/edits"), l_oS.m_iEdits);
    l_oQ.setValue(QStringLiteral("backup/keep"), l_oS.m_iKeep);
    for (AutoBackup* l_pB : f_Instances()) l_pB->f_ApplySettings();
}

AutoBackup::AutoBackup(QUndoStack* l_pStack, const QString& l_oExtension, std::function<QString()> l_fnCurrentFile, Writer l_fnWrite,
                       QObject* l_pParent)
    : QObject(l_pParent)
    , m_pStack(l_pStack)
    , m_oExt(l_oExtension)
    , m_fnFile(std::move(l_fnCurrentFile))
    , m_fnWrite(std::move(l_fnWrite))
{
    f_Instances().append(this);
    connect(l_pStack, &QUndoStack::indexChanged, this, &AutoBackup::f_Edited);
    connect(l_pStack, &QUndoStack::cleanChanged, this, [this](bool l_bClean) {
        if (l_bClean) m_iPending = 0;   // saved (or opened): the file on disk has it all
    });
    connect(&m_oTimer, &QTimer::timeout, this, [this] {
        if (m_iPending > 0 && f_Settings().m_bEnabled) f_BackupNow();
    });
    f_ApplySettings();
}

AutoBackup::~AutoBackup() { f_Instances().removeAll(this); }

void AutoBackup::f_ApplySettings()
{
    const Settings l_oS = f_Settings();
    m_oTimer.setInterval(l_oS.m_iMinutes * 60 * 1000);
    if (l_oS.m_bEnabled) m_oTimer.start();
    else m_oTimer.stop();
}

void AutoBackup::f_Edited()
{
    if (m_pStack->isClean()) {
        m_iPending = 0;
        return;
    }
    m_iPending++;
    const Settings l_oS = f_Settings();
    if (l_oS.m_bEnabled && m_iPending >= l_oS.m_iEdits) f_BackupNow();
}

QString AutoBackup::f_Folder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("backups"));
}

QString AutoBackup::f_BackupNow(QString* l_pError)
{
    const QString l_oFile = m_fnFile();
    QString l_oStem = l_oFile.isEmpty() ? QStringLiteral("untitled") : QFileInfo(l_oFile).completeBaseName();
    l_oStem.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    const QString l_oDir = QDir(f_Folder()).filePath(l_oStem);
    QDir().mkpath(l_oDir);
    const QString l_oTime = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    QString l_oPath = QDir(l_oDir).filePath(l_oStem + QLatin1Char('_') + l_oTime + m_oExt);
    // Two backups in the same millisecond: _002, _003 ... after the time. '_' sorts after the '.' of the extension and
    // the number is padded, so the later backup also sorts later (prune keeps the newest by name).
    for (int l_iI = 2; QFileInfo::exists(l_oPath) || QFileInfo::exists(l_oPath.left(l_oPath.size() - m_oExt.size())); l_iI++)
        l_oPath = QDir(l_oDir).filePath(QStringLiteral("%1_%2_%3%4").arg(l_oStem, l_oTime).arg(l_iI, 3, 10, QLatin1Char('0')).arg(m_oExt));
    QString l_oErr;
    if (!m_fnWrite(l_oPath, &l_oErr)) {
        if (l_pError) *l_pError = l_oErr;
        emit f_Message(tr("Backup failed: %1").arg(l_oErr));
        return QString();
    }
    m_iPending = 0;
    f_Prune(l_oDir, l_oStem);
    emit f_Message(tr("Backed up to %1").arg(QDir::toNativeSeparators(l_oPath)));
    return l_oPath;
}

void AutoBackup::f_Prune(const QString& l_oDir, const QString& l_oStem) const
{
    // Names end in a sortable time: the oldest come first.
    QDir l_oD(l_oDir);
    const QFileInfoList l_oAll = l_oD.entryInfoList({l_oStem + QStringLiteral("_*")}, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    const int l_iKeep = f_Settings().m_iKeep;
    for (int l_iI = 0; l_iI + l_iKeep < l_oAll.size(); l_iI++) {
        if (l_oAll[l_iI].isDir()) QDir(l_oAll[l_iI].absoluteFilePath()).removeRecursively();
        else QFile::remove(l_oAll[l_iI].absoluteFilePath());
    }
}

QMenu* AutoBackup::f_AddMenu(QMenu* l_pFileMenu, QWidget* l_pDialogParent)
{
    QMenu* l_pM = l_pFileMenu->addMenu(tr("Back&ups"));
    l_pM->addAction(Icons::f_Icon(Icons::Id::Save), tr("Back Up &Now"), this, [this] { f_BackupNow(); });
    l_pM->addAction(Icons::f_Icon(Icons::Id::Folder), tr("&Open Backup Folder"), this, [] {
        QDir().mkpath(f_Folder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(f_Folder()));
    });
    l_pM->addAction(tr("&Settings..."), this, [l_pDialogParent] {
        Settings l_oS = f_Settings();
        QDialog l_oDlg(l_pDialogParent);
        l_oDlg.setWindowTitle(tr("Backup Settings"));
        auto* l_pForm = new QFormLayout(&l_oDlg);
        auto* l_pOn = new QCheckBox(tr("Back up automatically"), &l_oDlg);
        l_pOn->setChecked(l_oS.m_bEnabled);
        auto l_fnSpin = [&l_oDlg](int l_iLo, int l_iHi, int l_iV, const QString& l_oSuffix) {
            auto* l_pB = new QSpinBox(&l_oDlg);
            l_pB->setRange(l_iLo, l_iHi);
            l_pB->setValue(l_iV);
            l_pB->setSuffix(l_oSuffix);
            return l_pB;
        };
        QSpinBox* l_pMinutes = l_fnSpin(1, 240, l_oS.m_iMinutes, tr(" min"));
        QSpinBox* l_pEdits = l_fnSpin(1, 1000, l_oS.m_iEdits, tr(" edits"));
        QSpinBox* l_pKeep = l_fnSpin(1, 500, l_oS.m_iKeep, tr(" backups per file"));
        l_pMinutes->setToolTip(tr("A backup every this many minutes, when something changed"));
        l_pEdits->setToolTip(tr("Also a backup after this many edits (undo / redo count too)"));
        l_pForm->addRow(l_pOn);
        l_pForm->addRow(tr("Every"), l_pMinutes);
        l_pForm->addRow(tr("Or after"), l_pEdits);
        l_pForm->addRow(tr("Keep"), l_pKeep);
        l_pForm->addRow(tr("Folder"), new QLabel(QDir::toNativeSeparators(f_Folder()), &l_oDlg));
        auto* l_pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &l_oDlg);
        l_pForm->addRow(l_pButtons);
        connect(l_pButtons, &QDialogButtonBox::accepted, &l_oDlg, &QDialog::accept);
        connect(l_pButtons, &QDialogButtonBox::rejected, &l_oDlg, &QDialog::reject);
        if (l_oDlg.exec() != QDialog::Accepted) return;
        f_SetSettings({l_pOn->isChecked(), l_pMinutes->value(), l_pEdits->value(), l_pKeep->value()});
    });
    return l_pM;
}

QDockWidget* f_CreateHistoryDock(QUndoStack* l_pStack, QWidget* l_pParent)
{
    auto* l_pDock = new QDockWidget(QObject::tr("History"), l_pParent);
    l_pDock->setObjectName(QStringLiteral("HistoryDock"));
    auto* l_pView = new QUndoView(l_pStack, l_pDock);
    l_pView->setEmptyLabel(QObject::tr("<opened>"));
    l_pView->setToolTip(QObject::tr("Every edit, oldest first: click one to undo or redo up to it"));
    l_pDock->setWidget(l_pView);
    return l_pDock;
}
