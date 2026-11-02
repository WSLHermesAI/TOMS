// Audio.cpp — miniaudio-backed SFX player.
#include "Audio.h"
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <cstdio>
#include <string>
#include <vector>

#include "../../engine/vfs.h"   // Android: sounds ship INSIDE the APK

struct ma_engine_wrapper { ma_engine m_oE; };

bool Audio::f_Init(const std::string& l_strSfxDir) {
    m_strDir = l_strSfxDir;
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
    ma_engine_set_volume(l_pE, m_fMaster);
    fprintf(stderr, "[audio] initialized OK (sfx dir: %s)\n", m_strDir.c_str());
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

void f_ReapFinished() {
    auto& l_vV = f_LiveSfx();
    for (size_t l_nI = 0; l_nI < l_vV.size();) {
        if (ma_sound_at_end(l_vV[l_nI].m_pSound)) {
            ma_sound_uninit(l_vV[l_nI].m_pSound); ma_decoder_uninit(l_vV[l_nI].m_pDecoder);
            delete l_vV[l_nI].m_pSound; delete l_vV[l_nI].m_pDecoder;
            l_vV.erase(l_vV.begin() + (long)l_nI);
        } else ++l_nI;
    }
}
}  // namespace
#endif

void Audio::f_Play(const std::string& l_strName) {
    if (!m_bOk || !m_pEngine) return;
    ma_engine* l_pE = &((ma_engine_wrapper*)m_pEngine)->m_oE;
    std::string l_strPath = m_strDir + "/" + l_strName + ".wav";
#if defined(__ANDROID__)
    f_ReapFinished();
    std::string l_strBytes;
    if (!toms::f_VfsReadAll(l_strPath, l_strBytes)) return;          // missing is not fatal, same as before
    ma_decoder* l_pDec = new ma_decoder();
    ma_decoder_config l_oDc = ma_decoder_config_init(ma_format_f32, 2, 48000);
    if (ma_decoder_init_memory(l_strBytes.data(), l_strBytes.size(), &l_oDc, l_pDec) != MA_SUCCESS) { delete l_pDec; return; }
    ma_sound* l_pSnd = new ma_sound();
    if (ma_sound_init_from_data_source(l_pE, l_pDec, 0, NULL, l_pSnd) != MA_SUCCESS) {
        ma_decoder_uninit(l_pDec); delete l_pDec; delete l_pSnd; return;
    }
    ma_sound_start(l_pSnd);
    f_LiveSfx().push_back({l_pSnd, l_pDec});
#else
    ma_result l_eR = ma_engine_play_sound(l_pE, l_strPath.c_str(), NULL);
    if (l_eR != MA_SUCCESS) {
        // file missing or decode error -- ignore, don't crash
    }
#endif
}

void Audio::f_SetVolume(float l_fV) {
    m_fMaster = l_fV;
    if (m_bOk && m_pEngine) ma_engine_set_volume(&((ma_engine_wrapper*)m_pEngine)->m_oE, m_fMaster);
}

Audio::~Audio() {
    if (m_pEngine) { ma_engine_uninit(&((ma_engine_wrapper*)m_pEngine)->m_oE); delete (ma_engine_wrapper*)m_pEngine; }
}
