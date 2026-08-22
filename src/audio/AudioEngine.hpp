#pragma once

#include <string>
#include <cstdint>
#include <cstddef>

namespace Engine {

class AudioEngine {
public:
    // Initializes the hardware audio device (macOS/iOS CoreAudio, Windows WASAPI, Linux ALSA/Pulse, Android AAudio, WebAudio)
    static bool init();

    // Shuts down the audio engine cleanly and frees background mixing threads
    static void shutdown();
    static void suspend();
    static void resume();
    [[nodiscard]] static bool is_suspended();

    // Plays a sound effect file from disk (WAV, MP3, FLAC, OGG) with volume and pitch control
    static void play_sound(const std::string& filepath, float volume = 1.0f, float pitch = 1.0f);

    // Plays a sound from an encoded in-memory file buffer (e.g. embedded assets or synthesized audio)
    static void play_sound_from_memory(const void* data, size_t size, float volume = 1.0f, float pitch = 1.0f);

    // Plays background music in streaming mode with seamless looping
    static void play_music(const std::string& filepath, float volume = 0.5f, bool loop = true);
    static void stop_music();
    static void pause_music();
    static void resume_music();
    static void set_music_volume(float volume);

    // Volume channels [0.0f .. 1.0f]
    static void set_master_volume(float volume);
    static void set_sfx_volume(float volume);
    static float get_master_volume();
    static float get_music_volume();
    static float get_sfx_volume();
};

} // namespace Engine
