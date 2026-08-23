#pragma once

#include "TypedIR.hpp"
#include "../ast/AST.hpp"
#include "../types/TypeChecker.hpp"
#include <memory>

namespace EngineScript {

class IRLowering {
public:
    explicit IRLowering(const TypeChecker& type_checker);

    IRModule lower_module(const ModuleNode& module);

private:
    IRClass lower_class(const ClassDecl& cls);
    IRStruct lower_struct(const StructDecl& st);
    IRFunction lower_function(const FunctionDecl& fn);
    IRMethod lower_method(const MethodDecl& m);
    IRProperty lower_property(const PropertyDecl& prop);

    void lower_statement(const std::shared_ptr<Statement>& stmt, IRBlock& current_block);
    std::shared_ptr<IROperation> lower_expression(const std::shared_ptr<Expression>& expr);

    const TypeChecker& m_type_checker;
};

} // namespace EngineScript
