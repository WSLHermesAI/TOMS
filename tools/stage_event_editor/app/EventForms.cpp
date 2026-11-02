// EventForms.cpp -- the event editor's inspector: an event, a floor, a missing id; the condition rows and action rows
// every editor shares (events, NPC chat lines, dialogue choices and start rows). As event_editor.html's renderForm,
// renderFloor, renderMissing, condHtml / bindCond, logicHtml / bindLogic.
#include "EventEditor.h"

#include "EventWidgets.h"
#include "FlowLayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using stage::Json;
using namespace EvUi;

namespace {
bool f_StartsWith(const std::string& l_strS, const char* l_pszP) { return l_strS.rfind(l_pszP, 0) == 0; }
std::string f_KindOf(const std::string& l_strS) { return l_strS.substr(0, l_strS.find(':')); }
std::string f_NameOf(const std::string& l_strS) { const size_t l_nC = l_strS.find(':'); return l_nC == std::string::npos ? std::string() : l_strS.substr(l_nC + 1); }

void f_AddHeader(QComboBox* l_pC, const QString& l_oText) {
    l_pC->addItem(l_oText, QStringLiteral("\x01"));
    if (auto* l_pM = qobject_cast<QStandardItemModel*>(l_pC->model())) {
        QStandardItem* l_pIt = l_pM->item(l_pC->count() - 1);
        l_pIt->setEnabled(false);
        QFont l_oF = l_pIt->font();
        l_oF.setBold(true);
        l_pIt->setFont(l_oF);
    }
}

QHBoxLayout* f_Row(QWidget* l_pLbl = nullptr) {
    auto* l_pH = new QHBoxLayout;
    l_pH->setContentsMargins(0, 0, 0, 0);
    if (l_pLbl) { l_pLbl->setMinimumWidth(130); l_pH->addWidget(l_pLbl); }
    return l_pH;
}

QSpinBox* f_Spin(int l_iV, int l_iLo = -999999, int l_iHi = 999999) {
    auto* l_pS = new QSpinBox;
    l_pS->setRange(l_iLo, l_iHi);
    l_pS->setValue(l_iV);
    l_pS->setKeyboardTracking(false);
    return l_pS;
}
}  // namespace

QString EventEditor::f_CatOf(const std::string& l_strRef) {
    const Json* l_pR = m_oP.f_RecOf(l_strRef);
    if (l_pR && l_pR->contains("category") && (*l_pR)["category"].is_string() && !(*l_pR)["category"].get<std::string>().empty()) return f_Q((*l_pR)["category"].get<std::string>());
    const std::string l_strK = f_KindOf(l_strRef);
    if (l_strK == "flag") {
        const std::string l_strSetBy = l_pR ? l_pR->value("setBy", std::string()) : std::string();
        const QRegularExpressionMatch l_oM = QRegularExpression(QStringLiteral("^(ch_\\d+)")).match(f_Q(l_strSetBy));
        return l_oM.hasMatch() ? tr("Chapter %1").arg(l_oM.captured(1)) : tr("Uncategorized");
    }
    return l_strK == "counter" ? tr("Story counters") : tr("Uncategorized");
}

QStringList EventEditor::f_ActionTargets(const std::string& l_strType) const {
    QStringList l_oOut;
    const Json& l_jS = m_oP.m_jS;
    auto l_fnKeys = [&](const char* l_pszK) { for (auto& [l_strN, l_jV] : l_jS[l_pszK].items()) { (void)l_jV; l_oOut << f_Q(l_strN); } };
    if (l_strType == "addVar" || l_strType == "setVar") l_fnKeys("vars");
    else if (l_strType == "addCounter") l_fnKeys("counters");
    else if (l_strType == "setFlag") l_fnKeys("flags");
    else if (l_strType == "addStat") for (const std::string& l_strS : m_oP.m_vStats) l_oOut << f_Q(l_strS);
    else for (const std::string& l_strI : m_oP.m_vItems) l_oOut << f_Q(l_strI);
    return l_oOut;
}

QComboBox* EventEditor::f_SubjectCombo(const std::string& l_strCur) {
    auto* l_pC = new QComboBox;
    l_pC->setMaxVisibleItems(24);
    auto l_fnAdd = [&](const std::string& l_strV, const QString& l_oLabel) { l_pC->addItem(l_oLabel, f_Q(l_strV)); if (l_strV == l_strCur) l_pC->setCurrentIndex(l_pC->count() - 1); };
    std::map<QString, std::vector<std::string>> l_mapVg;
    for (auto& [l_strV, l_jR] : m_oP.m_jS["vars"].items()) { (void)l_jR; l_mapVg[f_CatOf("var:" + l_strV)].push_back(l_strV); }
    for (const auto& [l_oCat, l_vList] : l_mapVg) { f_AddHeader(l_pC, tr("Event variables") + QStringLiteral(" · ") + l_oCat); for (const std::string& l_strV : l_vList) l_fnAdd("var:" + l_strV, f_Q(l_strV)); }
    auto l_fnGroup = [&](const QString& l_oTitle, const std::vector<std::string>& l_vIds, const char* l_pszPre) {
        if (l_vIds.empty()) return;
        f_AddHeader(l_pC, l_oTitle);
        for (const std::string& l_strId : l_vIds) l_fnAdd(l_pszPre + l_strId, f_Q(l_strId));
    };
    std::vector<std::string> l_vCounters, l_vFlags, l_vEvents, l_vFloors, l_vTalkers;
    for (auto& [l_strK, l_jV] : m_oP.m_jS["counters"].items()) { (void)l_jV; l_vCounters.push_back(l_strK); }
    for (auto& [l_strK, l_jV] : m_oP.m_jS["flags"].items()) { (void)l_jV; l_vFlags.push_back(l_strK); }
    for (const Json& l_jE : m_oP.m_jS["events"]) {
        l_vEvents.push_back(l_jE["id"].get<std::string>());
        if (l_jE["trigger"].value("on", std::string()) == "talk" && !l_jE["dialogue"].get<std::string>().empty()) l_vTalkers.push_back(l_jE["id"].get<std::string>());
    }
    for (const Json& l_jF : m_oP.m_jS["floors"]) l_vFloors.push_back(l_jF["id"].get<std::string>());
    l_fnGroup(tr("Counters"), l_vCounters, "counter:");
    l_fnGroup(tr("Flags"), l_vFlags, "flag:");
    l_fnGroup(tr("Events done"), l_vEvents, "event:");
    l_fnGroup(tr("Talks with (count)"), m_oP.f_NpcIds(), "talks:");
    l_fnGroup(tr("Status"), m_oP.m_vStats, "stat:");
    l_fnGroup(tr("Floor cleared"), l_vFloors, "floor:");
    l_fnGroup(tr("Item held"), m_oP.m_vItems, "item:");
    l_fnGroup(tr("Current stage"), l_vFloors, "stage:");
    l_fnGroup(tr("Choice made in"), l_vTalkers, "choice:");
    if (!l_strCur.empty() && l_pC->findData(f_Q(l_strCur)) < 0) { l_pC->addItem(QStringLiteral("⛔ ") + f_Q(l_strCur), f_Q(l_strCur)); l_pC->setCurrentIndex(l_pC->count() - 1); }
    return l_pC;
}

// ---- the condition editor ------------------------------------------------------------------------------------------

QWidget* EventEditor::f_CondBox(const QString& l_oKey, const std::function<Json()>& l_fnGet, const std::function<void(const Json&)>& l_fnSet,
                              const QString& l_oLabel, const QString& l_oNone) {
    auto* l_pBox = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pBox);
    l_pV->setContentsMargins(0, 0, 0, 0);
    l_pV->setSpacing(3);
    const Json l_jCond = l_fnGet();
    const stage::CondRows l_oCr = stage::f_CondToRows(l_jCond);
    const std::string l_strK = l_oKey.toStdString();
    if (l_oCr.m_bRaw || m_setCondJson.count(l_strK)) {   // nested logic, or the user's choice: JSON
        if (l_oCr.m_bRaw) l_pV->addWidget(f_Muted(tr("A condition the editor cannot show as rows (kept as written):")));
        auto* l_pH = f_Row();
        auto* l_pLine = new CommitLine(l_jCond.is_null() ? QString() : f_Q(l_jCond.dump()));
        l_pLine->setPlaceholderText(tr("JSON condition, e.g. {\"type\": \"talkCountAtLeast\", \"npc\": \"villager\", \"value\": 8} (empty = always)"));
        l_pLine->m_fnOnCommit = [this, l_fnSet, l_oLabel](const QString& l_oS) {
            if (l_oS.trimmed().isEmpty()) { f_Edit(l_oLabel, [l_fnSet] { l_fnSet(Json()); }); return; }
            const Json l_jJ = Json::parse(l_oS.toStdString(), nullptr, false);
            if (l_jJ.is_discarded()) { f_Toast(tr("Not valid JSON: the field was not changed."), true); return; }
            f_Edit(l_oLabel, [l_fnSet, l_jJ] { l_fnSet(l_jJ); });
        };
        l_pH->addWidget(l_pLine, 1);
        if (!l_oCr.m_bRaw) {
            auto* l_pBack = new QToolButton;
            l_pBack->setText(tr("back to rows"));
            connect(l_pBack, &QToolButton::clicked, this, [this, l_strK] { m_setCondJson.erase(l_strK); f_ScheduleRender(); });
            l_pH->addWidget(l_pBack);
        }
        l_pV->addLayout(l_pH);
        return l_pBox;
    }
    const std::vector<Json> l_vRows = l_oCr.m_vRows;
    const std::string l_strMode = l_oCr.m_strMode;
    auto l_fnWrite = [this, l_fnSet, l_oLabel](const std::string& l_strM, const std::vector<Json>& l_vRs) { f_Edit(l_oLabel, [l_fnSet, l_strM, l_vRs] { l_fnSet(stage::f_RowsToCond(l_strM, l_vRs)); }); };
    for (size_t l_nI = 0; l_nI < l_vRows.size(); l_nI++) {
        const stage::CondRow l_oR = stage::f_LeafToRow(l_vRows[l_nI]);
        const std::string l_strKk = f_KindOf(l_oR.m_strSubj);
        auto* l_pH = f_Row();
        QComboBox* l_pSubj = f_SubjectCombo(l_oR.m_strSubj);
        auto* l_pOp = new QComboBox;
        for (const auto& [l_strO, l_strLb] : stage::f_OpsFor(l_strKk)) {
            static const std::map<std::string, const char*> l_mapOpNames = {
                {"opSet", QT_TR_NOOP("is set")}, {"opNotSet", QT_TR_NOOP("is not set")}, {"opDone", QT_TR_NOOP("is done")}, {"opNotDone", QT_TR_NOOP("is not done")},
                {"opCleared", QT_TR_NOOP("is cleared")}, {"opNotCleared", QT_TR_NOOP("is not cleared")}, {"opOnStage", QT_TR_NOOP("is the current stage")},
                {"opNotOnStage", QT_TR_NOOP("is not the current stage")}, {"opChose", QT_TR_NOOP("chose")}};
            auto l_itIt = l_mapOpNames.find(l_strLb);
            l_pOp->addItem(l_itIt != l_mapOpNames.end() ? tr(l_itIt->second) : f_Q(l_strLb), f_Q(l_strO));
        }
        l_pOp->setCurrentIndex(std::max(0, l_pOp->findData(f_Q(l_oR.m_strOp))));
        l_pH->addWidget(l_pSubj, 2);
        l_pH->addWidget(l_pOp);
        QSpinBox* l_pVal = nullptr;
        QComboBox* l_pChoice = nullptr;
        if (l_oR.m_strOp != "set" && l_oR.m_strOp != "not") {
            if (l_strKk == "choice") {
                l_pChoice = new QComboBox;
                l_pChoice->setEditable(true);
                for (const std::string& l_strC : m_oP.f_ChoiceIdsOf(f_NameOf(l_oR.m_strSubj))) l_pChoice->addItem(f_Q(l_strC));
                l_pChoice->setEditText(l_oR.m_jVal.is_string() ? f_Q(l_oR.m_jVal.get<std::string>()) : QString());
                l_pH->addWidget(l_pChoice);
            } else {
                l_pVal = f_Spin(l_oR.m_jVal.is_number() ? (int)l_oR.m_jVal.get<double>() : 0);
                l_pH->addWidget(l_pVal);
            }
        }
        auto* l_pDel = new QToolButton;
        l_pDel->setText(QStringLiteral("✕"));
        l_pH->addWidget(l_pDel);
        auto l_fnUpd = [=]() {
            const std::string l_strS = l_pSubj->currentData().toString().toStdString();
            if (l_strS == "\x01") return;
            std::string l_strO = l_pOp->currentData().toString().toStdString();
            const auto l_vOps = stage::f_OpsFor(f_KindOf(l_strS));
            if (std::none_of(l_vOps.begin(), l_vOps.end(), [&](const auto& l_oX) { return l_oX.first == l_strO; })) l_strO = l_vOps[0].first;
            Json l_jValue = l_pVal ? Json(l_pVal->value()) : l_pChoice ? Json(l_pChoice->currentText().toStdString()) : Json(f_KindOf(l_strS) == "item" ? 1 : 0);
            std::vector<Json> l_vRs = l_vRows;
            l_vRs[l_nI] = stage::f_RowToLeaf(l_strS, l_strO, l_jValue);
            l_fnWrite(l_strMode, l_vRs);
        };
        connect(l_pSubj, &QComboBox::activated, this, l_fnUpd);
        connect(l_pOp, &QComboBox::activated, this, l_fnUpd);
        if (l_pVal) connect(l_pVal, &QSpinBox::valueChanged, this, l_fnUpd);
        if (l_pChoice) connect(l_pChoice->lineEdit(), &QLineEdit::editingFinished, this, l_fnUpd);
        connect(l_pDel, &QToolButton::clicked, this, [=] { std::vector<Json> l_vRs = l_vRows; l_vRs.erase(l_vRs.begin() + (long)l_nI); l_fnWrite(l_strMode, l_vRs); });
        l_pV->addLayout(l_pH);
    }
    if (l_vRows.empty()) l_pV->addWidget(f_Muted(l_oNone.isEmpty() ? tr("Always.") : l_oNone));
    auto* l_pH = f_Row();
    if (l_vRows.size() > 1) {
        auto* l_pM = new QComboBox;
        l_pM->addItem(tr("all of these"), QStringLiteral("all"));
        l_pM->addItem(tr("any of these"), QStringLiteral("any"));
        l_pM->setCurrentIndex(l_strMode == "any" ? 1 : 0);
        connect(l_pM, &QComboBox::activated, this, [=](int l_iIx) { l_fnWrite(l_iIx == 1 ? "any" : "all", l_vRows); });
        l_pH->addWidget(l_pM);
    }
    auto* l_pAddB = new QPushButton(tr("+ Add condition"));
    connect(l_pAddB, &QPushButton::clicked, this, [=] {
        std::vector<Json> l_vRs = l_vRows;
        const Json& l_jVars = m_oP.m_jS["vars"];
        if (!l_jVars.empty()) l_vRs.push_back(Json{{"type", "varAtLeast"}, {"var", l_jVars.begin().key()}, {"value", 1}});
        else l_vRs.push_back(Json{{"type", "counterAtLeast"}, {"counter", m_oP.m_jS["counters"].empty() ? std::string() : m_oP.m_jS["counters"].begin().key()}, {"min", 1}});
        l_fnWrite(l_strMode, l_vRs);
    });
    auto* l_pJs = new QToolButton;
    l_pJs->setText(QStringLiteral("{ }"));
    l_pJs->setToolTip(tr("Edit as JSON (for nested all / any / not)"));
    connect(l_pJs, &QToolButton::clicked, this, [this, l_strK] { m_setCondJson.insert(l_strK); f_ScheduleRender(); });
    l_pH->addWidget(l_pAddB);
    l_pH->addWidget(l_pJs);
    l_pH->addStretch(1);
    l_pV->addLayout(l_pH);
    return l_pBox;
}

// ---- action rows -------------------------------------------------------------------------------------------------

QWidget* EventEditor::f_ActsBox(const QString& l_oKey, const std::function<Json()>& l_fnGet, const std::function<Json*()>& l_fnList, const QString& l_oLabel) {
    (void)l_oKey;
    auto* l_pBox = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pBox);
    l_pV->setContentsMargins(0, 0, 0, 0);
    l_pV->setSpacing(3);
    const Json l_jG0 = l_fnGet();
    const Json l_jActs = l_jG0.is_array() ? l_jG0 : l_jG0.is_object() ? Json::array({l_jG0}) : Json::array();
    static const std::vector<std::pair<std::string, const char*>> l_vKinds = {
        {"addVar", QT_TR_NOOP("add to variable")}, {"setVar", QT_TR_NOOP("set variable")}, {"addCounter", QT_TR_NOOP("add to counter")},
        {"setFlag", QT_TR_NOOP("set flag")}, {"give", QT_TR_NOOP("give item")}, {"take", QT_TR_NOOP("take item")}, {"addStat", QT_TR_NOOP("add to status")}};
    for (size_t l_nI = 0; l_nI < l_jActs.size(); l_nI++) {
        const Json l_jA = l_jActs[l_nI];
        const std::string l_strType = l_jA.value("type", std::string());
        auto* l_pH = f_Row();
        const bool l_bKnown = std::any_of(l_vKinds.begin(), l_vKinds.end(), [&](const auto& l_oX) { return l_oX.first == l_strType; });
        if (l_bKnown) {
            auto* l_pTc = new QComboBox;
            for (const auto& [l_strT, l_pszLb] : l_vKinds) l_pTc->addItem(tr(l_pszLb), f_Q(l_strT));
            l_pTc->setCurrentIndex(l_pTc->findData(f_Q(l_strType)));
            auto* l_pTg = new QComboBox;
            const QStringList l_oTargets = f_ActionTargets(l_strType);
            l_pTg->addItems(l_oTargets);
            const QString l_oCur = f_Q(stage::f_ActTarget(l_jA));
            if (!l_oCur.isEmpty() && !l_oTargets.contains(l_oCur)) l_pTg->addItem(l_oCur);
            l_pTg->setCurrentText(l_oCur);
            l_pH->addWidget(l_pTc);
            l_pH->addWidget(l_pTg, 1);
            QSpinBox* l_pVal = nullptr;
            if (l_strType == "addVar" || l_strType == "setVar" || l_strType == "addCounter" || l_strType == "addStat") {
                const Json l_jAv = stage::f_ActValue(l_jA);
                l_pVal = f_Spin(l_jAv.is_number() ? (int)l_jAv.get<double>() : l_jAv.is_boolean() ? (l_jAv.get<bool>() ? 1 : 0) : 0);
                l_pH->addWidget(l_pVal);
            }
            connect(l_pTc, &QComboBox::activated, this, [=](int) {
                const std::string l_strNt = l_pTc->currentData().toString().toStdString();
                const QStringList l_oTs = f_ActionTargets(l_strNt);
                Json l_jOld = stage::f_ActValue(l_jA);
                if (!l_jOld.is_number() || l_jOld.get<double>() == 0) l_jOld = 1;
                f_Edit(l_oLabel, [=] { Json* l_pL = l_fnList(); (*l_pL)[l_nI] = stage::f_MakeAction(l_strNt, l_oTs.isEmpty() ? std::string() : l_oTs[0].toStdString(), l_jOld); });
            });
            auto l_fnUpd = [=] {
                const std::string l_strT = l_pTc->currentData().toString().toStdString(), l_strTarget = l_pTg->currentText().toStdString();
                const Json l_jValue = l_pVal ? Json(l_pVal->value()) : Json(1);
                f_Edit(l_oLabel, [=] { Json* l_pL = l_fnList(); (*l_pL)[l_nI] = stage::f_MakeAction(l_strT, l_strTarget, l_jValue); });
            };
            connect(l_pTg, &QComboBox::activated, this, l_fnUpd);
            if (l_pVal) connect(l_pVal, &QSpinBox::valueChanged, this, l_fnUpd);
        } else {   // an action the rows do not know: kept as JSON
            auto* l_pC = f_Chip(f_Q(l_strType.empty() ? "?" : l_strType) + QStringLiteral(" ") + f_Q(l_jA.dump()).left(60));
            l_pC->setToolTip(f_Q(l_jA.dump()));
            l_pH->addWidget(l_pC, 1);
        }
        auto* l_pDel = new QToolButton;
        l_pDel->setText(QStringLiteral("✕"));
        connect(l_pDel, &QToolButton::clicked, this, [=] { f_Edit(l_oLabel, [=] { Json* l_pL = l_fnList(); l_pL->erase(l_nI); }); });
        l_pH->addWidget(l_pDel);
        l_pV->addLayout(l_pH);
    }
    if (l_jActs.empty()) l_pV->addWidget(f_Muted(tr("Nothing else.")));
    auto* l_pAdd = new QPushButton(tr("+ Add action"));
    connect(l_pAdd, &QPushButton::clicked, this, [=] {
        f_Edit(l_oLabel, [=] {
            Json* l_pL = l_fnList();
            const Json& l_jVars = m_oP.m_jS["vars"];
            if (!l_jVars.empty()) l_pL->push_back(stage::f_MakeAction("addVar", l_jVars.begin().key(), 1));
            else l_pL->push_back(stage::f_MakeAction("addCounter", m_oP.m_jS["counters"].empty() ? std::string() : m_oP.m_jS["counters"].begin().key(), 1));
        });
    });
    auto* l_pH = f_Row();
    l_pH->addWidget(l_pAdd);
    l_pH->addStretch(1);
    l_pV->addLayout(l_pH);
    return l_pBox;
}

// ---- an event ------------------------------------------------------------------------------------------------------

QWidget* EventEditor::f_LogicBox(Json& l_jE) {
    const std::string l_strId = l_jE["id"].get<std::string>();
    auto* l_pG = new QGroupBox(tr("Logic"));
    auto* l_pV = new QVBoxLayout(l_pG);
    auto l_fnE = [this, l_strId](const QString& l_oLabel, const std::function<void(Json&)>& l_fnFn) {
        f_Edit(l_oLabel, [this, l_strId, l_fnFn] { if (Json* l_pX = m_oP.f_Ev(l_strId)) l_fnFn(*l_pX); });
    };
    const QString l_oHTrig = tr("Trigger of %1").arg(f_Q(l_strId));
    // Fires when
    {
        auto* l_pH = f_Row(new QLabel(tr("Fires when")));
        auto* l_pOn = new QComboBox;
        l_pOn->addItem(tr("the player steps on its tile"), QStringLiteral("step"));
        l_pOn->addItem(tr("the player talks to"), QStringLiteral("talk"));
        l_pOn->addItem(tr("this variable changes"), QStringLiteral("varChanged"));
        l_pOn->addItem(tr("another event links to it (next)"), QStringLiteral("chain"));
        const std::string l_strTrOn = l_jE["trigger"].value("on", std::string("step"));
        l_pOn->setCurrentIndex(std::max(0, l_pOn->findData(f_Q(l_strTrOn))));
        connect(l_pOn, &QComboBox::activated, this, [=](int) {
            const std::string l_strO = l_pOn->currentData().toString().toStdString();
            l_fnE(l_oHTrig, [this, l_strO](Json& l_jX) {
                if (l_strO == "talk") l_jX["trigger"] = Json{{"on", l_strO}, {"npc", m_oP.m_mapNpcPlaces.empty() ? std::string() : m_oP.m_mapNpcPlaces.begin()->first}};
                else if (l_strO == "varChanged") l_jX["trigger"] = Json{{"on", l_strO}, {"var", m_oP.m_jS["vars"].empty() ? std::string() : m_oP.m_jS["vars"].begin().key()}};
                else l_jX["trigger"] = Json{{"on", l_strO}};
            });
        });
        l_pH->addWidget(l_pOn);
        if (l_strTrOn == "talk" || l_strTrOn == "varChanged") {
            auto* l_pArg = new QComboBox;
            const std::string l_strCur = l_jE["trigger"].value(l_strTrOn == "talk" ? "npc" : "var", std::string());
            if (l_strTrOn == "talk") {
                std::vector<std::string> l_vIds;
                for (const auto& [l_strN, l_vF] : m_oP.m_mapNpcPlaces) { (void)l_vF; l_vIds.push_back(l_strN); }
                if (!l_strCur.empty() && std::find(l_vIds.begin(), l_vIds.end(), l_strCur) == l_vIds.end()) l_vIds.push_back(l_strCur);
                for (const std::string& l_strN : l_vIds) {
                    auto l_itIt = m_oP.m_mapNpcPlaces.find(l_strN);
                    QStringList l_oFl;
                    if (l_itIt != m_oP.m_mapNpcPlaces.end()) for (const std::string& l_strF : l_itIt->second) l_oFl << f_Q(l_strF);
                    l_pArg->addItem(l_itIt != m_oP.m_mapNpcPlaces.end() ? QStringLiteral("%1 (%2)").arg(f_Q(l_strN), l_oFl.join(QStringLiteral(", "))) : tr("%1 (not placed)").arg(f_Q(l_strN)), f_Q(l_strN));
                }
            } else {
                for (auto& [l_strK, l_jR] : m_oP.m_jS["vars"].items()) { (void)l_jR; l_pArg->addItem(f_Q(l_strK), f_Q(l_strK)); }
                if (!l_strCur.empty() && l_pArg->findData(f_Q(l_strCur)) < 0) l_pArg->addItem(f_Q(l_strCur), f_Q(l_strCur));
            }
            l_pArg->setCurrentIndex(std::max(0, l_pArg->findData(f_Q(l_strCur))));
            connect(l_pArg, &QComboBox::activated, this, [=](int) {
                const std::string l_strVal = l_pArg->currentData().toString().toStdString();
                l_fnE(l_oHTrig, [l_strVal](Json& l_jX) { if (l_jX["trigger"]["on"] == "talk") l_jX["trigger"]["npc"] = l_strVal; else l_jX["trigger"]["var"] = l_strVal; });
            });
            l_pH->addWidget(l_pArg, 1);
        }
        l_pH->addStretch(1);
        l_pV->addLayout(l_pH);
        // a talk event: its dialogue, what follows it, where, its menu order (08_NPC_DIALOGUE.md)
        if (l_strTrOn == "talk") {
            auto l_fnDlgCombo = [&](const std::string& l_strCur, const QString& l_oNone) {
                auto* l_pC = new QComboBox;
                l_pC->addItem(l_oNone, QString());
                std::vector<std::string> l_vDs = m_oP.f_DialogueList();
                if (!l_strCur.empty() && std::find(l_vDs.begin(), l_vDs.end(), l_strCur) == l_vDs.end()) l_vDs.push_back(l_strCur);
                for (const std::string& l_strD : l_vDs) {
                    const auto l_vDl = m_oP.f_DialogueList();
                    l_pC->addItem(f_Q(l_strD) + (std::find(l_vDl.begin(), l_vDl.end(), l_strD) == l_vDl.end() ? QStringLiteral(" ⛔") : QString()), f_Q(l_strD));
                }
                l_pC->setCurrentIndex(std::max(0, l_pC->findData(f_Q(l_strCur))));
                return l_pC;
            };
            auto* l_pH1 = f_Row(new QLabel(tr("Dialogue")));
            auto* l_pDlg = l_fnDlgCombo(l_jE["dialogue"].get<std::string>(), tr("(none: the event text as one page)"));
            connect(l_pDlg, &QComboBox::activated, this, [=](int) { const std::string l_strD = l_pDlg->currentData().toString().toStdString(); l_fnE(l_oHTrig, [l_strD](Json& l_jX) { l_jX["dialogue"] = l_strD; }); });
            l_pH1->addWidget(l_pDlg, 1);
            l_pV->addLayout(l_pH1);
            auto* l_pH2 = f_Row(new QLabel(tr("After finishing")));
            auto* l_pAfter = l_fnDlgCombo(l_jE["afterDialogue"].get<std::string>(), tr("(hide the topic)"));
            connect(l_pAfter, &QComboBox::activated, this, [=](int) { const std::string l_strD = l_pAfter->currentData().toString().toStdString(); l_fnE(l_oHTrig, [l_strD](Json& l_jX) { l_jX["afterDialogue"] = l_strD; }); });
            l_pH2->addWidget(l_pAfter, 1);
            l_pV->addLayout(l_pH2);
            auto* l_pH3 = f_Row(new QLabel(tr("Talked to on")));
            bool l_bCond = false;
            const std::string l_strCur = m_oP.f_TalkFloorOf(l_jE, &l_bCond);
            if (l_bCond) l_pH3->addWidget(f_Muted(tr("set by its conditions (any of / JSON)")));
            else {
                const std::string l_strNpc = l_jE["trigger"].value("npc", std::string());
                auto l_itIt = m_oP.m_mapNpcPlaces.find(l_strNpc);
                std::vector<std::string> l_vFl = l_itIt != m_oP.m_mapNpcPlaces.end() ? l_itIt->second : std::vector<std::string>();
                auto* l_pFc = new QComboBox;
                l_pFc->addItem(tr("any floor it stands on (%1)").arg(l_vFl.size()), QString());
                if (!l_strCur.empty() && std::find(l_vFl.begin(), l_vFl.end(), l_strCur) == l_vFl.end()) l_pFc->addItem(f_Q(l_strCur) + QStringLiteral(" ⚠"), f_Q(l_strCur));
                for (const std::string& l_strF : l_vFl) l_pFc->addItem(f_Q(l_strF), f_Q(l_strF));
                l_pFc->setCurrentIndex(std::max(0, l_pFc->findData(f_Q(l_strCur))));
                connect(l_pFc, &QComboBox::activated, this, [=](int) {
                    const std::string l_strF = l_pFc->currentData().toString().toStdString();
                    l_fnE(l_oHTrig, [this, l_strF](Json& l_jX) { m_oP.f_SetTalkFloor(l_jX, l_strF); });
                });
                l_pH3->addWidget(l_pFc, 1);
            }
            l_pV->addLayout(l_pH3);
            auto* l_pH4 = f_Row(new QLabel(tr("Menu order")));
            auto* l_pOrder = new CommitLine(l_jE["topicOrder"].is_number() ? QString::number(l_jE["topicOrder"].get<int>()) : QString());
            l_pOrder->setPlaceholderText(tr("auto"));
            l_pOrder->setMaximumWidth(80);
            l_pOrder->m_fnOnCommit = [=](const QString& l_oS) {
                bool l_bOk = false;
                const int l_iN = l_oS.trimmed().toInt(&l_bOk);
                l_fnE(l_oHTrig, [l_bOk, l_iN](Json& l_jX) { l_jX["topicOrder"] = l_bOk ? Json(l_iN) : Json(); });
            };
            l_pH4->addWidget(l_pOrder);
            l_pH4->addStretch(1);
            l_pV->addLayout(l_pH4);
        }
    }
    // Repeat
    {
        auto* l_pH = f_Row(new QLabel(tr("Repeat")));
        auto* l_pRep = new QCheckBox(tr("repeatable (runs again after finishing)"));
        l_pRep->setChecked(l_jE["once"] == false);
        connect(l_pRep, &QCheckBox::toggled, this, [=](bool l_bOn) { l_fnE(l_oHTrig, [l_bOn](Json& l_jX) { l_jX["once"] = !l_bOn; if (!l_bOn) l_jX["maxTimes"] = 0; }); });
        l_pH->addWidget(l_pRep);
        if (l_jE["once"] == false) {
            l_pH->addWidget(f_Muted(tr("at most")));
            auto* l_pMx = f_Spin(l_jE["maxTimes"].get<int>(), 0, 9999);
            connect(l_pMx, &QSpinBox::valueChanged, this, [=](int l_iN) { l_fnE(l_oHTrig, [l_iN](Json& l_jX) { l_jX["maxTimes"] = l_iN; }); });
            l_pH->addWidget(l_pMx);
            l_pH->addWidget(f_Muted(tr("times (0 = no limit); its \"Then fire\" links wake only the first time")), 1);
        }
        l_pH->addStretch(1);
        l_pV->addLayout(l_pH);
    }
    // Active only when
    l_pV->addWidget(f_Heading(tr("Active only when")));
    l_pV->addWidget(f_CondBox(QStringLiteral("ev"), [this, l_strId] { const Json* l_pX = m_oP.f_Ev(l_strId); return l_pX ? m_oP.f_ReqJson(*l_pX) : Json(); },
                         [this, l_strId](const Json& l_jC) { if (Json* l_pX = m_oP.f_Ev(l_strId)) m_oP.f_SetEventReq(*l_pX, l_jC); }, tr("Conditions of %1").arg(f_Q(l_strId)), tr("Always active.")));
    // Then do
    l_pV->addWidget(f_Heading(tr("Then do")));
    l_pV->addWidget(f_ActsBox(QStringLiteral("ev"), [this, l_strId] { const Json* l_pX = m_oP.f_Ev(l_strId); return l_pX ? (*l_pX)["actions"] : Json::array(); },
                         [this, l_strId] { Json* l_pX = m_oP.f_Ev(l_strId); return l_pX ? &(*l_pX)["actions"] : nullptr; }, tr("Actions of %1").arg(f_Q(l_strId))));
    // Then fire (direct link)
    l_pV->addWidget(f_Heading(tr("Then fire (direct link)")));
    {
        auto* l_pChips = new QWidget;
        auto* l_pFlow = new FlowLayout(l_pChips, 4);
        for (const Json& l_jN : l_jE["next"]) {
            const std::string l_strNx = l_jN.get<std::string>();
            auto* l_pC = f_Chip(f_Q(l_strNx) + QStringLiteral("  ✕"), m_oP.f_Ev(l_strNx) ? QColor() : QColor("#ff5d5d"), tr("Click: open it · ✕: remove the link (click its right edge)"));
            auto* l_pMenuOpen = new QAction(tr("Open"), l_pC);
            auto* l_pMenuDel = new QAction(tr("Remove link"), l_pC);
            l_pC->addAction(l_pMenuOpen);
            l_pC->addAction(l_pMenuDel);
            l_pC->setPopupMode(QToolButton::InstantPopup);
            connect(l_pMenuOpen, &QAction::triggered, this, [this, l_strNx] { if (m_oP.f_Ev(l_strNx)) f_Select(l_strNx); });
            connect(l_pMenuDel, &QAction::triggered, this, [=] { l_fnE(tr("Links of %1").arg(f_Q(l_strId)), [l_strNx](Json& l_jX) { Json& l_jL = l_jX["next"]; l_jL.erase(std::remove(l_jL.begin(), l_jL.end(), Json(l_strNx)), l_jL.end()); }); });
            l_pFlow->addWidget(l_pC);
        }
        auto* l_pAdd = new QComboBox;
        l_pAdd->addItem(tr("+ link an event…"), QString());
        for (const Json& l_jO : m_oP.m_jS["events"])
            if (l_jO["id"] != l_strId && std::find(l_jE["next"].begin(), l_jE["next"].end(), l_jO["id"]) == l_jE["next"].end()) l_pAdd->addItem(f_Q(l_jO["id"].get<std::string>()), f_Q(l_jO["id"].get<std::string>()));
        connect(l_pAdd, &QComboBox::activated, this, [=](int) {
            const std::string l_strNx = l_pAdd->currentData().toString().toStdString();
            if (!l_strNx.empty()) l_fnE(tr("Links of %1").arg(f_Q(l_strId)), [l_strNx](Json& l_jX) { l_jX["next"].push_back(l_strNx); });
        });
        l_pFlow->addWidget(l_pAdd);
        l_pV->addWidget(l_pChips);
    }
    // Fired by
    {
        auto* l_pH = f_Row(new QLabel(tr("Fired by")));
        auto* l_pChips = new QWidget;
        auto* l_pFlow = new FlowLayout(l_pChips, 4);
        int l_iN = 0;
        for (const Json& l_jO : m_oP.m_jS["events"])
            if (std::find(l_jO["next"].begin(), l_jO["next"].end(), Json(l_strId)) != l_jO["next"].end()) {
                const std::string l_strOid = l_jO["id"].get<std::string>();
                auto* l_pC = f_Chip(QStringLiteral("→ ") + f_Q(l_strOid));
                connect(l_pC, &QToolButton::clicked, this, [this, l_strOid] { f_Select(l_strOid); });
                l_pFlow->addWidget(l_pC);
                l_iN++;
            }
        const std::string l_strOn = l_jE["trigger"].value("on", std::string());
        if (l_strOn == "talk") { l_pFlow->addWidget(f_Chip(QStringLiteral("💬 ") + f_Q(l_jE["trigger"].value("npc", std::string())))); l_iN++; }
        if (l_strOn == "varChanged") { l_pFlow->addWidget(f_Chip(QStringLiteral("◆ ") + f_Q(l_jE["trigger"].value("var", std::string())))); l_iN++; }
        if (!l_iN) l_pFlow->addWidget(f_Muted(QStringLiteral("—")));
        l_pH->addWidget(l_pChips, 1);
        l_pV->addLayout(l_pH);
    }
    return l_pG;
}

QWidget* EventEditor::f_EventForm(Json& l_jE) {
    const std::string l_strId = l_jE["id"].get<std::string>();
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    l_pV->setContentsMargins(10, 8, 10, 10);
    const auto l_vFl = m_oP.f_FloorsOf(l_strId);
    const std::string l_strOn = l_jE["trigger"].value("on", std::string("step"));
    auto l_fnE = [this, l_strId](const QString& l_oLabel, const std::function<void(Json&)>& l_fnFn) {
        f_Edit(l_oLabel, [this, l_strId, l_fnFn] { if (Json* l_pX = m_oP.f_Ev(l_strId)) l_fnFn(*l_pX); });
    };
    if (l_vFl.empty() && l_strOn == "step") {
        auto* l_pH = f_Row();
        l_pH->addWidget(f_Banner(tr("⚠ <b>Orphan:</b> no floor can roll this event, so it never appears in game. Fix: <b>+ Add floor…</b> below, or"), false), 1);
        auto* l_pDel = new QPushButton(tr("Delete event"));
        connect(l_pDel, &QPushButton::clicked, this, [this, l_strId] {
            f_Edit(tr("Delete %1").arg(f_Q(l_strId)), [this, l_strId] {
                Json& l_jEvs = m_oP.m_jS["events"];
                for (size_t l_nI = 0; l_nI < l_jEvs.size(); l_nI++) if (l_jEvs[l_nI]["id"] == l_strId) { l_jEvs.erase(l_nI); break; }
                m_oP.m_strSelected.clear();
            });
        });
        l_pH->addWidget(l_pDel);
        l_pV->addLayout(l_pH);
    }
    // id, kind, pool, text key
    {
        auto* l_pGrid = new QGridLayout;
        l_pGrid->addWidget(f_Heading(QStringLiteral("eventId")), 0, 0);
        l_pGrid->addWidget(f_Heading(tr("kind")), 0, 1);
        l_pGrid->addWidget(f_Heading(tr("pool")), 0, 2);
        l_pGrid->addWidget(f_Heading(tr("text key")), 0, 3);
        auto* l_pIdEd = new CommitLine(f_Q(l_strId));
        auto* l_pIdErr = new QLabel;
        l_pIdErr->setStyleSheet(QStringLiteral("color:#ff5d5d"));
        l_pIdErr->setWordWrap(true);
        auto l_fnErrText = [this](const std::string& l_strErr) {
            if (l_strErr.empty()) return QString();
            if (l_strErr == "format") return tr("Any name works, Chinese too (e.g. ev_village_scarecrow or 稻草人), except empty, spaces at the ends, or \" ' < > & | \\ /");
            return tr("%1 already exists in %2").arg(QString(), f_Q(l_strErr.substr(4))).trimmed();
        };
        connect(l_pIdEd, &QLineEdit::textEdited, this, [=](const QString& l_oS) {
            const std::string l_strNu = l_oS.trimmed().toStdString();
            l_pIdErr->setText(l_strNu == l_strId ? QString() : l_fnErrText(m_oP.f_IdError(l_strNu, m_oP.f_Ev(l_strId))));
        });
        l_pIdEd->m_fnOnCommit = [=](const QString& l_oS) {
            const std::string l_strNu = l_oS.trimmed().toStdString();
            if (l_strNu == l_strId) return;
            const std::string l_strErr = m_oP.f_IdError(l_strNu, m_oP.f_Ev(l_strId));
            if (!l_strErr.empty()) { l_pIdErr->setText(l_fnErrText(l_strErr)); return; }
            f_Edit(tr("Rename %1 → %2").arg(f_Q(l_strId), f_Q(l_strNu)), [this, l_strId, l_strNu] { if (Json* l_pX = m_oP.f_Ev(l_strId)) m_oP.f_RenameEvent(*l_pX, l_strNu); });
        };
        l_pGrid->addWidget(l_pIdEd, 1, 0);
        l_pGrid->addWidget(l_pIdErr, 2, 0, 1, 4);
        auto* l_pKind = new QComboBox;
        const std::string l_strK = l_jE["kind"].get<std::string>();
        if (l_strK.empty()) l_pKind->addItem(tr("(choose a kind)"), QString());
        const auto l_vKl = m_oP.f_KindsList();
        for (const std::string& l_strKk : l_vKl) {
            const QString l_oN = f_Q(m_oP.f_KindName(l_strKk, f_Lang().toStdString()));
            l_pKind->addItem(f_Q(l_strKk) + (l_oN.isEmpty() ? QString() : QStringLiteral(" — ") + l_oN), f_Q(l_strKk));
        }
        if (!l_strK.empty() && std::find(l_vKl.begin(), l_vKl.end(), l_strK) == l_vKl.end()) l_pKind->addItem(QStringLiteral("⚠ ") + f_Q(l_strK), f_Q(l_strK));
        l_pKind->addItem(tr("+ New kind…"), QStringLiteral("__new"));
        l_pKind->setCurrentIndex(std::max(0, l_pKind->findData(f_Q(l_strK))));
        connect(l_pKind, &QComboBox::activated, this, [=](int) {
            const QString l_oNk = l_pKind->currentData().toString();
            if (l_oNk == QStringLiteral("__new")) { f_SetTab(QStringLiteral("kinds")); return; }
            l_fnE(tr("Kind of %1 → %2").arg(f_Q(l_strId), l_oNk), [l_oNk](Json& l_jX) { l_jX["kind"] = l_oNk.toStdString(); });
        });
        l_pGrid->addWidget(l_pKind, 1, 1);
        auto* l_pPool = new QComboBox;
        for (const std::string& l_strP : m_oP.f_Pools()) l_pPool->addItem(f_Q(l_strP));
        l_pPool->setCurrentText(f_Q(l_jE["pool"].get<std::string>()));
        connect(l_pPool, &QComboBox::activated, this, [=](int) {
            const std::string l_strP = l_pPool->currentText().toStdString();
            f_Edit(tr("Move %1 to %2").arg(f_Q(l_strId), f_Q(l_strP)), [this, l_strId, l_strP] {
                Json& l_jEvs = m_oP.m_jS["events"];
                for (size_t l_nI = 0; l_nI < l_jEvs.size(); l_nI++)
                    if (l_jEvs[l_nI]["id"] == l_strId) { Json l_jX = l_jEvs[l_nI]; l_jEvs.erase(l_nI); l_jX["pool"] = l_strP; l_jEvs.push_back(l_jX); break; }
            });
        });
        l_pGrid->addWidget(l_pPool, 1, 2);
        auto* l_pKey = new QLineEdit(f_Q(l_jE["text"].get<std::string>()));
        l_pKey->setEnabled(false);
        l_pGrid->addWidget(l_pKey, 1, 3);
        l_pV->addLayout(l_pGrid);
    }
    // floors that can roll it
    l_pV->addWidget(f_Heading(tr("Floors that can roll it (%1)").arg(l_vFl.size())));
    {
        auto* l_pChips = new QWidget;
        auto* l_pFlow = new FlowLayout(l_pChips, 4);
        for (const Json* l_pF : l_vFl) {
            const std::string l_strFid = (*l_pF)["id"].get<std::string>();
            auto* l_pBox = new QWidget;
            auto* l_pH = new QHBoxLayout(l_pBox);
            l_pH->setContentsMargins(0, 0, 0, 0);
            l_pH->setSpacing(0);
            auto* l_pC = f_Chip(f_Q(l_strFid) + QStringLiteral(" ") + f_Q((*l_pF)["act"].get<std::string>()));
            connect(l_pC, &QToolButton::clicked, this, [this, l_strFid] { f_Select(l_strFid, true); });
            auto* l_pPin = new QToolButton;
            l_pPin->setText(QStringLiteral("📍"));
            l_pPin->setAutoRaise(true);
            l_pPin->setToolTip(tr("Place on %1's map (stage editor)").arg(f_Q(l_strFid)));
            const QString l_oKindQ = f_Q(l_jE["kind"].get<std::string>());
            connect(l_pPin, &QToolButton::clicked, this, [this, l_strId, l_oKindQ, l_strFid] { Q_EMIT f_PlaceOnStage(f_Q(l_strId), l_oKindQ, {f_Q(l_strFid)}); });
            auto* l_pRm = new QToolButton;
            l_pRm->setText(QStringLiteral("✕"));
            l_pRm->setAutoRaise(true);
            l_pRm->setToolTip(tr("Disconnect"));
            connect(l_pRm, &QToolButton::clicked, this, [this, l_strId, l_strFid] {
                f_Edit(tr("Disconnect %1 from %2").arg(f_Q(l_strId), f_Q(l_strFid)), [this, l_strId, l_strFid] {
                    if (Json* l_pF2 = m_oP.f_Flo(l_strFid)) { Json& l_jL = (*l_pF2)["events"]; auto l_oIt = std::find(l_jL.begin(), l_jL.end(), Json(l_strId)); if (l_oIt != l_jL.end()) l_jL.erase(l_oIt); }
                });
            });
            l_pH->addWidget(l_pC);
            l_pH->addWidget(l_pPin);
            l_pH->addWidget(l_pRm);
            l_pFlow->addWidget(l_pBox);
        }
        auto* l_pAdd = new QPushButton(tr("+ Add floor…"));
        connect(l_pAdd, &QPushButton::clicked, this, [this, l_strId] { f_AddFloorsDialog(l_strId); });
        l_pFlow->addWidget(l_pAdd);
        l_pV->addWidget(l_pChips);
    }
    l_pV->addWidget(f_LogicBox(l_jE));
    // text in every language
    const std::string l_strTextKey = l_jE["text"].get<std::string>();
    const Json l_jTx = m_oP.f_Text(l_strTextKey);
    const bool l_bPh = m_oP.f_IsPlaceholder(l_strTextKey);
    l_pV->addWidget(f_Rich(QStringLiteral("<span style='color:#8b929c;font-weight:600'>%1</span> %2 <span style='color:#8b929c;font-size:11px'>%3</span>")
                          .arg(tr("Text"), l_bPh ? QStringLiteral("<span style='color:#ff9f43'>%1</span>").arg(tr("⚠ placeholder: write the real line")) : QString(),
                               f_Esc(tr("Enter = new line (saved as \\n)")))));
    QLabel* l_pPreview = nullptr;
    CommitText* l_pZhBox = nullptr;
    const QString l_oKindLbl = l_jE["kind"].get<std::string>().empty() ? tr("kind?") : f_Q(l_jE["kind"].get<std::string>());
    auto l_fnPreviewHtml = [l_oKindLbl](const QString& l_oZh) {
        return QStringLiteral("<small style='color:#c9a85a'>%1</small><br>%2").arg(f_Esc(l_oKindLbl),
                                                                                   l_oZh.isEmpty() ? QStringLiteral("<span style='color:#8b929c'>…</span>") : f_Esc(l_oZh).replace(QStringLiteral("\n"), QStringLiteral("<br>")));
    };
    for (const char* l_pszL : stage::g_arrLangs) {
        auto* l_pH = f_Row(new QLabel(QString::fromLatin1(l_pszL)));
        l_pH->itemAt(0)->widget()->setMinimumWidth(50);
        auto* l_pT = new CommitText(l_jTx.contains(l_pszL) && l_jTx[l_pszL].is_string() ? f_Q(l_jTx[l_pszL].get<std::string>()) : QString());
        l_pT->setPlaceholderText(std::string(l_pszL) == "zh_TW" ? tr("The line the player reads") : tr("untranslated"));
        const std::string l_strLang = l_pszL;
        l_pT->m_fnOnCommit = [=](const QString& l_oS) {
            f_Edit(tr("Text %1 [%2]").arg(f_Q(l_strId), f_Q(l_strLang)), [this, l_strTextKey, l_strLang, l_oS] {
                Json& l_jTt = m_oP.m_jS["texts"][l_strTextKey];
                if (!l_jTt.is_object()) l_jTt = Json::object();
                l_jTt[l_strLang] = l_oS.toStdString();
                if (l_strLang == "zh_TW") l_jTt.erase("_todo");
            });
        };
        if (l_strLang == "zh_TW") l_pZhBox = l_pT;
        l_pH->addWidget(l_pT, 1);
        l_pV->addLayout(l_pH);
    }
    // title and description (the player's event log)
    auto l_fnSetKeyText = [this, l_strId](const char* l_pszField, const std::string& l_strLang, const QString& l_oS, const QString& l_oLabel) {
        const std::string l_strF = l_pszField;
        f_Edit(l_oLabel, [this, l_strId, l_strF, l_strLang, l_oS] {
            Json* l_pX = m_oP.f_Ev(l_strId);
            if (!l_pX) return;
            if ((*l_pX)[l_strF].get<std::string>().empty()) (*l_pX)[l_strF] = l_strId + "." + l_strF;
            Json& l_jTt = m_oP.m_jS["texts"][(*l_pX)[l_strF].get<std::string>()];
            if (!l_jTt.is_object()) l_jTt = Json::object();
            l_jTt[l_strLang] = l_oS.toStdString();
        });
    };
    l_pV->addWidget(f_Heading(tr("Title (the player's event log)")));
    const Json l_jTi = m_oP.f_Text(l_jE["title"].get<std::string>()), l_jDe = m_oP.f_Text(l_jE["desc"].get<std::string>());
    for (const char* l_pszL : stage::g_arrLangs) {
        auto* l_pH = f_Row(new QLabel(QString::fromLatin1(l_pszL)));
        l_pH->itemAt(0)->widget()->setMinimumWidth(50);
        auto* l_pT = new CommitLine(l_jTi.contains(l_pszL) && l_jTi[l_pszL].is_string() ? f_Q(l_jTi[l_pszL].get<std::string>()) : QString());
        const std::string l_strLang = l_pszL;
        l_pT->m_fnOnCommit = [=](const QString& l_oS) { l_fnSetKeyText("title", l_strLang, l_oS, tr("Title %1 [%2]").arg(f_Q(l_strId), f_Q(l_strLang))); };
        l_pH->addWidget(l_pT, 1);
        l_pV->addLayout(l_pH);
    }
    l_pV->addWidget(f_Heading(tr("Description (the player's event log)")));
    for (const char* l_pszL : stage::g_arrLangs) {
        auto* l_pH = f_Row(new QLabel(QString::fromLatin1(l_pszL)));
        l_pH->itemAt(0)->widget()->setMinimumWidth(50);
        auto* l_pT = new CommitText(l_jDe.contains(l_pszL) && l_jDe[l_pszL].is_string() ? f_Q(l_jDe[l_pszL].get<std::string>()) : QString());
        const std::string l_strLang = l_pszL;
        l_pT->m_fnOnCommit = [=](const QString& l_oS) { l_fnSetKeyText("desc", l_strLang, l_oS, tr("Description %1 [%2]").arg(f_Q(l_strId), f_Q(l_strLang))); };
        l_pH->addWidget(l_pT, 1);
        l_pV->addLayout(l_pH);
    }
    {
        auto* l_pH = f_Row(new QLabel(tr("In the player's event log")));
        auto* l_pC = new QComboBox;
        const Json* l_pKr = m_oP.m_jS["kinds"].contains(l_jE["kind"].get<std::string>()) ? &m_oP.m_jS["kinds"][l_jE["kind"].get<std::string>()] : nullptr;
        const bool l_bKindNo = l_pKr && l_pKr->contains("inLog") && (*l_pKr)["inLog"] == false;
        l_pC->addItem(tr("as its kind (%1)").arg(l_bKindNo ? tr("no") : tr("yes")), QString());
        l_pC->addItem(tr("yes"), QStringLiteral("1"));
        l_pC->addItem(tr("no"), QStringLiteral("0"));
        l_pC->setCurrentIndex(!l_jE.contains("inLog") ? 0 : l_jE["inLog"] == true ? 1 : 2);
        connect(l_pC, &QComboBox::activated, this, [=](int) {
            const QString l_oVal = l_pC->currentData().toString();
            l_fnE(tr("Event log %1").arg(f_Q(l_strId)), [l_oVal](Json& l_jX) { if (l_oVal.isEmpty()) l_jX.erase("inLog"); else l_jX["inLog"] = l_oVal == QStringLiteral("1"); });
        });
        l_pH->addWidget(l_pC);
        l_pH->addStretch(1);
        l_pV->addLayout(l_pH);
    }
    l_pV->addWidget(f_Heading(tr("Preview (game dialog)")));
    l_pPreview = f_Rich(l_fnPreviewHtml(l_jTx.contains("zh_TW") && l_jTx["zh_TW"].is_string() ? f_Q(l_jTx["zh_TW"].get<std::string>()) : QString()));
    l_pPreview->setStyleSheet(QStringLiteral("background:#0d0f12; border:1px solid #6b5a3a; border-radius:4px; padding:8px; color:#f3e6c8"));
    l_pV->addWidget(l_pPreview);
    // the preview label lives as long as the form: the zh_TW box updates it while typing
    if (l_pZhBox) {
        QLabel* l_pPv = l_pPreview;
        l_pZhBox->m_fnOnEdit = [l_pPv, l_fnPreviewHtml](const QString& l_oS) { l_pPv->setText(l_fnPreviewHtml(l_oS)); };
    }
    l_pV->addStretch(1);
    return l_pW;
}

void EventEditor::f_AddFloorsDialog(const std::string& l_strEvId) {
    const Json* l_pE = m_oP.f_Ev(l_strEvId);
    if (!l_pE) return;
    QDialog l_oDlg(this);
    l_oDlg.setWindowTitle(tr("Floors that can roll %1").arg(f_Q(l_strEvId)));
    l_oDlg.resize(560, 520);
    auto* l_pV = new QVBoxLayout(&l_oDlg);
    l_pV->addWidget(f_Rich(tr("Floors that can roll <b>%1</b>").arg(f_Esc(f_Q(l_strEvId)))));
    auto* l_pArea = new QScrollArea;
    l_pArea->setWidgetResizable(true);
    auto* l_pInner = new QWidget;
    auto* l_pIv = new QVBoxLayout(l_pInner);
    std::vector<QCheckBox*> l_vBoxes;
    std::vector<std::string> l_vActs;
    for (const Json& l_jF : m_oP.m_jS["floors"]) if (std::find(l_vActs.begin(), l_vActs.end(), l_jF["act"].get<std::string>()) == l_vActs.end()) l_vActs.push_back(l_jF["act"].get<std::string>());
    const auto l_vHave = m_oP.f_FloorsOf(l_strEvId);
    for (const std::string& l_strA : l_vActs) {
        l_pIv->addWidget(f_Heading(f_Q(l_strA)));
        auto* l_pGw = new QWidget;
        auto* l_pFlow = new FlowLayout(l_pGw, 6);
        for (const Json& l_jF : m_oP.m_jS["floors"]) {
            if (l_jF["act"] != l_strA) continue;
            auto* l_pC = new QCheckBox(f_Q(l_jF["id"].get<std::string>()));
            const bool l_bOn = std::any_of(l_vHave.begin(), l_vHave.end(), [&](const Json* l_pX) { return (*l_pX)["id"] == l_jF["id"]; });
            l_pC->setChecked(l_bOn);
            l_pC->setEnabled(!l_bOn);
            l_pFlow->addWidget(l_pC);
            l_vBoxes.push_back(l_pC);
        }
        l_pIv->addWidget(l_pGw);
    }
    l_pIv->addStretch(1);
    l_pArea->setWidget(l_pInner);
    l_pV->addWidget(l_pArea, 1);
    auto* l_pOpenStageC = new QCheckBox(tr("Then open the stage editor to place it on the map"));
    l_pOpenStageC->setChecked(true);
    l_pV->addWidget(l_pOpenStageC);
    auto* l_pBb = new QDialogButtonBox(QDialogButtonBox::Cancel);
    l_pBb->addButton(tr("Connect"), QDialogButtonBox::AcceptRole);
    connect(l_pBb, &QDialogButtonBox::accepted, &l_oDlg, &QDialog::accept);
    connect(l_pBb, &QDialogButtonBox::rejected, &l_oDlg, &QDialog::reject);
    l_pV->addWidget(l_pBb);
    if (l_oDlg.exec() != QDialog::Accepted) return;
    std::vector<std::string> l_vIds;
    QStringList l_oQids;
    for (QCheckBox* l_pC : l_vBoxes) if (l_pC->isEnabled() && l_pC->isChecked()) { l_vIds.push_back(l_pC->text().toStdString()); l_oQids << l_pC->text(); }
    if (l_vIds.empty()) return;
    const QString l_oKind = f_Q((*l_pE)["kind"].get<std::string>());
    f_Edit(tr("Connect %1 to %2").arg(f_Q(l_strEvId), l_oQids.join(QStringLiteral(", "))), [this, l_vIds, l_strEvId] {
        for (const std::string& l_strF : l_vIds) if (Json* l_pFl = m_oP.f_Flo(l_strF)) (*l_pFl)["events"].push_back(l_strEvId);
    });
    if (l_pOpenStageC->isChecked()) Q_EMIT f_PlaceOnStage(f_Q(l_strEvId), l_oKind, l_oQids);
}

void EventEditor::f_CreateEvent(const std::string& l_strPoolIn) {
    std::string l_strPool = l_strPoolIn;
    if (l_strPool.empty()) { const Json* l_pS = m_oP.f_Ev(m_oP.m_strSelected); l_strPool = l_pS ? (*l_pS)["pool"].get<std::string>() : std::string("pool_act01"); }
    m_setCollapsed.erase(l_strPool);
    const std::string l_strId = m_oP.f_NewEventId();
    f_Edit(tr("New event %1 in %2").arg(f_Q(l_strId), f_Q(l_strPool)), [this, l_strId, l_strPool] {
        if (std::find(m_oP.m_jS["pools"].begin(), m_oP.m_jS["pools"].end(), Json(l_strPool)) == m_oP.m_jS["pools"].end()) m_oP.m_jS["pools"].push_back(l_strPool);
        m_oP.m_jS["events"].push_back(m_oP.f_BlankEvent(l_strId, l_strPool));
        for (const char* l_pszK : {".text", ".title", ".desc"}) {
            Json l_jT = Json::object();
            for (const char* l_pszL : stage::g_arrLangs) l_jT[l_pszL] = "";
            m_oP.m_jS["texts"][l_strId + l_pszK] = l_jT;
        }
        m_oP.m_strSelected = l_strId;
    });
}

void EventEditor::f_CreateFloor(const std::string& l_strId, const std::string& l_strFromId) {
    const Json* l_pFrom = m_oP.f_Flo(l_strFromId);
    if (!l_pFrom || m_oP.f_Flo(l_strId)) return;
    const std::string l_strAct = (*l_pFrom)["act"].get<std::string>();
    f_Edit(tr("Create floor %1 (%2)").arg(f_Q(l_strId), f_Q(l_strAct)), [this, l_strId, l_strAct, l_strFromId] {
        m_oP.m_jS["floors"].push_back(Json{{"id", l_strId}, {"act", l_strAct}, {"role", "normal"}, {"events", Json::array()}, {"next", nullptr}, {"derived", false},
                                       {"stairs", Json::array({Json{{"dir", "down"}, {"to", l_strFromId}}})}});
    });
}

void EventEditor::f_CreateChapter(const std::string& l_strAct) {
    std::vector<std::string> l_vIds;
    for (const auto& l_oP : m_oP.f_Problems()) if (l_oP.m_strRule == "missing_event" && l_oP.m_strAct == l_strAct) l_vIds.push_back(l_oP.m_strId);
    if (l_vIds.empty()) return;
    f_Edit(tr("Create %1 missing events for %2").arg(l_vIds.size()).arg(f_Q(l_strAct)), [this, l_vIds, l_strAct] { for (const std::string& l_strId : l_vIds) m_oP.f_MakeRecord(l_strId, l_strAct); });
    f_Toast(tr("Created %1 events for %2. Common variants went to pool_common, the rest to %3. They still need kinds and text: see the Problems tab.")
              .arg(l_vIds.size()).arg(f_Q(l_strAct), QStringLiteral("pool_act") + f_Q(l_strAct.size() > 3 ? l_strAct.substr(3) : l_strAct)));
}

// ---- a floor -------------------------------------------------------------------------------------------------------

QWidget* EventEditor::f_FloorForm(Json& l_jF) {
    const std::string l_strFid = l_jF["id"].get<std::string>();
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    l_pV->setContentsMargins(10, 8, 10, 10);
    auto l_fnEF = [this, l_strFid](const QString& l_oLabel, const std::function<void(Json&)>& l_fnFn) {   // a touched floor writes its own stairs list
        f_Edit(l_oLabel, [this, l_strFid, l_fnFn] { if (Json* l_pX = m_oP.f_Flo(l_strFid)) { l_fnFn(*l_pX); (*l_pX)["derived"] = false; } });
    };
    l_pV->addWidget(f_Rich(QStringLiteral("<span style='font-size:16px;font-weight:600'>%1</span> <span style='color:#8b929c'>%2 · %3</span>%4")
                          .arg(f_Esc(f_Q(l_strFid)), f_Esc(f_Q(l_jF["act"].get<std::string>())), f_Esc(f_Q(l_jF["role"].get<std::string>())),
                               m_oP.f_IsNewFloor(l_strFid) ? QStringLiteral(" <b style='color:#4cd18a'>%1</b>").arg(tr("NEW")) : QString())));
    const auto l_setReach = m_oP.f_Reachable();
    if (!l_setReach.count(l_strFid)) l_pV->addWidget(f_Banner(tr("⚠ <b>Unreachable:</b> no path of open or unlockable stairs leads here from F01."), false));
    {
        auto* l_pH = f_Row(new QLabel(tr("chapter")));
        auto* l_pAct = new QComboBox;
        std::set<std::string> l_setActs;
        for (const Json& l_jX : m_oP.m_jS["floors"]) l_setActs.insert(l_jX["act"].get<std::string>());
        for (const std::string& l_strA : l_setActs) l_pAct->addItem(f_Q(l_strA));
        l_pAct->setCurrentText(f_Q(l_jF["act"].get<std::string>()));
        connect(l_pAct, &QComboBox::activated, this, [=](int) {
            const std::string l_strA = l_pAct->currentText().toStdString();
            l_fnEF(tr("%1 chapter → %2").arg(f_Q(l_strFid), f_Q(l_strA)), [l_strA](Json& l_jX) { l_jX["act"] = l_strA; });
        });
        l_pH->addWidget(l_pAct);
        l_pH->addWidget(new QLabel(tr("role")));
        auto* l_pRole = new QLineEdit(f_Q(l_jF["role"].get<std::string>()));
        l_pRole->setEnabled(false);
        l_pH->addWidget(l_pRole);
        l_pH->addStretch(1);
        l_pV->addLayout(l_pH);
    }
    // stairs
    l_pV->addWidget(f_Heading(tr("Stairs (%1)").arg(l_jF["stairs"].size())));
    const Json l_jStairs = l_jF["stairs"];
    std::vector<std::string> l_vHere, l_vDefined;
    for (const Json& l_jX : l_jF["events"]) if (m_oP.f_Ev(l_jX.get<std::string>())) l_vHere.push_back(l_jX.get<std::string>());
    for (const Json& l_jE : m_oP.m_jS["events"]) l_vDefined.push_back(l_jE["id"].get<std::string>());
    if (l_jStairs.empty()) l_pV->addWidget(f_Muted(tr("No stairs.")));
    for (size_t l_nI = 0; l_nI < l_jStairs.size(); l_nI++) {
        const Json l_jSt = l_jStairs[l_nI];
        const std::string l_strTo = l_jSt.value("to", std::string());
        const std::vector<std::string> l_vLocks = stage::f_StairUnlockEvents(l_jSt);   // W38: the stair opens when all are done
        const std::string l_strLock = l_vLocks.empty() ? std::string() : l_vLocks[0];
        auto* l_pH = f_Row();
        auto* l_pDir = new QComboBox;
        l_pDir->addItem(tr("↑ up"), QStringLiteral("up"));
        l_pDir->addItem(tr("↓ down"), QStringLiteral("down"));
        l_pDir->setCurrentIndex(l_jSt.value("dir", std::string("up")) == "down" ? 1 : 0);
        connect(l_pDir, &QComboBox::activated, this, [=](int) {
            const std::string l_strD = l_pDir->currentData().toString().toStdString();
            l_fnEF(tr("Stair %1 of %2: direction").arg(l_nI + 1).arg(f_Q(l_strFid)), [l_nI, l_strD](Json& l_jX) { l_jX["stairs"][l_nI]["dir"] = l_strD; });
        });
        auto* l_pToC = new QComboBox;
        l_pToC->setEditable(true);
        for (const Json& l_jX : m_oP.m_jS["floors"]) l_pToC->addItem(f_Q(l_jX["id"].get<std::string>()));
        l_pToC->setEditText(f_Q(l_strTo));
        l_pToC->lineEdit()->setPlaceholderText(tr("floor id"));
        connect(l_pToC->lineEdit(), &QLineEdit::editingFinished, this, [=] {
            const std::string l_strNv = l_pToC->currentText().trimmed().toStdString();
            if (l_strNv == l_strTo) return;
            if (!l_strNv.empty() && !stage::f_NameOk(l_strNv, true)) { f_Toast(tr("A floor name is also its file name: any name works, Chinese too, except \" ' < > & | \\ / : * ?"), true); return; }
            l_fnEF(tr("Stair of %1 → %2").arg(f_Q(l_strFid), f_Q(l_strNv)), [l_nI, l_strNv](Json& l_jX) { l_jX["stairs"][l_nI]["to"] = l_strNv; });
        });
        connect(l_pToC, &QComboBox::activated, this, [=](int) { l_pToC->lineEdit()->editingFinished(); });
        // the event list of a combo: this floor's events first, then the others ("\x01": a heading, not choosable)
        auto l_fnFillEvents = [&](QComboBox* l_pC, const QString& l_oNone, const std::string& l_strCurrent) {
            l_pC->addItem(l_oNone, QString());
            if (!l_strCurrent.empty() && !m_oP.f_Ev(l_strCurrent)) l_pC->addItem(tr("⛔ %1 (not defined)").arg(f_Q(l_strCurrent)), f_Q(l_strCurrent));
            auto l_fnHdr = [l_pC](const QString& l_oT) {
                l_pC->addItem(l_oT, QStringLiteral("\x01"));
                if (auto* l_pM = qobject_cast<QStandardItemModel*>(l_pC->model())) l_pM->item(l_pC->count() - 1)->setEnabled(false);
            };
            l_fnHdr(tr("events this floor can roll"));
            for (const std::string& l_strX : l_vHere) l_pC->addItem(f_Q(l_strX), f_Q(l_strX));
            l_fnHdr(tr("other events"));
            for (const std::string& l_strX : l_vDefined) if (std::find(l_vHere.begin(), l_vHere.end(), l_strX) == l_vHere.end()) l_pC->addItem(f_Q(l_strX), f_Q(l_strX));
            l_pC->setCurrentIndex(std::max(0, l_pC->findData(f_Q(l_strCurrent))));
        };
        auto* l_pUn = new QComboBox;
        l_fnFillEvents(l_pUn, tr("(always open)"), l_strLock);
        connect(l_pUn, &QComboBox::activated, this, [=](int) {
            const std::string l_strEv2 = l_pUn->currentData().toString().toStdString();
            if (l_strEv2 == "\x01") return;
            std::vector<std::string> l_vNl = l_vLocks;   // the first event changes; the others stay (none: the stair opens)
            if (l_strEv2.empty()) l_vNl.clear(); else if (l_vNl.empty()) l_vNl.push_back(l_strEv2); else l_vNl[0] = l_strEv2;
            l_fnEF(l_strEv2.empty() ? tr("Unlock %1 → %2").arg(f_Q(l_strFid), f_Q(l_strTo)) : tr("Lock %1 → %2 until %3").arg(f_Q(l_strFid), f_Q(l_strTo), f_Q(l_strEv2)),
               [l_nI, l_vNl](Json& l_jX) { stage::f_SetStairUnlockEvents(l_jX["stairs"][l_nI], l_vNl); });
        });
        auto* l_pDel = new QToolButton;
        l_pDel->setText(QStringLiteral("✕"));
        connect(l_pDel, &QToolButton::clicked, this, [=] { l_fnEF(tr("Remove stair %1 → %2").arg(f_Q(l_strFid), f_Q(l_strTo)), [l_nI](Json& l_jX) { l_jX["stairs"].erase(l_nI); }); });
        l_pH->addWidget(l_pDir);
        l_pH->addWidget(l_pToC, 1);
        l_pH->addWidget(f_Muted(tr("unlock when done:")));
        l_pH->addWidget(l_pUn, 2);
        l_pH->addWidget(l_pDel);
        l_pV->addLayout(l_pH);
        if (!l_vLocks.empty()) {   // W38: more events the stair waits for (all must be done), each with ✕, and "+ another"
            auto* l_pEh = f_Row();
            l_pEh->addWidget(f_Muted(tr("and when done:")));
            for (size_t l_nK = 1; l_nK < l_vLocks.size(); l_nK++) {
                auto* l_pC = f_Chip(QStringLiteral("🔒 ") + f_Q(l_vLocks[l_nK]) + (m_oP.f_Ev(l_vLocks[l_nK]) ? QString() : QStringLiteral(" ⛔")) + QStringLiteral("  ✕"));
                l_pC->setToolTip(tr("Remove this event from the lock"));
                connect(l_pC, &QToolButton::clicked, this, [=] {
                    std::vector<std::string> l_vNl = l_vLocks;
                    l_vNl.erase(l_vNl.begin() + (long)l_nK);
                    l_fnEF(tr("Lock %1 → %2: without %3").arg(f_Q(l_strFid), f_Q(l_strTo), f_Q(l_vLocks[l_nK])), [l_nI, l_vNl](Json& l_jX) { stage::f_SetStairUnlockEvents(l_jX["stairs"][l_nI], l_vNl); });
                });
                l_pEh->addWidget(l_pC);
            }
            auto* l_pMore = new QComboBox;
            l_fnFillEvents(l_pMore, tr("+ another event"), std::string());
            connect(l_pMore, &QComboBox::activated, this, [=](int) {
                const std::string l_strEv2 = l_pMore->currentData().toString().toStdString();
                if (l_strEv2.empty() || l_strEv2 == "\x01") return;
                std::vector<std::string> l_vNl = l_vLocks;
                l_vNl.push_back(l_strEv2);
                l_fnEF(tr("Lock %1 → %2 also until %3").arg(f_Q(l_strFid), f_Q(l_strTo), f_Q(l_strEv2)), [l_nI, l_vNl](Json& l_jX) { stage::f_SetStairUnlockEvents(l_jX["stairs"][l_nI], l_vNl); });
            });
            l_pEh->addWidget(l_pMore, 1);
            l_pEh->addStretch(1);
            l_pV->addLayout(l_pEh);
        }
        if (!l_strTo.empty() && !m_oP.f_Flo(l_strTo)) {
            auto* l_pEh = f_Row();
            l_pEh->addWidget(f_Rich(QStringLiteral("<span style='color:#ff5d5d'>%1</span>").arg(tr("⛔ %1 does not exist yet.").arg(f_Esc(f_Q(l_strTo))))));
            auto* l_pMk = new QPushButton(tr("Create floor %1").arg(f_Q(l_strTo)));
            connect(l_pMk, &QPushButton::clicked, this, [this, l_strTo, l_strFid] { f_CreateFloor(l_strTo, l_strFid); });
            l_pEh->addWidget(l_pMk);
            l_pEh->addStretch(1);
            l_pV->addLayout(l_pEh);
        }
    }
    {
        auto* l_pAdd = new QPushButton(tr("+ Add stair"));
        connect(l_pAdd, &QPushButton::clicked, this, [=] { l_fnEF(tr("Add stair to %1").arg(f_Q(l_strFid)), [](Json& l_jX) { l_jX["stairs"].push_back(Json{{"dir", "up"}, {"to", ""}}); }); });
        auto* l_pH = f_Row();
        l_pH->addWidget(l_pAdd);
        l_pH->addStretch(1);
        l_pV->addLayout(l_pH);
    }
    // reached from, events
    l_pV->addWidget(f_Heading(tr("Reached from")));
    {
        auto* l_pChips = new QWidget;
        auto* l_pFlow = new FlowLayout(l_pChips, 4);
        int l_iN = 0;
        for (const Json& l_jO : m_oP.m_jS["floors"])
            for (const Json& l_jSt : l_jO["stairs"])
                if (l_jSt.value("to", std::string()) == l_strFid) {
                    const std::string l_strOid = l_jO["id"].get<std::string>();
                    QString l_oLock;
                    for (const std::string& l_strE : stage::f_StairUnlockEvents(l_jSt)) l_oLock += (l_oLock.isEmpty() ? QStringLiteral(" 🔒 ") : QStringLiteral(" + ")) + f_Q(l_strE);
                    auto* l_pC = f_Chip((l_jSt.value("dir", std::string("up")) == "down" ? QStringLiteral("↓ ") : QStringLiteral("↑ ")) + f_Q(l_strOid) + l_oLock);
                    connect(l_pC, &QToolButton::clicked, this, [this, l_strOid] { f_Select(l_strOid); });
                    l_pFlow->addWidget(l_pC);
                    l_iN++;
                    break;
                }
        if (!l_iN) l_pFlow->addWidget(f_Muted(QStringLiteral("—")));
        l_pV->addWidget(l_pChips);
    }
    l_pV->addWidget(f_Heading(tr("Events it can roll (%1)").arg(l_jF["events"].size())));
    {
        auto* l_pChips = new QWidget;
        auto* l_pFlow = new FlowLayout(l_pChips, 4);
        for (const Json& l_jX : l_jF["events"]) {
            const std::string l_strId = l_jX.get<std::string>();
            auto* l_pC = f_Chip(f_Q(l_strId), m_oP.f_Ev(l_strId) ? QColor() : QColor("#ff5d5d"));
            connect(l_pC, &QToolButton::clicked, this, [this, l_strId] { f_Select(l_strId); });
            l_pFlow->addWidget(l_pC);
        }
        if (l_jF["events"].empty()) l_pFlow->addWidget(f_Muted(QStringLiteral("—")));
        l_pV->addWidget(l_pChips);
    }
    l_pV->addStretch(1);
    return l_pW;
}

// ---- a missing id --------------------------------------------------------------------------------------------------

QWidget* EventEditor::f_MissingForm(const std::string& l_strId) {
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    l_pV->setContentsMargins(10, 8, 10, 10);
    const auto l_vFl = m_oP.f_FloorsOf(l_strId);
    const std::string l_strAct = (*l_vFl[0])["act"].get<std::string>();
    const std::string l_strPool = f_StartsWith(l_strId, "ev_common_") ? "pool_common" : "pool_act" + (l_strAct.size() > 3 ? l_strAct.substr(3) : l_strAct);
    l_pV->addWidget(f_Banner(QStringLiteral("<b style='color:#ff5d5d'>%1</b><br>%2").arg(tr("⛔ Missing event id: %1").arg(f_Esc(f_Q(l_strId))),
                        tr("%1 floor(s) list this id, but no pool defines it. When one of these floors rolls it, there is no event to show.").arg(l_vFl.size())), true));
    l_pV->addWidget(f_Heading(tr("Listed by")));
    auto* l_pChips = new QWidget;
    auto* l_pFlow = new FlowLayout(l_pChips, 4);
    for (const Json* l_pF : l_vFl) {
        const std::string l_strFid = (*l_pF)["id"].get<std::string>();
        auto* l_pC = f_Chip(f_Q(l_strFid) + QStringLiteral(" ") + f_Q((*l_pF)["act"].get<std::string>()));
        connect(l_pC, &QToolButton::clicked, this, [this, l_strFid] { f_Select(l_strFid); });
        l_pFlow->addWidget(l_pC);
    }
    l_pV->addWidget(l_pChips);
    l_pV->addWidget(f_Heading(tr("Text")));
    const Json l_jTx = m_oP.m_jS["texts"].contains(l_strId) ? m_oP.m_jS["texts"][l_strId] : Json();
    if (l_jTx.is_object())
        l_pV->addWidget(f_Rich(tr("A placeholder exists in text.json under <code>%1</code>%2: “%3”").arg(f_Esc(f_Q(l_strId)),
                          l_jTx.contains("_todo") ? tr(" (marked _todo)") : QString(), f_Esc(f_Q(l_jTx.value("zh_TW", std::string()))))));
    else l_pV->addWidget(f_Muted(tr("No string in text.json yet.")));
    l_pV->addWidget(f_Heading(tr("Fix it")));
    {
        auto* l_pH = f_Row();
        auto* l_pMk = new QPushButton(tr("Create in %1").arg(f_Q(l_strPool)));
        l_pMk->setDefault(true);
        connect(l_pMk, &QPushButton::clicked, this, [this, l_strId, l_strAct, l_strPool] {
            f_Edit(tr("Create %1 in %2").arg(f_Q(l_strId), f_Q(l_strPool)), [this, l_strId, l_strAct] { m_oP.f_MakeRecord(l_strId, l_strAct); m_oP.m_strSelected = l_strId; });
        });
        l_pH->addWidget(l_pMk);
        const std::string l_strGuess = m_oP.f_GuessKind(l_strId);
        l_pH->addWidget(f_Muted((std::find(m_oP.m_jS["pools"].begin(), m_oP.m_jS["pools"].end(), Json(l_strPool)) == m_oP.m_jS["pools"].end()
                                ? tr("%1.json does not exist yet; it will be created. ").arg(f_Q(l_strPool)) : QString())
                           + tr("Kind guess:") + QStringLiteral(" ") + (l_strGuess.empty() ? tr("none, choose one") : f_Q(l_strGuess))), 1);
        l_pV->addLayout(l_pH);
    }
    {
        int l_iN = 0;
        for (const auto& l_oP : m_oP.f_Problems()) if (l_oP.m_strRule == "missing_event" && l_oP.m_strAct == l_strAct) l_iN++;
        auto* l_pCh = new QPushButton(tr("Create all %1 missing ids of %2").arg(l_iN).arg(f_Q(l_strAct)));
        connect(l_pCh, &QPushButton::clicked, this, [this, l_strAct] { f_CreateChapter(l_strAct); });
        auto* l_pH = f_Row();
        l_pH->addWidget(l_pCh);
        l_pH->addStretch(1);
        l_pV->addLayout(l_pH);
    }
    {
        auto* l_pRm = new QPushButton(tr("Remove from floors"));
        connect(l_pRm, &QPushButton::clicked, this, [this, l_strId] {
            f_Edit(tr("Remove %1 from floors").arg(f_Q(l_strId)), [this, l_strId] {
                for (Json& l_jF : m_oP.m_jS["floors"]) { Json& l_jL = l_jF["events"]; l_jL.erase(std::remove(l_jL.begin(), l_jL.end(), Json(l_strId)), l_jL.end()); }
                m_oP.m_strSelected.clear();
            });
        });
        auto* l_pH = f_Row();
        l_pH->addWidget(l_pRm);
        l_pH->addWidget(f_Muted(tr("if the id is a typo or the event was dropped")), 1);
        l_pV->addLayout(l_pH);
    }
    l_pV->addStretch(1);
    return l_pW;
}
