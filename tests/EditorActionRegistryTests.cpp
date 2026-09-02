#include "editor/app/EditorActionRegistry.hpp"

#include <cassert>
#include <iostream>

using namespace Engine;

int main() {
    EditorActionRegistry registry;
    int executions = 0;
    bool enabled = true;
    bool checked = false;

    assert(registry.register_action({
        .id = "editor.save",
        .label = "Save Scene",
        .category = "File",
        .shortcut_id = "editor.save_scene",
        .execute = [&executions]() { ++executions; },
        .enabled = [&enabled]() { return enabled; },
        .checked = [&checked]() { return checked; }
    }));
    assert(!registry.register_action({
        .id = "editor.save",
        .label = "Duplicate",
        .execute = []() {}
    }));

    assert(registry.find("editor.save"));
    assert(registry.is_enabled("editor.save"));
    assert(!registry.is_checked("editor.save"));
    assert(registry.execute("editor.save"));
    assert(executions == 1);

    enabled = false;
    checked = true;
    assert(!registry.execute("editor.save"));
    assert(executions == 1);
    assert(registry.is_checked("editor.save"));

    assert(registry.search("save").size() == 1);
    assert(registry.search("FILE").size() == 1);
    assert(registry.search("missing").empty());

    std::cout << "EditorActionRegistryTests passed\n";
    return 0;
}
