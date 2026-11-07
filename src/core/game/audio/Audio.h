// Audio.h — minimal sound-effects subsystem (miniaudio, single-header).
// Plays one-shot OGG SFX and one looping OGG music track, each named by its key in assets/media/resources.json (the host
// gives it the way to turn a key into a path: no file name is written here). Same interface on desktop (native device backends)
// and the Emscripten/browser build (miniaudio's Web Audio backend). Degrades
// to no-op when no audio device/context is available (e.g. headless CI /
// lavapipe), so the game never crashes for lack of sound.
#pragma once
#include <functional>
#include <string>
#include <map>
#include <vector>
#include "object.h"   // Trackable base (Audio is leak-tracked)

struct Audio : public Trackable {
    using PathOfKey = std::function<std::string(const std::string&)>;
    bool f_Init(PathOfKey l_fnPathOf);
    void f_Play(const std::string& l_strKey);    // a key: toms::key::g_pszSfxWalk, or "sfx.walk" from data
    // The looping music. The same key again changes nothing; another fades over it. It plays through the same engine, so
    // the background mute covers it. Without a sound device the key is still kept, and nothing plays.
    void f_PlayMusic(const std::string& l_strKey, float l_fVolume, int l_iFadeInMs);
    void f_StopMusic(int l_iFadeOutMs);
    const std::string& f_MusicKey() const { return m_strMusicKey; }
    bool f_MusicPlaying() const;
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
    void f_ReapMusic(bool l_bAll);
    bool m_bOk = false;
    PathOfKey m_fnPathOf;
    std::string m_strMusicKey;
    void* m_pMusic = nullptr;                // the track playing
    std::vector<void*> m_vFadingMusic;       // tracks fading out
    float m_fMaster = 1.0f;
    void* m_pEngine = nullptr;               // ma_engine* (opaque to avoid pulling the 4MB header into every TU)
    TOMS_OBJECT(Audio)
};
