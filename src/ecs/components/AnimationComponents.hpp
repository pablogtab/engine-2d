#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <string>
#include <vector>

namespace Engine {

struct AnimationClip {
    std::string name{"Default"};
    int start_frame{0};
    int frame_count{1};
    float fps{10.0f};
    bool loop{true};
    bool ping_pong{false};
    bool reverse{false};
    std::vector<SDL_FRect> custom_frames;

    AnimationClip() = default;
    AnimationClip(
        const std::string& clip_name,
        int start,
        int count,
        float rate = 10.0f,
        bool looping = true
    ) : name(clip_name), start_frame(start), frame_count(count), fps(rate), loop(looping) {}

    [[nodiscard]] int get_total_frames() const {
        return custom_frames.empty() ? frame_count : static_cast<int>(custom_frames.size());
    }

    [[nodiscard]] int first_relative_frame() const {
        return reverse ? std::max(0, get_total_frames() - 1) : 0;
    }

    [[nodiscard]] int initial_direction() const { return reverse ? -1 : 1; }

    // Advances a relative frame cursor and returns true when a non-looping
    // clip reached its terminal frame.
    bool advance(int& relative_frame, int& direction) const {
        const int total_frames = get_total_frames();
        if (total_frames <= 0) return true;

        relative_frame += direction;
        if (ping_pong && total_frames > 1) {
            if (relative_frame >= total_frames) {
                direction = -1;
                relative_frame = total_frames - 2;
            } else if (relative_frame < 0) {
                if (loop) {
                    direction = 1;
                    relative_frame = 1;
                } else {
                    relative_frame = 0;
                    return true;
                }
            }
        } else if (relative_frame >= total_frames || relative_frame < 0) {
            if (loop) {
                relative_frame = direction > 0 ? 0 : total_frames - 1;
            } else {
                relative_frame = direction > 0 ? total_frames - 1 : 0;
                return true;
            }
        }
        return false;
    }

    bool operator==(const AnimationClip& other) const {
        if (name != other.name || start_frame != other.start_frame ||
            frame_count != other.frame_count || fps != other.fps || loop != other.loop ||
            ping_pong != other.ping_pong || reverse != other.reverse ||
            custom_frames.size() != other.custom_frames.size()) return false;
        for (std::size_t index = 0; index < custom_frames.size(); ++index) {
            const auto& lhs = custom_frames[index];
            const auto& rhs = other.custom_frames[index];
            if (lhs.x != rhs.x || lhs.y != rhs.y || lhs.w != rhs.w || lhs.h != rhs.h) return false;
        }
        return true;
    }
};

struct AnimationComponent {
    int columns{1};
    int rows{1};
    float frame_width{64.0f};
    float frame_height{64.0f};
    float spacing_x{0.0f};
    float spacing_y{0.0f};
    float margin_x{0.0f};
    float margin_y{0.0f};
    std::vector<AnimationClip> clips;
    int default_clip_index{0};
    bool play_on_start{true};

    AnimationComponent() = default;

    void add_clip(
        const std::string& name,
        int start_frame,
        int frame_count,
        float fps = 10.0f,
        bool loop = true
    ) {
        clips.emplace_back(name, start_frame, frame_count, fps, loop);
    }

    [[nodiscard]] AnimationClip* get_clip(int index) {
        if (clips.empty() || index < 0 || index >= static_cast<int>(clips.size())) return nullptr;
        return &clips[index];
    }

    [[nodiscard]] const AnimationClip* get_clip(int index) const {
        if (clips.empty() || index < 0 || index >= static_cast<int>(clips.size())) return nullptr;
        return &clips[index];
    }

    [[nodiscard]] SDL_FRect get_frame_rect(int frame_index, const AnimationClip* clip = nullptr) const {
        if (!clip) clip = get_clip(default_clip_index);
        if (clip && !clip->custom_frames.empty()) {
            if (frame_index >= 0 && frame_index < static_cast<int>(clip->custom_frames.size())) {
                return clip->custom_frames[frame_index];
            }
            return clip->custom_frames[0];
        }

        const int column = columns > 0 ? (frame_index % columns) : 0;
        const int row = columns > 0 ? (frame_index / columns) : 0;
        return SDL_FRect{
            margin_x + static_cast<float>(column) * (frame_width + spacing_x),
            margin_y + static_cast<float>(row) * (frame_height + spacing_y),
            frame_width,
            frame_height
        };
    }

    bool operator==(const AnimationComponent& other) const {
        return columns == other.columns && rows == other.rows &&
            frame_width == other.frame_width && frame_height == other.frame_height &&
            spacing_x == other.spacing_x && spacing_y == other.spacing_y &&
            margin_x == other.margin_x && margin_y == other.margin_y &&
            clips == other.clips && default_clip_index == other.default_clip_index &&
            play_on_start == other.play_on_start;
    }
};

struct AnimationPlaybackStateComponent {
    int active_clip_index{0};
    int current_frame{0};
    float timer{0.0f};
    bool is_playing{false};
    bool is_finished{false};
    bool initialized{false};
    int playback_direction{1};
};

} // namespace Engine
