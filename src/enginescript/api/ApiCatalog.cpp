#include "ApiCatalog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace EngineScript {

namespace {

using Json = nlohmann::json;

ApiAvailability parse_availability(const Json& value) {
    const std::string availability = value.value("availability", "available");
    if (availability == "available") return ApiAvailability::Available;
    if (availability == "planned") return ApiAvailability::Planned;
    throw std::runtime_error("EngineScript API catalog contains an invalid availability");
}

std::vector<ApiParameterDescriptor> parse_parameters(const Json& value) {
    std::vector<ApiParameterDescriptor> parameters;
    for (const Json& parameter : value.value("parameters", Json::array())) {
        parameters.push_back({
            parameter.at("name").get<std::string>(),
            parameter.at("type").get<std::string>(),
            parameter.value("default", ""),
            parameter.value("optional", false)
        });
    }
    return parameters;
}

} // namespace

const ApiCatalog& ApiCatalog::instance() {
    static const ApiCatalog catalog;
    return catalog;
}

ApiCatalog::ApiCatalog() {
    std::ifstream stream(ENGINE_SCRIPT_API_CATALOG_PATH);
    if (!stream) {
        throw std::runtime_error("EngineScript API catalog could not be opened");
    }

    const Json document = Json::parse(stream);
    m_abi_version = document.at("abiVersion").get<uint32_t>();
    m_sdk_hash = document.at("sdkHash").get<std::string>();
    if (m_abi_version == 0 || m_sdk_hash.empty()) {
        throw std::runtime_error("EngineScript API catalog requires a non-zero ABI version and SDK hash");
    }

    for (const Json& type : document.at("types")) {
        m_types.push_back({
            type.at("name").get<std::string>(),
            type.value("documentation", ""),
            type.value("valueType", false),
            parse_availability(type)
        });
    }
    for (const Json& function : document.at("functions")) {
        m_functions.push_back({
            function.at("name").get<std::string>(),
            function.at("returnType").get<std::string>(),
            parse_parameters(function),
            function.value("documentation", ""),
            parse_availability(function)
        });
    }
    for (const Json& method : document.at("methods")) {
        const std::vector<ApiParameterDescriptor> parameters = parse_parameters(method);
        const uint32_t inferred_min_arguments = static_cast<uint32_t>(std::count_if(
            parameters.begin(), parameters.end(), [](const ApiParameterDescriptor& parameter) { return !parameter.optional; }
        ));
        const uint32_t min_arguments = method.value("minArguments", inferred_min_arguments);
        const uint32_t max_arguments = method.value("maxArguments", static_cast<uint32_t>(parameters.size()));
        if (min_arguments > max_arguments) {
            throw std::runtime_error("EngineScript API catalog has an invalid method argument range");
        }
        m_methods.push_back({
            method.at("ownerType").get<std::string>(),
            method.at("name").get<std::string>(),
            method.at("returnType").get<std::string>(),
            parameters,
            min_arguments,
            max_arguments,
            method.value("nullableReturn", false),
            method.value("documentation", ""),
            parse_availability(method)
        });
    }
    for (const Json& property : document.value("properties", Json::array())) {
        m_properties.push_back({
            property.at("ownerType").get<std::string>(),
            property.at("name").get<std::string>(),
            property.at("type").get<std::string>(),
            property.value("readable", true),
            property.value("writable", false),
            property.value("nullable", false),
            property.value("documentation", ""),
            parse_availability(property)
        });
    }
}

const ApiPropertyDescriptor* ApiCatalog::find_available_property(
    std::string_view owner_type,
    std::string_view name
) const {
    for (const ApiPropertyDescriptor& property : m_properties) {
        if (property.owner_type == owner_type && property.name == name &&
            property.availability == ApiAvailability::Available) {
            return &property;
        }
    }
    return nullptr;
}

bool ApiCatalog::has_available_type(std::string_view name) const {
    return find_available_type(name) != nullptr;
}

const ApiTypeDescriptor* ApiCatalog::find_available_type(std::string_view name) const {
    for (const ApiTypeDescriptor& type : m_types) {
        if (type.name == name && type.availability == ApiAvailability::Available) return &type;
    }
    return nullptr;
}

const ApiFunctionDescriptor* ApiCatalog::find_available_function(std::string_view name) const {
    for (const ApiFunctionDescriptor& function : m_functions) {
        if (function.name == name && function.availability == ApiAvailability::Available) {
            return &function;
        }
    }
    return nullptr;
}

bool ApiCatalog::has_available_function(std::string_view name) const {
    for (const ApiFunctionDescriptor& function : m_functions) {
        if (function.name == name && function.availability == ApiAvailability::Available) return true;
    }
    return false;
}

const ApiMethodDescriptor* ApiCatalog::find_available_method(
    std::string_view owner_type,
    std::string_view name
) const {
    for (const ApiMethodDescriptor& method : m_methods) {
        if (method.owner_type == owner_type && method.name == name &&
            method.availability == ApiAvailability::Available) {
            return &method;
        }
    }
    return nullptr;
}

} // namespace EngineScript
