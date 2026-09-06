#include "core/AssetDatabase.hpp"
#include "editor/tools/AssetBrowserQuery.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << content;
}

const Engine::AssetBrowserEntry* find_entry(
    const std::vector<Engine::AssetBrowserEntry>& entries,
    const std::string& name
) {
    const auto found = std::find_if(entries.begin(), entries.end(), [&name](const auto& entry) {
        return entry.name == name;
    });
    return found == entries.end() ? nullptr : &*found;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace Engine;

    const fs::path root = fs::temp_directory_path() / "engine2d_asset_browser_query_tests";
    std::error_code error;
    fs::remove_all(root, error);
    write_file(root / "Assets/Characters/Hero.png", "large texture payload");
    write_file(root / "Assets/Characters/HeroController.e2s", "@behaviour\nexport class HeroController extends Behaviour {}\n");
    write_file(root / "Assets/Materials/Hero.material", "{}\n");
    write_file(root / "Assets/Scenes/Main.scene", "{}\n");
    write_file(
        root / "Assets/Layout.levelpalette",
        R"({"version":1,"type":"Engine2D.LevelPalette","name":"Layout","grid":{"origin":[0,0]},"categories":[{"id":"walls","label":"Walls"}],"entries":[]})"
    );
    write_file(root / "Assets/Readme.txt", "text\n");
    write_file(root / "Assets/.DS_Store", "finder metadata\n");
    write_file(root / "Assets/.hidden.png", "hidden image\n");
    write_file(root / "Assets/.cache/Ghost.png", "hidden cache\n");
    write_file(root / "Assets/Characters/Hero.png.meta", "sidecar\n");
    write_file(root / "Assets/Thumbs.db", "windows metadata\n");
    write_file(root / "Assets/draft.tmp", "temporary\n");

    AssetDatabase database(root);
    const AssetScanResult scan = database.scan(root / "Assets");
    assert(scan.ready == 12);

    AssetBrowserQueryOptions options;
    auto entries = AssetBrowserQuery::collect(
        database, root / "Assets", root / "Assets", options
    );
    assert(entries.size() == 5);
    assert(entries[0].is_directory && entries[1].is_directory && entries[2].is_directory);
    assert(find_entry(entries, "Readme.txt"));
    assert(!find_entry(entries, ".DS_Store"));
    assert(!find_entry(entries, ".hidden.png"));
    assert(!find_entry(entries, ".cache"));
    assert(!find_entry(entries, "Thumbs.db"));
    assert(!find_entry(entries, "draft.tmp"));

    options.scope = AssetBrowserScope::Project;
    options.text = "hero";
    entries = AssetBrowserQuery::collect(database, root / "Assets", root / "Assets", options);
    assert(entries.size() == 3);

    options.text = "t:script ext:e2s path:characters";
    entries = AssetBrowserQuery::collect(database, root / "Assets", root / "Assets", options);
    assert(entries.size() == 1 && entries.front().name == "HeroController.e2s");

    options.text = "t:folder characters";
    entries = AssetBrowserQuery::collect(database, root / "Assets", root / "Assets", options);
    assert(entries.size() == 1 && entries.front().is_directory);

    const AssetRecord* hero_texture = database.find_by_path("Assets/Characters/Hero.png");
    assert(hero_texture);
    options.text = "id:" + hero_texture->handle.to_string().substr(0, 6);
    entries = AssetBrowserQuery::collect(database, root / "Assets", root / "Assets", options);
    assert(entries.size() == 1 && entries.front().handle == hero_texture->handle);

    options.text.clear();
    options.type = AssetBrowserTypeFilter::Materials;
    entries = AssetBrowserQuery::collect(database, root / "Assets", root / "Assets", options);
    assert(entries.size() == 1 && entries.front().type == AssetType::Material);

    options.type = AssetBrowserTypeFilter::LevelPalettes;
    entries = AssetBrowserQuery::collect(database, root / "Assets", root / "Assets", options);
    assert(entries.size() == 1 && entries.front().type == AssetType::LevelPalette);
    options.type = AssetBrowserTypeFilter::All;
    options.text = "t:palette";
    entries = AssetBrowserQuery::collect(database, root / "Assets", root / "Assets", options);
    assert(entries.size() == 1 && entries.front().name == "Layout.levelpalette");

    options.text.clear();
    fs::remove(root / "Assets/Characters/Hero.png", error);
    assert(database.rescan().ready == 11);
    options.status = AssetBrowserStatusFilter::Issues;
    entries = AssetBrowserQuery::collect(database, root / "Assets", root / "Assets", options);
    assert(entries.size() == 1);
    assert(entries.front().status == AssetStatus::MissingSource);

    options.status = AssetBrowserStatusFilter::Ready;
    options.sort = AssetBrowserSort::Size;
    options.ascending = false;
    entries = AssetBrowserQuery::collect(database, root / "Assets", root / "Assets", options);
    assert(entries.size() == 5);
    assert(entries.front().size >= entries.back().size);

    const AssetBrowserEntry quoted{
        .project_path = "Assets/My Folder/Player Icon.png",
        .name = "Player Icon.png",
        .extension = ".png",
        .type = AssetType::Texture
    };
    assert(AssetBrowserQuery::matches_text(quoted, "\"player icon\" t:image ext:png"));
    assert(!AssetBrowserQuery::matches_text(quoted, "status:missing"));

    fs::remove_all(root, error);
    std::cout << "AssetBrowserQueryTests passed\n";
    return 0;
}
