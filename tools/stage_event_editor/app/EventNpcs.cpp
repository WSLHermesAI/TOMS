// EventNpcs.cpp -- the event editor's NPCs tab (an NPC file, its chat lines and topics, the talk preview) and Dialogues
// tab (a dialogue file's nodes as cards). docs/gameplay/event_editor/08_NPC_DIALOGUE.md; event_editor.html's renderNpcs,
// previewHtml and renderDialogues.
#include "EventEditor.h"

#include "EventWidgets.h"
#include "FlowLayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

using stage::Json;
using namespace EvUi;

namespace {
QHBoxLayout* f_Lrow(const QString& l_oLabel) {
    auto* l_pH = new QHBoxLayout;
    l_pH->setContentsMargins(0, 0, 0, 0);
    auto* l_pL = new QLabel(l_oLabel);
    l_pL->setMinimumWidth(130);
    l_pL->setStyleSheet(QStringLiteral("color:#8b929c"));
    l_pH->addWidget(l_pL);
    return l_pH;
}
Json f_Field(const Json& l_jV) { return l_jV.is_object() ? l_jV : Json{{"en", l_jV.is_string() ? l_jV.get<std::string>() : std::string()}}; }
std::string f_LineDialogue(const Json& l_jL) { return l_jL.is_string() ? l_jL.get<std::string>() : l_jL.value("dialogue", std::string()); }
}  // namespace

// ---- NPCs ----------------------------------------------------------------------------------------------------------

QWidget* EventEditor::f_NpcsPane() {
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    const std::vector<std::string> l_vIds = m_oP.f_NpcIds();
    if (m_strNpcSel.empty() || std::find(l_vIds.begin(), l_vIds.end(), m_strNpcSel) == l_vIds.end()) m_strNpcSel = l_vIds.empty() ? std::string() : l_vIds[0];
    // the NPCs, and a new one
    {
        auto* l_pChips = new QWidget;
        auto* l_pFlow = new FlowLayout(l_pChips, 4);
        for (const std::string& l_strN : l_vIds) {
            auto* l_pC = f_Chip(f_Q(l_strN) + (m_oP.m_jS["npcs"].contains(l_strN) ? QString() : QStringLiteral(" ⚠")), l_strN == m_strNpcSel ? QColor("#4c9eff") : QColor());
            connect(l_pC, &QToolButton::clicked, this, [this, l_strN] { m_strNpcSel = l_strN; f_ScheduleRender(); });
            l_pFlow->addWidget(l_pC);
        }
        auto* l_pNw = new QLineEdit;
        l_pNw->setPlaceholderText(tr("new NPC id"));
        l_pNw->setMaximumWidth(150);
        auto* l_pNb = new QPushButton(tr("+ NPC"));
        auto l_fnCreate = [this](const std::string& l_strId) {
            f_Edit(tr("New NPC %1").arg(f_Q(l_strId)), [this, l_strId] {
                const std::string l_strD = m_oP.m_jS["dialogues"].contains(l_strId) ? l_strId : m_oP.f_DialogueList().empty() ? l_strId : m_oP.f_DialogueList()[0];
                m_oP.m_jS["npcs"][l_strId] = Json{{"npcId", l_strId}, {"name", "npc." + l_strId + ".name"}, {"chat", Json{{"lines", Json::array({Json{{"dialogue", l_strD}}})}, {"order", "loop"}}},
                                         {"topics", Json{{"menu", "auto"}}}};
                if (!m_oP.m_jS["texts"].contains("npc." + l_strId + ".name")) m_oP.m_jS["texts"]["npc." + l_strId + ".name"] = Json{{"zh_TW", l_strId}, {"en", l_strId}};
            });
            m_strNpcSel = l_strId;
        };
        connect(l_pNb, &QPushButton::clicked, this, [=] {
            const std::string l_strN = l_pNw->text().trimmed().toStdString();
            if (!stage::f_IdOk(l_strN)) { f_Toast(tr("Use letters, digits, _ and - only."), true); return; }
            if (m_oP.m_jS["npcs"].contains(l_strN)) { f_Toast(tr("%1 already exists.").arg(f_Q(l_strN)), true); return; }
            l_fnCreate(l_strN);
        });
        l_pFlow->addWidget(l_pNw);
        l_pFlow->addWidget(l_pNb);
        l_pV->addWidget(l_pChips);
        if (m_strNpcSel.empty()) { l_pV->addWidget(f_Muted(tr("No NPCs yet."))); l_pV->addStretch(1); return l_pW; }
        if (!m_oP.m_jS["npcs"].contains(m_strNpcSel)) {
            l_pV->addWidget(f_Rich(tr("%1 has no NPC file: the game plays dialogue/%1.json as its only chat line.").arg(f_Esc(f_Q(m_strNpcSel)))));
            auto* l_pMk = new QPushButton(tr("Create its NPC file"));
            const std::string l_strId = m_strNpcSel;
            connect(l_pMk, &QPushButton::clicked, this, [=] { l_fnCreate(l_strId); });
            l_pV->addWidget(l_pMk, 0, Qt::AlignLeft);
            l_pV->addStretch(1);
            return l_pW;
        }
    }
    const std::string l_strId = m_strNpcSel;
    const Json l_jF = m_oP.m_jS["npcs"][l_strId];
    const QString l_oHNpc = tr("NPC %1").arg(f_Q(l_strId));
    auto l_fnF = [this, l_strId, l_oHNpc](const std::function<void(Json&)>& l_fnFn) { f_Edit(l_oHNpc, [this, l_strId, l_fnFn] { l_fnFn(m_oP.m_jS["npcs"][l_strId]); }); };
    // lines as objects (a plain string line is a dialogue id)
    auto l_fnLinesOf = [](Json& l_jNpc) -> Json& {
        if (!l_jNpc.contains("chat") || !l_jNpc["chat"].is_object()) l_jNpc["chat"] = Json{{"lines", Json::array()}};
        Json& l_jL = l_jNpc["chat"]["lines"];
        if (!l_jL.is_array()) l_jL = Json::array();
        for (Json& l_jX : l_jL) if (l_jX.is_string()) l_jX = Json{{"dialogue", l_jX}};
        return l_jL;
    };
    // the file: name, greeting, sprite, where, menu rule
    {
        auto* l_pG = new QGroupBox(tr("NPC file · npcs/%1.json").arg(f_Q(l_strId)));
        auto* l_pGv = new QVBoxLayout(l_pG);
        for (const char* l_pszKey : {"name", "greeting"}) {
            auto* l_pH = f_Lrow(std::string(l_pszKey) == "name" ? tr("Name") : tr("Greeting (menu)"));
            const Json l_jT = l_jF.contains(l_pszKey) ? m_oP.f_Text(l_jF[l_pszKey].get<std::string>()) : Json::object();
            for (const char* l_pszL : {"zh_TW", "en"}) {
                auto* l_pE = new CommitLine(l_jT.contains(l_pszL) && l_jT[l_pszL].is_string() ? f_Q(l_jT[l_pszL].get<std::string>()) : QString());
                l_pE->setPlaceholderText(QString::fromLatin1(l_pszL));
                const std::string l_strK = l_pszKey, l_strLang = l_pszL;
                l_pE->m_fnOnCommit = [=](const QString& l_oS) {
                    f_Edit(l_oHNpc, [this, l_strId, l_strK, l_strLang, l_oS] {
                        Json& l_jN = m_oP.m_jS["npcs"][l_strId];
                        if (!l_jN.contains(l_strK) || !l_jN[l_strK].is_string() || l_jN[l_strK].get<std::string>().empty()) l_jN[l_strK] = "npc." + l_strId + "." + l_strK;
                        Json& l_jTt = m_oP.m_jS["texts"][l_jN[l_strK].get<std::string>()];
                        if (!l_jTt.is_object()) l_jTt = Json::object();
                        l_jTt[l_strLang] = l_oS.toStdString();
                    });
                };
                l_pH->addWidget(l_pE, 1);
            }
            l_pGv->addLayout(l_pH);
        }
        {
            auto* l_pH = f_Lrow(tr("Map sprite"));
            auto* l_pSp = new QComboBox;
            l_pSp->setEditable(true);
            for (const std::string& l_strS : m_oP.m_vSprites) l_pSp->addItem(f_Q(l_strS));
            l_pSp->setEditText(f_Q(l_jF.value("sprite", std::string())));
            l_pSp->lineEdit()->setPlaceholderText(QStringLiteral("npc_") + f_Q(l_strId));
            connect(l_pSp->lineEdit(), &QLineEdit::editingFinished, this, [=] {
                const std::string l_strS = l_pSp->currentText().trimmed().toStdString();
                if (l_strS == l_jF.value("sprite", std::string())) return;
                l_fnF([l_strS](Json& l_jN) { if (l_strS.empty()) l_jN.erase("sprite"); else l_jN["sprite"] = l_strS; });
            });
            l_pH->addWidget(l_pSp, 1);
            l_pGv->addLayout(l_pH);
        }
        {
            auto* l_pH = f_Lrow(tr("Placed on"));
            auto l_itIt = m_oP.m_mapNpcPlaces.find(l_strId);
            QStringList l_oFl;
            if (l_itIt != m_oP.m_mapNpcPlaces.end()) for (const std::string& l_strX : l_itIt->second) l_oFl << f_Q(l_strX);
            l_pH->addWidget(l_oFl.isEmpty() ? f_Rich(QStringLiteral("<span style='color:#ff9f43'>⚠ %1</span>").arg(tr("not placed on any floor (place it in the stage editor)"))) : f_Rich(l_oFl.join(QStringLiteral(", "))), 1);
            l_pGv->addLayout(l_pH);
        }
        {
            auto* l_pH = f_Lrow(tr("Topic menu"));
            auto* l_pM = new QComboBox;
            l_pM->addItem(tr("auto: only when more than one topic"), QStringLiteral("auto"));
            l_pM->addItem(tr("always"), QStringLiteral("always"));
            l_pM->setCurrentIndex(l_jF.contains("topics") && l_jF["topics"].value("menu", std::string()) == "always" ? 1 : 0);
            connect(l_pM, &QComboBox::activated, this, [=](int) {
                const std::string l_strS = l_pM->currentData().toString().toStdString();
                l_fnF([l_strS](Json& l_jN) { if (!l_jN.contains("topics") || !l_jN["topics"].is_object()) l_jN["topics"] = Json::object(); l_jN["topics"]["menu"] = l_strS; });
            });
            l_pH->addWidget(l_pM);
            l_pH->addStretch(1);
            l_pGv->addLayout(l_pH);
        }
        l_pV->addWidget(l_pG);
    }
    // chat lines
    {
        auto* l_pG = new QGroupBox(tr("Chat lines (when no topic is offered)"));
        auto* l_pGv = new QVBoxLayout(l_pG);
        auto* l_pH = f_Lrow(tr("Order"));
        auto* l_pOrder = new QComboBox;
        l_pOrder->addItem(tr("loop: in order, then back to the first"), QStringLiteral("loop"));
        l_pOrder->addItem(tr("stay: in order, then repeat the last"), QStringLiteral("stay"));
        l_pOrder->addItem(tr("random (never twice in a row)"), QStringLiteral("random"));
        l_pOrder->setCurrentIndex(std::max(0, l_pOrder->findData(f_Q(l_jF.contains("chat") ? l_jF["chat"].value("order", std::string("loop")) : std::string("loop")))));
        connect(l_pOrder, &QComboBox::activated, this, [=](int) {
            const std::string l_strO = l_pOrder->currentData().toString().toStdString();
            l_fnF([l_strO, l_fnLinesOf](Json& l_jN) { l_fnLinesOf(l_jN); l_jN["chat"]["order"] = l_strO; });
        });
        l_pH->addWidget(l_pOrder);
        l_pH->addStretch(1);
        l_pGv->addLayout(l_pH);
        const Json l_jLines = l_jF.contains("chat") && l_jF["chat"].contains("lines") && l_jF["chat"]["lines"].is_array() ? l_jF["chat"]["lines"] : Json::array();
        for (size_t l_nI = 0; l_nI < l_jLines.size(); l_nI++) {
            const Json l_jLine = l_jLines[l_nI].is_string() ? Json{{"dialogue", l_jLines[l_nI]}} : l_jLines[l_nI];
            auto* l_pCard = new QWidget;
            l_pCard->setStyleSheet(QStringLiteral("QWidget#card { border-top:1px solid #3a3f47; }"));
            l_pCard->setObjectName(QStringLiteral("card"));
            auto* l_pCv = new QVBoxLayout(l_pCard);
            auto* l_pR = f_Lrow(QString::number(l_nI + 1));
            auto* l_pD = new QComboBox;
            std::vector<std::string> l_vDs = m_oP.f_DialogueList();
            const std::string l_strCur = f_LineDialogue(l_jLine);
            if (!l_strCur.empty() && std::find(l_vDs.begin(), l_vDs.end(), l_strCur) == l_vDs.end()) l_vDs.push_back(l_strCur);
            for (const std::string& l_strX : l_vDs) l_pD->addItem(f_Q(l_strX));
            l_pD->setCurrentText(f_Q(l_strCur));
            connect(l_pD, &QComboBox::activated, this, [=](int) { const std::string l_strS = l_pD->currentText().toStdString(); l_fnF([l_nI, l_strS, l_fnLinesOf](Json& l_jN) { l_fnLinesOf(l_jN)[l_nI]["dialogue"] = l_strS; }); });
            auto* l_pOpen = new QToolButton;
            l_pOpen->setText(tr("open ↗"));
            connect(l_pOpen, &QToolButton::clicked, this, [this, l_strCur] { m_strDlgSel = l_strCur; f_SetTab(QStringLiteral("dlg")); });
            auto l_fnMk = [&](const QString& l_oT, int l_iDir) {
                auto* l_pB = new QToolButton;
                l_pB->setText(l_oT);
                connect(l_pB, &QToolButton::clicked, this, [=] {
                    l_fnF([l_nI, l_iDir, l_fnLinesOf](Json& l_jN) {
                        Json& l_jL = l_fnLinesOf(l_jN);
                        if (l_iDir == 0) { l_jL.erase(l_nI); return; }
                        const long l_iJ = (long)l_nI + l_iDir;
                        if (l_iJ < 0 || l_iJ >= (long)l_jL.size()) return;
                        std::swap(l_jL[l_nI], l_jL[(size_t)l_iJ]);
                    });
                });
                return l_pB;
            };
            l_pR->addWidget(l_pD, 1);
            l_pR->addWidget(l_pOpen);
            l_pR->addWidget(l_fnMk(QStringLiteral("▲"), -1));
            l_pR->addWidget(l_fnMk(QStringLiteral("▼"), 1));
            l_pR->addWidget(l_fnMk(QStringLiteral("✕"), 0));
            l_pCv->addLayout(l_pR);
            auto* l_pWr = f_Lrow(tr("Only when"));
            l_pWr->addWidget(f_CondBox(QStringLiteral("line%1").arg(l_nI), [this, l_strId, l_nI] {
                const Json& l_jCs = m_oP.m_jS;
                const Json& l_jN = l_jCs["npcs"][l_strId];
                const Json l_jL = l_jN.contains("chat") && l_jN["chat"].contains("lines") ? l_jN["chat"]["lines"] : Json::array();
                return l_nI < l_jL.size() && l_jL[l_nI].is_object() ? l_jL[l_nI].value("requires", Json()) : Json();
            }, [this, l_strId, l_nI, l_fnLinesOf](const Json& l_jC) {
                Json& l_jL = l_fnLinesOf(m_oP.m_jS["npcs"][l_strId]);
                if (l_nI >= l_jL.size()) return;
                if (l_jC.is_null()) l_jL[l_nI].erase("requires"); else l_jL[l_nI]["requires"] = l_jC;
            }, l_oHNpc), 1);
            l_pCv->addLayout(l_pWr);
            l_pCv->addWidget(f_Muted(tr("When it plays (repeats every time: no rewards)")));
            l_pCv->addWidget(f_ActsBox(QStringLiteral("line%1").arg(l_nI), [l_jLine] { return l_jLine.value("actions", Json::array()); }, [this, l_strId, l_nI, l_fnLinesOf]() -> Json* {
                Json& l_jL = l_fnLinesOf(m_oP.m_jS["npcs"][l_strId]);
                if (l_nI >= l_jL.size()) return nullptr;
                if (!l_jL[l_nI].contains("actions") || !l_jL[l_nI]["actions"].is_array()) l_jL[l_nI]["actions"] = Json::array();
                return &l_jL[l_nI]["actions"];
            }, l_oHNpc));
            bool l_bMiss = false;
            const std::string l_strFirst = m_oPv.f_FirstText(m_oP, l_strCur, f_Lang().toStdString(), &l_bMiss);
            l_pCv->addWidget(f_Muted(QStringLiteral("“%1”").arg(l_bMiss ? tr("(dialogue/%1.json is missing)").arg(f_Q(l_strCur)) : f_Q(l_strFirst).left(80))));
            l_pGv->addWidget(l_pCard);
        }
        auto* l_pAdd = new QPushButton(tr("+ Chat line"));
        connect(l_pAdd, &QPushButton::clicked, this, [=] {
            const std::string l_strD = m_oP.f_DialogueList().empty() ? l_strId : m_oP.f_DialogueList()[0];
            l_fnF([l_strD, l_fnLinesOf](Json& l_jN) { l_fnLinesOf(l_jN).push_back(Json{{"dialogue", l_strD}}); });
        });
        l_pGv->addWidget(l_pAdd, 0, Qt::AlignLeft);
        l_pV->addWidget(l_pG);
    }
    // topics in menu order
    {
        std::vector<Json*> l_vTopics = m_oP.f_TopicsOf(l_strId);
        auto* l_pG = new QGroupBox(tr("Topics (%1) in menu order").arg(l_vTopics.size()));
        auto* l_pGv = new QVBoxLayout(l_pG);
        if (l_vTopics.empty()) l_pGv->addWidget(f_Muted(tr("No talk events yet: set an event's \"Fires when\" to \"the player talks to\" this NPC.")));
        else {
            auto* l_pT = new QTableWidget((int)l_vTopics.size(), 7);
            l_pT->setHorizontalHeaderLabels({QStringLiteral("#"), tr("Event"), tr("Talked to on"), tr("Active only when"), tr("After finishing"), tr("Repeat"), QString()});
            l_pT->verticalHeader()->hide();
            l_pT->setEditTriggers(QAbstractItemView::NoEditTriggers);
            std::vector<std::string> l_vTids;
            for (Json* l_pE : l_vTopics) l_vTids.push_back((*l_pE)["id"].get<std::string>());
            for (size_t l_nI = 0; l_nI < l_vTopics.size(); l_nI++) {
                const Json& l_jE = *l_vTopics[l_nI];
                const std::string l_strEid = l_jE["id"].get<std::string>();
                l_pT->setItem((int)l_nI, 0, new QTableWidgetItem(QString::number(l_nI + 1)));
                auto* l_pC = f_Chip(f_Q(l_strEid));
                l_pC->setToolTip(f_L(m_oP.f_Text(l_jE["title"].get<std::string>())));
                connect(l_pC, &QToolButton::clicked, this, [this, l_strEid] { f_Select(l_strEid); });
                l_pT->setCellWidget((int)l_nI, 1, l_pC);
                bool l_bCond = false;
                const std::string l_strTf = m_oP.f_TalkFloorOf(l_jE, &l_bCond);
                l_pT->setItem((int)l_nI, 2, new QTableWidgetItem(l_bCond ? QStringLiteral("?") : l_strTf.empty() ? tr("every floor") : f_Q(l_strTf)));
                QStringList l_oReq;
                if (!l_jE["reqRaw"].is_null()) l_oReq << f_Q(l_jE["reqRaw"].dump());
                for (const Json& l_jL : l_jE["req"]) l_oReq << f_Q(l_jL.dump());
                l_pT->setItem((int)l_nI, 3, new QTableWidgetItem((l_oReq.isEmpty() ? QStringLiteral("—") : l_oReq.join(QStringLiteral(" & "))) + (m_oP.f_Woken(l_strEid) ? QStringLiteral(" · ") + tr("waits to be woken") : QString())));
                l_pT->setItem((int)l_nI, 4, new QTableWidgetItem(l_jE["afterDialogue"].get<std::string>().empty() ? tr("(hide the topic)") : f_Q(l_jE["afterDialogue"].get<std::string>())));
                l_pT->setItem((int)l_nI, 5, new QTableWidgetItem(l_jE["once"] == false ? (l_jE["maxTimes"].get<int>() ? QStringLiteral("× %1").arg(l_jE["maxTimes"].get<int>()) : QStringLiteral("∞")) : QStringLiteral("—")));
                auto* l_pMv = new QWidget;
                auto* l_pMh = new QHBoxLayout(l_pMv);
                l_pMh->setContentsMargins(0, 0, 0, 0);
                for (int l_iD : {-1, 1}) {   // ▲ / ▼ renumber every topic of this NPC 1..n (topicOrder)
                    auto* l_pB = new QToolButton;
                    l_pB->setText(l_iD < 0 ? QStringLiteral("▲") : QStringLiteral("▼"));
                    l_pB->setEnabled(l_iD < 0 ? l_nI > 0 : l_nI + 1 < l_vTopics.size());
                    connect(l_pB, &QToolButton::clicked, this, [this, l_strId, l_nI, l_iD] {
                        f_Edit(tr("Topic order of %1").arg(f_Q(l_strId)), [this, l_strId, l_nI, l_iD] {
                            std::vector<Json*> l_vTp = m_oP.f_TopicsOf(l_strId);
                            std::swap(l_vTp[l_nI], l_vTp[(size_t)((long)l_nI + l_iD)]);
                            for (size_t l_nK = 0; l_nK < l_vTp.size(); l_nK++) (*l_vTp[l_nK])["topicOrder"] = (int)l_nK + 1;
                        });
                    });
                    l_pMh->addWidget(l_pB);
                }
                l_pT->setCellWidget((int)l_nI, 6, l_pMv);
            }
            l_pT->resizeColumnsToContents();
            l_pT->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
            l_pT->setMinimumHeight(std::min(320, 34 + 30 * (int)l_vTopics.size()));
            l_pGv->addWidget(l_pT);
        }
        l_pV->addWidget(l_pG);
    }
    l_pV->addWidget(f_PreviewBox(l_strId));
    l_pV->addStretch(1);
    return l_pW;
}

// ---- the talk preview ----------------------------------------------------------------------------------------------

QWidget* EventEditor::f_PreviewBox(const std::string& l_strNpc) {
    if (m_oPv.m_strNpc != l_strNpc) m_oPv.f_Reset(m_oP, l_strNpc);
    auto* l_pG = new QGroupBox(tr("Talk preview"));
    auto* l_pV = new QVBoxLayout(l_pG);
    l_pV->addWidget(f_Muted(tr("A pretend run with the game's rules: tick finished events, set values, press Talk.")));
    std::vector<Json*> l_vTopics = m_oP.f_TopicsOf(l_strNpc);
    auto l_fnName = [this](const std::string& l_strId) { const Json* l_pE = m_oP.f_Ev(l_strId); const QString l_oT = l_pE ? f_L(m_oP.f_Text((*l_pE)["title"].get<std::string>())) : QString(); return l_oT.isEmpty() ? f_Q(l_strId) : l_oT; };
    // events the topics wait for, and the topics themselves
    std::vector<std::string> l_vRefs;
    for (Json* l_pE : l_vTopics) {
        for (const Json& l_jL : (*l_pE)["req"]) {
            const std::string l_strF = l_jL.value("flag", std::string());
            if (l_jL.value("type", std::string()) == "runFlagSet" && l_strF.rfind("event_", 0) == 0 && std::find(l_vRefs.begin(), l_vRefs.end(), l_strF.substr(6)) == l_vRefs.end()) l_vRefs.push_back(l_strF.substr(6));
        }
        const std::string l_strId = (*l_pE)["id"].get<std::string>();
        if (std::find(l_vRefs.begin(), l_vRefs.end(), l_strId) == l_vRefs.end()) l_vRefs.push_back(l_strId);
    }
    {
        auto* l_pH = f_Lrow(tr("Finished events"));
        auto* l_pChips = new QWidget;
        auto* l_pFlow = new FlowLayout(l_pChips, 4);
        for (const std::string& l_strId : l_vRefs) {
            auto* l_pC = new QCheckBox(f_Q(l_strId));
            l_pC->setChecked(m_oPv.m_setFinished.count(l_strId) > 0);
            connect(l_pC, &QCheckBox::toggled, this, [this, l_strId](bool l_bOn) { m_oPv.f_Tick(m_oP, l_strId, l_bOn); f_ScheduleRender(); });
            l_pFlow->addWidget(l_pC);
        }
        l_pH->addWidget(l_pChips, 1);
        l_pV->addLayout(l_pH);
    }
    // the values its topics and chat lines read
    {
        const std::string l_strNpcJson = m_oP.m_jS["npcs"].contains(l_strNpc) ? m_oP.m_jS["npcs"][l_strNpc].dump() : std::string();
        auto l_fnUsed = [&](const std::string& l_strName2) {
            const std::string l_strNeedle = "\"" + l_strName2 + "\"";
            if (l_strNpcJson.find(l_strNeedle) != std::string::npos) return true;
            for (Json* l_pE : l_vTopics) if (m_oP.f_ReqJson(*l_pE).dump().find(l_strNeedle) != std::string::npos) return true;
            return false;
        };
        auto* l_pH = f_Lrow(tr("Values"));
        int l_iN = 0;
        for (auto& [l_strVname, l_jR] : m_oP.m_jS["vars"].items()) {
            (void)l_jR;
            if (!l_fnUsed(l_strVname)) continue;
            l_pH->addWidget(f_Muted(f_Q(l_strVname)));
            auto* l_pS = new QSpinBox;
            l_pS->setRange(-99999, 99999);
            l_pS->setValue((int)m_oPv.m_mapVars[l_strVname]);
            const std::string l_strVn = l_strVname;
            connect(l_pS, &QSpinBox::valueChanged, this, [this, l_strVn](int l_iX) { m_oPv.m_mapVars[l_strVn] = l_iX; });
            l_pH->addWidget(l_pS);
            l_iN++;
        }
        for (auto& [l_strCname, l_jR] : m_oP.m_jS["counters"].items()) {
            (void)l_jR;
            bool l_bHit = false;
            for (Json* l_pE : l_vTopics) if (m_oP.f_ReqJson(*l_pE).dump().find("\"" + l_strCname + "\"") != std::string::npos) l_bHit = true;
            if (!l_bHit) continue;
            l_pH->addWidget(f_Muted(f_Q(l_strCname)));
            auto* l_pS = new QSpinBox;
            l_pS->setRange(-99999, 99999);
            l_pS->setValue((int)m_oPv.m_mapCounters[l_strCname]);
            const std::string l_strCn = l_strCname;
            connect(l_pS, &QSpinBox::valueChanged, this, [this, l_strCn](int l_iX) { m_oPv.m_mapCounters[l_strCn] = l_iX; });
            l_pH->addWidget(l_pS);
            l_iN++;
        }
        l_pH->addStretch(1);
        if (l_iN) l_pV->addLayout(l_pH);
        else delete l_pH;
    }
    {
        auto* l_pH = f_Lrow(tr("Talk on floor"));
        auto* l_pFc = new QComboBox;
        auto l_itIt = m_oP.m_mapNpcPlaces.find(l_strNpc);
        std::vector<std::string> l_vFl = l_itIt != m_oP.m_mapNpcPlaces.end() ? l_itIt->second : std::vector<std::string>();
        if (!m_oPv.m_strStage.empty() && std::find(l_vFl.begin(), l_vFl.end(), m_oPv.m_strStage) == l_vFl.end()) l_vFl.push_back(m_oPv.m_strStage);
        for (const std::string& l_strF : l_vFl) l_pFc->addItem(f_Q(l_strF));
        l_pFc->setCurrentText(f_Q(m_oPv.m_strStage));
        connect(l_pFc, &QComboBox::activated, this, [this, l_pFc](int) { m_oPv.m_strStage = l_pFc->currentText().toStdString(); f_ScheduleRender(); });
        l_pH->addWidget(l_pFc);
        l_pH->addStretch(1);
        l_pV->addLayout(l_pH);
    }
    {
        auto* l_pH = f_Lrow(tr("Floors cleared"));
        QStringList l_oCl;
        for (const std::string& l_strC : m_oPv.m_setCleared) l_oCl << f_Q(l_strC);
        auto* l_pE = new CommitLine(l_oCl.join(QStringLiteral(", ")));
        l_pE->setPlaceholderText(QStringLiteral("F05, F10"));
        l_pE->m_fnOnCommit = [this](const QString& l_oS) {
            m_oPv.m_setCleared.clear();
            for (const QString& l_oX : l_oS.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) m_oPv.m_setCleared.insert(l_oX.toStdString());
        };
        l_pH->addWidget(l_pE, 1);
        l_pV->addLayout(l_pH);
    }
    {
        auto* l_pH = new QHBoxLayout;
        auto* l_pTalk = new QPushButton(tr("💬 Talk"));
        l_pTalk->setDefault(true);
        connect(l_pTalk, &QPushButton::clicked, this, [this, l_strNpc] { m_oPv.f_Talk(m_oP, l_strNpc, f_Lang().toStdString()); f_ScheduleRender(); });
        auto* l_pReset = new QPushButton(tr("Reset"));
        connect(l_pReset, &QPushButton::clicked, this, [this, l_strNpc] { m_oPv.f_Reset(m_oP, l_strNpc); f_ScheduleRender(); });
        l_pH->addWidget(l_pTalk);
        l_pH->addWidget(l_pReset);
        const auto l_oSt = m_oPv.m_mapTalks.count(l_strNpc) ? m_oPv.m_mapTalks[l_strNpc] : stage::TalkPreview::TalkState();
        l_pH->addWidget(f_Muted(tr("talks so far: %1 · chat index: %2").arg(l_oSt.m_iCount).arg(l_oSt.m_iIndex)), 1);
        l_pV->addLayout(l_pH);
    }
    for (size_t l_nI = 0; l_nI < m_oPv.m_vLog.size(); l_nI++) {
        const auto& l_oX = m_oPv.m_vLog[l_nI];
        auto* l_pRowW = new QWidget;
        if (l_nI) { auto* l_pEff = new QGraphicsOpacityEffect(l_pRowW); l_pEff->setOpacity(0.55); l_pRowW->setGraphicsEffect(l_pEff); }
        auto* l_pH = new QHBoxLayout(l_pRowW);
        l_pH->setContentsMargins(0, 0, 0, 0);
        auto* l_pL = new QLabel(tr("Talk %1").arg(l_oX.m_iN));
        l_pL->setMinimumWidth(130);
        l_pL->setStyleSheet(QStringLiteral("color:#8b929c"));
        l_pH->addWidget(l_pL);
        auto l_fnItem = [&](const stage::TalkPreview::MenuItem& l_oM) {
            auto* l_pBox = new QWidget;
            auto* l_pBh = new QHBoxLayout(l_pBox);
            l_pBh->setContentsMargins(0, 0, 0, 0);
            l_pBh->setSpacing(2);
            l_pBh->addWidget(f_Chip((l_oM.m_bFinished ? QStringLiteral("✓ ") : QString()) + l_fnName(l_oM.m_strId)));
            if (!l_oM.m_bFinished && l_nI == 0) {
                auto* l_pFin = new QToolButton;
                l_pFin->setText(tr("finish"));
                l_pFin->setToolTip(tr("Pretend the player finished it (its next events wake, its variables change)"));
                const std::string l_strMid = l_oM.m_strId;
                connect(l_pFin, &QToolButton::clicked, this, [this, l_strMid] { m_oPv.f_Finish(m_oP, l_strMid); f_ScheduleRender(); });
                l_pBh->addWidget(l_pFin);
            }
            return l_pBox;
        };
        if (l_oX.m_strKind == "chat") {
            QString l_oT;
            if (l_oX.m_strDialogue.empty()) l_oT = tr("(nothing to say: no chat line is available)");
            else if (l_oX.m_strText == "\x01missing") l_oT = QStringLiteral("💬 %1: ").arg(f_Q(l_oX.m_strDialogue)) + tr("(dialogue/%1.json is missing)").arg(f_Q(l_oX.m_strDialogue));
            else l_oT = QStringLiteral("💬 %1: %2").arg(f_Q(l_oX.m_strDialogue), f_Q(l_oX.m_strText));
            l_pH->addWidget(f_Rich(f_Esc(l_oT)), 1);
        } else if (l_oX.m_strKind == "topic") {
            l_pH->addWidget(f_Muted(tr("plays at once:")));
            l_pH->addWidget(l_fnItem(l_oX.m_vMenu[0]));
            l_pH->addStretch(1);
        } else {
            l_pH->addWidget(f_Muted(tr("menu:")));
            auto* l_pChips = new QWidget;
            auto* l_pFlow = new FlowLayout(l_pChips, 4);
            for (const auto& l_oM : l_oX.m_vMenu) l_pFlow->addWidget(l_fnItem(l_oM));
            l_pFlow->addWidget(f_Chip(tr("Just chat")));
            l_pFlow->addWidget(f_Chip(tr("Leave")));
            l_pH->addWidget(l_pChips, 1);
        }
        l_pV->addWidget(l_pRowW);
    }
    return l_pG;
}

// ---- dialogues -----------------------------------------------------------------------------------------------------

QWidget* EventEditor::f_DialoguesPane() {
    auto* l_pW = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pW);
    const std::vector<std::string> l_vIds = m_oP.f_DialogueList();
    if (m_strDlgSel.empty() || !m_oP.m_jS["dialogues"].contains(m_strDlgSel)) m_strDlgSel = l_vIds.empty() ? std::string() : l_vIds[0];
    {
        auto* l_pH = f_Lrow(tr("Dialogue"));
        auto* l_pPick = new QComboBox;
        for (const std::string& l_strI : l_vIds) l_pPick->addItem(f_Q(l_strI));
        l_pPick->setCurrentText(f_Q(m_strDlgSel));
        connect(l_pPick, &QComboBox::activated, this, [this, l_pPick](int) { m_strDlgSel = l_pPick->currentText().toStdString(); f_ScheduleRender(); });
        auto* l_pNw = new QLineEdit;
        l_pNw->setPlaceholderText(tr("new dialogue id"));
        l_pNw->setMaximumWidth(170);
        auto* l_pNb = new QPushButton(tr("+ Dialogue"));
        connect(l_pNb, &QPushButton::clicked, this, [=] {
            const std::string l_strN = l_pNw->text().trimmed().toStdString();
            if (!stage::f_IdOk(l_strN)) { f_Toast(tr("Use letters, digits, _ and - only."), true); return; }
            if (m_oP.m_jS["dialogues"].contains(l_strN)) { f_Toast(tr("%1 already exists.").arg(f_Q(l_strN)), true); return; }
            f_Edit(tr("New dialogue %1").arg(f_Q(l_strN)), [this, l_strN] {
                m_oP.m_jS["dialogues"][l_strN] = Json{{"start", "root"}, {"nodes", Json{{"root", Json{{"text", Json{{"zh_TW", ""}, {"en", ""}}},
                                                                                             {"choices", Json::array({Json{{"label", Json{{"zh_TW", "……"}, {"en", "..."}}}, {"next", nullptr}}})}}}}}};
            });
            m_strDlgSel = l_strN;
        });
        l_pH->addWidget(l_pPick, 1);
        l_pH->addWidget(l_pNw);
        l_pH->addWidget(l_pNb);
        l_pV->addLayout(l_pH);
    }
    if (m_strDlgSel.empty()) { l_pV->addStretch(1); return l_pW; }
    const std::string l_strId = m_strDlgSel;
    const Json l_jD = m_oP.m_jS["dialogues"][l_strId];
    const Json l_jNodes = l_jD.value("nodes", Json::object());
    std::vector<std::string> l_vNids;
    for (auto& [l_strK, l_jN] : l_jNodes.items()) { (void)l_jN; l_vNids.push_back(l_strK); }
    const bool l_bCondStart = l_jD.contains("start") && l_jD["start"].is_array();
    const QString l_oHD = tr("Dialogue %1").arg(f_Q(l_strId));
    auto l_fnE = [this, l_strId, l_oHD](const std::function<void(Json&)>& l_fnFn) { f_Edit(l_oHD, [this, l_strId, l_fnFn] { l_fnFn(m_oP.m_jS["dialogues"][l_strId]); }); };
    auto l_fnNodeCombo = [&](const std::string& l_strCur, bool l_bClose) {
        auto* l_pC = new QComboBox;
        if (l_bClose) l_pC->addItem(tr("(close the dialogue)"), QString());
        std::vector<std::string> l_vL = l_vNids;
        if (!l_strCur.empty() && std::find(l_vL.begin(), l_vL.end(), l_strCur) == l_vL.end()) l_vL.push_back(l_strCur);
        for (const std::string& l_strN : l_vL) l_pC->addItem(f_Q(l_strN) + (l_jNodes.contains(l_strN) ? QString() : QStringLiteral(" ⛔")), f_Q(l_strN));
        l_pC->setCurrentIndex(std::max(0, l_pC->findData(f_Q(l_strCur))));
        return l_pC;
    };
    // the file: used by, where it starts
    {
        auto* l_pG = new QGroupBox(f_Q(l_strId) + QStringLiteral(".json"));
        auto* l_pGv = new QVBoxLayout(l_pG);
        auto* l_pH = f_Lrow(tr("Used by"));
        auto* l_pChips = new QWidget;
        auto* l_pFlow = new FlowLayout(l_pChips, 4);
        int l_iN = 0;
        for (const Json& l_jE : m_oP.m_jS["events"]) {
            const std::string l_strEid = l_jE["id"].get<std::string>();
            for (const auto& [l_pszKey, l_oRole] : {std::pair<const char*, QString>{"dialogue", tr("topic")}, {"afterDialogue", tr("after finishing")}})
                if (l_jE[l_pszKey] == l_strId) {
                    auto* l_pC = f_Chip(f_Q(l_strEid) + QStringLiteral(" · ") + l_oRole);
                    connect(l_pC, &QToolButton::clicked, this, [this, l_strEid] { f_Select(l_strEid); });
                    l_pFlow->addWidget(l_pC);
                    l_iN++;
                }
        }
        for (auto& [l_strNid, l_jNpc] : m_oP.m_jS["npcs"].items())
            if (l_jNpc.contains("chat") && l_jNpc["chat"].contains("lines"))
                for (const Json& l_jL : l_jNpc["chat"]["lines"]) if (f_LineDialogue(l_jL) == l_strId) { l_pFlow->addWidget(f_Chip(f_Q(l_strNid) + QStringLiteral(" · ") + tr("chat line"))); l_iN++; break; }
        if (!l_iN) l_pFlow->addWidget(f_Muted(tr("not used yet")));
        l_pH->addWidget(l_pChips, 1);
        l_pGv->addLayout(l_pH);
        auto* l_pSh = f_Lrow(tr("Starts at"));
        if (l_bCondStart) l_pSh->addWidget(f_Muted(tr("the first row whose condition holds:")));
        else {
            auto* l_pSc = l_fnNodeCombo(l_jD.value("start", std::string()), false);
            connect(l_pSc, &QComboBox::activated, this, [=](int) { const std::string l_strS = l_pSc->currentData().toString().toStdString(); l_fnE([l_strS](Json& l_jX) { l_jX["start"] = l_strS; }); });
            l_pSh->addWidget(l_pSc);
        }
        auto* l_pCs = new QCheckBox(tr("by condition"));
        l_pCs->setChecked(l_bCondStart);
        connect(l_pCs, &QCheckBox::toggled, this, [=](bool l_bOn) {
            l_fnE([l_bOn, l_vNids](Json& l_jX) {
                if (l_bOn) l_jX["start"] = Json::array({Json{{"node", l_jX["start"].is_string() ? l_jX["start"].get<std::string>() : l_vNids.empty() ? std::string() : l_vNids[0]}}});
                else l_jX["start"] = l_jX["start"].is_array() && !l_jX["start"].empty() ? l_jX["start"].back().value("node", l_vNids.empty() ? std::string() : l_vNids[0]) : (l_vNids.empty() ? std::string() : l_vNids[0]);
            });
        });
        auto* l_pFoc = new QCheckBox(tr("finish the event however it closes"));
        l_pFoc->setChecked(l_jD.value("finishOnClose", false));
        connect(l_pFoc, &QCheckBox::toggled, this, [=](bool l_bOn) { l_fnE([l_bOn](Json& l_jX) { if (l_bOn) l_jX["finishOnClose"] = true; else l_jX.erase("finishOnClose"); }); });
        l_pSh->addWidget(l_pCs);
        l_pSh->addWidget(l_pFoc);
        l_pSh->addStretch(1);
        l_pGv->addLayout(l_pSh);
        if (l_bCondStart) {
            const Json l_jStarts = l_jD["start"];
            for (size_t l_nI = 0; l_nI < l_jStarts.size(); l_nI++) {
                auto* l_pR = f_Lrow(QString::number(l_nI + 1));
                auto* l_pNc = l_fnNodeCombo(l_jStarts[l_nI].value("node", std::string()), false);
                connect(l_pNc, &QComboBox::activated, this, [=](int) { const std::string l_strS = l_pNc->currentData().toString().toStdString(); l_fnE([l_nI, l_strS](Json& l_jX) { l_jX["start"][l_nI]["node"] = l_strS; }); });
                auto* l_pDel = new QToolButton;
                l_pDel->setText(QStringLiteral("✕"));
                connect(l_pDel, &QToolButton::clicked, this, [=] { l_fnE([l_nI, l_vNids](Json& l_jX) { l_jX["start"].erase(l_nI); if (l_jX["start"].empty()) l_jX["start"] = l_vNids.empty() ? std::string() : l_vNids[0]; }); });
                l_pR->addWidget(l_pNc);
                l_pR->addWidget(f_CondBox(QStringLiteral("st%1").arg(l_nI), [this, l_strId, l_nI] {
                    const Json& l_jCs = m_oP.m_jS;
                    const Json& l_jSt = l_jCs["dialogues"][l_strId]["start"];
                    return l_jSt.is_array() && l_nI < l_jSt.size() ? l_jSt[l_nI].value("requires", Json()) : Json();
                }, [this, l_strId, l_nI](const Json& l_jC) {
                    Json& l_jSt = m_oP.m_jS["dialogues"][l_strId]["start"];
                    if (!l_jSt.is_array() || l_nI >= l_jSt.size()) return;
                    if (l_jC.is_null()) l_jSt[l_nI].erase("requires"); else l_jSt[l_nI]["requires"] = l_jC;
                }, l_oHD, tr("Otherwise (no condition).")), 1);
                l_pR->addWidget(l_pDel);
                l_pGv->addLayout(l_pR);
            }
            auto* l_pAdd = new QPushButton(tr("+ start row"));
            connect(l_pAdd, &QPushButton::clicked, this, [=] { l_fnE([l_vNids](Json& l_jX) { l_jX["start"].push_back(Json{{"node", l_vNids.empty() ? std::string() : l_vNids[0]}}); }); });
            l_pGv->addWidget(l_pAdd, 0, Qt::AlignLeft);
        }
        l_pV->addWidget(l_pG);
    }
    // the nodes as cards
    for (const std::string& l_strNid : l_vNids) {
        const Json l_jN = l_jNodes[l_strNid];
        auto* l_pG = new QGroupBox;
        auto* l_pGv = new QVBoxLayout(l_pG);
        auto* l_pHead = new QHBoxLayout;
        l_pHead->addWidget(f_Rich(QStringLiteral("<b>%1</b>").arg(f_Esc(f_Q(l_strNid)))));
        auto* l_pRen = new CommitLine(f_Q(l_strNid));
        l_pRen->setMaximumWidth(140);
        l_pRen->setToolTip(tr("Rename the node"));
        l_pRen->m_fnOnCommit = [=](const QString& l_oS) {
            const std::string l_strNu = l_oS.trimmed().toStdString();
            if (l_strNu == l_strNid) return;
            if (!stage::f_IdOk(l_strNu)) { f_Toast(tr("Use letters, digits, _ and - only."), true); return; }
            if (l_jNodes.contains(l_strNu)) { f_Toast(tr("%1 already exists.").arg(f_Q(l_strNu)), true); return; }
            l_fnE([l_strNid, l_strNu](Json& l_jX) {
                Json l_jNn = Json::object();
                for (auto& [l_strK, l_jVv] : l_jX["nodes"].items()) l_jNn[l_strK == l_strNid ? l_strNu : l_strK] = l_jVv;
                l_jX["nodes"] = l_jNn;
                for (auto& [l_strK, l_jVv] : l_jX["nodes"].items()) { (void)l_strK; if (l_jVv.contains("choices")) for (Json& l_jC : l_jVv["choices"]) if (l_jC.value("next", Json()) == l_strNid) l_jC["next"] = l_strNu; }
                if (l_jX["start"] == l_strNid) l_jX["start"] = l_strNu;
                if (l_jX["start"].is_array()) for (Json& l_jSt : l_jX["start"]) if (l_jSt.value("node", std::string()) == l_strNid) l_jSt["node"] = l_strNu;
            });
        };
        l_pHead->addWidget(l_pRen);
        const bool l_bIsStart = l_bCondStart ? std::any_of(l_jD["start"].begin(), l_jD["start"].end(), [&](const Json& l_jS) { return l_jS.value("node", std::string()) == l_strNid; }) : l_jD.value("start", std::string()) == l_strNid;
        if (l_bIsStart) l_pHead->addWidget(f_Chip(tr("start")));
        l_pHead->addStretch(1);
        auto* l_pDel = new QPushButton(tr("Delete node"));
        connect(l_pDel, &QPushButton::clicked, this, [=] {
            if (l_vNids.size() < 2) { f_Toast(tr("A dialogue needs one node."), true); return; }
            if (QMessageBox::question(this, tr("Delete node"), tr("Delete node %1?").arg(f_Q(l_strNid))) != QMessageBox::Yes) return;
            l_fnE([l_strNid](Json& l_jX) { l_jX["nodes"].erase(l_strNid); });
        });
        l_pHead->addWidget(l_pDel);
        l_pGv->addLayout(l_pHead);
        const Json l_jTx = f_Field(l_jN.value("text", Json()));
        for (const char* l_pszL : {"zh_TW", "en"}) {
            auto* l_pR = f_Lrow(QString::fromLatin1(l_pszL));
            auto* l_pT = new CommitText(l_jTx.contains(l_pszL) && l_jTx[l_pszL].is_string() ? f_Q(l_jTx[l_pszL].get<std::string>()) : QString());
            const std::string l_strLang = l_pszL;
            l_pT->m_fnOnCommit = [=](const QString& l_oS) { l_fnE([l_strNid, l_strLang, l_oS](Json& l_jX) { Json& l_jNode = l_jX["nodes"][l_strNid]; Json l_jTt = f_Field(l_jNode.value("text", Json())); l_jTt[l_strLang] = l_oS.toStdString(); l_jNode["text"] = l_jTt; }); };
            l_pR->addWidget(l_pT, 1);
            l_pGv->addLayout(l_pR);
        }
        int l_iOther = 0;
        for (auto& [l_strK, l_jX] : l_jTx.items()) { (void)l_jX; if (l_strK != "zh_TW" && l_strK != "en") l_iOther++; }
        if (l_iOther) l_pGv->addWidget(f_Muted(tr("%1 other language(s) kept as they are").arg(l_iOther)));
        l_pGv->addWidget(f_Muted(tr("Choices (what the player can answer)")));
        const Json l_jChoices = l_jN.value("choices", Json::array());
        for (size_t l_nI = 0; l_nI < l_jChoices.size(); l_nI++) {
            const Json l_jC = l_jChoices[l_nI];
            auto* l_pCard = new QWidget;
            l_pCard->setObjectName(QStringLiteral("choice"));
            l_pCard->setStyleSheet(QStringLiteral("QWidget#choice { border-left:3px solid #3a3f47; }"));
            auto* l_pCv = new QVBoxLayout(l_pCard);
            l_pCv->setContentsMargins(8, 2, 0, 2);
            auto l_fnC = [=](const std::function<void(Json&)>& l_fnFn) { l_fnE([l_strNid, l_nI, l_fnFn](Json& l_jX) { l_fnFn(l_jX["nodes"][l_strNid]["choices"][l_nI]); }); };
            auto* l_pR1 = f_Lrow(QString::number(l_nI + 1));
            const Json l_jLb = f_Field(l_jC.value("label", Json()));
            for (const char* l_pszL : {"zh_TW", "en"}) {
                auto* l_pE = new CommitLine(l_jLb.contains(l_pszL) && l_jLb[l_pszL].is_string() ? f_Q(l_jLb[l_pszL].get<std::string>()) : QString());
                l_pE->setPlaceholderText(QString::fromLatin1(l_pszL));
                const std::string l_strLang = l_pszL;
                l_pE->m_fnOnCommit = [=](const QString& l_oS) { l_fnC([l_strLang, l_oS](Json& l_jCh) { Json l_jT = f_Field(l_jCh.value("label", Json())); l_jT[l_strLang] = l_oS.toStdString(); l_jCh["label"] = l_jT; }); };
                l_pR1->addWidget(l_pE, 1);
            }
            for (int l_iDir : {-1, 1, 0}) {
                auto* l_pB = new QToolButton;
                l_pB->setText(l_iDir < 0 ? QStringLiteral("▲") : l_iDir > 0 ? QStringLiteral("▼") : QStringLiteral("✕"));
                connect(l_pB, &QToolButton::clicked, this, [=] {
                    l_fnE([l_strNid, l_nI, l_iDir](Json& l_jX) {
                        Json& l_jL = l_jX["nodes"][l_strNid]["choices"];
                        if (l_iDir == 0) { l_jL.erase(l_nI); return; }
                        const long l_iJ = (long)l_nI + l_iDir;
                        if (l_iJ >= 0 && l_iJ < (long)l_jL.size()) std::swap(l_jL[l_nI], l_jL[(size_t)l_iJ]);
                    });
                });
                l_pR1->addWidget(l_pB);
            }
            l_pCv->addLayout(l_pR1);
            auto* l_pR2 = f_Lrow(tr("Then"));
            auto* l_pNx = l_fnNodeCombo(l_jC.value("next", Json()).is_string() ? l_jC["next"].get<std::string>() : std::string(), true);
            connect(l_pNx, &QComboBox::activated, this, [=](int) { const std::string l_strS = l_pNx->currentData().toString().toStdString(); l_fnC([l_strS](Json& l_jCh) { l_jCh["next"] = l_strS.empty() ? Json() : Json(l_strS); }); });
            auto* l_pFin = new QCheckBox(tr("finishes the event"));
            l_pFin->setChecked(l_jC.value("finish", false));
            connect(l_pFin, &QCheckBox::toggled, this, [=](bool l_bOn) { l_fnC([l_bOn](Json& l_jCh) { if (l_bOn) l_jCh["finish"] = true; else l_jCh.erase("finish"); }); });
            auto* l_pCid = new CommitLine(f_Q(l_jC.value("id", std::string())));
            l_pCid->setMaximumWidth(100);
            l_pCid->setToolTip(tr("Remembered as choiceMade {event, id}, for later conditions"));
            l_pCid->m_fnOnCommit = [=](const QString& l_oS) { const std::string l_strX = l_oS.trimmed().toStdString(); l_fnC([l_strX](Json& l_jCh) { if (l_strX.empty()) l_jCh.erase("id"); else l_jCh["id"] = l_strX; }); };
            l_pR2->addWidget(l_pNx);
            l_pR2->addWidget(l_pFin);
            l_pR2->addWidget(f_Muted(QStringLiteral("id")));
            l_pR2->addWidget(l_pCid);
            l_pR2->addStretch(1);
            l_pCv->addLayout(l_pR2);
            auto* l_pR3 = f_Lrow(tr("Only when"));
            l_pR3->addWidget(f_CondBox(QStringLiteral("ch:%1|%2").arg(f_Q(l_strNid)).arg(l_nI), [this, l_strId, l_strNid, l_nI] {
                const Json& l_jCs = m_oP.m_jS;
                const Json& l_jNode = l_jCs["dialogues"][l_strId]["nodes"][l_strNid];
                const Json l_jL = l_jNode.value("choices", Json::array());
                return l_nI < l_jL.size() ? l_jL[l_nI].value("requires", Json()) : Json();
            }, [this, l_strId, l_strNid, l_nI](const Json& l_jCond) {
                Json& l_jL = m_oP.m_jS["dialogues"][l_strId]["nodes"][l_strNid]["choices"];
                if (l_nI >= l_jL.size()) return;
                if (l_jCond.is_null()) l_jL[l_nI].erase("requires"); else l_jL[l_nI]["requires"] = l_jCond;
            }, l_oHD), 1);
            l_pCv->addLayout(l_pR3);
            auto* l_pR4 = f_Lrow(tr("Then do"));
            auto* l_pActs = new QWidget;
            auto* l_pAv = new QVBoxLayout(l_pActs);
            l_pAv->setContentsMargins(0, 0, 0, 0);
            l_pAv->addWidget(f_ActsBox(QStringLiteral("%1|%2").arg(f_Q(l_strNid)).arg(l_nI), [l_jC] { return l_jC.value("actions", Json::array()); }, [this, l_strId, l_strNid, l_nI]() -> Json* {
                Json& l_jL = m_oP.m_jS["dialogues"][l_strId]["nodes"][l_strNid]["choices"];
                if (l_nI >= l_jL.size()) return nullptr;
                Json& l_jCh = l_jL[l_nI];
                if (!l_jCh.contains("actions") || l_jCh["actions"].is_null()) l_jCh["actions"] = Json::array();
                else if (!l_jCh["actions"].is_array()) l_jCh["actions"] = Json::array({l_jCh["actions"]});
                return &l_jCh["actions"];
            }, l_oHD));
            if (l_jC.contains("action")) l_pAv->addWidget(f_Muted(tr("Older single action (kept):") + QStringLiteral(" ") + f_Q(l_jC["action"].dump()).left(80)));
            l_pR4->addWidget(l_pActs, 1);
            l_pCv->addLayout(l_pR4);
            auto* l_pR5 = f_Lrow(tr("Wakes"));
            auto* l_pChips = new QWidget;
            auto* l_pFlow = new FlowLayout(l_pChips, 4);
            if (l_jC.contains("fire") && l_jC["fire"].is_array())
                for (const Json& l_jFx : l_jC["fire"]) {
                    const std::string l_strX = l_jFx.get<std::string>();
                    auto* l_pB = f_Chip(f_Q(l_strX) + QStringLiteral("  ✕"), m_oP.f_Ev(l_strX) ? QColor() : QColor("#ff5d5d"));
                    connect(l_pB, &QToolButton::clicked, this, [=] {
                        l_fnC([l_strX](Json& l_jCh) { Json& l_jL = l_jCh["fire"]; l_jL.erase(std::remove(l_jL.begin(), l_jL.end(), Json(l_strX)), l_jL.end()); if (l_jL.empty()) l_jCh.erase("fire"); });
                    });
                    l_pFlow->addWidget(l_pB);
                }
            auto* l_pFa = new QComboBox;
            l_pFa->addItem(tr("+ link an event…"), QString());
            for (const Json& l_jE : m_oP.m_jS["events"]) l_pFa->addItem(f_Q(l_jE["id"].get<std::string>()), f_Q(l_jE["id"].get<std::string>()));
            connect(l_pFa, &QComboBox::activated, this, [=](int) {
                const std::string l_strX = l_pFa->currentData().toString().toStdString();
                if (l_strX.empty()) return;
                l_fnC([l_strX](Json& l_jCh) {
                    if (!l_jCh.contains("fire") || !l_jCh["fire"].is_array()) l_jCh["fire"] = Json::array();
                    if (std::find(l_jCh["fire"].begin(), l_jCh["fire"].end(), Json(l_strX)) == l_jCh["fire"].end()) l_jCh["fire"].push_back(l_strX);
                });
            });
            l_pFlow->addWidget(l_pFa);
            l_pR5->addWidget(l_pChips, 1);
            l_pCv->addLayout(l_pR5);
            l_pGv->addWidget(l_pCard);
        }
        auto* l_pAddC = new QPushButton(tr("+ Choice"));
        connect(l_pAddC, &QPushButton::clicked, this, [=] {
            l_fnE([l_strNid](Json& l_jX) { Json& l_jNode = l_jX["nodes"][l_strNid]; if (!l_jNode.contains("choices")) l_jNode["choices"] = Json::array(); l_jNode["choices"].push_back(Json{{"label", Json{{"zh_TW", ""}, {"en", ""}}}, {"next", nullptr}}); });
        });
        l_pGv->addWidget(l_pAddC, 0, Qt::AlignLeft);
        l_pV->addWidget(l_pG);
    }
    {
        auto* l_pH = new QHBoxLayout;
        auto* l_pNn = new QLineEdit;
        l_pNn->setPlaceholderText(tr("new node id"));
        l_pNn->setMaximumWidth(150);
        auto* l_pNb = new QPushButton(tr("+ Node"));
        connect(l_pNb, &QPushButton::clicked, this, [=] {
            const std::string l_strN = l_pNn->text().trimmed().toStdString();
            if (!stage::f_IdOk(l_strN)) { f_Toast(tr("Use letters, digits, _ and - only."), true); return; }
            if (l_jNodes.contains(l_strN)) { f_Toast(tr("%1 already exists.").arg(f_Q(l_strN)), true); return; }
            l_fnE([l_strN](Json& l_jX) { l_jX["nodes"][l_strN] = Json{{"text", Json{{"zh_TW", ""}, {"en", ""}}}, {"choices", Json::array({Json{{"label", Json{{"zh_TW", "……"}, {"en", "..."}}}, {"next", nullptr}}})}}; });
        });
        l_pH->addWidget(l_pNn);
        l_pH->addWidget(l_pNb);
        l_pH->addStretch(1);
        l_pV->addLayout(l_pH);
    }
    l_pV->addStretch(1);
    return l_pW;
}
