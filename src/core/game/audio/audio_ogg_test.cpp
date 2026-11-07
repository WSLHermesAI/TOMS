// audio_ogg_test.cpp -- all the game's sound against the file map (assets/media/resources.json): every sfx.* and music.* key
// is an OGG that really decodes the way Audio plays it, no file in those folders lacks a key, every sound the data names
// is a key, and music really loops past its end. A sound that fails only goes silent in the game, so nothing else would
// notice. Nothing is heard: the engine has no device.
#include "miniaudio_vorbis_impl.h"

#include <json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {
int g_iFailed = 0;

void f_Check(bool l_bOk, const std::string& l_strWhat) {
    if (l_bOk) return;
    std::fprintf(stderr, "FAIL: %s\n", l_strWhat.c_str());
    g_iFailed++;
}

nlohmann::json f_ReadJson(const std::string& l_strPath) {
    std::ifstream l_oFile(l_strPath);
    return nlohmann::json::parse(l_oFile, nullptr, false, true);
}

bool f_IsSoundKey(const std::string& l_strKey) { return l_strKey.rfind("sfx.", 0) == 0 || l_strKey.rfind("music.", 0) == 0; }

// Any "sound" or "music" string in a data file, at any depth, and every value of a "sounds" object.
void f_CollectNamed(const nlohmann::json& l_jValue, std::set<std::string>& l_setOut) {
    if (l_jValue.is_array()) {
        for (const nlohmann::json& l_jItem : l_jValue) f_CollectNamed(l_jItem, l_setOut);
    } else if (l_jValue.is_object()) {
        for (const auto& l_oItem : l_jValue.items()) {
            if ((l_oItem.key() == "sound" || l_oItem.key() == "music") && l_oItem.value().is_string()) l_setOut.insert(l_oItem.value().get<std::string>());
            if (l_oItem.key() == "sounds" && l_oItem.value().is_object())
                for (const auto& l_oSound : l_oItem.value().items()) l_setOut.insert(l_oSound.value().get<std::string>());
            f_CollectNamed(l_oItem.value(), l_setOut);
        }
    }
}
}  // namespace

int main() {
    namespace fs = std::filesystem;
    const nlohmann::json l_jManifest = f_ReadJson("media/resources.json");
    f_Check(l_jManifest.is_object() && l_jManifest.contains("resources"), "media/resources.json reads");
    if (g_iFailed) return 1;

    ma_engine_config l_oConfig = ma_engine_config_init();
    l_oConfig.noDevice = MA_TRUE;
    l_oConfig.channels = 2;
    l_oConfig.sampleRate = 48000;
    ma_engine l_oEngine;
    if (ma_engine_init(&l_oConfig, &l_oEngine) != MA_SUCCESS) {
        std::fprintf(stderr, "FAIL: no engine\n");
        return 1;
    }

    std::set<std::string> l_setKeys, l_setPaths;
    std::string l_strMusicPath;
    for (const auto& l_oItem : l_jManifest["resources"].items()) {
        const std::string& l_strKey = l_oItem.key();
        if (!f_IsSoundKey(l_strKey)) continue;
        l_setKeys.insert(l_strKey);
        const std::string l_strPath = "media/" + l_oItem.value().value("path", std::string());
        l_setPaths.insert(fs::path(l_strPath).lexically_normal().generic_string());
        if (l_strKey.rfind("music.", 0) == 0) l_strMusicPath = l_strPath;
        f_Check(fs::path(l_strPath).extension() == ".ogg", l_strKey + " is an OGG (all game sound is)");

        ma_sound l_oSound;
        const ma_result l_eResult = ma_sound_init_from_file(&l_oEngine, l_strPath.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &l_oSound);
        f_Check(l_eResult == MA_SUCCESS, l_strKey + " decodes: " + l_strPath + " (" + std::to_string((int)l_eResult) + ")");
        if (l_eResult != MA_SUCCESS) continue;
        ma_uint64 l_u64Frames = 0;
        ma_sound_get_length_in_pcm_frames(&l_oSound, &l_u64Frames);
        f_Check(l_u64Frames > 0, l_strKey + " has sound in it");
        ma_sound_uninit(&l_oSound);
    }
    f_Check(!l_setKeys.empty(), "there are sounds");

    for (const char* l_pszFolder : {"media/sfx", "media/music"})
        for (const fs::directory_entry& l_oEntry : fs::directory_iterator(l_pszFolder))
            f_Check(l_setPaths.count(l_oEntry.path().lexically_normal().generic_string()) > 0, l_oEntry.path().generic_string() + " has a key in resources.json");

    std::set<std::string> l_setNamed;
    for (const char* l_pszData : {"data/battle.json", "data/looks.json", "data/weapon_play.json", "data/special_moves.json"}) {
        const nlohmann::json l_jData = f_ReadJson(l_pszData);
        f_Check(!l_jData.is_discarded(), std::string(l_pszData) + " reads");
        f_CollectNamed(l_jData, l_setNamed);
    }
    f_Check(!l_setNamed.empty(), "the data names sounds");
    for (const std::string& l_strName : l_setNamed)
        f_Check(l_setKeys.count(l_strName) > 0, "the data's sound \"" + l_strName + "\" is a key");

    // The music loops, as Audio::f_PlayMusic sets it up: pull more than its whole length through the engine.
    f_Check(!l_strMusicPath.empty(), "there is music");
    if (!l_strMusicPath.empty()) {
        std::ifstream l_oFile(l_strMusicPath, std::ios::binary);
        const std::string l_strBytes((std::istreambuf_iterator<char>(l_oFile)), std::istreambuf_iterator<char>());
        ma_decoder_config l_oDc = ma_decoder_config_init(ma_format_f32, 2, 48000);
        ma_decoder l_oDecoder;
        ma_sound l_oSound;
        f_Check(ma_decoder_init_memory(l_strBytes.data(), l_strBytes.size(), &l_oDc, &l_oDecoder) == MA_SUCCESS, "the music decodes from memory");
        f_Check(ma_sound_init_from_data_source(&l_oEngine, &l_oDecoder, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &l_oSound) == MA_SUCCESS, "the music becomes a sound");
        ma_uint64 l_u64Length = 0;
        ma_decoder_get_length_in_pcm_frames(&l_oDecoder, &l_u64Length);
        f_Check(l_u64Length > 48000, "the music is longer than a second");
        ma_sound_set_looping(&l_oSound, MA_TRUE);
        ma_sound_start(&l_oSound);

        const ma_uint64 l_u64Want = l_u64Length + 48000 * 3;   // past its end, by three seconds
        std::vector<float> l_vBlock(2 * 4096);
        ma_uint64 l_u64Done = 0;
        double l_dLateEnergy = 0.0;
        while (l_u64Done < l_u64Want) {
            ma_uint64 l_u64Read = 0;
            ma_engine_read_pcm_frames(&l_oEngine, l_vBlock.data(), 4096, &l_u64Read);
            if (l_u64Read == 0) break;
            if (l_u64Done > l_u64Length + 4096)
                for (ma_uint64 l_u64I = 0; l_u64I < l_u64Read * 2; l_u64I++) l_dLateEnergy += (double)l_vBlock[(size_t)l_u64I] * (double)l_vBlock[(size_t)l_u64I];
            l_u64Done += l_u64Read;
        }
        f_Check(l_u64Done >= l_u64Want, "the engine keeps giving sound past the music's end");
        f_Check(ma_sound_is_playing(&l_oSound), "the music is still playing past its end");
        f_Check(l_dLateEnergy > 1.0, "there is real sound after the music's end: it looped");
        ma_sound_uninit(&l_oSound);
        ma_decoder_uninit(&l_oDecoder);
    }
    ma_engine_uninit(&l_oEngine);

    std::printf("audio_ogg_test: %zu keys, %zu named by data, %d failed\n", l_setKeys.size(), l_setNamed.size(), g_iFailed);
    return g_iFailed ? 1 : 0;
}
