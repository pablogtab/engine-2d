#include "core/Log.hpp"

#include <cassert>
#include <iostream>
#include <optional>

int main() {
    std::optional<Engine::LogRecord> received;
    Engine::Log::set_callback([&received](const Engine::LogRecord& record) {
        received = record;
    });

    Engine::LogMetadata metadata;
    metadata.category = "script";
    metadata.system = "EngineScript";
    metadata.entity = 42;
    metadata.asset = 77;
    metadata.source = Engine::LogSourceLocation{"Assets/Scripts/Player.e2s", 12, 8};
    Engine::Log::error("Expected runtime failure", metadata);

    assert(received.has_value());
    assert(received->level == Engine::LogLevel::ERR);
    assert(received->message == "Expected runtime failure");
    assert(received->metadata.category == "script");
    assert(received->metadata.system == "EngineScript");
    assert(received->metadata.entity == 42);
    assert(received->metadata.asset == 77);
    assert(received->metadata.source.has_value());
    assert(received->metadata.source->path == "Assets/Scripts/Player.e2s");
    assert(received->metadata.source->line == 12);
    assert(received->metadata.source->column == 8);

    Engine::Log::set_callback(nullptr);
    std::cout << "[PASS] StructuredLogTests passed!\n";
    return 0;
}
