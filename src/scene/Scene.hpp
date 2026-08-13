#pragma once

#include <string>
#include <utility>

namespace Engine {

class Renderer2D;

class Scene {
public:
    virtual ~Scene() = default;

    virtual void on_attach() {}
    virtual void on_detach() {}
    virtual void on_update(float delta_time) {}
    virtual void on_render(Renderer2D& renderer) {}
    virtual void on_resize(int width, int height) {}

    const std::string& get_name() const { return m_name; }

protected:
    explicit Scene(std::string name) : m_name(std::move(name)) {}

private:
    std::string m_name;
};

} // namespace Engine
