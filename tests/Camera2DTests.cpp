#include "scene/Camera2D.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

bool approximately(float actual, float expected, float tolerance = 0.001f) {
    return std::abs(actual - expected) <= tolerance;
}

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "Camera2D test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
    Engine::Camera2D camera(960.0f, 540.0f);
    camera.set_reference_size(1920.0f, 1080.0f);
    camera.set_projection_mode(Engine::CameraProjectionMode::FixedHeight);
    require(approximately(camera.get_projection_scale(), 0.5f), "Fixed Height preview scale");

    camera.set_position({125.0f, -40.0f});
    camera.set_rotation(27.0f);
    camera.set_zoom(1.4f);
    const glm::vec3 world(420.0f, 175.0f, 0.0f);
    const glm::vec2 round_trip = camera.screen_to_world(camera.world_to_screen(world));
    require(approximately(round_trip.x, world.x), "world/screen round trip X");
    require(approximately(round_trip.y, world.y), "world/screen round trip Y");

    camera.set_position({0.0f, 0.0f});
    camera.set_rotation(90.0f);
    camera.set_zoom(1.0f);
    const glm::vec2 rotated = camera.world_to_screen({100.0f, 0.0f, 0.0f});
    require(rotated.y > 270.0f, "positive camera rotation direction");

    camera.set_viewport_size(1000.0f, 1000.0f);
    camera.set_rotation(0.0f);
    camera.set_projection_mode(Engine::CameraProjectionMode::FixedHeight);
    require(approximately(camera.get_world_half_extents().y, 540.0f), "Fixed Height preserves vertical extent");
    require(approximately(camera.get_world_half_extents().x, 540.0f), "Fixed Height adapts horizontal extent");

    camera.set_projection_mode(Engine::CameraProjectionMode::FixedWidth);
    require(approximately(camera.get_world_half_extents().x, 960.0f), "Fixed Width preserves horizontal extent");
    require(approximately(camera.get_world_half_extents().y, 960.0f), "Fixed Width adapts vertical extent");

    camera.set_projection_mode(Engine::CameraProjectionMode::PixelPerfect);
    camera.set_viewport_size(3840.0f, 2160.0f);
    require(approximately(camera.get_projection_scale(), 2.0f), "Pixel Perfect uses integer upscale");

    return EXIT_SUCCESS;
}
