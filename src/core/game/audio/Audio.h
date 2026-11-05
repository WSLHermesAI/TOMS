// Audio.h — minimal sound-effects subsystem (miniaudio, single-header).
// Plays one-shot WAV SFX. Same interface on desktop (native device backends)
// and the Emscripten/browser build (miniaudio's Web Audio backend). Degrades
// to no-op when no audio device/context is available (e.g. headless CI /
// lavapipe), so the game never crashes for lack of sound.
#pragma once
#include <string>
#include <map>
#include "object.h"   // Trackable base (Audio is leak-tracked)

struct Audio : public Trackable {
    bool f_Init(const std::string& l_strSfxDir);
    void f_Play(const std::string& l_strName);   // name without extension, e.g. "walk"
    void f_SetVolume(float l_fV);              // 0..1 master
    bool f_Available() const { return m_bOk; }
    // A run in the background (hidden, minimized, without focus, a scripted test) has volume 0 for everything the
    // engine plays, a sound already playing too (a project rule, CLAUDE.md). The host calls this every frame: the
    // SDL main loop and the editors' viewport. It keeps the master volume for when the run comes back.
    static void f_SetBackgroundMuted(bool l_bMuted);
    static bool f_BackgroundMuted();
    ~Audio();
private:
    void f_ApplyVolume();
    bool m_bOk = false;
    std::string m_strDir;
    float m_fMaster = 1.0f;
    void* m_pEngine = nullptr;               // ma_engine* (opaque to avoid pulling the 4MB header into every TU)
    TOMS_OBJECT(Audio)
};
