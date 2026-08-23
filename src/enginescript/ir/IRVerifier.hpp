#pragma once

#include "TypedIR.hpp"
#include "../core/DiagnosticEngine.hpp"

namespace EngineScript {

class IRVerifier {
public:
    explicit IRVerifier(DiagnosticEngine& diagnostics);

    bool verify_module(const IRModule& module);

private:
    bool verify_class(const IRClass& cls);
    bool verify_struct(const IRStruct& st);
    bool verify_function(const IRFunction& fn);
    bool verify_block(const IRBlock& block);
    bool verify_operation(const IROperation* op);

    DiagnosticEngine& m_diagnostics;
};

} // namespace EngineScript
