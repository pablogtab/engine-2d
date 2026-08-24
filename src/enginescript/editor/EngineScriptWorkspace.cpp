#include "EngineScriptWorkspace.hpp"
#include "../lexer/Lexer.hpp"
#include "../parser/Parser.hpp"
#include "../ir/IRVerifier.hpp"
#include "../module/GameModule.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace EngineScript {

using json = nlohmann::ordered_json;

EngineScriptWorkspace::EngineScriptWorkspace(std::string project_root)
    : m_source_manager(project_root), m_cache(project_root + "/Library/EngineScript/Cache") {
}

void EngineScriptWorkspace::set_project_root(std::string project_root) {
    m_source_manager.set_project_root(project_root);
    m_cache = CompilerCache(m_source_manager.project_root() + "/Library/EngineScript/Cache");
    m_dep_graph.clear();
    m_inferred_types.clear();
    m_last_workspace_hash.clear();
    m_last_result.reset();
}

void EngineScriptWorkspace::add_source_file(const std::string& relative_path, const std::string& content) {
    m_source_manager.register_source(relative_path, content, false);
    if (std::find(m_registered_files.begin(), m_registered_files.end(), relative_path) == m_registered_files.end()) {
        m_registered_files.push_back(relative_path);
        m_dep_graph.add_node(relative_path);
    }
}

void EngineScriptWorkspace::update_source_file(const std::string& relative_path, const std::string& content) {
    m_source_manager.update_virtual_file(relative_path, content);
}

void EngineScriptWorkspace::remove_source_file(const std::string& relative_path) {
    m_registered_files.erase(
        std::remove(m_registered_files.begin(), m_registered_files.end(), relative_path),
        m_registered_files.end()
    );
    m_cache.invalidate(relative_path);
}

CompilationResult EngineScriptWorkspace::compile(bool force_rebuild) {
    std::string workspace_fingerprint = m_source_manager.project_root();
    for (const auto& path : m_registered_files) {
        const SourceFile* source = m_source_manager.get_file(path);
        if (!source) continue;
        workspace_fingerprint += '\n' + path + ':' + CompilerCache::hash_content(source->content);
    }
    const std::string workspace_hash = CompilerCache::hash_content(workspace_fingerprint);
    if (!force_rebuild && m_last_result && workspace_hash == m_last_workspace_hash) {
        for (const auto& path : m_registered_files) {
            const SourceFile* source = m_source_manager.get_file(path);
            if (source) {
                m_cache.lookup(path, CompilerCache::hash_content(source->content), ENGINE_SCRIPT_SDK_HASH);
            }
        }
        return *m_last_result;
    }

    m_diagnostics.clear();
    m_ir_modules.clear();
    m_inferred_types.clear();
    m_manifest = {};
    m_dep_graph.clear();
    CompilationResult result;

    const auto finish = [&](CompilationResult value) {
        m_last_workspace_hash = workspace_hash;
        m_last_result = value;
        return value;
    };

    for (const auto& path : m_registered_files) {
        m_dep_graph.add_node(path);
    }

    std::vector<std::shared_ptr<ModuleNode>> ast_modules;

    // 1. Lex and Parse each file
    for (const auto& path : m_registered_files) {
        FileId file_id = m_source_manager.find_file_id(path);
        const SourceFile* sf = m_source_manager.get_file(file_id);
        if (!sf) continue;

        Lexer lexer(file_id, sf->content, m_diagnostics);
        auto tokens = lexer.tokenize_all();

        Parser parser(file_id, sf->path, std::move(tokens), m_diagnostics);
        auto mod_node = parser.parse_module();
        if (mod_node) {
            ast_modules.push_back(mod_node);

            // Record dependency edges
            for (const auto& decl : mod_node->declarations) {
                if (auto imp = dynamic_cast<const ImportDecl*>(decl.get())) {
                    auto target_opt = m_source_manager.resolve_import(path, imp->module_path);
                    if (target_opt) {
                        m_dep_graph.add_edge(path, *target_opt, EdgeKind::Import);
                    }
                }
            }
        }
    }

    if (m_diagnostics.has_errors()) {
        result.success = false;
        result.error_count = m_diagnostics.error_count();
        result.warning_count = m_diagnostics.warning_count();
        result.diagnostics = m_diagnostics.get_diagnostics();
        return finish(std::move(result));
    }

    // 2. Check for dependency cycles
    std::vector<std::string> cycle;
    if (m_dep_graph.has_cycle(&cycle)) {
        std::string cycle_str;
        for (size_t i = 0; i < cycle.size(); ++i) {
            cycle_str += cycle[i];
            if (i + 1 < cycle.size()) cycle_str += " -> ";
        }
        m_diagnostics.report_error("E5001", "Circular dependency detected: " + cycle_str, {});
        result.success = false;
        result.error_count = m_diagnostics.error_count();
        result.diagnostics = m_diagnostics.get_diagnostics();
        return finish(std::move(result));
    }

    // 3. Symbol Resolution
    SymbolResolver resolver(m_source_manager, m_diagnostics);
    resolver.resolve_all(ast_modules);

    if (m_diagnostics.has_errors()) {
        result.success = false;
        result.error_count = m_diagnostics.error_count();
        result.warning_count = m_diagnostics.warning_count();
        result.diagnostics = m_diagnostics.get_diagnostics();
        return finish(std::move(result));
    }

    // 4. Type Checking
    TypeChecker checker(resolver, m_diagnostics);
    checker.check_all(ast_modules);
    for (const InferredTypeInfo& inferred : checker.inferred_types()) {
        m_inferred_types.push_back({inferred.span, inferred.type_name});
    }

    if (m_diagnostics.has_errors()) {
        result.success = false;
        result.error_count = m_diagnostics.error_count();
        result.warning_count = m_diagnostics.warning_count();
        result.diagnostics = m_diagnostics.get_diagnostics();
        return finish(std::move(result));
    }

    // 5. Lower to TypedIR
    IRLowering lowering(checker);
    IRVerifier verifier(m_diagnostics);
    std::vector<IRModule> ir_modules;

    for (const auto& ast_mod : ast_modules) {
        IRModule ir_mod = lowering.lower_module(*ast_mod);
        verifier.verify_module(ir_mod);
        ir_modules.push_back(std::move(ir_mod));
    }

    m_ir_modules = ir_modules;

    if (m_diagnostics.has_errors()) {
        result.success = false;
        result.error_count = m_diagnostics.error_count();
        result.diagnostics = m_diagnostics.get_diagnostics();
        return finish(std::move(result));
    }

    // 6. Generate Metadata
    result.metadata_json = MetadataEmitter::emit_json(ir_modules);
    m_manifest = MetadataEmitter::parse_json(result.metadata_json);

    // 7. Generate C++
    CppEmitter cpp_emitter(m_source_manager);
    for (const auto& ir_mod : ir_modules) {
        const SourceFile* source = m_source_manager.get_file(ir_mod.module_path);
        const std::string content_hash = source ? CompilerCache::hash_content(source->content) : std::string{};
        const CacheEntry* cached = force_rebuild
            ? nullptr
            : m_cache.lookup(ir_mod.module_path, content_hash, ENGINE_SCRIPT_SDK_HASH);

        CppOutput cpp_out;
        if (cached) {
            cpp_out.header_content = cached->generated_header;
            cpp_out.source_content = cached->generated_source;
            cpp_out.header_filename = cached->generated_header_filename;
            cpp_out.source_filename = cached->generated_source_filename;
        } else {
            cpp_out = cpp_emitter.emit_module(ir_mod);
            CacheEntry entry;
            entry.module_path = ir_mod.module_path;
            entry.content_hash = content_hash;
            entry.public_signature_hash = content_hash;
            entry.sdk_hash = ENGINE_SCRIPT_SDK_HASH;
            entry.generated_header = cpp_out.header_content;
            entry.generated_source = cpp_out.source_content;
            entry.generated_header_filename = cpp_out.header_filename;
            entry.generated_source_filename = cpp_out.source_filename;
            entry.metadata_json = result.metadata_json;
            m_cache.store(entry);
        }
        result.generated_cpp.push_back(std::move(cpp_out));
    }

    result.success = true;
    result.error_count = 0;
    result.warning_count = m_diagnostics.warning_count();
    result.diagnostics = m_diagnostics.get_diagnostics();
    return finish(std::move(result));
}

std::optional<std::string> EngineScriptWorkspace::inferred_type_at(FileId file_id, uint32_t byte_offset) const {
    const WorkspaceInferredType* best_match = nullptr;
    for (const WorkspaceInferredType& inferred : m_inferred_types) {
        if (inferred.span.file_id != file_id || byte_offset < inferred.span.start_offset ||
            byte_offset > inferred.span.end_offset) {
            continue;
        }
        if (!best_match || inferred.span.length() < best_match->span.length()) {
            best_match = &inferred;
        }
    }
    if (!best_match) return std::nullopt;
    return best_match->type_name;
}

std::string EngineScriptWorkspace::serialize_behaviour_state(const std::string& behaviour_type, const std::unordered_map<std::string, std::string>& properties) const {
    json j;
    j["type"] = behaviour_type;
    json props_obj = json::object();
    for (const auto& [k, v] : properties) {
        props_obj[k] = v;
    }
    j["properties"] = props_obj;
    return j.dump(2);
}

std::unordered_map<std::string, std::string> EngineScriptWorkspace::deserialize_behaviour_state(const std::string& json_str) const {
    std::unordered_map<std::string, std::string> props;
    try {
        json j = json::parse(json_str);
        if (j.contains("properties") && j["properties"].is_object()) {
            for (auto it = j["properties"].begin(); it != j["properties"].end(); ++it) {
                props[it.key()] = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
            }
        }
    } catch (...) {}
    return props;
}

} // namespace EngineScript
