#include "AudioEngine.hpp"
#include "../core/Log.hpp"
#include <miniaudio/miniaudio.h>
#include <algorithm>
#include <vector>
#include <cstring>

namespace Engine {

static ma_engine s_engine;
static bool s_initialized = false;
static float s_master_volume = 1.0f;
static float s_music_volume = 0.5f;
static float s_sfx_volume = 0.7f;
static ma_sound s_music;
static bool s_music_loaded = false;
static bool s_suspended = false;

bool AudioEngine::init() {
    if (s_initialized) return true;

    ma_engine_config config = ma_engine_config_init();
    ma_result res = ma_engine_init(&config, &s_engine);
    if (res != MA_SUCCESS) {
        ENGINE_LOG_ERROR("[AUDIO] Failed to initialize miniaudio engine. Code: {}", static_cast<int>(res));
        return false;
    }

    s_initialized = true;
    ma_engine_set_volume(&s_engine, s_master_volume);
    if (s_suspended) ma_engine_stop(&s_engine);
    ENGINE_LOG_INFO("[AUDIO] AudioEngine initialized successfully (cross-platform driver).");
    return true;
}

void AudioEngine::shutdown() {
    if (!s_initialized) return;

    if (s_music_loaded) {
        ma_sound_uninit(&s_music);
        s_music_loaded = false;
    }

    ma_engine_uninit(&s_engine);
    s_initialized = false;
    s_suspended = false;
    ENGINE_LOG_INFO("[AUDIO] AudioEngine shutdown cleanly.");
}

void AudioEngine::suspend() {
    if (s_suspended) return;
    s_suspended = true;
    if (!s_initialized) return;
    const ma_result result = ma_engine_stop(&s_engine);
    if (result != MA_SUCCESS) {
        ENGINE_LOG_WARN("[AUDIO] Failed to suspend audio device. Code: {}", static_cast<int>(result));
    }
}

void AudioEngine::resume() {
    if (!s_suspended) return;
    s_suspended = false;
    if (!s_initialized) return;
    const ma_result result = ma_engine_start(&s_engine);
    if (result != MA_SUCCESS) {
        ENGINE_LOG_WARN("[AUDIO] Failed to resume audio device. Code: {}", static_cast<int>(result));
    }
}

bool AudioEngine::is_suspended() { return s_suspended; }

void AudioEngine::play_sound(const std::string& filepath, float volume, float pitch) {
    if (!s_initialized || s_suspended) return;

    ma_sound* sound = new ma_sound();
    ma_result res = ma_sound_init_from_file(&s_engine, filepath.c_str(), 0, nullptr, nullptr, sound);
    if (res == MA_SUCCESS) {
        ma_sound_set_volume(sound, volume * s_sfx_volume);
        ma_sound_set_pitch(sound, pitch);
        ma_sound_start(sound);
    } else {
        delete sound;
        ENGINE_LOG_WARN("[AUDIO] Failed to play sound file: '{}'", filepath);
    }
}

void AudioEngine::play_sound_from_memory(const void* data, size_t size, float volume, float pitch) {
    if (!s_initialized || s_suspended || !data || size == 0) return;

    // Allocate memory buffer copy for playback lifecycle
    uint8_t* mem_copy = new uint8_t[size];
    std::memcpy(mem_copy, data, size);

    ma_decoder* decoder = new ma_decoder();
    ma_decoder_config d_cfg = ma_decoder_config_init_default();
    ma_result res = ma_decoder_init_memory(mem_copy, size, &d_cfg, decoder);
    if (res == MA_SUCCESS) {
        ma_sound* sound = new ma_sound();
        res = ma_sound_init_from_data_source(&s_engine, decoder, 0, nullptr, sound);
        if (res == MA_SUCCESS) {
            ma_sound_set_volume(sound, volume * s_sfx_volume);
            ma_sound_set_pitch(sound, pitch);
            ma_sound_start(sound);
        } else {
            delete sound;
            delete decoder;
            delete[] mem_copy;
        }
    } else {
        delete decoder;
        delete[] mem_copy;
    }
}

void AudioEngine::play_music(const std::string& filepath, float volume, bool loop) {
    if (!s_initialized || s_suspended) return;

    if (s_music_loaded) {
        ma_sound_stop(&s_music);
        ma_sound_uninit(&s_music);
        s_music_loaded = false;
    }

    ma_result res = ma_sound_init_from_file(
        &s_engine, filepath.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, &s_music
    );

    if (res == MA_SUCCESS) {
        s_music_loaded = true;
        ma_sound_set_volume(&s_music, volume * s_music_volume);
        ma_sound_set_looping(&s_music, loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(&s_music);
    } else {
        ENGINE_LOG_WARN("[AUDIO] Failed to play music stream: '{}'", filepath);
    }
}

void AudioEngine::stop_music() {
    if (s_music_loaded) {
        ma_sound_stop(&s_music);
    }
}

void AudioEngine::pause_music() {
    if (s_music_loaded) {
        ma_sound_stop(&s_music);
    }
}

void AudioEngine::resume_music() {
    if (s_music_loaded) {
        ma_sound_start(&s_music);
    }
}

void AudioEngine::set_music_volume(float volume) {
    s_music_volume = std::clamp(volume, 0.0f, 1.0f);
    if (s_music_loaded) {
        ma_sound_set_volume(&s_music, s_music_volume);
    }
}

void AudioEngine::set_master_volume(float volume) {
    s_master_volume = std::clamp(volume, 0.0f, 1.0f);
    if (s_initialized) {
        ma_engine_set_volume(&s_engine, s_master_volume);
    }
}

void AudioEngine::set_sfx_volume(float volume) {
    s_sfx_volume = std::clamp(volume, 0.0f, 1.0f);
}

float AudioEngine::get_master_volume() { return s_master_volume; }
float AudioEngine::get_music_volume() { return s_music_volume; }
float AudioEngine::get_sfx_volume() { return s_sfx_volume; }

} // namespace Engine
