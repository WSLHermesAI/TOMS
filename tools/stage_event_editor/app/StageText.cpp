// StageText.cpp -- see StageText.h.
#include "StageText.h"

#include "Language.h"

#include <QCoreApplication>

namespace StageText {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("StageEditor", s); }
QString a(const stage::Issue& i, size_t n) { return n < i.args.size() ? q(i.args[n]) : QString(); }
}  // namespace

QString q(const std::string& s) { return QString::fromStdString(s); }

std::string lang() { return Language::current() == Language::Lang::Chinese ? "zh_TW" : "en"; }

QString keyColor(const std::string& c) {
    if (c == "yellow") return tr("yellow");
    if (c == "blue") return tr("blue");
    if (c == "red") return tr("red");
    return q(c);
}

QString issue(const stage::Issue& i) {
    const std::string& c = i.code;
    if (c == "pNoStart") return tr("No stairs and no player start: the player would appear at the bottom-left corner.");
    if (c == "pManyStart") return tr("%1 player starts (@); only one is used.").arg(a(i, 0));
    if (c == "pUnknownChar") return tr("Char '%1' at %2,%3 has no legend entry.").arg(a(i, 0), a(i, 1), a(i, 2));
    if (c == "pEvUndefined") return tr("%1 is placed but no pool defines it.").arg(a(i, 0));
    if (c == "pEvNotListed") return tr("%1 is placed but not in this floor's events list.").arg(a(i, 0));
    if (c == "pEvDup") return tr("%1 is placed %2 times; one floor should not repeat an event.").arg(a(i, 0), a(i, 1));
    if (c == "pEvUnplaced") return tr("%1 is in the events list but not placed.").arg(a(i, 0));
    if (c == "pNoUp") return tr("connect.up is %1 but there is no stairs-up tile.").arg(a(i, 0));
    if (c == "pUpNoTarget") return tr("A stairs-up tile, but connect.up is empty.");
    if (c == "pNoDown") return tr("connect.down is %1 but there is no stairs-down tile.").arg(a(i, 0));
    if (c == "pDownNoTarget") return tr("A stairs-down tile, but connect.down is empty.");
    if (c == "pUnreach") return tr("%1 object(s) cannot be reached from where the player arrives.").arg(a(i, 0));
    if (c == "pFootprint") return tr("%1 at %2,%3 is wider than the free cells next to it.").arg(a(i, 0), a(i, 1), a(i, 2));
    if (c == "pBorder") return tr("The outer edge has open cells.");
    if (c == "pReservedChar") return tr("%1 uses '%2', but the game only treats '%3' as %1.").arg(a(i, 0), a(i, 1), a(i, 2));
    if (c == "pObjEmpty") return tr("Object settings at (%1, %2) but nothing stands there.").arg(a(i, 0), a(i, 1));
    if (c == "pObjEvent") return tr("The object at (%1, %2) links %3, which no pool defines.").arg(a(i, 1), a(i, 2), a(i, 0));
    if (c == "sNoStart") return tr("No stairs and no player start.");
    if (c == "sUpWalled") return tr("The stairs up cannot be reached at all (walls).");
    if (c == "sNeedKeys")
        return tr("The way up needs %1 %2 key(s); %3 carried in + %4 on this floor.").arg(a(i, 0), keyColor(i.args.size() > 1 ? i.args[1] : ""), a(i, 2), a(i, 3));
    if (c == "sEvUnreach") return tr("%1 is walled off: no path reaches it.").arg(a(i, 0));
    if (c == "sEvNoKey") return tr("%1 needs a key that is not available by this floor.").arg(a(i, 0));
    if (c == "sEvOptional") return tr("%1 is behind a door that is not on the way up: a player who spends the key there may not get up.").arg(a(i, 0));
    if (c == "sNeedsEvent")
        return a(i, 2).isEmpty() ? tr("%1 needs %2 done, but %2 is placed nowhere.").arg(a(i, 0), a(i, 1))
                                 : tr("%1 needs %2 done, but %2 first appears on %3.").arg(a(i, 0), a(i, 1), a(i, 2));
    if (c == "sNeedsVar") return tr("%1 needs %2 ≥ %3, but events up to this floor can only reach %4.").arg(a(i, 0), a(i, 1), a(i, 2), a(i, 3));
    if (c == "sStairLock") return tr("The stair to %1 waits for %2, which cannot be done by this floor.").arg(a(i, 0), a(i, 1));
    if (c == "apSkipNoCell") return tr("%1: no safe free cell for %2").arg(a(i, 1), a(i, 0));
    if (c == "apSkipUndef") return tr("%1 listed id(s) that no pool defines were left out").arg(a(i, 0));
    return q(c);
}

}  // namespace StageText
