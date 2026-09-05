#include "scripting/ScriptRuntime.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "enginescript/editor/EngineScriptWorkspace.hpp"
#include "enginescript/module/NativeModuleBuilder.hpp"
#include "enginescript/module/GameModule.hpp"
#include "audio/AudioEngine.hpp"
#include "physics/PhysicsSystem.hpp"
#include "tilemap/TilemapColliderSystem.hpp"
#include "core/AssetDatabase.hpp"
#include "core/FileSystem.hpp"
#include "core/PlayerPrefs.hpp"
#include "core/ProjectContext.hpp"
#include "core/ProjectSettings.hpp"
#include "core/SaveSystem.hpp"
#include "core/ThreadPool.hpp"
#include "ecs/EntityManager.hpp"
#include "events/EventDispatcher.hpp"
#include "renderer/material/MaterialAsset.hpp"
#include "renderer/material/Material.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#if !defined(_WIN32)
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef NDEBUG
#error Regression checks must remain active in every build configuration
#endif

using namespace Engine;
namespace fs = std::filesystem;

int main() {
    const auto root = fs::temp_directory_path() / ("engine-integrity-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const auto write = [](const fs::path& p, const std::string& text) {
        assert(FileSystem::write_text_atomically(p.string(), text));
    };
    const auto save = root / "save.txt";
    {
        const auto project = root / "shader-cache";
        fs::create_directories(project / "Assets");
        AssetDatabase database(project);
        write(project / "Assets/source.msl", "shader revision one");
        const auto source = database.import_asset(project / "Assets/source.msl"); assert(source);
        const auto descriptor = [&](int samplers) {
            return "{\"name\":\"Shared shader\",\"sampler_count\":" + std::to_string(samplers) +
                ",\"uniform_buffers\":[],\"variants\":[{\"format\":\"msl\",\"source_handle\":\"" +
                source->to_string() + "\",\"entry_point\":\"main0\"}]}";
        };
        write(project / "Assets/shared.shader", descriptor(1));
        const auto shader = database.import_asset(project / "Assets/shared.shader"); assert(shader);
        write(project / "Assets/shared.material", "{\"name\":\"Shared material\",\"shader_handle\":\"" +
            shader->to_string() + "\",\"uniforms\":[]}");
        const auto handle = database.import_asset(project / "Assets/shared.material"); assert(handle);
        const auto first = MaterialAsset::load(*handle, database); assert(first);
        for (int i = 0; i < 100; ++i) {
            const auto next = MaterialAsset::load(*handle, database); assert(next);
            assert(next != first && next->shader_ptr() == first->shader_ptr());
        }
        // File edits must invalidate identity without relying on timestamps or rescan.
        write(project / "Assets/source.msl", "shader revision two");
        const auto changed = MaterialAsset::load(*handle, database); assert(changed);
        assert(changed->shader_ptr() != first->shader_ptr());
        write(project / "Assets/shared.shader", descriptor(0));
        const auto layout = MaterialAsset::load(*handle, database); assert(layout);
        assert(layout->shader_ptr() != changed->shader_ptr() && layout->shader().sampler_count() == 0);
        fs::remove(project / "Assets/source.msl");
        assert(!MaterialAsset::load(*handle, database));
    }
    write(save, "ORIGINAL");
#if !defined(_WIN32)
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        signal(SIGXFSZ, SIG_IGN);
        const rlimit limit{64,64};
        if (setrlimit(RLIMIT_FSIZE, &limit)) _exit(2);
        const std::string large(1000, 'X');
        if (FileSystem::write_text_atomically(save.string(), large)) _exit(3);
        if (FileSystem::read_text(save.string()) != "ORIGINAL") _exit(4);
        if (FileSystem::write_text((root / "plain.txt").string(), large)) _exit(5);
        if (FileSystem::write_bytes((root / "bytes.bin").string(),
                                   reinterpret_cast<const uint8_t*>(large.data()), large.size())) _exit(6);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif
    std::vector<std::thread> writers;
    for (int i=0;i<4;++i) writers.emplace_back([&,i] { write(save, std::string(1000, char('A'+i))); });
    for (auto& writer : writers) writer.join();
    const auto written = FileSystem::read_text(save.string());
    assert(written.size()==1000 && written==std::string(1000,written.front()));

    BinaryWriter payload; payload.write_u32(123);
    const auto slot = (root / "slot.sav").string();
    assert(SaveSystem::save_file(slot, payload));
    BinaryReader reader;
    assert(SaveSystem::load_file(slot, reader) && reader.read_u32()==123);
    { std::fstream stream(slot,std::ios::binary|std::ios::in|std::ios::out);
      uint32_t version=999; stream.seekp(4); stream.write(reinterpret_cast<char*>(&version),sizeof(version)); }
    assert(!SaveSystem::load_file(slot, reader));

    write(root / "prefs.json", "[]");
    PlayerPrefs::init((root / "prefs.json").string());
    PlayerPrefs::set_int("score", 7);
    assert(PlayerPrefs::get_int("score")==7 && !PlayerPrefs::load());
    assert(PlayerPrefs::get_int("score")==7);
    write(root / "prefs.json", R"({"vector":[1,"invalid",3]})");
    assert(PlayerPrefs::load());
    assert(PlayerPrefs::get_vec2("vector", {7, 8}) == glm::vec2(7, 8));
    assert(PlayerPrefs::get_vec3("vector", {7, 8, 9}) == glm::vec3(7, 8, 9));
    write(root / "project.engine2d", R"({"version":"bad"})");
    std::string error;
    assert(!ProjectContext::open(root,&error) && !error.empty());
    ProjectSettings settings;
    const auto previous=settings.display();
    write(root / "settings.json", R"({"display":{"reference_width":2000,"default_vsync":"bad"}})");
    assert(!settings.load((root / "settings.json").string()));
    assert(settings.display()==previous);

    const std::string bare_filename = root.filename().string() + ".txt";
    assert(FileSystem::write_text_atomically(bare_filename, "bare filename"));
    assert(FileSystem::read_text(bare_filename) == "bare filename");
    assert(fs::remove(bare_filename));

    const auto dependency=root/"Assets/dep.json", owner=root/"Assets/owner.json";
    write(dependency,"{}");
    AssetDatabase database(root);
    assert(database.scan(root/"Assets").invalid==0);
    const auto handle=database.find_by_path(dependency)->handle;
    write(owner,"{\"texture_handle\":\""+handle.to_string()+"\"}");
    assert(database.import_asset(owner));
    fs::remove(dependency); assert(database.remove_asset(dependency));
    assert(database.find_by_path(owner)->status==AssetStatus::MissingDependency);
    write(dependency,"{}"); assert(database.import_asset(dependency));
    assert(database.find_by_path(owner)->status==AssetStatus::Ready);
    write(database.database_path(),"{corrupt");
    assert(database.rescan().invalid==1);
    assert(database.find_by_path(dependency)->handle==handle);
    assert(FileSystem::read_text(database.database_path().string())=="{corrupt");
    write(database.database_path(),R"({"version":"bad","assets":[]})");
    assert(database.rescan().invalid==1 && database.find_by_path(dependency)->handle==handle);
    fs::remove(database.database_path());fs::create_directory(database.database_path());
    assert(!database.move_asset(dependency,root/"Assets/new.json"));
    assert(database.find_by_path(dependency) && !database.find_by_path(root/"Assets/new.json"));
    assert(!database.remove_asset(dependency));
    assert(database.find_by_path(dependency)->status==AssetStatus::Ready);

    write(root / "Assets/uncommitted.json", "{}");
    assert(!database.import_asset(root / "Assets/uncommitted.json"));
    assert(!database.find_by_path(root / "Assets/uncommitted.json"));
    const auto old_ppu = database.find(handle)->import_settings.pixels_per_unit;
    auto changed_settings = database.find(handle)->import_settings;
    changed_settings.pixels_per_unit = old_ppu * 2;
    assert(!database.update_import_settings(handle, changed_settings));
    assert(database.find(handle)->import_settings.pixels_per_unit == old_ppu);

    EntityManager entities;
    const auto one=entities.create_entity();entities.add_component<int>(one,1);
    const auto two=entities.create_entity();entities.add_component<int>(two,2);
    std::vector<EntityID> visited;
    entities.for_each<int>([&](EntityID id,int&) { visited.push_back(id); });
    assert((visited==std::vector<EntityID>{one,two}));
    entities.destroy_entity(one);assert(!entities.is_alive(one));
    assert(entities.create_entity()==one && entities.is_alive(one));
    entities.clear();assert(!entities.is_alive(one));
    entities.create_entity_with_id(std::numeric_limits<EntityID>::max()-1);
    assert(entities.create_entity()==std::numeric_limits<EntityID>::max());
    bool exhausted=false;
    try { (void)entities.create_entity(); } catch (const std::overflow_error&) { exhausted=true; }
    assert(exhausted && !entities.is_alive(0));

    EventDispatcher dispatcher;
    int calls=0;bool first=true;
    EventDispatcher::SubscriptionId removed=0;
    dispatcher.subscribe<CustomLogEvent>([&](const auto&) {
        ++calls;
        if (!first) return;
        first=false;assert(dispatcher.unsubscribe(removed));
        for(int i=0;i<100;++i) dispatcher.subscribe<CustomLogEvent>([&](const auto&) { ++calls; });
        dispatcher.queue(CustomLogEvent{"next"});
    });
    removed=dispatcher.subscribe<CustomLogEvent>([](const auto&) { assert(false); });
    dispatcher.queue(CustomLogEvent{"first"});dispatcher.dispatch_queued();assert(calls==1);
    dispatcher.dispatch_queued();assert(calls==102);
    dispatcher.clear();dispatcher.publish(CustomLogEvent{"cleared"});assert(calls==102);

    dispatcher.subscribe<CustomLogEvent>([&](const auto&) {
        ++calls;
        dispatcher.clear();
        dispatcher.subscribe<CustomLogEvent>([&](const auto&) { calls += 10; });
    });
    dispatcher.queue(CustomLogEvent{"clear batch"}); dispatcher.queue(CustomLogEvent{"cancelled"});
    dispatcher.dispatch_queued(); assert(calls == 103);
    dispatcher.queue(CustomLogEvent{"new batch"}); dispatcher.dispatch_queued(); assert(calls == 113);

    ThreadPool pool(3);
    std::atomic<int> completed{0};
    bool threw=false;
    try {
        pool.parallel_for(0,900,[&](size_t i) {
            if(i>=600) throw std::runtime_error("main chunk");
            if(i%300==0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ++completed;
        });
    } catch (const std::runtime_error&) { threw=true; }
    assert(threw && completed==600);
    completed=0;threw=false;
    try {
        pool.parallel_for(0,900,[&](size_t i) {
            if(i==0) throw std::runtime_error("worker chunk");
            if(i==300) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ++completed;
        });
    } catch (const std::runtime_error&) { threw=true; }
    assert(threw && completed==600);
    {
        EntityManager em;
        EventDispatcher events;
        std::vector<EntityID> branch;
        for (int i = 0; i < 4; ++i) {
            auto id = em.create_entity();
            em.add_component<TransformComponent>(id, TransformComponent{{float(i + 1), float(i), 2}, {2, 3}, 15});
            if (!branch.empty()) assert(em.set_parent(id, branch.back()).succeeded());
            branch.push_back(id);
        }
        auto& ancestor = em.get_component<TransformComponent>(branch.front());
        ancestor.position = {100, 30, 5}; ancestor.rotation = 90; ancestor.visible = false;
        auto& moving = em.get_component<TransformComponent>(branch[2]);
        moving.position = {20, 10, 2}; moving.pivot = {.2f, .8f}; moving.skew = {.1f, .2f};
        TransformSystem::update(em);
        std::vector<TransformComponent> expected;
        for (auto id : branch) {
            expected.push_back(em.get_component<TransformComponent>(id));
            // Invalidate derived fields, as when multiple script edits precede a tick.
            auto& tx = em.get_component<TransformComponent>(id);
            tx.world_position = {-999, -999, -999}; tx.world_scale = {1, 1}; tx.world_rotation = 0;
        }
        PhysicsSystem::set_position(em, branch[2], {20, 10});
        for (std::size_t i = 0; i < branch.size(); ++i) {
            const auto& tx = em.get_component<TransformComponent>(branch[i]);
            assert(tx.position == expected[i].position && tx.rotation == expected[i].rotation);
            assert(glm::length(tx.world_position - expected[i].world_position) < .001f);
            assert(tx.world_scale == expected[i].world_scale && tx.world_rotation == expected[i].world_rotation);
            assert(tx.world_pivot == expected[i].world_pivot && tx.world_skew == expected[i].world_skew);
            assert(tx.world_visible == expected[i].world_visible);
        }
        PhysicsSystem::set_position(em, 0, {1, 1});
    }
    {
        EntityManager em;
        EventDispatcher events;
        const auto body = [&](float x) {
            auto id = em.create_entity();
            TransformComponent tx; tx.position.x = x;
            em.add_component<TransformComponent>(id, tx);
            auto rb = RigidBodyComponent{}; rb.fixed_rotation = false;
            em.add_component<RigidBodyComponent>(id, rb);
            em.add_component<ColliderComponent>(id, ColliderComponent::Box({10, 10}));
            em.get_component<ColliderComponent>(id).shapes.front().is_trigger = true;
            return id;
        };
        PhysicsSystem::clear(); PhysicsSystem::set_gravity({0, 0});
        auto parent = body(100); auto child = body(10);
        assert(em.set_parent(child, parent).succeeded());
        PhysicsSystem::update(em, events, 0);
        assert(std::abs(em.get_component<TransformComponent>(child).world_position.x - 110) < .001f);
        PhysicsSystem::set_velocity(em, parent, {100, 0});
        PhysicsSystem::update(em, events, .01f);
        assert(std::abs(em.get_component<TransformComponent>(child).world_position.x - 110) < .001f);
        PhysicsSystem::set_position(em, child, {20, 0});
        PhysicsSystem::update(em, events, 0);
        assert(std::abs(em.get_component<TransformComponent>(child).position.x - 20) < .001f);
        PhysicsSystem::set_angular_velocity(em, child, 90);
        PhysicsSystem::update(em, events, .01f);
        assert(std::abs(em.get_component<RigidBodyComponent>(child).angular_velocity - 90) < .001f);
        em.destroy_entity(child); auto replacement = body(500); assert(replacement == child);
        PhysicsSystem::update(em, events, 0);
        assert(std::abs(em.get_component<TransformComponent>(replacement).position.x - 500) < .001f);
        em.remove_component<ColliderComponent>(replacement);
        PhysicsSystem::update(em, events, 0);
        assert(PhysicsSystem::active_body_count() == 1);
        auto invalid = body(700);
        auto& shape = em.get_component<ColliderComponent>(invalid).shapes.front();
        shape.type = ColliderShapeType::Polygon;
        for (int i = 0; i < 9; ++i) shape.vertices.push_back({float(i), float(i % 2)});
        assert(!collider_shape_valid(shape));
        PhysicsSystem::update(em, events, 0);
        assert(PhysicsSystem::active_body_count() == 1);
        shape.vertices = {{0, 0}, {1, 0}, {2, 0}};
        assert(!collider_shape_valid(shape));
        shape.vertices = {{0, 0}, {10, 0}, {0, 10}};
        assert(collider_shape_valid(shape));
        shape.vertices[0].x = std::numeric_limits<float>::quiet_NaN();
        assert(!collider_shape_valid(shape));
        PhysicsSystem::clear();
        em.clear();
        auto tiles = em.create_entity();
        em.add_component<TransformComponent>(tiles, {});
        auto& map = em.add_component<TilemapComponent>(tiles, {});
        map.set_tile(0, 0, 1); map.set_tile(65535, 65535, 1);
        map.set_tile(std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), 1);
        TilemapColliderSystem::build_composite_colliders(em, tiles);
        std::size_t boxes = 0;
        em.for_each<TilemapGeneratedColliderComponent>([&](EntityID, auto&) { ++boxes; });
        assert(boxes == 3);
        map.clear();
        for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) map.set_tile(x, y, 1);
        TilemapColliderSystem::build_composite_colliders(em, tiles);
        boxes = 0;
        em.for_each<TilemapGeneratedColliderComponent>([&](EntityID id, auto&) {
            ++boxes;
            assert(em.get_component<ColliderComponent>(id).shapes.front().size == glm::vec2(64, 64));
            assert(em.get_component<TransformComponent>(id).position == glm::vec3(32, -32, 0));
        });
        assert(boxes == 1);
    }
    {
        // A short mono PCM WAV exercises both decoder ownership and file voices.
        std::vector<uint8_t> wav(44 + 960, 0);
        const auto put = [&](std::size_t at, uint32_t value, int bytes) {
            for (int i = 0; i < bytes; ++i) wav[at + i] = uint8_t(value >> (8 * i));
        };
        std::copy_n("RIFF", 4, wav.begin()); put(4, wav.size() - 8, 4);
        std::copy_n("WAVEfmt ", 8, wav.begin() + 8);
        put(16, 16, 4); put(20, 1, 2); put(22, 1, 2); put(24, 48000, 4);
        put(28, 96000, 4); put(32, 2, 2); put(34, 16, 2);
        std::copy_n("data", 4, wav.begin() + 36); put(40, 960, 4);
        assert(FileSystem::write_bytes((root / "sound.wav").string(), wav));
        assert(AudioEngine::init(true));
        std::vector<float> samples(4800 * 2);
        for (int cycle = 0; cycle < 20; ++cycle) {
            AudioEngine::play_sound_from_memory(wav.data(), wav.size());
            AudioEngine::play_sound((root / "sound.wav").string());
            assert(AudioEngine::active_sound_count() == 2);
            assert(AudioEngine::render_offline(samples.data(), 4800));
            assert(AudioEngine::active_sound_count() == 0);
        }
        const uint8_t invalid[] = {0, 1, 2};
        AudioEngine::play_sound_from_memory(invalid, sizeof(invalid));
        assert(AudioEngine::active_sound_count() == 0);
        AudioEngine::play_sound_from_memory(wav.data(), wav.size());
        AudioEngine::shutdown(); assert(AudioEngine::active_sound_count() == 0);
        assert(AudioEngine::init(true)); AudioEngine::shutdown();
    }
    {
        auto scripts_root = root / "literal $(printf EXPANDED) `printf BAD`";
#if !defined(_WIN32)
        scripts_root += " \" quote";
#endif
        write(scripts_root / "Assets/Scripts/Disposable.e2s", R"(
@behaviour
export class Disposable extends Behaviour {
    constructor(@inject private audio: Audio) {}
    on_destroy(): Void { audio.stopMusic(); }
}
)");
        AssetDatabase db(scripts_root); assert(db.scan(scripts_root / "Assets").invalid == 0);
        const auto handle = db.find_by_path(scripts_root / "Assets/Scripts/Disposable.e2s")->handle;
        EntityManager em; ScriptRuntime runtime; int destroyed = 0;
        const auto attach = [&] {
            const auto id = em.create_entity(); em.add_component<TransformComponent>(id, {});
            ScriptComponent scripts; scripts.instances.front().script_handle = handle;
            em.add_component<ScriptComponent>(id, scripts); return id;
        };
        const auto parent = attach(), child = attach(), grandchild = attach();
        assert(em.set_parent(child, parent).succeeded()); assert(em.set_parent(grandchild, child).succeeded());
        ScriptRuntimeServices services; services.stop_music = [&] { ++destroyed; };
        const auto result = runtime.start(em, db, nullptr, services);
        assert(result.started == 3 && result.failed == 0);
        runtime.request_entity_destroy(parent); runtime.request_entity_destroy(child);
        runtime.update(0);
        assert(destroyed == 3 && runtime.instance_count() == 0 && em.get_living_entity_count() == 0);
        runtime.shutdown(); assert(destroyed == 4); // Session-level music cleanup.
        const auto empty_root = root / "empty-module";
        fs::create_directories(empty_root);
        EngineScript::EngineScriptWorkspace workspace(empty_root.string());
        const auto module = EngineScript::NativeModuleBuilder::build(workspace, empty_root.string(), 1);
        assert(module.success && fs::is_regular_file(module.library_path));
        std::string error;
        const auto loaded = EngineScript::GameModule::load_dynamic_library(module.library_path, &error);
        assert(loaded);
    }
    fs::remove_all(root);
}
