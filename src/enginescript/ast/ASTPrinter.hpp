#pragma once

#include "AST.hpp"
#include <string>

namespace EngineScript {

class ASTPrinter {
public:
    static std::string print(const ModuleNode& module);
    static std::string print(const ASTNode& node);

private:
    static void print_decl(const Declaration& decl, std::string& out, int indent);
    static void print_stmt(const Statement& stmt, std::string& out, int indent);
    static void print_expr(const Expression& expr, std::string& out, int indent);
    static std::string make_indent(int indent);
};

} // namespace EngineScript
