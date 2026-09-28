// Audio.cpp — miniaudio-backed SFX player.
#include "Audio.h"
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <cstdio>
#include <string>
#include <vector>

#include "../../engine/vfs.h"   // Android: sounds ship INSIDE the APK

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

#if defined(__ANDROID__)
namespace {
// Sounds live inside the APK, where miniaudio's own file reader (stdio) cannot see them. They are short
// effects, so each one is read through vfs and decoded from memory; the decoder and the sound must
// outlive playback, hence this pool, reaped when each sound finishes. Held in the file (not in Audio)
// so no header or call-site changes are needed for a platform-specific detail.
struct LiveSfx { ma_sound* sound; ma_decoder* decoder; };
std::vector<LiveSfx>& liveSfx() { static std::vector<LiveSfx> v; return v; }

void reapFinished() {
    auto& v = liveSfx();
    for (size_t i = 0; i < v.size();) {
        if (ma_sound_at_end(v[i].sound)) {
            ma_sound_uninit(v[i].sound); ma_decoder_uninit(v[i].decoder);
            delete v[i].sound; delete v[i].decoder;
            v.erase(v.begin() + (long)i);
        } else ++i;
    }
}
}  // namespace
#endif

void Audio::play(const std::string& name) {
    if (!ok || !engine) return;
    ma_engine* e = &((ma_engine_wrapper*)engine)->e;
    std::string path = dir + "/" + name + ".wav";
#if defined(__ANDROID__)
    reapFinished();
    std::string bytes;
    if (!toms::vfsReadAll(path, bytes)) return;          // missing is not fatal, same as before
    ma_decoder* dec = new ma_decoder();
    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 2, 48000);
    if (ma_decoder_init_memory(bytes.data(), bytes.size(), &dc, dec) != MA_SUCCESS) { delete dec; return; }
    ma_sound* snd = new ma_sound();
    if (ma_sound_init_from_data_source(e, dec, 0, NULL, snd) != MA_SUCCESS) {
        ma_decoder_uninit(dec); delete dec; delete snd; return;
    }
    ma_sound_start(snd);
    liveSfx().push_back({snd, dec});
#else
    ma_result r = ma_engine_play_sound(e, path.c_str(), NULL);
    if (r != MA_SUCCESS) {
        // file missing or decode error -- ignore, don't crash
    }
#endif
}

void Audio::setVolume(float v) {
    master = v;
    if (ok && engine) ma_engine_set_volume(&((ma_engine_wrapper*)engine)->e, master);
}

Audio::~Audio() {
    if (engine) { ma_engine_uninit(&((ma_engine_wrapper*)engine)->e); delete (ma_engine_wrapper*)engine; }
}
