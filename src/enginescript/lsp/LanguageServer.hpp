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
    std::vector<std::string> take_pending_notifications();

    // Direct API for LSP handlers
    std::string on_initialize(const std::string& id);
    std::string on_did_open(const std::string& uri, const std::string& text, uint32_t version);
    std::string on_did_change(const std::string& uri, const std::string& text, uint32_t version);
    std::string on_did_close(const std::string& uri);
    std::string on_document_symbol(const std::string& id, const std::string& uri);
    std::string on_completion(const std::string& id, const std::string& uri, uint32_t line, uint32_t character);
    std::string on_hover(const std::string& id, const std::string& uri, uint32_t line, uint32_t character);
    std::string on_definition(const std::string& id, const std::string& uri, uint32_t line, uint32_t character);
    std::string on_signature_help(const std::string& id, const std::string& uri, uint32_t line, uint32_t character);
    void on_watched_files_changed(const std::string& params_json);

    const EngineScriptWorkspace& workspace() const { return m_workspace; }

private:
    std::string publish_diagnostics(const std::string& uri);
    std::string source_path_for_uri(const std::string& uri) const;
    std::string uri_for_source_path(const std::string& source_path) const;

    EngineScriptWorkspace m_workspace;
    std::unordered_map<std::string, uint32_t> m_open_document_versions;
    std::unordered_map<std::string, std::string> m_uri_to_source_path;
    std::vector<std::string> m_pending_notifications;
};

} // namespace EngineScript
