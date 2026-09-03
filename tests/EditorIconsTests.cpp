#include "editor/EditorTheme.hpp"
#include "editor/tools/EditorIcons.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <cassert>
#include <iostream>

using namespace Engine;

int main() {
    ImGui::CreateContext();
    assert(EditorTheme::initialize_fonts());
    assert(initialize_editor_icons());

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    assert(pixels && width > 0 && height > 0);

    ImFont* font = io.FontDefault;
    assert(font);
    ImFontBaked* baked = font->GetFontBaked(16.0f);
    assert(baked);

    for (int index = 0; index < static_cast<int>(EditorIcon::Count); ++index) {
        unsigned int codepoint = 0;
        const char* glyph_text = editor_icon_glyph(static_cast<EditorIcon>(index));
        assert(ImTextCharFromUtf8(&codepoint, glyph_text, nullptr) > 0);
        const ImFontGlyph* glyph = baked->FindGlyphNoFallback(static_cast<ImWchar>(codepoint));
        assert(glyph);
        assert(glyph->Visible);
        // Source 0 is Inter, source 1 is the merged Lucide font. This catches
        // private-use collisions that otherwise render icons as letters or
        // numeric alternates while still producing a technically valid glyph.
        assert(glyph->SourceIdx == 1);
        assert(glyph->X1 > glyph->X0);
        assert(glyph->Y1 > glyph->Y0);
    }

    ImGui::DestroyContext();
    std::cout << "EditorIconsTests passed\n";
    return 0;
}
