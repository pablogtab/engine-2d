#pragma once

#include "../EditorPanel.hpp"

#include <array>
#include <string>

namespace Engine {

class UiGalleryPanel final : public EditorPanel {
public:
    UiGalleryPanel() { is_open = false; }

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

private:
    std::string m_name{"UI Gallery"};
    std::array<char, 128> m_search{};
    bool m_toggle{true};
    int m_mode{0};
};

} // namespace Engine
