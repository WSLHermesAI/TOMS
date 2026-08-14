// Audio.h — minimal sound-effects subsystem (miniaudio, single-header).
// Plays one-shot WAV SFX. Degrades to no-op when no audio device is available
// (e.g. headless CI / lavapipe), so the game never crashes for lack of sound.
#pragma once
#include <string>
#include <map>

struct Audio {
    bool init(const std::string& sfxDir);
    void play(const std::string& name);   // name without extension, e.g. "walk"
    void setVolume(float v);              // 0..1 master
    bool available() const { return ok; }
    ~Audio();
private:
    bool ok = false;
    std::string dir;
    float master = 1.0f;
    void* engine = nullptr;               // ma_engine* (opaque to avoid pulling the 4MB header into every TU)
};
