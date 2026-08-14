// catalog.h — object-type catalog shared by the editor model and UI.
// Header-only so it needs no extra translation unit / moc.
#pragma once
#include <QString>
#include <QMap>
#include <QList>
#include <QColor>

struct ObjectType {
    QChar ch;                 // legend char (e.g. '#', '1', 'v')
    QString category;         // terrain | stairs | door | key | player | npc | monster | item
    QString typeId;          // slime, villager, yellow, start, floor, wall ...
    QString label;           // Traditional Chinese display name
    QMap<QString, QString> defaults; // monster stats / item effects
};

inline QString categoryColor(const QString& cat) {
    if (cat == "wall" || cat == "terrain") return "#5b5b66";
    if (cat == "floor")              return "#2b2b33";
    if (cat == "stairs")             return "#e0a83a";
    if (cat == "door")               return "#c98b2b";
    if (cat == "key")                return "#d8c84a";
    if (cat == "player")             return "#3aa0ff";
    if (cat == "npc")                return "#46c46a";
    if (cat == "monster")            return "#e0473a";
    if (cat == "item")               return "#b46bff";
    return "#888888";
}

inline const QList<ObjectType>& catalogAll() {
    static QList<ObjectType> list = {
        // terrain
        {'#', "terrain", "wall",    "牆壁",   {}},
        {'.', "terrain", "floor",   "地板",   {}},
        {'U', "stairs",  "stairs_up",   "樓梯上", {}},
        {'D', "stairs",  "stairs_down", "樓梯下", {}},
        // doors
        {'y', "door", "yellow", "黃門", {{"color","yellow"}}},
        {'b', "door", "blue",   "藍門", {{"color","blue"}}},
        {'r', "door", "red",    "紅門", {{"color","red"}}},
        // keys
        {'Y', "key", "yellow", "黃鑰匙", {}},
        {'B', "key", "blue",   "藍鑰匙", {}},
        {'R', "key", "red",    "紅鑰匙", {}},
        // player
        {'@', "player", "start", "玩家起點", {}},
        // npcs
        {'s', "npc", "sorcerer",   "法師",   {{"dialogue","sorcerer_teacher"}}},
        {'v', "npc", "villager",   "村民",   {{"dialogue","villager_elder"}}},
        {'k', "npc", "king",       "國王",   {{"dialogue","king_lieutenant"}}},
        {'p', "npc", "princess",   "公主",   {{"dialogue","princess_liora"}}},
        {'m', "npc", "handmaiden", "侍女",   {{"dialogue","handmaiden"}}},
        // monsters
        {'1', "monster", "slime",   "史萊姆", {{"hp","24"},{"atk","6"},{"def","0"},{"exp","5"},{"gold","3"},{"boss","false"}}},
        {'2', "monster", "bat",     "蝙蝠",   {{"hp","40"},{"atk","9"},{"def","1"},{"exp","9"},{"gold","6"},{"boss","false"}}},
        {'3', "monster", "golem",   "石魔像", {{"hp","90"},{"atk","16"},{"def","6"},{"exp","22"},{"gold","15"},{"boss","false"}}},
        {'4', "monster", "skeleton","骷髏",   {{"hp","70"},{"atk","13"},{"def","3"},{"exp","18"},{"gold","12"},{"boss","false"}}},
        {'5', "monster", "wraith",  "幽靈",   {{"hp","120"},{"atk","20"},{"def","8"},{"exp","30"},{"gold","20"},{"boss","false"}}},
        {'6', "monster", "demon",   "惡魔",   {{"hp","180"},{"atk","28"},{"def","12"},{"exp","50"},{"gold","35"},{"boss","false"}}},
        {'Z', "monster", "demonlord_vorkath","巫王 Vorkath", {{"hp","240"},{"atk","36"},{"def","18"},{"exp","200"},{"gold","100"},{"boss","true"}}},
        // items
        {'a', "item", "gem_atk",     "攻擊寶石", {{"effect","atk+6"}}},
        {'d', "item", "gem_def",     "防禦寶石", {{"effect","def+5"}}},
        {'h', "item", "potion_red",  "紅藥水",   {{"effect","hp+40"}}},
        {'H', "item", "potion_blue", "藍藥水",   {{"effect","hp+80"}}},
        {'c', "item", "coin",        "金幣",     {{"effect","gold"}}},
    };
    return list;
}

inline const ObjectType* catalogByChar(QChar c) {
    for (const auto& t : catalogAll()) if (t.ch == c) return &t;
    return nullptr;
}
inline const ObjectType* catalogByType(const QString& category, const QString& typeId) {
    for (const auto& t : catalogAll())
        if (t.category == category && t.typeId == typeId) return &t;
    return nullptr;
}

// legend semantic used by the game (e.g. "monster:slime", "npc:villager")
inline QString semanticOf(const QString& category, const QString& typeId) {
    if (category == "terrain") {
        if (typeId == "wall") return "wall";
        if (typeId == "floor") return "floor";
        if (typeId == "stairs_up") return "stairs_up";
        if (typeId == "stairs_down") return "stairs_down";
    }
    if (category == "stairs")  return typeId; // stairs_up / stairs_down
    if (category == "door")    return "door:" + typeId;
    if (category == "key")     return "key:" + typeId;
    if (category == "player")  return "player_start";
    if (category == "npc")     return "npc:" + typeId;
    if (category == "monster") return "monster:" + typeId;
    if (category == "item")    return "item:" + typeId;
    return typeId;
}
