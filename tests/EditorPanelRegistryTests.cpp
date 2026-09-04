#include "editor/panels/EditorPanelRegistry.hpp"

#include <cassert>

namespace {

class TestPanel final : public Engine::EditorPanel {
public:
    explicit TestPanel(std::string name) : m_name(std::move(name)) {}
    void on_gui() override {}
    void on_context_changed(const Engine::EditorPanelContext&) override { ++context_updates; }
    void on_render_resources_invalidated() override { ++invalidations; }
    void on_visibility_changed(bool visible) override {
        ++visibility_updates;
        last_visibility = visible;
    }
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    int context_updates{0};
    int invalidations{0};
    int visibility_updates{0};
    bool last_visibility{true};

private:
    std::string m_name;
};

} // namespace

int main() {
    Engine::EditorPanelRegistry registry;
    auto viewport = std::make_shared<TestPanel>("Viewport");

    assert(registry.add(Engine::EditorPanelId::Viewport, viewport));
    assert(!registry.add(Engine::EditorPanelId::Viewport, std::make_shared<TestPanel>("Duplicate")));
    assert(viewport->get_id() == Engine::EditorPanelId::Viewport);
    assert(viewport->context_updates == 1);
    assert(registry.find("Viewport") == viewport);
    assert(registry.find(Engine::EditorPanelId::Viewport) == viewport);

    registry.broadcast_context({});
    registry.invalidate_render_resources();
    assert(viewport->context_updates == 2);
    assert(viewport->invalidations == 1);

    viewport->notify_visibility_if_changed();
    viewport->notify_visibility_if_changed();
    assert(viewport->visibility_updates == 1);
    viewport->is_open = false;
    viewport->notify_visibility_if_changed();
    assert(viewport->visibility_updates == 2);
    assert(!viewport->last_visibility);

    assert(registry.remove("Viewport"));
    assert(!registry.remove("Viewport"));
    assert(registry.panels().empty());
    return 0;
}
