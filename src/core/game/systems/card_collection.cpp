// card_collection.cpp -- see card_collection.h.
#include "card_collection.h"

#include <algorithm>

namespace toms {

CardChance f_CardChanceFromJson(const nlohmann::json& l_jE) {
    CardChance l_oC;
    if (!l_jE.is_object() || !l_jE.contains("card") || !l_jE["card"].is_object()) return l_oC;
    const nlohmann::json& l_jJ = l_jE["card"];
    if (l_jJ.contains("chance") && l_jJ["chance"].is_number()) l_oC.m_fFirst = std::clamp(l_jJ["chance"].get<float>(), 0.0f, 1.0f);
    if (l_jJ.contains("repeatChance") && l_jJ["repeatChance"].is_number()) l_oC.m_fRepeat = std::clamp(l_jJ["repeatChance"].get<float>(), 0.0f, 1.0f);
    return l_oC;
}

bool f_RecordCardWin(CardBook& l_mapBook, const std::string& l_strMonster, const CardChance& l_oChance, float l_fRoll01) {
    if (l_strMonster.empty()) return false;
    CardRecord& l_oR = l_mapBook[l_strMonster];
    l_oR.m_iDefeated++;
    const float l_fP = l_oR.m_iCopies == 0 ? l_oChance.m_fFirst : l_oChance.m_fRepeat;
    if (l_fRoll01 < l_fP) { l_oR.m_iCopies++; return true; }   // p 1.0: always; p 0: never
    return false;
}

void f_RecordCardLoss(CardBook& l_mapBook, const std::string& l_strMonster) {
    if (l_strMonster.empty()) return;
    l_mapBook[l_strMonster].m_iDefeatedBy++;
}

nlohmann::json f_CardBookToJson(const CardBook& l_mapBook) {
    nlohmann::json l_jJ = nlohmann::json::object();
    for (const auto& [l_strId, l_oR] : l_mapBook)
        l_jJ[l_strId] = {{"copies", l_oR.m_iCopies}, {"defeated", l_oR.m_iDefeated}, {"defeatedBy", l_oR.m_iDefeatedBy}};
    return l_jJ;
}

CardBook f_CardBookFromJson(const nlohmann::json& l_jJ) {
    CardBook l_mapBook;
    if (!l_jJ.is_object()) return l_mapBook;
    for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
        if (l_oIt.key().empty() || !l_oIt.value().is_object()) continue;
        CardRecord l_oR;
        l_oR.m_iCopies = std::max(0, l_oIt.value().value("copies", 0));
        l_oR.m_iDefeated = std::max(0, l_oIt.value().value("defeated", 0));
        l_oR.m_iDefeatedBy = std::max(0, l_oIt.value().value("defeatedBy", 0));
        l_mapBook[l_oIt.key()] = l_oR;
    }
    return l_mapBook;
}

} // namespace toms
