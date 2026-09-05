#include "AudioEngine.hpp"
#include "../core/Log.hpp"
#include "../core/FileSystem.hpp"
#include <miniaudio/miniaudio.h>
#include <algorithm>
#include <vector>
#include <cstring>
#include <memory>

namespace Engine {

static ma_engine s_engine;
static bool s_initialized = false;
static float s_master_volume = 1.0f;
static float s_music_volume = 0.5f;
static float s_sfx_volume = 0.7f;
static ma_sound s_music;
static bool s_music_loaded = false;
static bool s_suspended = false;

// Voice storage is owned by the main thread. Uninitializing the sound detaches
// it from the mixer before the decoder and its backing bytes are released.
struct SoundVoice {
    ma_sound sound{};
    ma_decoder decoder{};
    std::vector<uint8_t> bytes;
    bool sound_ready{false};
    bool decoder_ready{false};
    ~SoundVoice() {
        if (sound_ready) ma_sound_uninit(&sound);
        if (decoder_ready) ma_decoder_uninit(&decoder);
    }
};
static std::vector<std::unique_ptr<SoundVoice>> s_voices;
static bool s_offline = false;

void AudioEngine::update() {
    if (!s_initialized) return;
    std::erase_if(s_voices, [](const auto& voice) { return ma_sound_at_end(&voice->sound); });
}

std::size_t AudioEngine::active_sound_count() { return s_voices.size(); }

bool AudioEngine::render_offline(float* stereo_samples, std::size_t frame_count) {
    if (!s_initialized || !s_offline || !stereo_samples) return false;
    const auto result = ma_engine_read_pcm_frames(&s_engine, stereo_samples, frame_count, nullptr);
    update();
    return result == MA_SUCCESS;
}

bool AudioEngine::init(bool offline) {
    if (s_initialized) return true;

    ma_engine_config config = ma_engine_config_init();
    if (offline) {
        config.noDevice = MA_TRUE;
        config.channels = 2;
        config.sampleRate = 48000;
    }
    ma_result res = ma_engine_init(&config, &s_engine);
    if (res != MA_SUCCESS) {
        ENGINE_LOG_ERROR("[AUDIO] Failed to initialize miniaudio engine. Code: {}", static_cast<int>(res));
        return false;
    }

    s_initialized = true;
    s_offline = offline;
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

    s_voices.clear();
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

    update();
    auto voice = std::make_unique<SoundVoice>();
    const std::string resolved_path = FileSystem::resolve_path(filepath);
    if (ma_sound_init_from_file(&s_engine, resolved_path.c_str(), 0, nullptr, nullptr, &voice->sound) != MA_SUCCESS) {
        ENGINE_LOG_WARN("[AUDIO] Failed to play sound file: '{}'", filepath);
        return;
    }
    voice->sound_ready = true;
    ma_sound_set_volume(&voice->sound, volume * s_sfx_volume);
    ma_sound_set_pitch(&voice->sound, pitch);
    if (ma_sound_start(&voice->sound) == MA_SUCCESS) s_voices.push_back(std::move(voice));
}

void AudioEngine::play_sound_from_memory(const void* data, size_t size, float volume, float pitch) {
    if (!s_initialized || s_suspended || !data || size == 0) return;
    update();
    auto voice = std::make_unique<SoundVoice>();
    const auto* bytes = static_cast<const uint8_t*>(data);
    voice->bytes.assign(bytes, bytes + size);
    ma_decoder_config config = ma_decoder_config_init_default();
    if (ma_decoder_init_memory(voice->bytes.data(), voice->bytes.size(), &config, &voice->decoder) != MA_SUCCESS) return;
    voice->decoder_ready = true;
    if (ma_sound_init_from_data_source(&s_engine, &voice->decoder, 0, nullptr, &voice->sound) != MA_SUCCESS) return;
    voice->sound_ready = true;
    ma_sound_set_volume(&voice->sound, volume * s_sfx_volume);
    ma_sound_set_pitch(&voice->sound, pitch);
    if (ma_sound_start(&voice->sound) == MA_SUCCESS) s_voices.push_back(std::move(voice));
}

void AudioEngine::play_music(const std::string& filepath, float volume, bool loop) {
    if (!s_initialized || s_suspended) return;

    if (s_music_loaded) {
        ma_sound_stop(&s_music);
        ma_sound_uninit(&s_music);
        s_music_loaded = false;
    }

    const std::string resolved_path = FileSystem::resolve_path(filepath);
    ma_result res = ma_sound_init_from_file(
        &s_engine, resolved_path.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, &s_music
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
