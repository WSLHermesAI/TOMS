// stage.h — stage data structures and JSON loader for Tower of the Sorcerer.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <json.hpp>
#include "object.h"   // Stage derives Trackable so stage loads are leak-checked

struct Entity {
    int x, y;
    std::string kind;   // from legend: monster:slime, npc:villager, item:gem_atk, stairs_up...
    std::string id;     // resolved id (slime, villager, gem_atk, ...)
    std::string raw;    // single char
    bool consumed = false; // items/monsters removed after use
};

struct Stage : public Trackable {
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
    TOMS_OBJECT(Stage)
};

// Load a stage JSON file. legend maps char -> semantic; entities parsed from tiles.
inline Stage parseStage(const std::string& path) {
    // Read the whole file into a string first, then json::parse. Using
    // operator>>(istream, json) directly is unreliable under Emscripten's libc++
    // (it can report "empty input" even though the file is present and non-empty).
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        fprintf(stderr, "[parseStage] FAILED to open %s\n", path.c_str());
        Stage s; s.id = path; return s;
    }
    std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.empty()) {
        fprintf(stderr, "[parseStage] EMPTY file %s\n", path.c_str());
        Stage s; s.id = path; return s;
    }
    nlohmann::json j;
    try { j = nlohmann::json::parse(buf); }
    catch (const std::exception& e) {
        fprintf(stderr, "[parseStage] parse error in %s: %s\n", path.c_str(), e.what());
        Stage s; s.id = path; return s;
    }
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
