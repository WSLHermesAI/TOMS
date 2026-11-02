// StageText.cpp -- see StageText.h.
#include "StageText.h"

#include "Language.h"

#include <QCoreApplication>

namespace StageText {

namespace {
QString f_Tr(const char* l_pszS) { return QCoreApplication::translate("StageEditor", l_pszS); }
QString f_A(const stage::Issue& l_oI, size_t l_nN) { return l_nN < l_oI.m_vArgs.size() ? f_Q(l_oI.m_vArgs[l_nN]) : QString(); }
}  // namespace

QString f_Q(const std::string& l_strS) { return QString::fromStdString(l_strS); }

std::string f_Lang() { return Language::f_Current() == Language::Lang::Chinese ? "zh_TW" : "en"; }

QString f_KeyColor(const std::string& l_strC) {
    if (l_strC == "yellow") return f_Tr("yellow");
    if (l_strC == "blue") return f_Tr("blue");
    if (l_strC == "red") return f_Tr("red");
    return f_Q(l_strC);
}

QString f_Issue(const stage::Issue& l_oI) {
    const std::string& l_strC = l_oI.m_strCode;
    if (l_strC == "pNoStart") return f_Tr("No stairs and no player start: the player would appear at the bottom-left corner.");
    if (l_strC == "pManyStart") return f_Tr("%1 player starts (@); only one is used.").arg(f_A(l_oI, 0));
    if (l_strC == "pUnknownChar") return f_Tr("Char '%1' at %2,%3 has no legend entry.").arg(f_A(l_oI, 0), f_A(l_oI, 1), f_A(l_oI, 2));
    if (l_strC == "pEvUndefined") return f_Tr("%1 is placed but no pool defines it.").arg(f_A(l_oI, 0));
    if (l_strC == "pEvNotListed") return f_Tr("%1 is placed but not in this floor's events list.").arg(f_A(l_oI, 0));
    if (l_strC == "pEvDup") return f_Tr("%1 is placed %2 times; one floor should not repeat an event.").arg(f_A(l_oI, 0), f_A(l_oI, 1));
    if (l_strC == "pEvUnplaced") return f_Tr("%1 is in the events list but not placed.").arg(f_A(l_oI, 0));
    if (l_strC == "pNoUp") return f_Tr("connect.up is %1 but there is no stairs-up tile.").arg(f_A(l_oI, 0));
    if (l_strC == "pUpNoTarget") return f_Tr("A stairs-up tile, but connect.up is empty.");
    if (l_strC == "pNoDown") return f_Tr("connect.down is %1 but there is no stairs-down tile.").arg(f_A(l_oI, 0));
    if (l_strC == "pDownNoTarget") return f_Tr("A stairs-down tile, but connect.down is empty.");
    if (l_strC == "pUnreach") return f_Tr("%1 object(s) cannot be reached from where the player arrives.").arg(f_A(l_oI, 0));
    if (l_strC == "pFootprint") return f_Tr("%1 at %2,%3 is wider than the free cells next to it.").arg(f_A(l_oI, 0), f_A(l_oI, 1), f_A(l_oI, 2));
    if (l_strC == "pBorder") return f_Tr("The outer edge has open cells.");
    if (l_strC == "pReservedChar") return f_Tr("%1 uses '%2', but the game only treats '%3' as %1.").arg(f_A(l_oI, 0), f_A(l_oI, 1), f_A(l_oI, 2));
    if (l_strC == "pObjEmpty") return f_Tr("Object settings at (%1, %2) but nothing stands there.").arg(f_A(l_oI, 0), f_A(l_oI, 1));
    if (l_strC == "pObjEvent") return f_Tr("The object at (%1, %2) links %3, which no pool defines.").arg(f_A(l_oI, 1), f_A(l_oI, 2), f_A(l_oI, 0));
    if (l_strC == "sNoStart") return f_Tr("No stairs and no player start.");
    if (l_strC == "sUpWalled") return f_Tr("The stairs up cannot be reached at all (walls).");
    if (l_strC == "sNeedKeys")
        return f_Tr("The way up needs %1 %2 key(s); %3 carried in + %4 on this floor.").arg(f_A(l_oI, 0), f_KeyColor(l_oI.m_vArgs.size() > 1 ? l_oI.m_vArgs[1] : ""), f_A(l_oI, 2), f_A(l_oI, 3));
    if (l_strC == "sEvUnreach") return f_Tr("%1 is walled off: no path reaches it.").arg(f_A(l_oI, 0));
    if (l_strC == "sEvNoKey") return f_Tr("%1 needs a key that is not available by this floor.").arg(f_A(l_oI, 0));
    if (l_strC == "sEvOptional") return f_Tr("%1 is behind a door that is not on the way up: a player who spends the key there may not get up.").arg(f_A(l_oI, 0));
    if (l_strC == "sNeedsEvent")
        return f_A(l_oI, 2).isEmpty() ? f_Tr("%1 needs %2 done, but %2 is placed nowhere.").arg(f_A(l_oI, 0), f_A(l_oI, 1))
                                 : f_Tr("%1 needs %2 done, but %2 first appears on %3.").arg(f_A(l_oI, 0), f_A(l_oI, 1), f_A(l_oI, 2));
    if (l_strC == "sNeedsVar") return f_Tr("%1 needs %2 ≥ %3, but events up to this floor can only reach %4.").arg(f_A(l_oI, 0), f_A(l_oI, 1), f_A(l_oI, 2), f_A(l_oI, 3));
    if (l_strC == "sStairLock") return f_Tr("The stair to %1 waits for %2, which cannot be done by this floor.").arg(f_A(l_oI, 0), f_A(l_oI, 1));
    if (l_strC == "apSkipNoCell") return f_Tr("%1: no safe free cell for %2").arg(f_A(l_oI, 1), f_A(l_oI, 0));
    if (l_strC == "apSkipUndef") return f_Tr("%1 listed id(s) that no pool defines were left out").arg(f_A(l_oI, 0));
    return f_Q(l_strC);
}

}  // namespace StageText
