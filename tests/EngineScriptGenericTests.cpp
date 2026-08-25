#include <cassert>
#include <iostream>
#include "enginescript/generics/Generics.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptGenericTests...\n";

    GenericsManager manager;

    // Test 1: Monomorphization name mangling
    std::string m1 = manager.request_instantiation("Array", {"ItemSlot"});
    assert(m1 == "Array_ItemSlot");

    std::string m2 = manager.request_instantiation("Map", {"String", "Float"});
    assert(m2 == "Map_String_Float");

    // Test 2: Instantiation reuse
    std::string m3 = manager.request_instantiation("Array", {"ItemSlot"});
    assert(m3 == m1);
    assert(manager.instantiation_count() == 2); // Reused Array<ItemSlot>, did not duplicate!

    // Test 3: Complex generic nesting
    std::string m4 = manager.request_instantiation("Result", {"Array<ItemSlot>", "String"});
    assert(m4.find("Result_Array_ItemSlot__String") != std::string::npos);
    assert(manager.instantiation_count() == 3);

    // Emission order is independent of request order and recursive expansion
    // reports a cycle before unbounded monomorphization.
    GenericsManager deterministic;
    deterministic.request_instantiation("Zed", {"Int"});
    deterministic.request_instantiation("Alpha", {"String"});
    assert(deterministic.all_instantiations().front().template_name == "Alpha");
    assert(deterministic.begin_instantiation("Node", {"Int"}));
    assert(!deterministic.begin_instantiation("Node", {"Int"}));
    deterministic.end_instantiation("Node", {"Int"});
    assert(deterministic.begin_instantiation("Node", {"Int"}));

    GenericsManager bounded({.max_instantiations = 2, .max_active_depth = 2});
    assert(bounded.try_request_instantiation("Box", {"Int"}));
    assert(bounded.try_request_instantiation("Box", {"String"}));
    assert(bounded.try_request_instantiation("Box", {"Int"}));
    assert(!bounded.try_request_instantiation("Box", {"Float"}));
    assert(bounded.last_failure() == GenericInstantiationFailure::CountLimit);
    assert(bounded.failed_instantiation() == "Box<Float>");
    bounded.clear();
    assert(bounded.begin_instantiation("Outer", {"Int"}));
    assert(bounded.begin_instantiation("Inner", {"Int"}));
    assert(!bounded.begin_instantiation("TooDeep", {"Int"}));
    assert(bounded.last_failure() == GenericInstantiationFailure::DepthLimit);

    // Concrete roots are expanded after every module has registered its generic
    // declarations. A specialization that grows its own type argument is
    // stopped deterministically instead of exhausting the C++ compiler.
    auto type_parameter = Type::make_custom(TypeKind::TypeParameter, "T");
    auto nested_argument = Type::make_generic(TypeKind::Array, "Array", {type_parameter});
    auto recursive_call = std::make_shared<IRCall>();
    recursive_call->kind = IROpKind::Call;
    recursive_call->callee_name = "grow";
    recursive_call->type_arguments = {nested_argument};
    IRFunction grow;
    grow.name = "grow";
    grow.generic_params.push_back({"T", "Send"});
    grow.blocks.push_back({"entry", {{recursive_call, {}}}});
    IRModule generic_module;
    generic_module.functions.push_back(std::move(grow));

    auto root_call = std::make_shared<IRCall>();
    root_call->kind = IROpKind::Call;
    root_call->callee_name = "grow";
    root_call->type_arguments = {Type::make_primitive(TypeKind::Int32)};
    IRFunction root;
    root.name = "root";
    root.blocks.push_back({"entry", {{root_call, {}}}});
    IRModule root_module;
    root_module.functions.push_back(std::move(root));

    GenericsManager recursive({.max_instantiations = 64, .max_active_depth = 4});
    recursive.analyze_modules({root_module, generic_module});
    assert(recursive.last_failure() == GenericInstantiationFailure::DepthLimit);
    assert(recursive.failed_instantiation().find("grow<Array<Array<Array<Array<Int") == 0);

    std::cout << "[PASS] EngineScriptGenericTests passed!\n";
    return 0;
}
