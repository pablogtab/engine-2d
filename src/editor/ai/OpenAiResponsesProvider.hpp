#pragma once

#include "EditorAiProvider.hpp"

#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

struct EditorAiHttpRequest {
    std::string url;
    std::vector<std::string> headers;
    std::string body;
};

struct EditorAiHttpResponse {
    long status_code{0};
    std::string body;
    std::string error;
    bool cancelled{false};
};

class IEditorAiHttpTransport {
public:
    virtual ~IEditorAiHttpTransport() = default;
    virtual EditorAiHttpResponse post(
        const EditorAiHttpRequest& request,
        const std::function<bool(std::string_view)>& on_chunk,
        const std::function<bool()>& is_cancelled
    ) = 0;
};

using OpenAiProviderEventType = EditorAiProviderEventType;
using OpenAiProviderEvent = EditorAiProviderEvent;
using OpenAiProviderStatus = EditorAiProviderStatus;
using OpenAiResponsesRequest = EditorAiProviderRequest;
using OpenAiResponsesResult = EditorAiProviderResult;

class OpenAiResponsesProvider final : public IEditorAiProvider {
public:
    explicit OpenAiResponsesProvider(std::unique_ptr<IEditorAiHttpTransport> transport);

    [[nodiscard]] bool is_available() const override { return m_transport != nullptr; }
    [[nodiscard]] OpenAiResponsesResult run(
        const OpenAiResponsesRequest& request,
        const std::function<void(const OpenAiProviderEvent&)>& on_event,
        const std::function<bool()>& is_cancelled = {}
    ) override;

private:
    std::unique_ptr<IEditorAiHttpTransport> m_transport;
};

[[nodiscard]] std::unique_ptr<IEditorAiHttpTransport> create_default_editor_ai_http_transport();

} // namespace Engine
