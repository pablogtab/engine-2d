#include <iostream>
#include <string>
#include <sstream>
#include <filesystem>
#include "enginescript/lsp/LanguageServer.hpp"

using namespace EngineScript;

int main(int argc, char* argv[]) {
    std::string root_dir = ".";
    if (argc > 1) {
        root_dir = argv[1];
    }

    LanguageServer server(root_dir);

    // Read JSON-RPC messages from stdin framing via Content-Length headers
    while (std::cin.good()) {
        std::string line;
        size_t content_length = 0;

        while (std::getline(std::cin, line)) {
            // Strip trailing \r if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) {
                // Empty line marks end of header section
                break;
            }

            const std::string header_prefix = "Content-Length: ";
            if (line.rfind(header_prefix, 0) == 0) {
                try {
                    content_length = std::stoull(line.substr(header_prefix.size()));
                } catch (...) {
                    content_length = 0;
                }
            }
        }

        if (content_length == 0) {
            if (std::cin.eof()) break;
            continue;
        }

        // Read content_length bytes of JSON body
        std::string body(content_length, '\0');
        std::cin.read(&body[0], content_length);

        if (std::cin.gcount() < static_cast<std::streamsize>(content_length)) {
            break; // Unexpected EOF
        }

        std::string response = server.handle_message(body);
        if (!response.empty()) {
            std::cout << "Content-Length: " << response.size() << "\r\n\r\n" << response;
            std::cout.flush();
        }
    }

    return 0;
}
