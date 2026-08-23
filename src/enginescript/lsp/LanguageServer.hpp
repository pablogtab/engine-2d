#pragma once

#include "../editor/EngineScriptWorkspace.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace EngineScript {

class LanguageServer {
public:
    explicit LanguageServer(std::string project_root);

    std::string handle_message(const std::string& json_rpc_msg);

    // Direct API for LSP handlers
    std::string on_initialize(const std::string& id);
    std::string on_did_open(const std::string& uri, const std::string& text, uint32_t version);
    std::string on_did_change(const std::string& uri, const std::string& text, uint32_t version);
    std::string on_document_symbol(const std::string& id, const std::string& uri);
    std::string on_completion(const std::string& id, const std::string& uri, uint32_t line, uint32_t character);
    std::string on_hover(const std::string& id, const std::string& uri, uint32_t line, uint32_t character);
    std::string on_definition(const std::string& id, const std::string& uri, uint32_t line, uint32_t character);

    const EngineScriptWorkspace& workspace() const { return m_workspace; }

private:
    std::string publish_diagnostics(const std::string& uri);

    EngineScriptWorkspace m_workspace;
    std::unordered_map<std::string, uint32_t> m_open_document_versions;
};

} // namespace EngineScript
