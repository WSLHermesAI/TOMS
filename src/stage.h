// stage.h — stage data structures and JSON loader for Tower of the Sorcerer.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <json.hpp>

struct Entity {
    int x, y;
    std::string kind;   // from legend: monster:slime, npc:villager, item:gem_atk, stairs_up...
    std::string id;     // resolved id (slime, villager, gem_atk, ...)
    std::string raw;    // single char
    bool consumed = false; // items/monsters removed after use
};

struct Stage {
    std::string id, name, subtitle;
    int index = 0, width = 0, height = 0;
    std::vector<std::string> tiles;        // rows of chars
    std::map<std::string,std::string> legend;
    std::vector<Entity> entities;
    std::string up, down;                  // connected stage ids
    std::string story_note;

    char at(int x, int y) const {
        if (y < 0 || y >= (int)tiles.size()) return '#';
        const auto& r = tiles[y];
        if (x < 0 || x >= (int)r.size()) return '#';
        return r[x];
    }
};

// Load a stage JSON file. legend maps char -> semantic; entities parsed from tiles.
inline Stage parseStage(const std::string& path) {
    std::ifstream f(path);
    nlohmann::json j; f >> j;
    Stage s;
    s.id = j["id"]; s.name = j["name"]; s.subtitle = j["subtitle"];
    s.index = j["index"]; s.width = j["width"]; s.height = j["height"];
    s.tiles = j["tiles"].get<std::vector<std::string>>();
    for (auto& [k,v] : j["legend"].get<std::map<std::string,std::string>>()) s.legend[k] = v;
    s.up = j["connect"]["up"].is_null() ? "" : (std::string)j["connect"]["up"];
    s.down = j["connect"]["down"].is_null() ? "" : (std::string)j["connect"]["down"];
    s.story_note = j["story_note"];
    // parse entities
    for (int y = 0; y < (int)s.tiles.size(); y++) {
        for (int x = 0; x < (int)s.tiles[y].size(); x++) {
            char c = s.tiles[y][x];
            if (c == '#' || c == '.' || c == '@') continue;
            auto it = s.legend.find(std::string(1, c));
            if (it == s.legend.end()) continue;
            Entity e; e.x = x; e.y = y; e.raw = std::string(1,c); e.kind = it->second;
            // resolve id: e.g. "monster:slime" -> "slime", "npc:villager" -> "villager", "item:gem_atk" -> "gem_atk"
            auto pos = e.kind.find(':');
            e.id = (pos == std::string::npos) ? e.kind : e.kind.substr(pos+1);
            s.entities.push_back(e);
        }
    }
    return s;
}
