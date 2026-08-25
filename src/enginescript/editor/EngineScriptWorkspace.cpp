#include "EngineScriptWorkspace.hpp"
#include "../lexer/Lexer.hpp"
#include "../parser/Parser.hpp"
#include "../ir/IRVerifier.hpp"
#include "../module/GameModule.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_set>

namespace EngineScript {

using json = nlohmann::ordered_json;

namespace {

std::optional<SourceSpan> exact_symbol_span(
    const SourceManager& source_manager,
    const SourceSpan& containing_span,
    std::string_view name
) {
    const SourceFile* file = source_manager.get_file(containing_span.file_id);
    if (!file || name.empty() || containing_span.start_offset >= file->content.size()) return std::nullopt;
    const std::size_t end = std::min<std::size_t>(containing_span.end_offset, file->content.size());
    std::size_t position = containing_span.start_offset;
    while ((position = file->content.find(name, position)) != std::string::npos && position < end) {
        const std::size_t name_end = position + name.size();
        const auto is_identifier = [](char value) {
            return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
        };
        const bool starts_word = position == 0 || !is_identifier(file->content[position - 1]);
        const bool ends_word = name_end >= file->content.size() || !is_identifier(file->content[name_end]);
        if (starts_word && ends_word && name_end <= end) {
            return source_manager.get_span(
                containing_span.file_id,
                static_cast<uint32_t>(position),
                static_cast<uint32_t>(name_end)
            );
        }
        position = name_end;
    }
    return std::nullopt;
}

} // namespace

EngineScriptWorkspace::EngineScriptWorkspace(std::string project_root)
    : m_source_manager(project_root), m_cache(project_root + "/Library/EngineScript/Cache") {
}

void EngineScriptWorkspace::set_project_root(std::string project_root) {
    m_source_manager.set_project_root(project_root);
    m_cache = CompilerCache(m_source_manager.project_root() + "/Library/EngineScript/Cache");
    m_dep_graph.clear();
    m_inferred_types.clear();
    m_document_symbols.clear();
    m_exported_symbols.clear();
    m_symbol_occurrences.clear();
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
    const FileId file_id = m_source_manager.find_file_id(relative_path);
    std::erase_if(m_document_symbols, [file_id](const WorkspaceDocumentSymbol& symbol) {
        return symbol.file_id == file_id;
    });
    std::erase_if(m_exported_symbols, [file_id](const WorkspaceExportedSymbol& symbol) {
        return symbol.file_id == file_id;
    });
    std::erase_if(m_symbol_occurrences, [file_id](const WorkspaceSymbolOccurrence& occurrence) {
        return occurrence.span.file_id == file_id;
    });
}

size_t EngineScriptWorkspace::discover_project_sources() {
    namespace fs = std::filesystem;
    size_t discovered = 0;
    const fs::path root(m_source_manager.project_root());
    if (root.empty() || !fs::exists(root)) return 0;

    const auto excluded = [](const fs::path& relative) {
        for (const fs::path& part : relative) {
            const std::string name = part.generic_string();
            if (name == "Library" || name == ".e2s_build" || name == "build" || name == "Build") return true;
        }
        return false;
    };
    try {
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".e2s") continue;
            const fs::path relative = fs::relative(entry.path(), root);
            if (excluded(relative)) continue;
            std::ifstream input(entry.path(), std::ios::binary);
            if (!input) continue;
            add_source_file(relative.generic_string(), {std::istreambuf_iterator<char>(input), {}});
            ++discovered;
        }
    } catch (const fs::filesystem_error&) {
        return discovered;
    }
    return discovered;
}

bool EngineScriptWorkspace::restore_source_file_from_disk(const std::string& relative_path) {
    namespace fs = std::filesystem;
    const fs::path absolute = fs::path(m_source_manager.project_root()) / relative_path;
    std::ifstream input(absolute, std::ios::binary);
    if (!input) return false;
    add_source_file(relative_path, {std::istreambuf_iterator<char>(input), {}});
    return true;
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
    m_exported_symbols.clear();
    m_symbol_occurrences.clear();
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

            const auto index_export = [this, file_id, &path](
                                          const std::shared_ptr<Declaration>& declaration,
                                          const std::string& name,
                                          WorkspaceExportedSymbolKind kind) {
                if (!declaration || !declaration->is_exported || name.empty()) return;
                m_exported_symbols.push_back({file_id, path, name, declaration->span, kind});
            };
            for (const auto& declaration : mod_node->declarations) {
                if (const auto value = std::dynamic_pointer_cast<ClassDecl>(declaration)) {
                    index_export(declaration, value->name, WorkspaceExportedSymbolKind::Class);
                } else if (const auto value = std::dynamic_pointer_cast<StructDecl>(declaration)) {
                    index_export(declaration, value->name, WorkspaceExportedSymbolKind::Struct);
                } else if (const auto value = std::dynamic_pointer_cast<InterfaceDecl>(declaration)) {
                    index_export(declaration, value->name, WorkspaceExportedSymbolKind::Interface);
                } else if (const auto value = std::dynamic_pointer_cast<EnumDecl>(declaration)) {
                    index_export(declaration, value->name, WorkspaceExportedSymbolKind::Enum);
                } else if (const auto value = std::dynamic_pointer_cast<FunctionDecl>(declaration)) {
                    index_export(declaration, value->name, WorkspaceExportedSymbolKind::Function);
                } else if (const auto value = std::dynamic_pointer_cast<ConstDecl>(declaration)) {
                    index_export(declaration, value->name, WorkspaceExportedSymbolKind::Constant);
                }
            }

            std::erase_if(m_document_symbols, [file_id](const WorkspaceDocumentSymbol& symbol) {
                return symbol.file_id == file_id;
            });
            const auto add_symbol = [this, file_id](const std::string& name, const TypeAnnotation& type,
                                                     const SourceSpan& declaration, const SourceSpan& scope,
                                                     WorkspaceDocumentSymbolKind kind) {
                m_document_symbols.push_back({file_id, name, type.to_string(), declaration, scope, kind});
            };
            std::function<void(const std::shared_ptr<Statement>&, const SourceSpan&)> index_statement;
            index_statement = [&](const std::shared_ptr<Statement>& statement, const SourceSpan& scope) {
                if (!statement) return;
                if (const auto variable = std::dynamic_pointer_cast<VarDeclStmt>(statement)) {
                    if (variable->type_annotation) {
                        add_symbol(variable->name, *variable->type_annotation, variable->span, scope,
                                   WorkspaceDocumentSymbolKind::Variable);
                    }
                    return;
                }
                if (const auto block = std::dynamic_pointer_cast<BlockStmt>(statement)) {
                    for (const auto& child : block->statements) index_statement(child, block->span);
                    return;
                }
                if (const auto conditional = std::dynamic_pointer_cast<IfStmt>(statement)) {
                    index_statement(conditional->then_branch, scope);
                    index_statement(conditional->else_branch, scope);
                    return;
                }
                if (const auto loop = std::dynamic_pointer_cast<WhileStmt>(statement)) {
                    index_statement(loop->body, scope);
                    return;
                }
                if (const auto loop = std::dynamic_pointer_cast<ForStmt>(statement)) {
                    index_statement(loop->initializer, scope);
                    index_statement(loop->body, scope);
                }
            };
            for (const auto& declaration : mod_node->declarations) {
                if (const auto import_decl = std::dynamic_pointer_cast<ImportDecl>(declaration)) {
                    for (const ImportDecl::ImportItem& item : import_decl->items) {
                        const std::string& local_name = item.alias ? *item.alias : item.name;
                        m_document_symbols.push_back({
                            file_id, local_name, item.name, item.span, mod_node->span,
                            WorkspaceDocumentSymbolKind::Import
                        });
                    }
                    if (import_decl->is_wildcard && !import_decl->wildcard_alias.empty()) {
                        m_document_symbols.push_back({
                            file_id, import_decl->wildcard_alias, import_decl->module_path,
                            import_decl->span, mod_node->span, WorkspaceDocumentSymbolKind::Import
                        });
                    }
                    continue;
                }
                const auto class_decl = std::dynamic_pointer_cast<ClassDecl>(declaration);
                if (!class_decl) continue;
                for (const auto& property : class_decl->properties) {
                    if (property && property->type_annotation) {
                        add_symbol(property->name, *property->type_annotation, property->span, class_decl->span,
                                   WorkspaceDocumentSymbolKind::Field);
                    }
                }
                if (class_decl->constructor_decl) {
                    for (const auto& parameter : class_decl->constructor_decl->params) {
                        if (!parameter) continue;
                        const SourceSpan& scope = parameter->access == AccessModifier::None
                            ? class_decl->constructor_decl->span : class_decl->span;
                        add_symbol(parameter->name, parameter->type_annotation, parameter->span, scope,
                                   parameter->access == AccessModifier::None
                                       ? WorkspaceDocumentSymbolKind::Parameter
                                       : WorkspaceDocumentSymbolKind::Field);
                    }
                    index_statement(class_decl->constructor_decl->body, class_decl->constructor_decl->span);
                }
                for (const auto& method : class_decl->methods) {
                    if (!method) continue;
                    for (const auto& parameter : method->params) {
                        if (parameter) {
                            add_symbol(parameter->name, parameter->type_annotation, parameter->span, method->span,
                                       WorkspaceDocumentSymbolKind::Parameter);
                        }
                    }
                    index_statement(method->body, method->span);
                }
            }

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

    const bool has_parse_errors = m_diagnostics.has_errors();
    std::unordered_set<FileId> files_with_parse_errors;
    if (has_parse_errors) {
        for (const Diagnostic& diagnostic : m_diagnostics.get_diagnostics()) {
            if (diagnostic.is_error() && diagnostic.span.is_valid()) {
                files_with_parse_errors.insert(diagnostic.span.file_id);
            }
        }
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
    // Parsing errors in one open document must not disable semantic completion
    // everywhere else. Resolve and type-check the recovered AST with an
    // isolated diagnostic sink so speculative cascades are not published.
    DiagnosticEngine speculative_diagnostics;
    DiagnosticEngine& semantic_diagnostics = has_parse_errors ? speculative_diagnostics : m_diagnostics;
    SymbolResolver resolver(m_source_manager, semantic_diagnostics);
    resolver.resolve_all(ast_modules);

    // Retain the resolver's symbol identity for LSP definition, references,
    // and rename. Textual matching is unsafe because unrelated scopes may use
    // the same spelling.
    for (const auto& [node, symbol] : resolver.resolved_symbols()) {
        if (!node || !symbol || symbol->id == 0) continue;
        const bool is_reference = dynamic_cast<const IdentifierExpr*>(node) != nullptr;
        const SourceSpan containing_span = node->span.is_valid() ? node->span : symbol->span;
        const auto occurrence_span = is_reference
            ? std::optional<SourceSpan>{containing_span}
            : exact_symbol_span(m_source_manager, containing_span, symbol->name);
        if (!occurrence_span || !occurrence_span->is_valid()) continue;
        m_symbol_occurrences.push_back({symbol->id, symbol->name, *occurrence_span, !is_reference, symbol->kind});
    }
    for (const auto& module : ast_modules) {
        if (!module) continue;
        const auto module_scope = resolver.get_module_scope(module->module_path);
        if (!module_scope) continue;
        for (const auto& declaration : module->declarations) {
            const auto import_decl = std::dynamic_pointer_cast<ImportDecl>(declaration);
            if (!import_decl) continue;
            for (const ImportDecl::ImportItem& item : import_decl->items) {
                const std::string local_name = item.alias ? *item.alias : item.name;
                const auto imported_symbol = module_scope->lookup_local(local_name);
                const auto occurrence_span = exact_symbol_span(m_source_manager, item.span, local_name);
                if (imported_symbol && occurrence_span) {
                    m_symbol_occurrences.push_back({
                        imported_symbol->id, local_name, *occurrence_span, true, SymbolKind::Import
                    });
                }
            }
        }
    }

    if (!has_parse_errors && semantic_diagnostics.has_errors()) {
        result.success = false;
        result.error_count = semantic_diagnostics.error_count();
        result.warning_count = semantic_diagnostics.warning_count();
        result.diagnostics = semantic_diagnostics.get_diagnostics();
        return finish(std::move(result));
    }

    // 4. Type Checking
    TypeChecker checker(resolver, semantic_diagnostics);
    checker.check_all(ast_modules);
    for (const InferredTypeInfo& inferred : checker.inferred_types()) {
        m_inferred_types.push_back({inferred.span, inferred.type_name});
    }

    // A syntax error in one open document must not hide semantic diagnostics
    // from the rest of the workspace. Keep diagnostics attached to files that
    // parsed successfully, while dropping unanchored errors and cascades from
    // the files whose recovered AST may be incomplete.
    if (has_parse_errors) {
        for (const Diagnostic& diagnostic : speculative_diagnostics.get_diagnostics()) {
            if (diagnostic.span.is_valid()
                && !files_with_parse_errors.contains(diagnostic.span.file_id)) {
                m_diagnostics.report(diagnostic);
            }
        }
    }

    // Complete the syntactic symbol index with types inferred by the checker.
    // Explicitly typed symbols are indexed before semantic analysis so that
    // completion still works while the user is editing invalid code; inferred
    // symbols are added here whenever the current document can be checked.
    for (const auto& module : ast_modules) {
        if (!module) continue;
        const FileId file_id = module->file_id;
        const auto add_inferred_symbol = [this, &checker, file_id](
                                             const std::string& name,
                                             const ASTNode* node,
                                             const SourceSpan& declaration,
                                             const SourceSpan& scope,
                                             WorkspaceDocumentSymbolKind kind) {
            const std::shared_ptr<Type> type = checker.get_type_of_node(node);
            if (!type || type->is_error()) return;
            m_document_symbols.push_back({file_id, name, type->to_string(), declaration, scope, kind, true});
        };
        std::function<void(const std::shared_ptr<Statement>&, const SourceSpan&)> index_inferred_statement;
        index_inferred_statement = [&](const std::shared_ptr<Statement>& statement, const SourceSpan& scope) {
            if (!statement) return;
            if (const auto variable = std::dynamic_pointer_cast<VarDeclStmt>(statement)) {
                if (!variable->type_annotation) {
                    add_inferred_symbol(variable->name,
                                        variable->initializer
                                            ? static_cast<const ASTNode*>(variable->initializer.get())
                                            : static_cast<const ASTNode*>(variable.get()),
                                        variable->span, scope,
                                        WorkspaceDocumentSymbolKind::Variable);
                }
                return;
            }
            if (const auto block = std::dynamic_pointer_cast<BlockStmt>(statement)) {
                for (const auto& child : block->statements) index_inferred_statement(child, block->span);
                return;
            }
            if (const auto conditional = std::dynamic_pointer_cast<IfStmt>(statement)) {
                index_inferred_statement(conditional->then_branch, scope);
                index_inferred_statement(conditional->else_branch, scope);
                return;
            }
            if (const auto loop = std::dynamic_pointer_cast<WhileStmt>(statement)) {
                index_inferred_statement(loop->body, scope);
                return;
            }
            if (const auto loop = std::dynamic_pointer_cast<ForStmt>(statement)) {
                index_inferred_statement(loop->initializer, scope);
                index_inferred_statement(loop->body, scope);
            }
        };
        for (const auto& declaration : module->declarations) {
            const auto class_decl = std::dynamic_pointer_cast<ClassDecl>(declaration);
            if (!class_decl) continue;
            for (const auto& property : class_decl->properties) {
                if (property && !property->type_annotation) {
                    add_inferred_symbol(property->name,
                                        property->initializer
                                            ? static_cast<const ASTNode*>(property->initializer.get())
                                            : static_cast<const ASTNode*>(property.get()),
                                        property->span, class_decl->span,
                                        WorkspaceDocumentSymbolKind::Field);
                }
            }
            if (class_decl->constructor_decl) {
                index_inferred_statement(class_decl->constructor_decl->body, class_decl->constructor_decl->span);
            }
            for (const auto& method : class_decl->methods) {
                if (method) index_inferred_statement(method->body, method->span);
            }
        }
    }

    if (has_parse_errors) {
        result.success = false;
        result.error_count = m_diagnostics.error_count();
        result.warning_count = m_diagnostics.warning_count();
        result.diagnostics = m_diagnostics.get_diagnostics();
        return finish(std::move(result));
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

std::optional<std::string> EngineScriptWorkspace::symbol_type_at(
    FileId file_id,
    std::string_view name,
    uint32_t byte_offset
) const {
    const WorkspaceDocumentSymbol* best_match = nullptr;
    for (const WorkspaceDocumentSymbol& symbol : m_document_symbols) {
        if (symbol.file_id != file_id || symbol.name != name ||
            byte_offset < symbol.scope_span.start_offset || byte_offset > symbol.scope_span.end_offset ||
            byte_offset < symbol.declaration_span.start_offset) {
            continue;
        }
        if (!best_match || symbol.scope_span.length() < best_match->scope_span.length()) best_match = &symbol;
    }
    if (!best_match) return std::nullopt;
    return best_match->type_name;
}

std::vector<WorkspaceDocumentSymbol> EngineScriptWorkspace::symbols_in_scope(
    FileId file_id,
    uint32_t byte_offset
) const {
    std::unordered_map<std::string, WorkspaceDocumentSymbol> visible;
    for (const WorkspaceDocumentSymbol& symbol : m_document_symbols) {
        if (symbol.file_id != file_id || byte_offset < symbol.scope_span.start_offset ||
            byte_offset > symbol.scope_span.end_offset || byte_offset < symbol.declaration_span.start_offset) {
            continue;
        }
        const auto existing = visible.find(symbol.name);
        if (existing == visible.end() || symbol.scope_span.length() < existing->second.scope_span.length() ||
            (symbol.scope_span.length() == existing->second.scope_span.length() &&
             symbol.declaration_span.start_offset > existing->second.declaration_span.start_offset)) {
            visible[symbol.name] = symbol;
        }
    }
    std::vector<WorkspaceDocumentSymbol> result;
    result.reserve(visible.size());
    for (auto& [name, symbol] : visible) result.push_back(std::move(symbol));
    std::sort(result.begin(), result.end(), [](const WorkspaceDocumentSymbol& lhs, const WorkspaceDocumentSymbol& rhs) {
        return lhs.name < rhs.name;
    });
    return result;
}

std::vector<WorkspaceDocumentSymbol> EngineScriptWorkspace::document_symbols(FileId file_id) const {
    std::vector<WorkspaceDocumentSymbol> result;
    for (const WorkspaceDocumentSymbol& symbol : m_document_symbols) {
        if (symbol.file_id == file_id) result.push_back(symbol);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.declaration_span.start_offset < rhs.declaration_span.start_offset;
    });
    return result;
}

std::vector<WorkspaceExportedSymbol> EngineScriptWorkspace::exports_from(std::string_view module_path) const {
    const std::string normalized = m_source_manager.normalize_path(std::string(module_path));
    std::vector<WorkspaceExportedSymbol> exports;
    for (const WorkspaceExportedSymbol& symbol : m_exported_symbols) {
        if (m_source_manager.normalize_path(symbol.module_path) == normalized) exports.push_back(symbol);
    }
    return exports;
}

std::optional<uint64_t> EngineScriptWorkspace::symbol_id_at(FileId file_id, uint32_t byte_offset) const {
    const auto occurrence = symbol_occurrence_at(file_id, byte_offset);
    return occurrence ? std::optional<uint64_t>{occurrence->symbol_id} : std::nullopt;
}

std::optional<WorkspaceSymbolOccurrence> EngineScriptWorkspace::symbol_occurrence_at(
    FileId file_id,
    uint32_t byte_offset
) const {
    const WorkspaceSymbolOccurrence* best_match = nullptr;
    for (const WorkspaceSymbolOccurrence& occurrence : m_symbol_occurrences) {
        if (occurrence.span.file_id != file_id || byte_offset < occurrence.span.start_offset ||
            byte_offset > occurrence.span.end_offset) {
            continue;
        }
        if (!best_match || occurrence.span.length() < best_match->span.length()) best_match = &occurrence;
    }
    return best_match ? std::optional<WorkspaceSymbolOccurrence>{*best_match} : std::nullopt;
}

std::vector<WorkspaceSymbolOccurrence> EngineScriptWorkspace::occurrences_for_symbol(uint64_t symbol_id) const {
    std::vector<WorkspaceSymbolOccurrence> occurrences;
    for (const WorkspaceSymbolOccurrence& occurrence : m_symbol_occurrences) {
        if (occurrence.symbol_id == symbol_id) occurrences.push_back(occurrence);
    }
    std::sort(occurrences.begin(), occurrences.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.span.file_id != rhs.span.file_id) return lhs.span.file_id < rhs.span.file_id;
        return lhs.span.start_offset < rhs.span.start_offset;
    });
    return occurrences;
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
