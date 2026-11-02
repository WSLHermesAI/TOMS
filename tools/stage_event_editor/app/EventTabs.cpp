// EventTabs.cpp -- the event editor's Floor map, Variables, value tabs and Kinds (event_editor.html's renderFloorMap,
// renderVars, renderValueTab, renderKinds).
#include "EventEditor.h"

#include "EventWidgets.h"
#include "FlowLayout.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QTimer>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

using stage::Json;
using namespace EvUi;

namespace {
bool f_StartsWith(const std::string& l_strS, const char* l_pszP) { return l_strS.rfind(l_pszP, 0) == 0; }
std::string f_KindOf(const std::string& l_strS) { return l_strS.substr(0, l_strS.find(':')); }
std::string f_NameOf(const std::string& l_strS) { const size_t l_nC = l_strS.find(':'); return l_nC == std::string::npos ? std::string() : l_strS.substr(l_nC + 1); }
int f_Fseq(const std::string& l_strId) { try { return std::stoi(l_strId.substr(1)); } catch (...) { return 0; } }
QString f_Icon(const std::string& l_strK) { return l_strK == "var" ? QStringLiteral("◆") : l_strK == "counter" ? QStringLiteral("▲") : QStringLiteral("⚑"); }
QString f_CleanCat(const QString& l_oC) {
    QString l_oS = l_oC.trimmed();
    l_oS.replace(QRegularExpression(QStringLiteral("\\s*/\\s*")), QStringLiteral("/"));
    while (l_oS.startsWith(QLatin1Char('/'))) l_oS.remove(0, 1);
    while (l_oS.endsWith(QLatin1Char('/'))) l_oS.chop(1);
    return l_oS;
}
bool f_CatNameOk(const QString& l_oC) { return !l_oC.isEmpty() && !l_oC.contains(QRegularExpression(QStringLiteral("[\"<>&|\\\\]"))); }
QString f_HowWrite(const Json& l_jE, const std::string& l_strRef, const Json& l_jFlags) {
    QStringList l_oOut;
    for (const Json& l_jA : l_jE["actions"]) {
        if (stage::f_ActRef(l_jA) != l_strRef) continue;
        const std::string l_strT = l_jA.value("type", std::string());
        if (l_strT == "setVar") l_oOut << QStringLiteral("= ") + QString::fromStdString(l_jA.value("value", Json(0)).dump());
        else if (l_strT == "setFlag") l_oOut << QStringLiteral("set");
        else { const double l_dD = l_jA.value("delta", Json(0)).is_number() ? l_jA["delta"].get<double>() : 0; l_oOut << (l_dD >= 0 ? QStringLiteral("+") : QString()) + QString::number(l_dD); }
    }
    if (f_StartsWith(l_strRef, "flag:") && l_jFlags.contains(l_strRef.substr(5)) && l_jFlags[l_strRef.substr(5)].value("setBy", std::string()) == l_jE["id"].get<std::string>()) l_oOut << QStringLiteral("setBy");
    return l_oOut.join(QStringLiteral(", "));
}
QString f_HowRead(const Json& l_jE, const std::string& l_strRef) {
    QStringList l_oOut;
    for (const Json& l_jL : l_jE["req"]) {
        if (stage::f_LeafRef(l_jL) != l_strRef) continue;
        const std::string l_strT = l_jL.value("type", std::string());
        l_oOut << (l_strT == "varEquals" ? QStringLiteral("= ") + QString::fromStdString(l_jL.value("value", Json(0)).dump())
              : l_strT == "varAtLeast" ? QStringLiteral("≥ ") + QString::fromStdString(l_jL.value("value", Json(0)).dump())
              : l_strT == "counterAtLeast" ? QStringLiteral("≥ ") + QString::fromStdString(l_jL.value("min", Json(0)).dump()) : QStringLiteral("set"));
    }
    return l_oOut.join(QStringLiteral(", "));
}
}  // namespace

// ---- the floor map -------------------------------------------------------------------------------------------------

QWidget* EventEditor::f_FloorMapPane() {
    const double l_dFW = 62, l_dFH = 24, l_dGX = 30, l_dGY = 30, l_dX0 = 56, l_dY0 = 26;
    std::vector<std::string> l_vActs;
    for (const Json& l_jF : m_oP.m_jS["floors"]) if (std::find(l_vActs.begin(), l_vActs.end(), l_jF["act"].get<std::string>()) == l_vActs.end()) l_vActs.push_back(l_jF["act"].get<std::string>());
    std::sort(l_vActs.begin(), l_vActs.end());
    std::vector<FloorMapView::Node> l_vNodes;
    std::map<std::string, size_t> l_mapPos;
    std::vector<std::pair<QString, double>> l_vRows;
    const auto l_setReach = m_oP.f_Reachable();
    for (size_t l_nR = 0; l_nR < l_vActs.size(); l_nR++) {
        l_vRows.push_back({f_Q(l_vActs[l_nR]), l_dY0 + l_nR * (l_dFH + l_dGY)});
        std::vector<const Json*> l_vFl;
        for (const Json& l_jF : m_oP.m_jS["floors"]) if (l_jF["act"] == l_vActs[l_nR]) l_vFl.push_back(&l_jF);
        std::stable_sort(l_vFl.begin(), l_vFl.end(), [](const Json* l_pA, const Json* l_pB) { return f_Fseq((*l_pA)["id"].get<std::string>()) < f_Fseq((*l_pB)["id"].get<std::string>()); });
        for (size_t l_nC = 0; l_nC < l_vFl.size(); l_nC++) {
            FloorMapView::Node l_oN;
            l_oN.m_oId = f_Q((*l_vFl[l_nC])["id"].get<std::string>());
            l_oN.m_oAct = f_Q(l_vActs[l_nR]);
            l_oN.m_bBoss = (*l_vFl[l_nC])["role"] == "boss-stage";
            l_oN.m_bWarn = !l_setReach.count((*l_vFl[l_nC])["id"].get<std::string>());
            l_oN.m_bIsNew = m_oP.f_IsNewFloor((*l_vFl[l_nC])["id"].get<std::string>());
            l_oN.m_oR = QRectF(l_dX0 + l_nC * (l_dFW + l_dGX), l_dY0 + l_nR * (l_dFH + l_dGY), l_dFW, l_dFH);
            l_mapPos[(*l_vFl[l_nC])["id"].get<std::string>()] = l_vNodes.size();
            l_vNodes.push_back(l_oN);
        }
    }
    // stairs to floors that do not exist yet: a red stand-in at the end of the source's row
    for (const Json& l_jF : m_oP.m_jS["floors"])
        for (const Json& l_jSt : l_jF["stairs"]) {
            const std::string l_strTo = l_jSt.value("to", std::string());
            if (l_strTo.empty() || m_oP.f_Flo(l_strTo) || l_mapPos.count(l_strTo)) continue;
            const FloorMapView::Node& l_oSrc = l_vNodes[l_mapPos[l_jF["id"].get<std::string>()]];
            int l_iCol = 0;
            for (const auto& l_oN : l_vNodes) if (std::abs(l_oN.m_oR.y() - l_oSrc.m_oR.y()) < 1) l_iCol++;
            FloorMapView::Node l_oG;
            l_oG.m_oId = f_Q(l_strTo);
            l_oG.m_bGhost = true;
            l_oG.m_oR = QRectF(l_dX0 + l_iCol * (l_dFW + l_dGX), l_oSrc.m_oR.y(), l_dFW + 10, l_dFH);
            l_mapPos[l_strTo] = l_vNodes.size();
            l_vNodes.push_back(l_oG);
        }
    std::vector<FloorMapView::Edge> l_vEdges;
    int l_iNStairs = 0, l_iNLocked = 0;
    for (const Json& l_jF : m_oP.m_jS["floors"])
        for (const Json& l_jSt : l_jF["stairs"]) {
            l_iNStairs++;
            const std::string l_strTo = l_jSt.value("to", std::string());
            const std::vector<std::string> l_vLocks = stage::f_StairUnlockEvents(l_jSt);
            const std::string l_strLock = l_vLocks.empty() ? std::string() : l_vLocks[0];
            if (!l_strLock.empty()) l_iNLocked++;
            if (l_strTo.empty() || !l_mapPos.count(l_strTo)) continue;
            if (l_jSt.value("dir", std::string("up")) == "down")
                if (const Json* l_pOther = m_oP.f_Flo(l_strTo)) {   // drawn once, as the up stair
                    bool l_bDup = false;
                    for (const Json& l_jX : (*l_pOther)["stairs"]) if (l_jX.value("to", std::string()) == l_jF["id"].get<std::string>() && l_jX.value("dir", std::string("up")) == "up") l_bDup = true;
                    if (l_bDup) continue;
                }
            l_vEdges.push_back({f_Q(l_jF["id"].get<std::string>()), f_Q(l_strTo), f_Q(l_strLock), !m_oP.f_Flo(l_strTo)});
        }
    auto* l_pView = new FloorMapView;
    const QString l_oSel = m_oP.f_Flo(m_oP.m_strSelected) || l_mapPos.count(m_oP.m_strSelected) ? f_Q(m_oP.m_strSelected) : QString();
    l_pView->f_SetGraph(l_vNodes, l_vEdges, l_vRows, l_oSel);
    connect(l_pView, &FloorMapView::f_Clicked, this, [this](const QString& l_oId) {
        const std::string l_strS = l_oId.toStdString();
        if (m_oP.f_Flo(l_strS)) { f_Select(l_strS); return; }
        for (const Json& l_jF : m_oP.m_jS["floors"])
            for (const Json& l_jSt : l_jF["stairs"]) if (l_jSt.value("to", std::string()) == l_strS) { f_Select(l_jF["id"].get<std::string>()); return; }
    });
    m_pInfo->setText(tr("%1 floors · %2 stairs · %3 locked · click a floor").arg(m_oP.m_jS["floors"].size()).arg(l_iNStairs).arg(l_iNLocked));
    return l_pView;
}

// ---- variables -----------------------------------------------------------------------------------------------------

QWidget* EventEditor::f_VarsPane() {
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    // the filter: text, category, kinds of value
    std::vector<std::string> l_vRefs = m_oP.f_ValueRefs();
    std::set<QString> l_setCats;
    for (const std::string& l_strR : l_vRefs) l_setCats.insert(f_CatOf(l_strR));
    for (const Json& l_jC : m_oP.m_jS["cats"]) l_setCats.insert(f_Q(l_jC.get<std::string>()));
    {
        auto* l_pH = new QHBoxLayout;
        auto* l_pQe = new QLineEdit(m_oVq);
        l_pQe->setPlaceholderText(tr("Find: name, category, description, label…"));
        l_pQe->setClearButtonEnabled(true);
        connect(l_pQe, &QLineEdit::textChanged, this, [this](const QString& l_oS) { m_oVq = l_oS; f_ScheduleRender(); });
        l_pH->addWidget(l_pQe, 2);
        auto* l_pCat = new QComboBox;
        l_pCat->addItem(tr("All categories"), QString());
        for (const QString& l_oC : l_setCats) l_pCat->addItem(l_oC, l_oC);
        l_pCat->setCurrentIndex(std::max(0, l_pCat->findData(m_oVcat)));
        connect(l_pCat, &QComboBox::activated, this, [this, l_pCat](int) { m_oVcat = l_pCat->currentData().toString(); f_ScheduleRender(); });
        l_pH->addWidget(l_pCat, 1);
        for (const char* l_pszK : {"var", "counter", "flag"}) {
            auto* l_pC = new QToolButton;
            l_pC->setCheckable(true);
            l_pC->setChecked(m_setVtypes.count(l_pszK) > 0);
            l_pC->setText(f_Icon(l_pszK) + QStringLiteral(" ") + (std::string(l_pszK) == "var" ? tr("event variable") : std::string(l_pszK) == "counter" ? tr("counter") : tr("flag")));
            const std::string l_strKk = l_pszK;
            connect(l_pC, &QToolButton::clicked, this, [this, l_strKk] { if (m_setVtypes.count(l_strKk)) m_setVtypes.erase(l_strKk); else m_setVtypes.insert(l_strKk); f_ScheduleRender(); });
            l_pH->addWidget(l_pC);
        }
        l_pV->addLayout(l_pH);
        // keep the caret in the search box after the re-render
        if (!m_oVq.isEmpty()) QTimer::singleShot(0, l_pQe, [l_pQe] { l_pQe->setFocus(); l_pQe->setCursorPosition(l_pQe->text().size()); });
    }
    // a new variable
    {
        auto* l_pH = new QHBoxLayout;
        auto* l_pName = new QLineEdit;
        l_pName->setPlaceholderText(tr("name, e.g. well_clues"));
        auto* l_pCat = new QComboBox;
        l_pCat->setEditable(true);
        for (const QString& l_oC : l_setCats) l_pCat->addItem(l_oC);
        l_pCat->setEditText(m_oVcat);
        l_pCat->lineEdit()->setPlaceholderText(tr("e.g. village/clues"));
        auto* l_pDef = new QSpinBox;
        l_pDef->setRange(-999999, 999999);
        auto* l_pAdd = new QPushButton(tr("Add variable"));
        l_pAdd->setDefault(true);
        auto* l_pErr = new QLabel;
        l_pErr->setStyleSheet(QStringLiteral("color:#ff5d5d"));
        auto l_fnDoAdd = [=] {
            const std::string l_strN = l_pName->text().trimmed().toStdString();
            if (!stage::f_NameOk(l_strN)) { l_pErr->setText(tr("Any name works, Chinese too, except empty, spaces at the ends, or \" ' < > & | \\ /")); return; }
            if (m_oP.m_jS["vars"].contains(l_strN) || m_oP.m_jS["counters"].contains(l_strN) || m_oP.m_jS["flags"].contains(l_strN)) { l_pErr->setText(tr("%1 already exists").arg(f_Q(l_strN))); return; }
            const std::string l_strC = l_pCat->currentText().trimmed().toStdString();
            const int l_iD = l_pDef->value();
            f_Edit(tr("New variable %1").arg(f_Q(l_strN)), [this, l_strN, l_strC, l_iD] {
                m_oP.m_jS["vars"][l_strN] = Json{{"type", "int"}, {"default", l_iD}, {"scope", "run"}};
                if (!l_strC.empty()) m_oP.m_jS["vars"][l_strN]["category"] = l_strC;
            });
        };
        connect(l_pAdd, &QPushButton::clicked, this, l_fnDoAdd);
        connect(l_pName, &QLineEdit::returnPressed, this, l_fnDoAdd);
        l_pH->addWidget(l_pName, 2);
        l_pH->addWidget(new QLabel(tr("Category")));
        l_pH->addWidget(l_pCat, 2);
        l_pH->addWidget(new QLabel(tr("Default")));
        l_pH->addWidget(l_pDef);
        l_pH->addWidget(l_pAdd);
        l_pH->addWidget(l_pErr, 2);
        l_pV->addLayout(l_pH);
    }
    // categories: create, rename, delete
    {
        auto* l_pH = new QHBoxLayout;
        auto* l_pCe = new QComboBox;
        l_pCe->setEditable(true);
        for (const QString& l_oC : l_setCats) l_pCe->addItem(l_oC);
        l_pCe->setEditText(m_oVcat);
        l_pCe->lineEdit()->setPlaceholderText(tr("category, e.g. village/clues"));
        auto* l_pCreate = new QPushButton(tr("+ Create category"));
        auto* l_pRename = new QPushButton(tr("✎ Rename category"));
        auto* l_pDel = new QPushButton(tr("🗑 Delete category"));
        connect(l_pCreate, &QPushButton::clicked, this, [=] {
            const QString l_oC = f_CleanCat(l_pCe->currentText());
            if (!f_CatNameOk(l_oC)) { f_Toast(tr("Type a name (any characters, Chinese too, except \" < > & | \\)."), true); return; }
            if (l_setCats.count(l_oC)) { f_Toast(tr("%1 already exists").arg(l_oC), true); return; }
            f_Edit(tr("New category %1").arg(l_oC), [this, l_oC] {
                m_oP.m_jS["cats"].push_back(l_oC.toStdString());
                std::vector<std::string> l_vL = m_oP.m_jS["cats"].get<std::vector<std::string>>();
                std::sort(l_vL.begin(), l_vL.end());
                m_oP.m_jS["cats"] = l_vL;
            });
            m_oVcat = l_oC;
        });
        // the values in a category and its sub-categories
        auto l_fnMembers = [this](const QString& l_oC) {
            std::vector<std::string> l_vUsers;
            for (const std::string& l_strR : m_oP.f_ValueRefs()) {
                const Json* l_pRec = m_oP.f_RecOf(l_strR);
                const QString l_oOwn = l_pRec && l_pRec->contains("category") ? f_Q((*l_pRec)["category"].get<std::string>()) : QString();
                if (!l_oOwn.isEmpty() && (l_oOwn == l_oC || l_oOwn.startsWith(l_oC + QLatin1Char('/')))) l_vUsers.push_back(l_strR);
            }
            return l_vUsers;
        };
        connect(l_pRename, &QPushButton::clicked, this, [=] {
            const QString l_oC = f_CleanCat(l_pCe->currentText());
            if (l_oC.isEmpty()) { f_Toast(tr("Type or pick a category to delete."), true); return; }
            bool l_bOk = false;
            const QString l_oNu = f_CleanCat(QInputDialog::getText(this, tr("Rename the category \"%1\"").arg(l_oC),
                tr("Every variable, counter and flag in it (and in its sub-categories) moves to the new name. Use / for a sub-category, e.g. village/clues. A name that already exists merges the two."),
                QLineEdit::Normal, l_oC, &l_bOk));
            if (!l_bOk || l_oNu.isEmpty() || l_oNu == l_oC) return;
            if (!f_CatNameOk(l_oNu)) { f_Toast(tr("Type a name (any characters, Chinese too, except \" < > & | \\)."), true); return; }
            const auto l_vUsers = l_fnMembers(l_oC);
            f_Edit(tr("Rename category %1 → %2").arg(l_oC, l_oNu), [this, l_vUsers, l_oC, l_oNu] {
                for (const std::string& l_strR : l_vUsers) {
                    Json* l_pRec = m_oP.f_RecOf(l_strR);
                    const QString l_oOwn = f_Q((*l_pRec)["category"].get<std::string>());
                    (*l_pRec)["category"] = (l_oNu + l_oOwn.mid(l_oC.size())).toStdString();
                }
                std::set<std::string> l_setNc;
                for (const Json& l_jX : m_oP.m_jS["cats"]) {
                    const QString l_oS = f_Q(l_jX.get<std::string>());
                    l_setNc.insert((l_oS == l_oC || l_oS.startsWith(l_oC + QLatin1Char('/')) ? l_oNu + l_oS.mid(l_oC.size()) : l_oS).toStdString());
                }
                m_oP.m_jS["cats"] = std::vector<std::string>(l_setNc.begin(), l_setNc.end());
            });
            if (m_oVcat == l_oC) m_oVcat = l_oNu;
        });
        connect(l_pDel, &QPushButton::clicked, this, [=] {
            const QString l_oC = f_CleanCat(l_pCe->currentText());
            if (l_oC.isEmpty()) { f_Toast(tr("Type or pick a category to delete."), true); return; }
            const auto l_vUsers = l_fnMembers(l_oC);
            std::vector<std::string> l_vCreated;
            for (const Json& l_jX : m_oP.m_jS["cats"]) { const QString l_oS = f_Q(l_jX.get<std::string>()); if (l_oS == l_oC || l_oS.startsWith(l_oC + QLatin1Char('/'))) l_vCreated.push_back(l_jX.get<std::string>()); }
            if (l_vUsers.empty() && l_vCreated.empty()) { f_Toast(l_setCats.count(l_oC) ? tr("%1 is an automatic group (from the data); it cannot be deleted.").arg(l_oC) : tr("There is no category %1.").arg(l_oC), true); return; }
            std::vector<std::string> l_vVars;
            for (const std::string& l_strR : l_vUsers) if (f_KindOf(l_strR) == "var") l_vVars.push_back(l_strR);
            QMessageBox l_oBox(this);
            l_oBox.setWindowTitle(tr("Delete the category \"%1\"").arg(l_oC));
            l_oBox.setText(l_vUsers.empty() ? tr("Nothing uses this category. Delete it?")
                                      : tr("\"%1\" and its sub-categories hold %2 event variable(s) and %3 counter(s) / flag(s).").arg(l_oC).arg(l_vVars.size()).arg(l_vUsers.size() - l_vVars.size()));
            QPushButton* l_pDelVars = l_vVars.empty() ? nullptr : l_oBox.addButton(tr("Delete %1 variable(s)").arg(l_vVars.size()), QMessageBox::DestructiveRole);
            QPushButton* l_pUncat = l_oBox.addButton(l_vUsers.empty() ? tr("🗑 Delete category") : tr("Make uncategorized"), QMessageBox::AcceptRole);
            l_oBox.addButton(QMessageBox::Cancel);
            l_oBox.exec();
            const bool l_bKillVars = l_pDelVars && l_oBox.clickedButton() == l_pDelVars;
            if (!l_bKillVars && l_oBox.clickedButton() != l_pUncat) return;
            f_Edit(l_bKillVars ? tr("Delete category %1 and %2 variable(s)").arg(l_oC).arg(l_vVars.size()) : tr("Delete category %1").arg(l_oC), [this, l_vUsers, l_vVars, l_vCreated, l_bKillVars] {
                for (const std::string& l_strR : l_vUsers) {
                    if (l_bKillVars && std::find(l_vVars.begin(), l_vVars.end(), l_strR) != l_vVars.end()) m_oP.m_jS["vars"].erase(f_NameOf(l_strR));
                    else if (Json* l_pRec = m_oP.f_RecOf(l_strR)) l_pRec->erase("category");
                }
                Json l_jKeep = Json::array();
                for (const Json& l_jX : m_oP.m_jS["cats"]) if (std::find(l_vCreated.begin(), l_vCreated.end(), l_jX.get<std::string>()) == l_vCreated.end()) l_jKeep.push_back(l_jX);
                m_oP.m_jS["cats"] = l_jKeep;
            });
            if (m_oVcat == l_oC || m_oVcat.startsWith(l_oC + QLatin1Char('/'))) m_oVcat.clear();
        });
        l_pH->addWidget(new QLabel(QStringLiteral("📁")));
        l_pH->addWidget(l_pCe, 2);
        l_pH->addWidget(l_pCreate);
        l_pH->addWidget(l_pRename);
        l_pH->addWidget(l_pDel);
        l_pH->addStretch(1);
        l_pV->addLayout(l_pH);
    }
    // the list
    const QString l_oQq = m_oVq.trimmed().toLower();
    auto l_fnMatches = [&](const std::string& l_strR) {
        if (!m_setVtypes.count(f_KindOf(l_strR))) return false;
        const QString l_oC = f_CatOf(l_strR);
        if (!m_oVcat.isEmpty() && l_oC != m_oVcat && !l_oC.startsWith(m_oVcat + QLatin1Char('/'))) return false;
        if (l_oQq.isEmpty()) return true;
        const Json* l_pRec = m_oP.f_RecOf(l_strR);
        QStringList l_oHay{f_Q(f_NameOf(l_strR)), l_oC};
        if (l_pRec) for (const char* l_pszK : {"desc", "label"}) if (l_pRec->contains(l_pszK)) { const Json& l_jX = (*l_pRec)[l_pszK]; if (l_jX.is_object()) { l_oHay << f_Q(l_jX.value("zh_TW", std::string())) << f_Q(l_jX.value("en", std::string())); } else if (l_jX.is_string()) l_oHay << f_Q(l_jX.get<std::string>()); }
        if (l_pRec && l_pRec->contains("setBy")) l_oHay << f_Q((*l_pRec)["setBy"].get<std::string>());
        for (const QString& l_oS : l_oHay) if (l_oS.toLower().contains(l_oQq)) return true;
        return false;
    };
    std::vector<std::string> l_vShown;
    for (const std::string& l_strR : l_vRefs) if (l_fnMatches(l_strR)) l_vShown.push_back(l_strR);
    static const std::map<std::string, int> l_mapTord = {{"var", 0}, {"counter", 1}, {"flag", 2}};
    std::stable_sort(l_vShown.begin(), l_vShown.end(), [&](const std::string& l_strA, const std::string& l_strB) {
        if (!m_oVcat.isEmpty() && f_CatOf(l_strA) != f_CatOf(l_strB)) return f_CatOf(l_strA) < f_CatOf(l_strB);
        if (l_mapTord.at(f_KindOf(l_strA)) != l_mapTord.at(f_KindOf(l_strB))) return l_mapTord.at(f_KindOf(l_strA)) < l_mapTord.at(f_KindOf(l_strB));
        return f_NameOf(l_strA) < f_NameOf(l_strB);
    });
    for (auto l_itIt = m_setVarSel.begin(); l_itIt != m_setVarSel.end();) l_itIt = std::find(l_vRefs.begin(), l_vRefs.end(), *l_itIt) == l_vRefs.end() ? m_setVarSel.erase(l_itIt) : std::next(l_itIt);
    if (!m_setVarSel.empty()) {   // several values into one category at once
        auto* l_pH = new QHBoxLayout;
        l_pH->addWidget(f_Rich(QStringLiteral("<b>%1</b>").arg(tr("%1 selected").arg(m_setVarSel.size()))));
        l_pH->addWidget(new QLabel(tr("Move to category")));
        auto* l_pBc = new QComboBox;
        l_pBc->setEditable(true);
        for (const QString& l_oC : l_setCats) l_pBc->addItem(l_oC);
        l_pBc->setEditText(m_oVcat);
        auto l_fnApply = [=](const QString& l_oCat) {
            const QString l_oC = f_CleanCat(l_oCat);
            if (!l_oC.isEmpty() && !f_CatNameOk(l_oC)) { f_Toast(tr("Type a name (any characters, Chinese too, except \" < > & | \\)."), true); return; }
            const std::vector<std::string> l_vSel(m_setVarSel.begin(), m_setVarSel.end());
            f_Edit(l_oC.isEmpty() ? tr("Take the category off %1 value(s)").arg(l_vSel.size()) : tr("Move %1 value(s) to category %2").arg(l_vSel.size()).arg(l_oC), [this, l_vSel, l_oC] {
                for (const std::string& l_strR : l_vSel) if (Json* l_pRec = m_oP.f_RecOf(l_strR)) { if (l_oC.isEmpty()) l_pRec->erase("category"); else (*l_pRec)["category"] = l_oC.toStdString(); }
            });
        };
        auto* l_pAp = new QPushButton(tr("Apply"));
        connect(l_pAp, &QPushButton::clicked, this, [=] { l_fnApply(l_pBc->currentText()); });
        auto* l_pUn = new QPushButton(tr("Make uncategorized"));
        connect(l_pUn, &QPushButton::clicked, this, [=] { l_fnApply(QString()); });
        auto* l_pCl = new QPushButton(tr("Clear selection"));
        connect(l_pCl, &QPushButton::clicked, this, [this] { m_setVarSel.clear(); f_ScheduleRender(); });
        l_pH->addWidget(l_pBc, 1);
        l_pH->addWidget(l_pAp);
        l_pH->addWidget(l_pUn);
        l_pH->addWidget(l_pCl);
        l_pV->addLayout(l_pH);
    }
    auto* l_pTable = new QTableWidget((int)l_vShown.size(), 8);
    l_pTable->setHorizontalHeaderLabels({QStringLiteral("✓"), tr("Name"), tr("Type"), tr("Category"), QString(), tr("Changed by"), tr("Read by"), QString()});
    l_pTable->verticalHeader()->hide();
    l_pTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    l_pTable->setSelectionMode(QAbstractItemView::NoSelection);
    auto l_fnChipsText = [](const std::vector<const Json*>& l_vL) {
        QStringList l_oS;
        for (const Json* l_pE : l_vL) l_oS << QString::fromStdString((*l_pE)["id"].get<std::string>());
        return l_oS.isEmpty() ? QStringLiteral("—") : l_oS.join(QStringLiteral(", "));
    };
    for (size_t l_nI = 0; l_nI < l_vShown.size(); l_nI++) {
        const std::string l_strRef = l_vShown[l_nI], l_strK = f_KindOf(l_strRef), l_strN = f_NameOf(l_strRef);
        const Json l_jRec = m_oP.f_RecOf(l_strRef) ? *m_oP.f_RecOf(l_strRef) : Json::object();
        auto* l_pPick = new QCheckBox;
        l_pPick->setChecked(m_setVarSel.count(l_strRef) > 0);
        connect(l_pPick, &QCheckBox::toggled, this, [this, l_strRef](bool l_bOn) { if (l_bOn) m_setVarSel.insert(l_strRef); else m_setVarSel.erase(l_strRef); f_ScheduleRender(); });
        l_pTable->setCellWidget((int)l_nI, 0, l_pPick);
        auto* l_pNb = new QToolButton;
        l_pNb->setText(f_Icon(l_strK) + QStringLiteral(" ") + f_Q(l_strN));
        l_pNb->setAutoRaise(true);
        l_pNb->setToolTip(tr("Click to open it in a tab."));
        connect(l_pNb, &QToolButton::clicked, this, [this, l_strRef] { f_OpenValue(l_strRef); });
        l_pTable->setCellWidget((int)l_nI, 1, l_pNb);
        l_pTable->setItem((int)l_nI, 2, new QTableWidgetItem(l_strK == "var" ? tr("event variable") : l_strK == "counter" ? tr("counter") : tr("flag")));
        auto* l_pCe = new CommitLine(l_jRec.contains("category") ? f_Q(l_jRec["category"].get<std::string>()) : QString());
        l_pCe->setPlaceholderText(f_CatOf(l_strRef));
        l_pCe->m_fnOnCommit = [this, l_strRef, l_strN](const QString& l_oS) {
            const std::string l_strC = l_oS.trimmed().toStdString();
            f_Edit(tr("Category of %1").arg(f_Q(l_strN)), [this, l_strRef, l_strC] { if (Json* l_pR = m_oP.f_RecOf(l_strRef)) { if (l_strC.empty()) l_pR->erase("category"); else (*l_pR)["category"] = l_strC; } });
        };
        l_pTable->setCellWidget((int)l_nI, 3, l_pCe);
        if (l_strK == "var") {
            auto* l_pCell = new QWidget;
            auto* l_pH = new QHBoxLayout(l_pCell);
            l_pH->setContentsMargins(2, 0, 2, 0);
            auto* l_pD = new QSpinBox;
            l_pD->setRange(-999999, 999999);
            l_pD->setValue(l_jRec.value("default", Json(0)).is_number() ? l_jRec["default"].get<int>() : l_jRec.value("default", false) == true ? 1 : 0);
            l_pD->setKeyboardTracking(false);
            connect(l_pD, &QSpinBox::valueChanged, this, [this, l_strN](int l_iX) { f_Edit(tr("Variable %1").arg(f_Q(l_strN)), [this, l_strN, l_iX] { m_oP.m_jS["vars"][l_strN]["default"] = l_iX; }); });
            auto* l_pSc = new QComboBox;
            l_pSc->addItems({QStringLiteral("run"), QStringLiteral("meta")});
            l_pSc->setCurrentText(l_jRec.value("scope", std::string("run")) == "meta" ? QStringLiteral("meta") : QStringLiteral("run"));
            connect(l_pSc, &QComboBox::activated, this, [this, l_strN, l_pSc](int) { const std::string l_strS = l_pSc->currentText().toStdString(); f_Edit(tr("Variable %1").arg(f_Q(l_strN)), [this, l_strN, l_strS] { m_oP.m_jS["vars"][l_strN]["scope"] = l_strS; }); });
            l_pH->addWidget(l_pD);
            l_pH->addWidget(l_pSc);
            l_pTable->setCellWidget((int)l_nI, 4, l_pCell);
            auto* l_pDel = new QToolButton;
            l_pDel->setText(tr("Delete"));
            connect(l_pDel, &QToolButton::clicked, this, [this, l_strN] { f_Edit(tr("Delete variable %1").arg(f_Q(l_strN)), [this, l_strN] { m_oP.m_jS["vars"].erase(l_strN); }); });
            l_pTable->setCellWidget((int)l_nI, 7, l_pDel);
        } else if (l_strK == "counter") {
            QStringList l_oCl;
            if (l_jRec.contains("clamp")) for (const Json& l_jX : l_jRec["clamp"]) l_oCl << f_Q(l_jX.dump());
            l_pTable->setItem((int)l_nI, 4, new QTableWidgetItem(f_L(l_jRec.value("label", Json())) + QStringLiteral(" · ") + l_oCl.join(QStringLiteral(" … "))));
        } else {
            l_pTable->setItem((int)l_nI, 4, new QTableWidgetItem(f_Q(l_jRec.value("scope", std::string())) + QStringLiteral(" · ") + f_Q(l_jRec.value("setBy", std::string()))));
        }
        l_pTable->setItem((int)l_nI, 5, new QTableWidgetItem(l_fnChipsText(m_oP.f_Changers(l_strRef))));
        l_pTable->setItem((int)l_nI, 6, new QTableWidgetItem(l_fnChipsText(m_oP.f_Readers(l_strRef))));
    }
    l_pTable->resizeColumnsToContents();
    l_pTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    l_pTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    l_pTable->setMinimumHeight(300);
    if (l_vShown.empty()) l_pV->addWidget(f_Muted(tr("No value matches the filter.")));
    l_pV->addWidget(l_pTable, 1);
    m_pInfo->setText(tr("%1 of %2 values").arg(l_vShown.size()).arg(l_vRefs.size()));
    return l_pW;
}

// ---- a value tab ---------------------------------------------------------------------------------------------------

QWidget* EventEditor::f_ValuePane(const std::string& l_strRef) {
    const std::string l_strK = f_KindOf(l_strRef), l_strN = f_NameOf(l_strRef);
    Json* l_pRecP = m_oP.f_RecOf(l_strRef);
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    if (!l_pRecP) { l_pV->addWidget(f_Muted(tr("This value no longer exists."))); return l_pW; }
    const Json l_jRec = *l_pRecP;
    const QString l_oFile = l_strK == "var" ? QStringLiteral("story/vars.json") : l_strK == "counter" ? QStringLiteral("story/counters.json") : QStringLiteral("story/flags.json");
    const QString l_oTypeName = l_strK == "var" ? tr("event variable") : l_strK == "counter" ? tr("counter") : tr("flag");
    auto* l_pTop = new QHBoxLayout;
    auto* l_pFormW = new QWidget;
    auto* l_pForm = new QFormLayout(l_pFormW);
    const QString l_oHEdit = tr("Edit %1").arg(f_Q(l_strN));
    auto l_fnUpd = [this, l_strRef, l_oHEdit](const std::function<void(Json&)>& l_fnFn) { f_Edit(l_oHEdit, [this, l_strRef, l_fnFn] { if (Json* l_pR = m_oP.f_RecOf(l_strRef)) l_fnFn(*l_pR); }); };
    l_pForm->addRow(f_Rich(QStringLiteral("<span style='font-size:15px;font-weight:600'>%1 %2</span> <span style='color:#8b929c'>%3</span>").arg(f_Icon(l_strK), f_Esc(f_Q(l_strN)), l_oTypeName)));
    if (l_strK == "var") {
        auto* l_pName = new CommitLine(f_Q(l_strN));
        auto* l_pErr = new QLabel;
        l_pErr->setStyleSheet(QStringLiteral("color:#ff5d5d"));
        l_pName->m_fnOnCommit = [=](const QString& l_oS) {
            const std::string l_strNu = l_oS.trimmed().toStdString();
            if (l_strNu == l_strN) return;
            if (!stage::f_NameOk(l_strNu) || m_oP.m_jS["vars"].contains(l_strNu) || m_oP.m_jS["counters"].contains(l_strNu) || m_oP.m_jS["flags"].contains(l_strNu)) {
                l_pErr->setText(tr("Use a name not in use; any characters work, Chinese too, except \" ' < > & | \\ / and spaces at the ends"));
                return;
            }
            f_Edit(tr("Rename variable %1 → %2").arg(f_Q(l_strN), f_Q(l_strNu)), [this, l_strN, l_strNu] {
                Json l_jVars = Json::object();
                for (auto& [l_strKk, l_jVv] : m_oP.m_jS["vars"].items()) l_jVars[l_strKk == l_strN ? l_strNu : l_strKk] = l_jVv;
                m_oP.m_jS["vars"] = l_jVars;
                for (Json& l_jE : m_oP.m_jS["events"]) {
                    for (Json& l_jA : l_jE["actions"]) if (l_jA.value("var", std::string()) == l_strN) l_jA["var"] = l_strNu;
                    for (Json& l_jL : l_jE["req"]) if (l_jL.value("var", std::string()) == l_strN) l_jL["var"] = l_strNu;
                    if (l_jE["trigger"].value("on", std::string()) == "varChanged" && l_jE["trigger"].value("var", std::string()) == l_strN) l_jE["trigger"]["var"] = l_strNu;
                }
            });
            m_oOpenVals.replaceInStrings(QStringLiteral("var:") + f_Q(l_strN), QStringLiteral("var:") + f_Q(l_strNu));
            m_oTab = QStringLiteral("val:var:") + f_Q(l_strNu);
        };
        l_pForm->addRow(tr("Name"), l_pName);
        l_pForm->addRow(QString(), l_pErr);
    } else {
        l_pForm->addRow(tr("Name"), f_Rich(QStringLiteral("<b>%1</b><br><span style='color:#8b929c;font-size:11px'>%2</span>").arg(f_Esc(f_Q(l_strN)),
            tr("Counters and flags are named in chapter and dialogue files and in the game code, so they are renamed there, not here."))));
    }
    auto* l_pCat = new CommitLine(l_jRec.contains("category") ? f_Q(l_jRec["category"].get<std::string>()) : QString());
    l_pCat->setPlaceholderText(f_CatOf(l_strRef));
    l_pCat->m_fnOnCommit = [=](const QString& l_oS) { const std::string l_strC = l_oS.trimmed().toStdString(); l_fnUpd([l_strC](Json& l_jR) { if (l_strC.empty()) l_jR.erase("category"); else l_jR["category"] = l_strC; }); };
    l_pForm->addRow(tr("Category"), l_pCat);
    auto l_fnLoc = [](const Json& l_jO, const char* l_pszL) { return l_jO.is_object() && l_jO.contains(l_pszL) && l_jO[l_pszL].is_string() ? QString::fromStdString(l_jO[l_pszL].get<std::string>()) : QString(); };
    auto l_fnScopeBox = [&] {
        auto* l_pSc = new QComboBox;
        l_pSc->addItems({QStringLiteral("run"), QStringLiteral("meta")});
        l_pSc->setCurrentText(l_jRec.value("scope", std::string("run")) == "meta" ? QStringLiteral("meta") : QStringLiteral("run"));
        connect(l_pSc, &QComboBox::activated, this, [=](int) { const std::string l_strS = l_pSc->currentText().toStdString(); l_fnUpd([l_strS](Json& l_jR) { l_jR["scope"] = l_strS; }); });
        return l_pSc;
    };
    if (l_strK == "var") {
        auto* l_pType = new QComboBox;
        l_pType->addItems({QStringLiteral("int"), QStringLiteral("bool")});
        l_pType->setCurrentText(l_jRec.value("type", std::string("int")) == "bool" ? QStringLiteral("bool") : QStringLiteral("int"));
        connect(l_pType, &QComboBox::activated, this, [=](int) { const std::string l_strT = l_pType->currentText().toStdString(); l_fnUpd([l_strT](Json& l_jR) { l_jR["type"] = l_strT; }); });
        l_pForm->addRow(tr("Type"), l_pType);
        auto* l_pD = new QSpinBox;
        l_pD->setRange(-999999, 999999);
        l_pD->setKeyboardTracking(false);
        l_pD->setValue(l_jRec.value("default", Json(0)).is_number() ? l_jRec["default"].get<int>() : 0);
        connect(l_pD, &QSpinBox::valueChanged, this, [=](int l_iX) { l_fnUpd([l_iX](Json& l_jR) { l_jR["default"] = l_iX; }); });
        l_pForm->addRow(tr("Default"), l_pD);
        l_pForm->addRow(tr("Scope"), l_fnScopeBox());
        for (const char* l_pszL : {"zh_TW", "en"}) {
            auto* l_pT = new CommitLine(l_fnLoc(l_jRec.value("desc", Json()), l_pszL));
            const std::string l_strLang = l_pszL;
            l_pT->m_fnOnCommit = [=](const QString& l_oS) {
                l_fnUpd([l_strLang, l_oS](Json& l_jR) {
                    Json l_jDd = l_jR.contains("desc") && l_jR["desc"].is_object() ? l_jR["desc"] : Json::object();
                    l_jDd[l_strLang] = l_oS.toStdString();
                    if (l_jDd.value("zh_TW", std::string()).empty() && l_jDd.value("en", std::string()).empty()) l_jR.erase("desc"); else l_jR["desc"] = l_jDd;
                });
            };
            l_pForm->addRow(tr("Description") + QStringLiteral(" · ") + QString::fromLatin1(l_pszL), l_pT);
        }
    }
    if (l_strK == "counter") {
        for (const char* l_pszL : {"zh_TW", "en"}) {
            auto* l_pT = new CommitLine(l_fnLoc(l_jRec.value("label", Json()), l_pszL));
            const std::string l_strLang = l_pszL;
            l_pT->m_fnOnCommit = [=](const QString& l_oS) {
                l_fnUpd([l_strLang, l_oS](Json& l_jR) { if (!l_jR.contains("label") || !l_jR["label"].is_object()) l_jR["label"] = Json::object(); l_jR["label"][l_strLang] = l_oS.toStdString(); });
            };
            l_pForm->addRow(tr("HUD label") + QStringLiteral(" · ") + QString::fromLatin1(l_pszL), l_pT);
        }
        auto* l_pDisp = new CommitLine(l_jRec.contains("displayAt") ? f_Q(l_jRec["displayAt"].dump()) : QString());
        l_pDisp->m_fnOnCommit = [=](const QString& l_oS) { bool l_bOk; const int l_iX = l_oS.toInt(&l_bOk); l_fnUpd([l_bOk, l_iX](Json& l_jR) { if (l_bOk) l_jR["displayAt"] = l_iX; else l_jR.erase("displayAt"); }); };
        l_pForm->addRow(tr("Show on HUD at"), l_pDisp);
        auto* l_pClampRow = new QWidget;
        auto* l_pCh = new QHBoxLayout(l_pClampRow);
        l_pCh->setContentsMargins(0, 0, 0, 0);
        const Json l_jCl = l_jRec.value("clamp", Json::array());
        auto* l_pMn = new CommitLine(l_jCl.size() > 0 && !l_jCl[0].is_null() ? f_Q(l_jCl[0].dump()) : QString());
        auto* l_pMx = new CommitLine(l_jCl.size() > 1 && !l_jCl[1].is_null() ? f_Q(l_jCl[1].dump()) : QString());
        auto l_fnSetClamp = [=](const QString&) {
            bool l_bA, l_bB;
            const int l_iX = l_pMn->text().toInt(&l_bA), l_iY = l_pMx->text().toInt(&l_bB);
            l_fnUpd([=](Json& l_jR) { l_jR["clamp"] = Json::array({l_bA ? Json(l_iX) : Json(), l_bB ? Json(l_iY) : Json()}); });
        };
        l_pMn->m_fnOnCommit = l_fnSetClamp;
        l_pMx->m_fnOnCommit = l_fnSetClamp;
        l_pCh->addWidget(l_pMn);
        l_pCh->addWidget(new QLabel(QStringLiteral("…")));
        l_pCh->addWidget(l_pMx);
        l_pForm->addRow(tr("Clamp (min … max)"), l_pClampRow);
    }
    if (l_strK == "flag") {
        l_pForm->addRow(tr("Scope"), l_fnScopeBox());
        auto* l_pSb = new CommitLine(f_Q(l_jRec.value("setBy", std::string())));
        l_pSb->m_fnOnCommit = [=](const QString& l_oS) { const std::string l_strX = l_oS.toStdString(); l_fnUpd([l_strX](Json& l_jR) { l_jR["setBy"] = l_strX; }); };
        l_pForm->addRow(tr("Set by (story data)"), l_pSb);
    }
    l_pForm->addRow(f_Muted(tr("Saved in %1").arg(l_oFile)));
    auto* l_pBtns = new QHBoxLayout;
    auto* l_pSv = new QPushButton(tr("Save…"));
    connect(l_pSv, &QPushButton::clicked, this, [this] { f_Save(); });
    l_pBtns->addWidget(l_pSv);
    if (l_strK == "var") {
        auto* l_pDel = new QPushButton(tr("Delete variable"));
        connect(l_pDel, &QPushButton::clicked, this, [this, l_strN, l_strRef] { f_Edit(tr("Delete variable %1").arg(f_Q(l_strN)), [this, l_strN] { m_oP.m_jS["vars"].erase(l_strN); }); f_CloseValue(l_strRef); });
        l_pBtns->addWidget(l_pDel);
    }
    auto* l_pCl = new QPushButton(tr("Close tab"));
    connect(l_pCl, &QPushButton::clicked, this, [this, l_strRef] { f_CloseValue(l_strRef); });
    l_pBtns->addWidget(l_pCl);
    l_pBtns->addStretch(1);
    l_pForm->addRow(l_pBtns);
    l_pTop->addWidget(l_pFormW, 1);
    // who changes, reads and watches it
    auto* l_pRight = new QWidget;
    auto* l_pRv = new QVBoxLayout(l_pRight);
    auto l_fnList = [&](const QString& l_oTitle, const std::vector<const Json*>& l_vEvs, const std::function<QString(const Json&)>& l_fnHow) {
        l_pRv->addWidget(f_Heading(l_oTitle + QStringLiteral(" (%1)").arg(l_vEvs.size())));
        if (l_vEvs.empty()) l_pRv->addWidget(f_Muted(QStringLiteral("—")));
        for (const Json* l_pE : l_vEvs) {
            auto* l_pH = new QHBoxLayout;
            const std::string l_strId = (*l_pE)["id"].get<std::string>();
            auto* l_pC = f_Chip(f_Q(l_strId));
            connect(l_pC, &QToolButton::clicked, this, [this, l_strId] { f_Select(l_strId); });
            l_pH->addWidget(l_pC);
            l_pH->addWidget(f_Muted(l_fnHow(*l_pE)), 1);
            l_pRv->addLayout(l_pH);
        }
    };
    l_fnList(tr("Changed by"), m_oP.f_Changers(l_strRef), [&](const Json& l_jE) { return f_HowWrite(l_jE, l_strRef, m_oP.m_jS["flags"]); });
    l_fnList(tr("Read by"), m_oP.f_ReadersOnly(l_strRef), [&](const Json& l_jE) { return f_HowRead(l_jE, l_strRef); });
    if (l_strK == "var") l_fnList(tr("Fires on change"), m_oP.f_Watchers(l_strRef), [this](const Json&) { return tr("on change"); });
    const auto l_vLinked = m_oP.f_LinkedValues(l_strRef);
    l_pRv->addWidget(f_Heading(tr("Linked values (through an event)") + QStringLiteral(" (%1)").arg(l_vLinked.size())));
    auto* l_pChips = new QWidget;
    auto* l_pFlow = new FlowLayout(l_pChips, 4);
    for (const std::string& l_strR : l_vLinked) {
        auto* l_pC = f_Chip(f_Icon(f_KindOf(l_strR)) + QStringLiteral(" ") + f_Q(f_NameOf(l_strR)));
        connect(l_pC, &QToolButton::clicked, this, [this, l_strR] { f_OpenValue(l_strR); });
        l_pFlow->addWidget(l_pC);
    }
    if (l_vLinked.empty()) l_pFlow->addWidget(f_Muted(QStringLiteral("—")));
    l_pRv->addWidget(l_pChips);
    l_pRv->addStretch(1);
    l_pTop->addWidget(l_pRight, 1);
    l_pV->addLayout(l_pTop);
    m_pInfo->setText(QStringLiteral("%1 · ✎ %2 · 👁 %3 · ⇄ %4").arg(l_oTypeName).arg(m_oP.f_Changers(l_strRef).size()).arg(m_oP.f_ReadersOnly(l_strRef).size() + m_oP.f_Watchers(l_strRef).size()).arg(l_vLinked.size()));
    return l_pW;
}

// ---- kinds ---------------------------------------------------------------------------------------------------------

QWidget* EventEditor::f_KindsPane() {
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    l_pV->addWidget(f_Muted(tr("Every event has a kind. This list is saved in assets/data/events/kinds.json; the kind drop-down, the filter and the colours in every view come from it.")));
    std::vector<std::string> l_vKinds = m_oP.f_KindsList();
    std::vector<std::string> l_vUnknown;
    for (const Json& l_jE : m_oP.m_jS["events"]) {
        const std::string l_strK = l_jE["kind"].get<std::string>();
        if (!l_strK.empty() && !m_oP.m_jS["kinds"].contains(l_strK) && std::find(l_vUnknown.begin(), l_vUnknown.end(), l_strK) == l_vUnknown.end()) l_vUnknown.push_back(l_strK);
    }
    std::vector<std::string> l_vRows = l_vKinds;
    l_vRows.insert(l_vRows.end(), l_vUnknown.begin(), l_vUnknown.end());
    auto* l_pTable = new QTableWidget((int)l_vRows.size() + 1, 8);
    l_pTable->setHorizontalHeaderLabels({QString(), tr("Kind id"), tr("Name") + QStringLiteral(" · zh_TW"), QStringLiteral("en"), tr("Description"), tr("In log"), tr("Events"), QString()});
    l_pTable->verticalHeader()->hide();
    l_pTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    l_pTable->setSelectionMode(QAbstractItemView::NoSelection);
    auto l_fnUsers = [this](const std::string& l_strK) {
        std::vector<std::string> l_vOut;
        for (const Json& l_jE : m_oP.m_jS["events"]) if (l_jE["kind"] == l_strK) l_vOut.push_back(l_jE["id"].get<std::string>());
        return l_vOut;
    };
    for (size_t l_nI = 0; l_nI < l_vRows.size(); l_nI++) {
        const std::string l_strK = l_vRows[l_nI];
        const bool l_bKnown = l_nI < l_vKinds.size();
        const std::vector<std::string> l_vU = l_fnUsers(l_strK);
        std::set<std::string>& l_setSel = m_mapKindSel[l_strK];
        for (auto l_itIt = l_setSel.begin(); l_itIt != l_setSel.end();) l_itIt = std::find(l_vU.begin(), l_vU.end(), *l_itIt) == l_vU.end() ? l_setSel.erase(l_itIt) : std::next(l_itIt);
        auto l_fnSetRec = [this, l_strK](const QString& l_oLabel, const std::function<void(Json&)>& l_fnFn) { f_Edit(l_oLabel, [this, l_strK, l_fnFn] { l_fnFn(m_oP.m_jS["kinds"][l_strK]); }); };
        if (l_bKnown) {
            const Json l_jR = m_oP.m_jS["kinds"][l_strK];
            auto* l_pCol = new QToolButton;
            const QColor l_oC(f_Q(l_jR.value("color", std::string("#8b929c"))));
            l_pCol->setStyleSheet(QStringLiteral("background:%1; min-width:22px").arg(l_oC.name()));
            l_pCol->setToolTip(tr("Colour in the editors"));
            connect(l_pCol, &QToolButton::clicked, this, [=] {
                const QColor l_oNc = QColorDialog::getColor(l_oC, this, tr("Colour in the editors"));
                if (l_oNc.isValid()) l_fnSetRec(tr("Edit kind %1").arg(f_Q(l_strK)), [l_oNc](Json& l_jX) { l_jX["color"] = l_oNc.name().toStdString(); });
            });
            l_pTable->setCellWidget((int)l_nI, 0, l_pCol);
            auto* l_pIdE = new CommitLine(f_Q(l_strK));
            l_pIdE->m_fnOnCommit = [=](const QString& l_oS) {
                const std::string l_strNu = l_oS.trimmed().toStdString();
                if (l_strNu == l_strK) return;
                if (!stage::f_NameOk(l_strNu)) { f_Toast(tr("Any name works, Chinese too, except empty, spaces at the ends, or \" ' < > & | \\ /"), true); return; }
                if (m_oP.m_jS["kinds"].contains(l_strNu)) { f_Toast(tr("%1 already exists").arg(f_Q(l_strNu)), true); return; }
                f_Edit(tr("Rename kind %1 → %2").arg(f_Q(l_strK), f_Q(l_strNu)), [this, l_strK, l_strNu] {
                    Json l_jKs = Json::object();
                    for (auto& [l_strKk, l_jVv] : m_oP.m_jS["kinds"].items()) l_jKs[l_strKk == l_strK ? l_strNu : l_strKk] = l_jVv;
                    m_oP.m_jS["kinds"] = l_jKs;
                    for (Json& l_jE : m_oP.m_jS["events"]) if (l_jE["kind"] == l_strK) l_jE["kind"] = l_strNu;
                });
                if (m_oKind == f_Q(l_strK)) m_oKind = f_Q(l_strNu);
                if (m_mapKindSel.count(l_strK)) { m_mapKindSel[l_strNu] = m_mapKindSel[l_strK]; m_mapKindSel.erase(l_strK); }
            };
            l_pTable->setCellWidget((int)l_nI, 1, l_pIdE);
            for (int l_iLi = 0; l_iLi < 2; l_iLi++) {
                const char* l_pszL = l_iLi ? "en" : "zh_TW";
                auto* l_pT = new CommitLine(l_jR.contains("name") ? f_Q(l_jR["name"].value(l_pszL, std::string())) : QString());
                const std::string l_strLang = l_pszL;
                l_pT->m_fnOnCommit = [=](const QString& l_oS) { l_fnSetRec(tr("Edit kind %1").arg(f_Q(l_strK)), [l_strLang, l_oS](Json& l_jX) { if (!l_jX.contains("name") || !l_jX["name"].is_object()) l_jX["name"] = Json::object(); l_jX["name"][l_strLang] = l_oS.trimmed().toStdString(); }); };
                l_pTable->setCellWidget((int)l_nI, 2 + l_iLi, l_pT);
            }
            auto* l_pDesc = new CommitLine(f_L(l_jR.value("desc", Json())));
            l_pDesc->setPlaceholderText(tr("what this kind of event does"));
            const std::string l_strLng = f_Lang().toStdString();
            l_pDesc->m_fnOnCommit = [=](const QString& l_oS) {
                l_fnSetRec(tr("Edit kind %1").arg(f_Q(l_strK)), [l_strLng, l_oS](Json& l_jX) {
                    const std::string l_strD = l_oS.trimmed().toStdString();
                    if (l_strD.empty()) l_jX.erase("desc");
                    else { if (!l_jX.contains("desc") || !l_jX["desc"].is_object()) l_jX["desc"] = Json::object(); l_jX["desc"][l_strLng] = l_strD; }
                });
            };
            l_pTable->setCellWidget((int)l_nI, 4, l_pDesc);
            auto* l_pLog = new QCheckBox;
            l_pLog->setChecked(!(l_jR.contains("inLog") && l_jR["inLog"] == false));
            l_pLog->setToolTip(tr("Events of this kind are listed in the player's event log"));
            connect(l_pLog, &QCheckBox::toggled, this, [=](bool l_bOn) { l_fnSetRec(tr("Event log of kind %1").arg(f_Q(l_strK)), [l_bOn](Json& l_jX) { if (l_bOn) l_jX.erase("inLog"); else l_jX["inLog"] = false; }); });
            l_pTable->setCellWidget((int)l_nI, 5, l_pLog);
        } else {
            auto* l_pAdd = new QPushButton(QStringLiteral("⚠ ") + f_Q(l_strK) + QStringLiteral(" · ") + tr("Add to the list"));
            l_pAdd->setToolTip(tr("used by %1 event(s) but not in this list").arg(l_vU.size()));
            connect(l_pAdd, &QPushButton::clicked, this, [this, l_strK] {
                f_Edit(tr("New kind %1").arg(f_Q(l_strK)), [this, l_strK] { m_oP.m_jS["kinds"][l_strK] = Json{{"name", Json{{"zh_TW", l_strK}, {"en", l_strK}}}, {"color", "#8b929c"}}; });
            });
            l_pTable->setCellWidget((int)l_nI, 1, l_pAdd);
        }
        // its events: tick some, move them to another kind
        auto* l_pEv = new QWidget;
        auto* l_pEh = new QHBoxLayout(l_pEv);
        l_pEh->setContentsMargins(2, 0, 2, 0);
        auto* l_pCnt = new QToolButton;
        l_pCnt->setText(QString::number(l_vU.size()));
        l_pCnt->setToolTip(tr("Show these events in the Events tree"));
        connect(l_pCnt, &QToolButton::clicked, this, [this, l_strK] { m_oKind = f_Q(l_strK); f_ScheduleRender(); });
        auto* l_pPick = new QComboBox;
        l_pPick->addItem(tr("%1 selected").arg(l_setSel.size()), QString());
        auto* l_pModel = new QStandardItemModel(l_pPick);
        auto* l_pHead = new QStandardItem(tr("%1 selected").arg(l_setSel.size()));
        l_pHead->setEnabled(false);
        l_pModel->appendRow(l_pHead);
        for (const std::string& l_strId : l_vU) {
            auto* l_pIt = new QStandardItem(f_Q(l_strId));
            l_pIt->setCheckable(true);
            l_pIt->setCheckState(l_setSel.count(l_strId) ? Qt::Checked : Qt::Unchecked);
            l_pIt->setData(f_Q(l_strId));
            l_pModel->appendRow(l_pIt);
        }
        l_pPick->setModel(l_pModel);
        connect(l_pModel, &QStandardItemModel::itemChanged, this, [this, l_strK](QStandardItem* l_pIt) {
            const std::string l_strId = l_pIt->data().toString().toStdString();
            if (l_strId.empty()) return;
            if (l_pIt->checkState() == Qt::Checked) m_mapKindSel[l_strK].insert(l_strId); else m_mapKindSel[l_strK].erase(l_strId);
        });
        auto* l_pAll = new QToolButton;
        l_pAll->setText(l_setSel.size() == l_vU.size() && !l_vU.empty() ? tr("Select none") : tr("Select all (%1)").arg(l_vU.size()));
        connect(l_pAll, &QToolButton::clicked, this, [this, l_strK, l_vU] {
            auto& l_setS = m_mapKindSel[l_strK];
            if (l_setS.size() == l_vU.size()) l_setS.clear(); else l_setS = std::set<std::string>(l_vU.begin(), l_vU.end());
            f_ScheduleRender();
        });
        l_pEh->addWidget(l_pCnt);
        l_pEh->addWidget(l_pPick, 1);
        l_pEh->addWidget(l_pAll);
        l_pTable->setCellWidget((int)l_nI, 6, l_pEv);
        if (!l_vU.empty()) {
            auto* l_pMv = new QComboBox;
            l_pMv->addItem(tr("Move selected to…"), QString());
            for (const std::string& l_strX : l_vKinds) if (l_strX != l_strK) l_pMv->addItem(f_Q(l_strX), f_Q(l_strX));
            connect(l_pMv, &QComboBox::activated, this, [this, l_pMv, l_strK](int) {
                const std::string l_strTo = l_pMv->currentData().toString().toStdString();
                const std::set<std::string> l_setIds = m_mapKindSel[l_strK];
                if (l_strTo.empty() || l_setIds.empty()) { f_Toast(tr("Tick events to move them")); return; }
                m_mapKindSel[l_strK].clear();
                m_mapKindSel[l_strTo].insert(l_setIds.begin(), l_setIds.end());
                f_Edit(tr("Move %1 event(s) from %2 to %3").arg(l_setIds.size()).arg(f_Q(l_strK), f_Q(l_strTo)), [this, l_setIds, l_strTo] { for (Json& l_jE : m_oP.m_jS["events"]) if (l_setIds.count(l_jE["id"].get<std::string>())) l_jE["kind"] = l_strTo; });
            });
            l_pTable->setCellWidget((int)l_nI, 7, l_pMv);
        } else if (l_bKnown) {
            auto* l_pDel = new QToolButton;
            l_pDel->setText(tr("Delete"));
            connect(l_pDel, &QToolButton::clicked, this, [this, l_strK] { f_Edit(tr("Delete kind %1").arg(f_Q(l_strK)), [this, l_strK] { m_oP.m_jS["kinds"].erase(l_strK); }); });
            l_pTable->setCellWidget((int)l_nI, 7, l_pDel);
        }
    }
    // a new kind
    {
        const int l_iR = (int)l_vRows.size();
        auto* l_pCol = new QToolButton;
        auto l_spColor = std::make_shared<QColor>(QColor("#8b929c"));
        l_pCol->setStyleSheet(QStringLiteral("background:#8b929c; min-width:22px"));
        connect(l_pCol, &QToolButton::clicked, this, [this, l_pCol, l_spColor] {
            const QColor l_oNc = QColorDialog::getColor(*l_spColor, this);
            if (l_oNc.isValid()) { *l_spColor = l_oNc; l_pCol->setStyleSheet(QStringLiteral("background:%1; min-width:22px").arg(l_oNc.name())); }
        });
        auto* l_pId = new QLineEdit;
        l_pId->setPlaceholderText(tr("new kind id, e.g. secret"));
        auto* l_pZh = new QLineEdit;
        l_pZh->setPlaceholderText(QStringLiteral("zh_TW"));
        auto* l_pEn = new QLineEdit;
        l_pEn->setPlaceholderText(QStringLiteral("en"));
        auto* l_pAdd = new QPushButton(tr("+ Add kind"));
        auto l_fnDoAdd = [=] {
            const std::string l_strK = l_pId->text().trimmed().toStdString();
            if (!stage::f_NameOk(l_strK)) { f_Toast(tr("Any name works, Chinese too, except empty, spaces at the ends, or \" ' < > & | \\ /"), true); return; }
            if (m_oP.m_jS["kinds"].contains(l_strK)) { f_Toast(tr("%1 already exists").arg(f_Q(l_strK)), true); return; }
            const std::string l_strZ = l_pZh->text().trimmed().toStdString(), l_strE = l_pEn->text().trimmed().toStdString(), l_strC = l_spColor->name().toStdString();
            f_Edit(tr("New kind %1").arg(f_Q(l_strK)), [this, l_strK, l_strZ, l_strE, l_strC] { m_oP.m_jS["kinds"][l_strK] = Json{{"name", Json{{"zh_TW", l_strZ.empty() ? l_strK : l_strZ}, {"en", l_strE.empty() ? l_strK : l_strE}}}, {"color", l_strC}}; });
        };
        connect(l_pAdd, &QPushButton::clicked, this, l_fnDoAdd);
        connect(l_pId, &QLineEdit::returnPressed, this, l_fnDoAdd);
        l_pTable->setCellWidget(l_iR, 0, l_pCol);
        l_pTable->setCellWidget(l_iR, 1, l_pId);
        l_pTable->setCellWidget(l_iR, 2, l_pZh);
        l_pTable->setCellWidget(l_iR, 3, l_pEn);
        l_pTable->setCellWidget(l_iR, 4, l_pAdd);
    }
    l_pTable->resizeColumnsToContents();
    l_pTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    l_pTable->setMinimumHeight(260);
    l_pV->addWidget(l_pTable, 1);
    l_pV->addWidget(f_Muted(tr("Renaming a kind updates every event of that kind. The floors' meta.eventKinds are notes left by the floor generator and are not changed. The game does not read kinds yet: trap and cache effects still come from words in the event id (06_EVENT_LOGIC.md).")));
    m_pInfo->setText(tr("%1 kinds · %2 events").arg(l_vKinds.size()).arg(m_oP.m_jS["events"].size()));
    return l_pW;
}
