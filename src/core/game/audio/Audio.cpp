// Audio.cpp — miniaudio-backed SFX player.
#include "Audio.h"
#include "miniaudio_vorbis_impl.h"
#include <cstdio>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "../../engine/vfs.h"   // Android: sounds ship INSIDE the APK

struct ma_engine_wrapper { ma_engine m_oE; };

namespace {
void* g_pLastSound = nullptr;    // the ma_sound f_Play made last (f_PlayOwned takes it)
bool g_bBackgroundMuted = false;
Audio* g_pLiveAudio = nullptr;   // the engine now in use (a session that restarts makes a new one)
}

void Audio::f_ApplyVolume() {
    if (m_bOk && m_pEngine) ma_engine_set_volume(&((ma_engine_wrapper*)m_pEngine)->m_oE, g_bBackgroundMuted ? 0.0f : m_fMaster);
}

void Audio::f_SetBackgroundMuted(bool l_bMuted) {
    if (l_bMuted == g_bBackgroundMuted) return;
    g_bBackgroundMuted = l_bMuted;
    fprintf(stderr, "[audio] %s\n", l_bMuted ? "muted: running in the background" : "back in the foreground: sound on");
    if (g_pLiveAudio) g_pLiveAudio->f_ApplyVolume();
}

bool Audio::f_BackgroundMuted() { return g_bBackgroundMuted; }

namespace {
// A music track: its file's bytes, read through the VFS as every platform can, decoded while it plays.
struct MusicTrack {
    std::string m_strBytes;
    ma_decoder m_oDecoder;
    ma_sound m_oSound;
};
}  // namespace

bool Audio::f_Init(PathOfKey l_fnPathOf) {
    m_fnPathOf = std::move(l_fnPathOf);
    m_pEngine = new ma_engine_wrapper();
    ma_engine* l_pE = &((ma_engine_wrapper*)m_pEngine)->m_oE;
    ma_result l_eR = ma_engine_init(NULL, l_pE);
    if (l_eR != MA_SUCCESS) {
        fprintf(stderr, "[audio] no device (%d) -- sound disabled\n", (int)l_eR);
        m_bOk = false;
        delete (ma_engine_wrapper*)m_pEngine; m_pEngine = nullptr;
        return false;
    }
    m_bOk = true;
    g_pLiveAudio = this;
    f_ApplyVolume();
    fprintf(stderr, "[audio] initialized OK%s\n", g_bBackgroundMuted ? ", muted" : "");
    return true;
}

#if defined(__ANDROID__)
namespace {
// Sounds live inside the APK, where miniaudio's own file reader (stdio) cannot see them. They are short
// effects, so each one is read through vfs and decoded from memory; the decoder and the sound must
// outlive playback, hence this pool, reaped when each sound finishes. Held in the file (not in Audio)
// so no header or call-site changes are needed for a platform-specific detail.
struct LiveSfx { ma_sound* m_pSound; ma_decoder* m_pDecoder; };
std::vector<LiveSfx>& f_LiveSfx() { static std::vector<LiveSfx> l_vV; return l_vV; }
std::map<uint32_t, ma_sound*>& f_OwnedSfx() { static std::map<uint32_t, ma_sound*> l_mapM; return l_mapM; }   // f_PlayOwned's ids

void f_ForgetOwned(ma_sound* l_pS) {
    for (auto l_itIt = f_OwnedSfx().begin(); l_itIt != f_OwnedSfx().end();) l_itIt = l_itIt->second == l_pS ? f_OwnedSfx().erase(l_itIt) : std::next(l_itIt);
}

void f_ReapFinished() {
    auto& l_vV = f_LiveSfx();
    for (size_t l_nI = 0; l_nI < l_vV.size();) {
        if (ma_sound_at_end(l_vV[l_nI].m_pSound) || !ma_sound_is_playing(l_vV[l_nI].m_pSound)) {   // (ended, or stopped by its owner)
            f_ForgetOwned(l_vV[l_nI].m_pSound);
            ma_sound_uninit(l_vV[l_nI].m_pSound); ma_decoder_uninit(l_vV[l_nI].m_pDecoder);
            delete l_vV[l_nI].m_pSound; delete l_vV[l_nI].m_pDecoder;
            l_vV.erase(l_vV.begin() + (long)l_nI);
        } else ++l_nI;
    }
}
}  // namespace
#endif

#if !defined(__ANDROID__)
namespace {
// A sound at another pitch or volume is its own ma_sound (the engine's fire-and-forget sound has neither): kept until it ends.
// The decoded data is cached by miniaudio's resource manager, so the same sound again is not read again.
std::vector<ma_sound*>& f_PitchedSfx() { static std::vector<ma_sound*> l_vV; return l_vV; }
std::map<uint32_t, ma_sound*>& f_OwnedSfx() { static std::map<uint32_t, ma_sound*> l_mapM; return l_mapM; }   // f_PlayOwned's ids

void f_ForgetOwned(ma_sound* l_pS) {
    for (auto l_itIt = f_OwnedSfx().begin(); l_itIt != f_OwnedSfx().end();) l_itIt = l_itIt->second == l_pS ? f_OwnedSfx().erase(l_itIt) : std::next(l_itIt);
}

void f_ReapPitched() {
    auto& l_vV = f_PitchedSfx();
    for (size_t l_nI = 0; l_nI < l_vV.size();) {
        if (ma_sound_at_end(l_vV[l_nI]) || !ma_sound_is_playing(l_vV[l_nI])) {   // (ended, or stopped by its owner)
            f_ForgetOwned(l_vV[l_nI]);
            ma_sound_uninit(l_vV[l_nI]); delete l_vV[l_nI]; l_vV.erase(l_vV.begin() + (long)l_nI);
        } else ++l_nI;
    }
}
}  // namespace
#endif

void Audio::f_Play(const std::string& l_strKey) { f_Play(l_strKey, 1.0f, 1.0f); }

namespace {
uint32_t g_uNextOwned = 0;
}

uint32_t Audio::f_PlayOwned(const std::string& l_strKey, float l_fPitch, float l_fVolume) {
    if (!m_bOk || !m_pEngine) return 0;
    // its own ma_sound (never the engine's fire-and-forget one, which cannot be stopped): f_Play notes the one it made
    g_pLastSound = nullptr;
#if defined(__ANDROID__)
    f_Play(l_strKey, l_fPitch, l_fVolume);
#else
    f_Play(l_strKey, l_fPitch == 1.0f && l_fVolume == 1.0f ? 1.0f + 1e-6f : l_fPitch, l_fVolume);   // (not exactly 1: its own sound)
#endif
    if (!g_pLastSound) return 0;
    if (++g_uNextOwned == 0) ++g_uNextOwned;
    f_OwnedSfx()[g_uNextOwned] = (ma_sound*)g_pLastSound;
    return g_uNextOwned;
}

void Audio::f_StopOwned(uint32_t l_uId, int l_iFadeMs) {
    auto l_itIt = f_OwnedSfx().find(l_uId);
    if (l_itIt == f_OwnedSfx().end()) return;   // ended already (reaped)
    if (l_iFadeMs > 0) ma_sound_stop_with_fade_in_milliseconds(l_itIt->second, (ma_uint64)l_iFadeMs);
    else ma_sound_stop(l_itIt->second);
}

bool Audio::f_OwnedPlaying(uint32_t l_uId) const {
    auto l_itIt = f_OwnedSfx().find(l_uId);
    return l_itIt != f_OwnedSfx().end() && !ma_sound_at_end(l_itIt->second) && ma_sound_is_playing(l_itIt->second);
}

void Audio::f_Play(const std::string& l_strKey, float l_fPitch, float l_fVolume) {
    if (!m_bOk || !m_pEngine) return;
    ma_engine* l_pE = &((ma_engine_wrapper*)m_pEngine)->m_oE;
    const std::string l_strPath = m_fnPathOf(l_strKey);
#if defined(__ANDROID__)
    f_ReapFinished();
    std::string l_strBytes;
    if (!toms::f_VfsReadAll(l_strPath, l_strBytes)) { fprintf(stderr, "[audio] cannot read %s\n", l_strPath.c_str()); return; }   // not fatal
    ma_decoder* l_pDec = new ma_decoder();
    ma_decoder_config l_oDc = ma_decoder_config_init(ma_format_f32, 2, 48000);
    if (ma_decoder_init_memory(l_strBytes.data(), l_strBytes.size(), &l_oDc, l_pDec) != MA_SUCCESS) { delete l_pDec; return; }
    ma_sound* l_pSnd = new ma_sound();
    if (ma_sound_init_from_data_source(l_pE, l_pDec, 0, NULL, l_pSnd) != MA_SUCCESS) {
        ma_decoder_uninit(l_pDec); delete l_pDec; delete l_pSnd; return;
    }
    ma_sound_set_pitch(l_pSnd, l_fPitch);
    ma_sound_set_volume(l_pSnd, l_fVolume);
    ma_sound_start(l_pSnd);
    f_LiveSfx().push_back({l_pSnd, l_pDec});
    g_pLastSound = l_pSnd;
#else
    if (l_fPitch == 1.0f && l_fVolume == 1.0f) {   // the engine's own fire-and-forget sound
        ma_result l_eR = ma_engine_play_sound(l_pE, l_strPath.c_str(), NULL);
        if (l_eR != MA_SUCCESS) fprintf(stderr, "[audio] cannot play %s (%d)\n", l_strPath.c_str(), (int)l_eR);   // missing or bad: silence, not a crash
        return;
    }
    f_ReapPitched();
    ma_sound* l_pSnd = new ma_sound();
    if (ma_sound_init_from_file(l_pE, l_strPath.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, NULL, l_pSnd) != MA_SUCCESS) {
        fprintf(stderr, "[audio] cannot play %s\n", l_strPath.c_str());
        delete l_pSnd;
        return;
    }
    ma_sound_set_pitch(l_pSnd, l_fPitch);
    ma_sound_set_volume(l_pSnd, l_fVolume);
    ma_sound_start(l_pSnd);
    f_PitchedSfx().push_back(l_pSnd);
    g_pLastSound = l_pSnd;
#endif
}

void Audio::f_ReapMusic(bool l_bAll) {
    for (size_t l_nI = m_vFadingMusic.size(); l_nI-- > 0;) {
        MusicTrack* l_pT = (MusicTrack*)m_vFadingMusic[l_nI];
        if (!l_bAll && ma_sound_is_playing(&l_pT->m_oSound)) continue;
        ma_sound_uninit(&l_pT->m_oSound);
        ma_decoder_uninit(&l_pT->m_oDecoder);
        delete l_pT;
        m_vFadingMusic.erase(m_vFadingMusic.begin() + (std::ptrdiff_t)l_nI);
    }
}

void Audio::f_StopMusic(int l_iFadeOutMs) {
    m_strMusicKey.clear();
    if (!m_pMusic) return;
    MusicTrack* l_pT = (MusicTrack*)m_pMusic;
    if (l_iFadeOutMs > 0) ma_sound_stop_with_fade_in_milliseconds(&l_pT->m_oSound, (ma_uint64)l_iFadeOutMs);
    else ma_sound_stop(&l_pT->m_oSound);
    m_vFadingMusic.push_back(m_pMusic);
    m_pMusic = nullptr;
}

void Audio::f_PlayMusic(const std::string& l_strKey, float l_fVolume, int l_iFadeInMs) {
    if (l_strKey == m_strMusicKey) return;
    f_StopMusic(l_iFadeInMs);
    m_strMusicKey = l_strKey;
    if (!m_bOk || !m_pEngine) return;
    f_ReapMusic(false);
    const std::string l_strPath = m_fnPathOf(l_strKey);
    MusicTrack* l_pT = new MusicTrack();
    if (!toms::f_VfsReadAll(l_strPath, l_pT->m_strBytes)) {
        fprintf(stderr, "[audio] cannot read %s\n", l_strPath.c_str());
        delete l_pT;
        return;
    }
    ma_decoder_config l_oDc = ma_decoder_config_init(ma_format_f32, 2, 48000);
    ma_result l_eR = ma_decoder_init_memory(l_pT->m_strBytes.data(), l_pT->m_strBytes.size(), &l_oDc, &l_pT->m_oDecoder);
    if (l_eR != MA_SUCCESS) {
        fprintf(stderr, "[audio] cannot decode %s (%d)\n", l_strPath.c_str(), (int)l_eR);
        delete l_pT;
        return;
    }
    l_eR = ma_sound_init_from_data_source(&((ma_engine_wrapper*)m_pEngine)->m_oE, &l_pT->m_oDecoder, MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, &l_pT->m_oSound);
    if (l_eR != MA_SUCCESS) {
        fprintf(stderr, "[audio] cannot play %s (%d)\n", l_strPath.c_str(), (int)l_eR);
        ma_decoder_uninit(&l_pT->m_oDecoder);
        delete l_pT;
        return;
    }
    ma_sound_set_looping(&l_pT->m_oSound, MA_TRUE);
    ma_sound_set_volume(&l_pT->m_oSound, l_fVolume);
    if (l_iFadeInMs > 0) ma_sound_set_fade_in_milliseconds(&l_pT->m_oSound, 0.0f, 1.0f, (ma_uint64)l_iFadeInMs);
    ma_sound_start(&l_pT->m_oSound);
    m_pMusic = l_pT;
}

bool Audio::f_MusicPlaying() const {
    return m_pMusic && ma_sound_is_playing(&((MusicTrack*)m_pMusic)->m_oSound);
}

void Audio::f_SetVolume(float l_fV) {
    m_fMaster = l_fV;
    f_ApplyVolume();
}

Audio::~Audio() {
    if (g_pLiveAudio == this) g_pLiveAudio = nullptr;
    f_StopMusic(0);
    if (m_pEngine) f_ReapMusic(true);
#if !defined(__ANDROID__)
    for (ma_sound* l_pSnd : f_PitchedSfx()) { ma_sound_uninit(l_pSnd); delete l_pSnd; }   // before their engine
    f_PitchedSfx().clear();
#endif
    f_OwnedSfx().clear();   // (their sounds are gone with the lists above)
    if (m_pEngine) { ma_engine_uninit(&((ma_engine_wrapper*)m_pEngine)->m_oE); delete (ma_engine_wrapper*)m_pEngine; }
}
