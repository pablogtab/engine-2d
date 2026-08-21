#include "OpenAiResponsesProvider.hpp"

#if ENGINE_EDITOR_HAS_CURL
#include <curl/curl.h>

#include <algorithm>
#include <mutex>

namespace Engine {
namespace {

struct CurlRequestContext {
    const std::function<bool(std::string_view)>* on_chunk{nullptr};
    const std::function<bool()>* is_cancelled{nullptr};
    std::string response_body;
    bool callback_cancelled{false};
};

std::size_t write_callback(char* data, std::size_t size, std::size_t count, void* user_data) {
    const std::size_t bytes = size * count;
    auto* context = static_cast<CurlRequestContext*>(user_data);
    constexpr std::size_t MAX_CAPTURED_RESPONSE_BYTES = 256 * 1024;
    if (context->response_body.size() < MAX_CAPTURED_RESPONSE_BYTES) {
        const std::size_t remaining = MAX_CAPTURED_RESPONSE_BYTES - context->response_body.size();
        context->response_body.append(data, std::min(bytes, remaining));
    }
    if ((context->is_cancelled && (*context->is_cancelled)()) ||
        (context->on_chunk && !(*context->on_chunk)(std::string_view(data, bytes)))) {
        context->callback_cancelled = true;
        return 0;
    }
    return bytes;
}

int progress_callback(void* user_data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* context = static_cast<const CurlRequestContext*>(user_data);
    return context->is_cancelled && (*context->is_cancelled)() ? 1 : 0;
}

class CurlEditorAiHttpTransport final : public IEditorAiHttpTransport {
public:
    CurlEditorAiHttpTransport() {
        static std::once_flag init_flag;
        std::call_once(init_flag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
    }

    EditorAiHttpResponse post(
        const EditorAiHttpRequest& request,
        const std::function<bool(std::string_view)>& on_chunk,
        const std::function<bool()>& is_cancelled
    ) override {
        EditorAiHttpResponse response;
        CURL* curl = curl_easy_init();
        if (!curl) {
            response.error = "Failed to initialize the HTTP client";
            return response;
        }

        curl_slist* headers = nullptr;
        for (const std::string& header : request.headers) {
            headers = curl_slist_append(headers, header.c_str());
        }
        CurlRequestContext context{&on_chunk, &is_cancelled};
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        const CURLcode code = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
        response.body = std::move(context.response_body);
        response.cancelled = is_cancelled && is_cancelled();
        if (code != CURLE_OK && !response.cancelled && !context.callback_cancelled) {
            response.error = curl_easy_strerror(code);
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return response;
    }
};

} // namespace

std::unique_ptr<IEditorAiHttpTransport> create_default_editor_ai_http_transport() {
    return std::make_unique<CurlEditorAiHttpTransport>();
}

} // namespace Engine

#else

namespace Engine {

std::unique_ptr<IEditorAiHttpTransport> create_default_editor_ai_http_transport() {
    return nullptr;
}

} // namespace Engine

#endif
