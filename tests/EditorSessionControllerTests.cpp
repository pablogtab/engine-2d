#include "editor/session/EditorSessionController.hpp"

#include <cassert>

int main() {
    Engine::EditorSessionController controller;
    const auto* first = reinterpret_cast<const Engine::Scene*>(0x1);
    const auto* second = reinterpret_cast<const Engine::Scene*>(0x2);
    int creations = 0;

    auto& session = controller.ensure(first, [&creations]() {
        ++creations;
        Engine::EditorSceneSession value;
        value.file_path = "first.scene";
        return value;
    });
    auto& same = controller.ensure(first, [&creations]() {
        ++creations;
        return Engine::EditorSceneSession{};
    });
    assert(&session == &same);
    assert(creations == 1);
    assert(controller.find(first)->file_path == "first.scene");
    assert(controller.find(second) == nullptr);

    controller.open_tab(first);
    controller.open_tab(first);
    controller.open_tab(second);
    assert(controller.tab_count() == 2);
    assert(controller.is_tab_open(first));

    controller.prune([first](const Engine::Scene* scene) { return scene == first; });
    assert(controller.tab_count() == 1);
    assert(controller.find(first) != nullptr);

    controller.erase(first);
    assert(controller.find(first) == nullptr);
    assert(!controller.is_tab_open(first));
    controller.clear_tabs();
    assert(controller.tabs_empty());
    return 0;
}
