#include "core/Application.hpp"
#include "scene/GenericScene.hpp"
#include "scene/SceneSerializer.hpp"
#include <memory>

namespace {

class StandaloneApp final : public Engine::Application {
public:
    StandaloneApp()
        : Application("Engine2D", 1280, 720, false) {}

protected:
    void on_init() override {
        auto scene = std::make_shared<Engine::GenericScene>("PlatformerDemo");
        Engine::SceneSerializer serializer(scene.get(), get_window().get_sdl_renderer());
        if (serializer.deserialize("assets/scenes/PlatformerDemo.scene")) {
            get_scene_manager().add_scene(scene);
            get_scene_manager().set_active_scene("PlatformerDemo");
            get_scene_manager().start_play();
        }
    }
};

} // namespace

int main() {
    StandaloneApp app;
    app.run();
    return 0;
}
