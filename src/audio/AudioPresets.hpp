#pragma once

#include "AudioEngine.hpp"
#include <vector>
#include <cmath>
#include <random>
#include <cstring>

namespace Engine {

namespace AudioPresets {

inline std::vector<uint8_t> create_wav_buffer(const std::vector<int16_t>& pcm_samples, int sample_rate) {
    uint32_t data_size = static_cast<uint32_t>(pcm_samples.size() * sizeof(int16_t));
    uint32_t total_size = 36 + data_size;

    std::vector<uint8_t> buffer(44 + data_size);
    uint8_t* ptr = buffer.data();

    // RIFF Header
    std::memcpy(ptr, "RIFF", 4); ptr += 4;
    std::memcpy(ptr, &total_size, 4); ptr += 4;
    std::memcpy(ptr, "WAVE", 4); ptr += 4;

    // fmt subchunk
    std::memcpy(ptr, "fmt ", 4); ptr += 4;
    uint32_t subchunk1_size = 16;
    std::memcpy(ptr, &subchunk1_size, 4); ptr += 4;
    uint16_t audio_format = 1; // PCM
    std::memcpy(ptr, &audio_format, 2); ptr += 2;
    uint16_t num_channels = 1; // Mono
    std::memcpy(ptr, &num_channels, 2); ptr += 2;
    std::memcpy(ptr, &sample_rate, 4); ptr += 4;
    uint32_t byte_rate = sample_rate * num_channels * sizeof(int16_t);
    std::memcpy(ptr, &byte_rate, 4); ptr += 4;
    uint16_t block_align = num_channels * sizeof(int16_t);
    std::memcpy(ptr, &block_align, 2); ptr += 2;
    uint16_t bits_per_sample = 16;
    std::memcpy(ptr, &bits_per_sample, 2); ptr += 2;

    // data subchunk
    std::memcpy(ptr, "data", 4); ptr += 4;
    std::memcpy(ptr, &data_size, 4); ptr += 4;
    std::memcpy(ptr, pcm_samples.data(), data_size);

    return buffer;
}

inline void play_jump(float volume = 0.6f, float pitch = 1.0f) {
    constexpr int sample_rate = 44100;
    constexpr float PI = 3.14159265358979323846f;
    float duration = 0.18f;
    int total_samples = static_cast<int>(duration * sample_rate);
    std::vector<int16_t> pcm(total_samples);

    float phase = 0.0f;
    for (int i = 0; i < total_samples; ++i) {
        float t = static_cast<float>(i) / total_samples;
        float freq = (140.0f + 480.0f * (t * t)) * pitch;
        phase += 2.0f * PI * freq / sample_rate;

        float envelope = (1.0f - t);
        float sample = (std::sin(phase) >= 0.0f ? 0.65f : -0.65f) + 0.35f * std::sin(phase);
        pcm[i] = static_cast<int16_t>(sample * envelope * 24000.0f);
    }

    auto wav = create_wav_buffer(pcm, sample_rate);
    AudioEngine::play_sound_from_memory(wav.data(), wav.size(), volume, 1.0f);
}

inline void play_coin(float volume = 0.75f, float pitch = 1.0f) {
    constexpr int sample_rate = 44100;
    constexpr float PI = 3.14159265358979323846f;
    float duration = 0.30f;
    int total_samples = static_cast<int>(duration * sample_rate);
    std::vector<int16_t> pcm(total_samples);

    float phase = 0.0f;
    for (int i = 0; i < total_samples; ++i) {
        float t = static_cast<float>(i) / total_samples;
        float freq = (t < 0.35f ? 987.77f : 1318.51f) * pitch;
        phase += 2.0f * PI * freq / sample_rate;

        float envelope = std::exp(-t * 8.0f);
        float sample = 0.75f * std::sin(phase) + 0.25f * std::sin(phase * 2.0f);
        pcm[i] = static_cast<int16_t>(sample * envelope * 26000.0f);
    }

    auto wav = create_wav_buffer(pcm, sample_rate);
    AudioEngine::play_sound_from_memory(wav.data(), wav.size(), volume, 1.0f);
}

inline void play_explosion(float volume = 0.85f, float pitch = 1.0f) {
    constexpr int sample_rate = 44100;
    constexpr float PI = 3.14159265358979323846f;
    float duration = 0.45f;
    int total_samples = static_cast<int>(duration * sample_rate);
    std::vector<int16_t> pcm(total_samples);

    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    float filter = 0.0f;
    float rumble_phase = 0.0f;

    for (int i = 0; i < total_samples; ++i) {
        float t = static_cast<float>(i) / total_samples;
        float raw_noise = dist(rng);

        filter += (raw_noise - filter) * 0.15f;
        float sub_freq = (65.0f * (1.0f - t * 0.7f)) * pitch;
        rumble_phase += 2.0f * PI * sub_freq / sample_rate;
        float sub_bass = std::sin(rumble_phase);

        float envelope = std::exp(-t * 6.0f);
        float sample = (filter * 0.6f + sub_bass * 0.5f);
        pcm[i] = static_cast<int16_t>(sample * envelope * 28000.0f);
    }

    auto wav = create_wav_buffer(pcm, sample_rate);
    AudioEngine::play_sound_from_memory(wav.data(), wav.size(), volume, 1.0f);
}

inline void play_grapple(float volume = 0.70f, float pitch = 1.0f) {
    constexpr int sample_rate = 44100;
    constexpr float PI = 3.14159265358979323846f;
    float duration = 0.22f;
    int total_samples = static_cast<int>(duration * sample_rate);
    std::vector<int16_t> pcm(total_samples);

    float phase = 0.0f;
    for (int i = 0; i < total_samples; ++i) {
        float t = static_cast<float>(i) / total_samples;
        float freq = (580.0f * std::exp(-t * 5.0f) + 180.0f) * pitch;
        phase += 2.0f * PI * freq / sample_rate;

        float envelope = std::exp(-t * 7.0f);
        float sample = (std::sin(phase) + 0.5f * std::sin(phase * 3.0f)) * 0.7f;
        pcm[i] = static_cast<int16_t>(sample * envelope * 24000.0f);
    }

    auto wav = create_wav_buffer(pcm, sample_rate);
    AudioEngine::play_sound_from_memory(wav.data(), wav.size(), volume, 1.0f);
}

} // namespace AudioPresets

} // namespace Engine
