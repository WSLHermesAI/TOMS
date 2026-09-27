// Audio.cpp — miniaudio-backed SFX player.
#include "Audio.h"
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <cstdio>

struct ma_engine_wrapper { ma_engine e; };

bool Audio::init(const std::string& sfxDir) {
    dir = sfxDir;
    engine = new ma_engine_wrapper();
    ma_engine* e = &((ma_engine_wrapper*)engine)->e;
    ma_result r = ma_engine_init(NULL, e);
    if (r != MA_SUCCESS) {
        fprintf(stderr, "[audio] no device (%d) -- sound disabled\n", (int)r);
        ok = false;
        delete (ma_engine_wrapper*)engine; engine = nullptr;
        return false;
    }
    ok = true;
    ma_engine_set_volume(e, master);
    fprintf(stderr, "[audio] initialized OK (sfx dir: %s)\n", dir.c_str());
    return true;
}

void Audio::play(const std::string& name) {
    if (!ok || !engine) return;
    ma_engine* e = &((ma_engine_wrapper*)engine)->e;
    std::string path = dir + "/" + name + ".wav";
    ma_result r = ma_engine_play_sound(e, path.c_str(), NULL);
    if (r != MA_SUCCESS) {
        // file missing or decode error -- ignore, don't crash
    }
}

void Audio::setVolume(float v) {
    master = v;
    if (ok && engine) ma_engine_set_volume(&((ma_engine_wrapper*)engine)->e, master);
}

Audio::~Audio() {
    if (engine) { ma_engine_uninit(&((ma_engine_wrapper*)engine)->e); delete (ma_engine_wrapper*)engine; }
}
