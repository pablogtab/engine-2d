#pragma once

#include "../ir/TypedIR.hpp"
#include "../core/SourceManager.hpp"
#include <string>

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

private:
    std::string type_to_cpp(const Type* type, bool is_const_ref = false) const;
    void emit_class_header(const IRClass& cls, std::string& out);
    void emit_struct_header(const IRStruct& st, std::string& out);
    void emit_function_header(const IRFunction& fn, std::string& out);

    void emit_class_source(const IRClass& cls, const std::string& header_name, std::string& out);
    void emit_method_source(const IRClass& cls, const IRMethod& m, std::string& out);
    void emit_statement_cpp(const IRStatement& st, std::string& out, int indent);
    std::string emit_operation_cpp(const IROperation* op);

    const SourceManager& m_source_manager;
};

} // namespace EngineScript
