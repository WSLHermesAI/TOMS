// card_collection.cpp -- see card_collection.h.
#include "card_collection.h"

#include <algorithm>

namespace toms {

CardChance cardChanceFromJson(const nlohmann::json& e) {
    CardChance c;
    if (!e.is_object() || !e.contains("card") || !e["card"].is_object()) return c;
    const nlohmann::json& j = e["card"];
    if (j.contains("chance") && j["chance"].is_number()) c.first = std::clamp(j["chance"].get<float>(), 0.0f, 1.0f);
    if (j.contains("repeatChance") && j["repeatChance"].is_number()) c.repeat = std::clamp(j["repeatChance"].get<float>(), 0.0f, 1.0f);
    return c;
}

bool recordCardWin(CardBook& book, const std::string& monster, const CardChance& chance, float roll01) {
    if (monster.empty()) return false;
    CardRecord& r = book[monster];
    r.defeated++;
    const float p = r.copies == 0 ? chance.first : chance.repeat;
    if (roll01 < p) { r.copies++; return true; }   // p 1.0: always; p 0: never
    return false;
}

void recordCardLoss(CardBook& book, const std::string& monster) {
    if (monster.empty()) return;
    book[monster].defeatedBy++;
}

nlohmann::json cardBookToJson(const CardBook& book) {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [id, r] : book)
        j[id] = {{"copies", r.copies}, {"defeated", r.defeated}, {"defeatedBy", r.defeatedBy}};
    return j;
}

CardBook cardBookFromJson(const nlohmann::json& j) {
    CardBook book;
    if (!j.is_object()) return book;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key().empty() || !it.value().is_object()) continue;
        CardRecord r;
        r.copies = std::max(0, it.value().value("copies", 0));
        r.defeated = std::max(0, it.value().value("defeated", 0));
        r.defeatedBy = std::max(0, it.value().value("defeatedBy", 0));
        book[it.key()] = r;
    }
    return book;
}

} // namespace toms
