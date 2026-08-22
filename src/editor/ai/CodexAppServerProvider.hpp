#pragma once

#include "EditorAiProvider.hpp"

#include <memory>
#include <string>

namespace Engine {

enum class CodexAppServerReadStatus {
    Line,
    Cancelled,
    Closed,
    Failed
};

struct CodexAppServerReadResult {
    CodexAppServerReadStatus status{CodexAppServerReadStatus::Failed};
    std::string line;
    std::string error;
};

class ICodexAppServerTransport {
public:
    virtual ~ICodexAppServerTransport() = default;
    virtual bool start(const std::string& executable, std::string& error) = 0;
    virtual bool write_line(const std::string& line, std::string& error) = 0;
    virtual CodexAppServerReadResult read_line(
        const std::function<bool()>& is_cancelled
    ) = 0;
    virtual void stop() = 0;
};

class CodexAppServerProvider final : public IEditorAiProvider {
public:
    explicit CodexAppServerProvider(std::unique_ptr<ICodexAppServerTransport> transport);

    [[nodiscard]] bool is_available() const override { return m_transport != nullptr; }
    [[nodiscard]] EditorAiProviderResult run(
        const EditorAiProviderRequest& request,
        const std::function<void(const EditorAiProviderEvent&)>& on_event,
        const std::function<bool()>& is_cancelled = {}
    ) override;

private:
    std::unique_ptr<ICodexAppServerTransport> m_transport;
};

[[nodiscard]] std::string find_default_codex_executable();
[[nodiscard]] std::string find_default_codex_skill_path(const std::string& skill_name);
[[nodiscard]] std::unique_ptr<ICodexAppServerTransport> create_default_codex_app_server_transport();

} // namespace Engine
