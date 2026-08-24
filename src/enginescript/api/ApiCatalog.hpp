#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace EngineScript {

enum class ApiAvailability {
    Available,
    Planned
};

struct ApiTypeDescriptor {
    std::string name;
    ApiAvailability availability{ApiAvailability::Available};
};

struct ApiFunctionDescriptor {
    std::string name;
    std::string return_type;
    ApiAvailability availability{ApiAvailability::Available};
};

struct ApiMethodDescriptor {
    std::string owner_type;
    std::string name;
    std::string return_type;
    uint32_t min_arguments{0};
    uint32_t max_arguments{0};
    ApiAvailability availability{ApiAvailability::Available};
};

class ApiCatalog {
public:
    static const ApiCatalog& instance();

    [[nodiscard]] const std::string& sdk_hash() const { return m_sdk_hash; }
    [[nodiscard]] uint32_t abi_version() const { return m_abi_version; }
    [[nodiscard]] const std::vector<ApiTypeDescriptor>& types() const { return m_types; }
    [[nodiscard]] const std::vector<ApiFunctionDescriptor>& functions() const { return m_functions; }
    [[nodiscard]] const std::vector<ApiMethodDescriptor>& methods() const { return m_methods; }
    [[nodiscard]] bool has_available_type(std::string_view name) const;
    [[nodiscard]] bool has_available_function(std::string_view name) const;
    [[nodiscard]] const ApiMethodDescriptor* find_available_method(
        std::string_view owner_type,
        std::string_view name
    ) const;

private:
    ApiCatalog();

    uint32_t m_abi_version{0};
    std::string m_sdk_hash;
    std::vector<ApiTypeDescriptor> m_types;
    std::vector<ApiFunctionDescriptor> m_functions;
    std::vector<ApiMethodDescriptor> m_methods;
};

} // namespace EngineScript
