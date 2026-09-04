#pragma once

#include "../ir/TypedIR.hpp"
#include "../core/SourceManager.hpp"
#include <string>
#include <unordered_set>
#include <vector>

namespace EngineScript {

struct CppOutput {
    std::string header_content;
    std::string source_content;
    std::string header_filename;
    std::string source_filename;
};

class CppEmitter {
public:
    explicit CppEmitter(const SourceManager& source_manager);

    CppOutput emit_module(const IRModule& module);
    [[nodiscard]] std::string emit_game_module_entrypoint(
        const std::vector<IRModule>& modules,
        const std::string& module_name,
        uint32_t build_number = 0
    ) const;

private:
    std::string type_to_cpp(const Type* type, bool is_const_ref = false) const;
    void emit_class_header(const IRClass& cls, std::string& out);
    void emit_struct_header(const IRStruct& st, std::string& out);
    void emit_interface_header(const IRInterface& interface_decl, std::string& out);
    void emit_enum_header(const IREnum& enum_decl, std::string& out);
    void emit_function_header(const IRFunction& fn, std::string& out);

    void emit_class_source(const IRClass& cls, const std::string& header_name, std::string& out);
    void emit_exposed_property_bridge(const IRClass& cls, std::string& out);
    void emit_method_source(const IRClass& cls, const IRMethod& m, std::string& out);
    void emit_struct_source(const IRStruct& st, std::string& out);
    void emit_function_source(const IRFunction& fn, std::string& out);
    void emit_statement_cpp(const IRStatement& st, std::string& out, int indent);
    void emit_operation_statement_cpp(const IROperation* op, std::string& out, int indent);
    std::string emit_operation_cpp(const IROperation* op);
    std::string emit_truthy_cpp(const IROperation* op);
    std::string emit_match_condition_cpp(
        const IRMatchPattern& pattern,
        std::string_view subject,
        const Type* subject_type
    ) const;
    [[nodiscard]] std::string member_access_operator(const IROperation* target) const;
    [[nodiscard]] std::string identifier_to_cpp(const std::string& identifier) const;

    const SourceManager& m_source_manager;
    std::unordered_set<std::string> m_injected_members;
    std::unordered_set<std::string> m_async_task_methods;
    bool m_emitting_async_method{false};
};

} // namespace EngineScript
