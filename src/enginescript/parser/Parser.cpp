#include "Parser.hpp"
#include <format>

namespace EngineScript {

Parser::Parser(FileId file_id, std::string module_path, std::vector<Token> tokens, DiagnosticEngine& diagnostics)
    : m_file_id(file_id), m_module_path(std::move(module_path)), m_tokens(std::move(tokens)), m_diagnostics(diagnostics) {
}

const Token& Parser::peek() const {
    if (m_current >= m_tokens.size()) {
        return m_tokens.back(); // EOF token
    }
    return m_tokens[m_current];
}

const Token& Parser::previous() const {
    if (m_current == 0) return m_tokens[0];
    return m_tokens[m_current - 1];
}

bool Parser::is_at_end() const {
    return peek().is_eof();
}

Token Parser::advance() {
    if (!is_at_end()) {
        m_current++;
    }
    return previous();
}

bool Parser::check(TokenType type) const {
    if (is_at_end()) return false;
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

Token Parser::consume(TokenType type, const std::string& error_code, const std::string& message) {
    if (check(type)) {
        return advance();
    }
    m_diagnostics.report_error(error_code, message, peek().span);
    return peek();
}

void Parser::synchronize() {
    advance();
    while (!is_at_end()) {
        if (previous().type == TokenType::Semicolon) return;
        switch (peek().type) {
            case TokenType::Class:
            case TokenType::Struct:
            case TokenType::Interface:
            case TokenType::Enum:
            case TokenType::Function:
            case TokenType::Let:
            case TokenType::Const:
            case TokenType::For:
            case TokenType::If:
            case TokenType::While:
            case TokenType::Return:
            case TokenType::Export:
            case TokenType::Import:
            case TokenType::At:
            case TokenType::CloseBrace:
                return;
            default:
                advance();
                break;
        }
    }
}

std::shared_ptr<ModuleNode> Parser::parse_module() {
    auto module_node = std::make_shared<ModuleNode>();
    module_node->file_id = m_file_id;
    module_node->module_path = m_module_path;

    SourceSpan start_span;
    if (!m_tokens.empty()) start_span = m_tokens.front().span;

    while (!is_at_end()) {
        try {
            auto decl = parse_declaration();
            if (decl) {
                module_node->declarations.push_back(decl);
            }
        } catch (...) {
            synchronize();
        }
    }

    if (!m_tokens.empty()) {
        module_node->span = start_span.merge(m_tokens.back().span);
    }
    return module_node;
}

std::vector<DecoratorNode> Parser::parse_decorators() {
    std::vector<DecoratorNode> decorators;
    while (check(TokenType::At)) {
        decorators.push_back(parse_decorator());
    }
    return decorators;
}

DecoratorNode Parser::parse_decorator() {
    DecoratorNode dec;
    Token at_tok = consume(TokenType::At, "E1001", "Expected '@' for decorator");
    Token name_tok = consume(TokenType::Identifier, "E1002", "Expected identifier after '@'");
    dec.name = name_tok.text;
    dec.span = at_tok.span.merge(name_tok.span);

    if (match(TokenType::OpenParen)) {
        // Parse arguments: could be positional expressions or object literal { min: 0, max: 10 }
        if (!check(TokenType::CloseParen)) {
            if (check(TokenType::OpenBrace)) {
                // Parse object literal entries directly into named_args
                advance(); // Consume {
                while (!check(TokenType::CloseBrace) && !is_at_end()) {
                    Token key_tok = consume(TokenType::Identifier, "E1003", "Expected property name in decorator config");
                    consume(TokenType::Colon, "E1004", "Expected ':' after property name");
                    auto val = parse_expression();
                    ObjectEntry entry;
                    entry.key = key_tok.text;
                    entry.value = val;
                    entry.span = key_tok.span.merge(val ? val->span : key_tok.span);
                    dec.named_args.push_back(entry);

                    if (!match(TokenType::Comma)) break;
                }
                consume(TokenType::CloseBrace, "E1005", "Expected '}' after decorator config");
            } else {
                while (!check(TokenType::CloseParen) && !is_at_end()) {
                    auto arg = parse_expression();
                    if (arg) dec.positional_args.push_back(arg);
                    if (!match(TokenType::Comma)) break;
                }
            }
        }
        Token close_paren = consume(TokenType::CloseParen, "E1006", "Expected ')' after decorator arguments");
        dec.span = dec.span.merge(close_paren.span);
    }
    return dec;
}

std::shared_ptr<Declaration> Parser::parse_declaration() {
    std::vector<DecoratorNode> decorators = parse_decorators();

    bool is_exported = false;
    if (match(TokenType::Export)) {
        is_exported = true;
    }

    if (check(TokenType::Import)) {
        return parse_import();
    }
    if (check(TokenType::Class)) {
        return parse_class(is_exported, std::move(decorators));
    }
    if (check(TokenType::Struct)) {
        return parse_struct(is_exported, std::move(decorators));
    }
    if (check(TokenType::Interface)) {
        return parse_interface(is_exported, std::move(decorators));
    }
    if (check(TokenType::Enum)) {
        return parse_enum(is_exported, std::move(decorators));
    }
    if (check(TokenType::Function) || (check(TokenType::Async) && m_current + 1 < m_tokens.size() && m_tokens[m_current + 1].is(TokenType::Function))) {
        bool is_async = match(TokenType::Async);
        return parse_function(is_exported, is_async, std::move(decorators));
    }
    if (check(TokenType::Const) || check(TokenType::Let)) {
        return parse_const_decl(is_exported);
    }

    // Unrecognized declaration
    Token tok = advance();
    m_diagnostics.report_error("E1010", std::format("Unexpected token '{}' in declaration", tok.text), tok.span);
    return nullptr;
}

std::shared_ptr<ImportDecl> Parser::parse_import() {
    auto imp = std::make_shared<ImportDecl>();
    Token imp_tok = consume(TokenType::Import, "E1020", "Expected 'import'");
    imp->span = imp_tok.span;

    if (match(TokenType::OpenBrace)) {
        const Token open_brace = previous();
        while (!check(TokenType::CloseBrace) && !is_at_end()) {
            Token item_tok = consume(TokenType::Identifier, "E1021", "Expected imported item name");
            ImportDecl::ImportItem item;
            item.name = item_tok.text;
            item.span = item_tok.span;
            if (match(TokenType::As)) {
                Token alias_tok = consume(TokenType::Identifier, "E1022", "Expected alias name after 'as'");
                item.alias = alias_tok.text;
                item.span = item.span.merge(alias_tok.span);
            }
            imp->items.push_back(item);
            if (!match(TokenType::Comma)) break;
        }
        const Token close_brace = consume(TokenType::CloseBrace, "E1023", "Expected '}' in import declaration");
        imp->specifier_span = open_brace.span.merge(close_brace.span);
    } else if (match(TokenType::Star)) {
        imp->is_wildcard = true;
        imp->specifier_span = previous().span;
        consume(TokenType::As, "E1024", "Expected 'as' after '*'");
        Token alias_tok = consume(TokenType::Identifier, "E1025", "Expected alias name");
        imp->wildcard_alias = alias_tok.text;
    } else {
        Token default_tok = consume(TokenType::Identifier, "E1026", "Expected import item name");
        ImportDecl::ImportItem item;
        item.name = default_tok.text;
        item.span = default_tok.span;
        imp->specifier_span = default_tok.span;
        imp->items.push_back(item);
    }

    consume(TokenType::From, "E1027", "Expected 'from' after import specifiers");
    Token path_tok = consume(TokenType::StringLiteral, "E1028", "Expected module path string");
    imp->module_path = path_tok.string_value;
    imp->module_path_span = path_tok.span;
    match(TokenType::Semicolon);

    imp->span = imp->span.merge(path_tok.span);
    return imp;
}

std::vector<std::string> Parser::parse_generic_param_list() {
    std::vector<std::string> params;
    if (match(TokenType::Less)) {
        while (!check(TokenType::Greater) && !is_at_end()) {
            Token p = consume(TokenType::Identifier, "E1030", "Expected generic type parameter name");
            params.push_back(p.text);
            if (!match(TokenType::Comma)) break;
        }
        consume(TokenType::Greater, "E1031", "Expected '>' to close generic parameter list");
    }
    return params;
}

TypeAnnotation Parser::parse_type_annotation() {
    TypeAnnotation type_annot;
    Token name_tok = consume(TokenType::Identifier, "E1040", "Expected type name");
    type_annot.name = name_tok.text;
    type_annot.span = name_tok.span;

    if (match(TokenType::Less)) {
        while (!check(TokenType::Greater) && !is_at_end()) {
            type_annot.generic_args.push_back(parse_type_annotation());
            if (!match(TokenType::Comma)) break;
        }
        Token gt = consume(TokenType::Greater, "E1041", "Expected '>' to close generic type arguments");
        type_annot.span = type_annot.span.merge(gt.span);
    }

    if (match(TokenType::Question)) {
        type_annot.is_nullable = true;
        type_annot.span = type_annot.span.merge(previous().span);
    }

    return type_annot;
}

std::shared_ptr<ClassDecl> Parser::parse_class(bool is_exported, std::vector<DecoratorNode> decorators) {
    auto cls = std::make_shared<ClassDecl>();
    cls->is_exported = is_exported;
    cls->decorators = std::move(decorators);

    Token class_tok = consume(TokenType::Class, "E1050", "Expected 'class'");
    Token name_tok = consume(TokenType::Identifier, "E1051", "Expected class name");
    cls->name = name_tok.text;
    cls->span = class_tok.span.merge(name_tok.span);
    cls->generic_params = parse_generic_param_list();

    if (match(TokenType::Extends)) {
        cls->base_class = parse_type_annotation();
    }

    if (match(TokenType::Implements)) {
        while (!is_at_end()) {
            cls->interfaces.push_back(parse_type_annotation());
            if (!match(TokenType::Comma)) break;
        }
    }

    consume(TokenType::OpenBrace, "E1052", "Expected '{' before class body");

    while (!check(TokenType::CloseBrace) && !is_at_end()) {
        std::vector<DecoratorNode> member_decorators = parse_decorators();

        AccessModifier access = AccessModifier::Public;
        if (match(TokenType::Private)) access = AccessModifier::Private;
        else if (match(TokenType::Protected)) access = AccessModifier::Protected;
        else if (match(TokenType::Public)) access = AccessModifier::Public;

        bool is_override = match(TokenType::Override);
        bool is_async = match(TokenType::Async);

        if (check(TokenType::Constructor)) {
            cls->constructor_decl = parse_constructor();
            cls->constructor_decl->decorators = std::move(member_decorators);
        } else if (check(TokenType::Identifier) && m_current + 1 < m_tokens.size() && (m_tokens[m_current + 1].is(TokenType::OpenParen) || m_tokens[m_current + 1].is(TokenType::Less))) {
            // Method
            auto method = parse_method(access, is_override, is_async, std::move(member_decorators));
            cls->methods.push_back(method);
        } else if (check(TokenType::Identifier)) {
            // Property
            auto prop = parse_property(access, is_override, std::move(member_decorators));
            cls->properties.push_back(prop);
        } else {
            Token unexpected = advance();
            m_diagnostics.report_error("E1053", std::format("Unexpected token '{}' in class body", unexpected.text), unexpected.span);
        }
    }

    Token close_brace = consume(TokenType::CloseBrace, "E1054", "Expected '}' after class body");
    cls->span = cls->span.merge(close_brace.span);
    return cls;
}

std::shared_ptr<ConstructorDecl> Parser::parse_constructor() {
    auto ctor = std::make_shared<ConstructorDecl>();
    Token ctor_tok = consume(TokenType::Constructor, "E1060", "Expected 'constructor'");
    ctor->span = ctor_tok.span;

    consume(TokenType::OpenParen, "E1061", "Expected '(' after constructor");
    ctor->params = parse_param_list(true);
    consume(TokenType::CloseParen, "E1062", "Expected ')' after constructor parameters");

    ctor->body = parse_block();
    if (ctor->body) {
        ctor->span = ctor->span.merge(ctor->body->span);
    }
    return ctor;
}

std::vector<std::shared_ptr<ParamDecl>> Parser::parse_param_list(bool allow_access_and_decorators) {
    std::vector<std::shared_ptr<ParamDecl>> params;
    while (!check(TokenType::CloseParen) && !is_at_end()) {
        auto param = std::make_shared<ParamDecl>();
        if (allow_access_and_decorators) {
            param->decorators = parse_decorators();
            if (match(TokenType::Private)) param->access = AccessModifier::Private;
            else if (match(TokenType::Protected)) param->access = AccessModifier::Protected;
            else if (match(TokenType::Public)) param->access = AccessModifier::Public;
        }

        Token name_tok = consume(TokenType::Identifier, "E1070", "Expected parameter name");
        param->name = name_tok.text;
        param->span = name_tok.span;

        consume(TokenType::Colon, "E1071", "Expected ':' after parameter name");
        param->type_annotation = parse_type_annotation();
        param->span = param->span.merge(param->type_annotation.span);

        if (match(TokenType::Equals)) {
            param->default_value = parse_expression();
            if (param->default_value) {
                param->span = param->span.merge(param->default_value->span);
            }
        }

        params.push_back(param);
        if (!match(TokenType::Comma)) break;
    }
    return params;
}

std::shared_ptr<MethodDecl> Parser::parse_method(AccessModifier access, bool is_override, bool is_async, std::vector<DecoratorNode> decorators) {
    auto method = std::make_shared<MethodDecl>();
    method->access = access;
    method->is_override = is_override;
    method->is_async = is_async;
    method->decorators = std::move(decorators);

    Token name_tok = consume(TokenType::Identifier, "E1080", "Expected method name");
    method->name = name_tok.text;
    method->span = name_tok.span;
    method->generic_params = parse_generic_param_list();

    consume(TokenType::OpenParen, "E1081", "Expected '(' after method name");
    method->params = parse_param_list(false);
    consume(TokenType::CloseParen, "E1082", "Expected ')' after method parameters");

    if (match(TokenType::Colon)) {
        method->return_type = parse_type_annotation();
    }

    method->body = parse_block();
    if (method->body) {
        method->span = method->span.merge(method->body->span);
    }
    return method;
}

std::shared_ptr<PropertyDecl> Parser::parse_property(AccessModifier access, bool is_override, std::vector<DecoratorNode> decorators) {
    auto prop = std::make_shared<PropertyDecl>();
    prop->access = access;
    prop->is_override = is_override;
    prop->decorators = std::move(decorators);

    Token name_tok = consume(TokenType::Identifier, "E1090", "Expected property name");
    prop->name = name_tok.text;
    prop->span = name_tok.span;

    if (match(TokenType::Colon)) {
        prop->type_annotation = parse_type_annotation();
    }

    if (match(TokenType::Equals)) {
        prop->initializer = parse_expression();
    }

    match(TokenType::Semicolon);
    return prop;
}

std::shared_ptr<StructDecl> Parser::parse_struct(bool is_exported, std::vector<DecoratorNode> decorators) {
    auto st = std::make_shared<StructDecl>();
    st->is_exported = is_exported;
    st->decorators = std::move(decorators);

    Token struct_tok = consume(TokenType::Struct, "E1100", "Expected 'struct'");
    Token name_tok = consume(TokenType::Identifier, "E1101", "Expected struct name");
    st->name = name_tok.text;
    st->span = struct_tok.span.merge(name_tok.span);
    st->generic_params = parse_generic_param_list();

    consume(TokenType::OpenBrace, "E1102", "Expected '{' before struct body");

    while (!check(TokenType::CloseBrace) && !is_at_end()) {
        std::vector<DecoratorNode> member_decorators = parse_decorators();
        if (check(TokenType::Identifier) && m_current + 1 < m_tokens.size() && m_tokens[m_current + 1].is(TokenType::OpenParen)) {
            auto method = parse_method(AccessModifier::Public, false, false, std::move(member_decorators));
            st->methods.push_back(method);
        } else {
            auto prop = parse_property(AccessModifier::Public, false, std::move(member_decorators));
            st->properties.push_back(prop);
        }
    }

    Token close_brace = consume(TokenType::CloseBrace, "E1103", "Expected '}' after struct body");
    st->span = st->span.merge(close_brace.span);
    return st;
}

std::shared_ptr<InterfaceDecl> Parser::parse_interface(bool is_exported, std::vector<DecoratorNode> decorators) {
    auto iface = std::make_shared<InterfaceDecl>();
    iface->is_exported = is_exported;
    iface->decorators = std::move(decorators);

    Token iface_tok = consume(TokenType::Interface, "E1110", "Expected 'interface'");
    Token name_tok = consume(TokenType::Identifier, "E1111", "Expected interface name");
    iface->name = name_tok.text;
    iface->span = iface_tok.span.merge(name_tok.span);
    iface->generic_params = parse_generic_param_list();

    if (match(TokenType::Extends)) {
        while (!is_at_end()) {
            iface->extended_interfaces.push_back(parse_type_annotation());
            if (!match(TokenType::Comma)) break;
        }
    }

    consume(TokenType::OpenBrace, "E1112", "Expected '{' before interface body");

    while (!check(TokenType::CloseBrace) && !is_at_end()) {
        InterfaceMember member;
        Token mem_name = consume(TokenType::Identifier, "E1113", "Expected member name");
        member.name = mem_name.text;
        member.span = mem_name.span;

        if (check(TokenType::OpenParen) || check(TokenType::Less)) {
            member.is_method = true;
            member.generic_params = parse_generic_param_list();
            consume(TokenType::OpenParen, "E1114", "Expected '('");
            member.params = parse_param_list(false);
            consume(TokenType::CloseParen, "E1115", "Expected ')'");
            consume(TokenType::Colon, "E1116", "Expected ':' before return type");
            member.return_type = parse_type_annotation();
        } else {
            member.is_method = false;
            consume(TokenType::Colon, "E1117", "Expected ':' before property type");
            member.return_type = parse_type_annotation();
        }
        match(TokenType::Semicolon);
        iface->members.push_back(std::move(member));
    }

    Token close_brace = consume(TokenType::CloseBrace, "E1118", "Expected '}' after interface body");
    iface->span = iface->span.merge(close_brace.span);
    return iface;
}

std::shared_ptr<EnumDecl> Parser::parse_enum(bool is_exported, std::vector<DecoratorNode> decorators) {
    auto en = std::make_shared<EnumDecl>();
    en->is_exported = is_exported;
    en->decorators = std::move(decorators);

    Token enum_tok = consume(TokenType::Enum, "E1120", "Expected 'enum'");
    Token name_tok = consume(TokenType::Identifier, "E1121", "Expected enum name");
    en->name = name_tok.text;
    en->span = enum_tok.span.merge(name_tok.span);

    consume(TokenType::OpenBrace, "E1122", "Expected '{' before enum body");

    while (!check(TokenType::CloseBrace) && !is_at_end()) {
        EnumEntry entry;
        Token item_name = consume(TokenType::Identifier, "E1123", "Expected enum entry name");
        entry.name = item_name.text;
        entry.span = item_name.span;

        if (match(TokenType::Equals)) {
            if (check(TokenType::IntegerLiteral)) {
                Token val = advance();
                entry.int_value = val.int_value;
                entry.span = entry.span.merge(val.span);
            } else if (check(TokenType::StringLiteral)) {
                Token val = advance();
                entry.string_value = val.string_value;
                entry.span = entry.span.merge(val.span);
            } else {
                Token bad = advance();
                m_diagnostics.report_error("E1124", "Expected integer or string literal for enum value", bad.span);
            }
        }
        en->entries.push_back(entry);
        if (!match(TokenType::Comma)) break;
    }

    Token close_brace = consume(TokenType::CloseBrace, "E1125", "Expected '}' after enum body");
    en->span = en->span.merge(close_brace.span);
    return en;
}

std::shared_ptr<FunctionDecl> Parser::parse_function(bool is_exported, bool is_async, std::vector<DecoratorNode> decorators) {
    auto fn = std::make_shared<FunctionDecl>();
    fn->is_exported = is_exported;
    fn->is_async = is_async;
    fn->decorators = std::move(decorators);

    Token fn_tok = consume(TokenType::Function, "E1130", "Expected 'function'");
    Token name_tok = consume(TokenType::Identifier, "E1131", "Expected function name");
    fn->name = name_tok.text;
    fn->span = fn_tok.span.merge(name_tok.span);
    fn->generic_params = parse_generic_param_list();

    consume(TokenType::OpenParen, "E1132", "Expected '(' after function name");
    fn->params = parse_param_list(false);
    consume(TokenType::CloseParen, "E1133", "Expected ')' after function parameters");

    if (match(TokenType::Colon)) {
        fn->return_type = parse_type_annotation();
    }

    fn->body = parse_block();
    if (fn->body) {
        fn->span = fn->span.merge(fn->body->span);
    }
    return fn;
}

std::shared_ptr<ConstDecl> Parser::parse_const_decl(bool is_exported) {
    auto c = std::make_shared<ConstDecl>();
    c->is_exported = is_exported;
    Token const_tok = advance(); // consumes 'const' or 'let'
    Token name_tok = consume(TokenType::Identifier, "E1141", "Expected name in declaration");
    c->name = name_tok.text;
    c->span = const_tok.span.merge(name_tok.span);

    if (match(TokenType::Colon)) {
        c->type_annotation = parse_type_annotation();
    }

    consume(TokenType::Equals, "E1142", "Expected '=' in declaration");
    c->initializer = parse_expression();
    match(TokenType::Semicolon);

    if (c->initializer) {
        c->span = c->span.merge(c->initializer->span);
    }
    return c;
}

// Statement Parsing
std::shared_ptr<Statement> Parser::parse_statement() {
    if (check(TokenType::OpenBrace)) return parse_block();
    if (check(TokenType::Let) || check(TokenType::Const)) return parse_var_decl();
    if (check(TokenType::If)) return parse_if();
    if (check(TokenType::While)) return parse_while();
    if (check(TokenType::For)) return parse_for();
    if (check(TokenType::Return)) return parse_return();
    if (match(TokenType::Break)) {
        auto b = std::make_shared<BreakStmt>();
        b->span = previous().span;
        match(TokenType::Semicolon);
        return b;
    }
    if (match(TokenType::Continue)) {
        auto c = std::make_shared<ContinueStmt>();
        c->span = previous().span;
        match(TokenType::Semicolon);
        return c;
    }
    return parse_expr_stmt();
}

std::shared_ptr<BlockStmt> Parser::parse_block() {
    auto blk = std::make_shared<BlockStmt>();
    Token open_brace = consume(TokenType::OpenBrace, "E1150", "Expected '{'");
    blk->span = open_brace.span;

    while (!check(TokenType::CloseBrace) && !is_at_end()) {
        blk->statements.push_back(parse_statement());
    }

    Token close_brace = consume(TokenType::CloseBrace, "E1151", "Expected '}'");
    blk->span = blk->span.merge(close_brace.span);
    return blk;
}

std::shared_ptr<VarDeclStmt> Parser::parse_var_decl() {
    auto v = std::make_shared<VarDeclStmt>();
    Token kw = advance(); // let or const
    v->is_const = (kw.type == TokenType::Const);
    v->span = kw.span;

    Token name_tok = consume(TokenType::Identifier, "E1160", "Expected variable name");
    v->name = name_tok.text;
    v->span = v->span.merge(name_tok.span);

    if (match(TokenType::Colon)) {
        v->type_annotation = parse_type_annotation();
    }

    if (match(TokenType::Equals)) {
        v->initializer = parse_expression();
        if (v->initializer) v->span = v->span.merge(v->initializer->span);
    }

    match(TokenType::Semicolon);
    return v;
}

std::shared_ptr<IfStmt> Parser::parse_if() {
    auto if_s = std::make_shared<IfStmt>();
    Token if_tok = consume(TokenType::If, "E1170", "Expected 'if'");
    if_s->span = if_tok.span;

    consume(TokenType::OpenParen, "E1171", "Expected '(' after 'if'");
    if_s->condition = parse_expression();
    consume(TokenType::CloseParen, "E1172", "Expected ')' after if condition");

    if_s->then_branch = parse_statement();
    if_s->span = if_s->span.merge(if_s->then_branch ? if_s->then_branch->span : if_tok.span);

    if (match(TokenType::Else)) {
        if_s->else_branch = parse_statement();
        if (if_s->else_branch) {
            if_s->span = if_s->span.merge(if_s->else_branch->span);
        }
    }
    return if_s;
}

std::shared_ptr<WhileStmt> Parser::parse_while() {
    auto w = std::make_shared<WhileStmt>();
    Token w_tok = consume(TokenType::While, "E1180", "Expected 'while'");
    w->span = w_tok.span;

    consume(TokenType::OpenParen, "E1181", "Expected '(' after 'while'");
    w->condition = parse_expression();
    consume(TokenType::CloseParen, "E1182", "Expected ')' after while condition");

    w->body = parse_statement();
    if (w->body) w->span = w->span.merge(w->body->span);
    return w;
}

std::shared_ptr<Statement> Parser::parse_for() {
    Token for_tok = consume(TokenType::For, "E1190", "Expected 'for'");
    consume(TokenType::OpenParen, "E1191", "Expected '(' after 'for'");

    // Check for "for (let x in collection)" vs "for (let i = 0; i < 10; i++)"
    if (check(TokenType::Let) && m_current + 2 < m_tokens.size() && m_tokens[m_current + 2].is(TokenType::In)) {
        advance(); // consume let
        Token var_tok = consume(TokenType::Identifier, "E1192", "Expected variable name in for-in");
        consume(TokenType::In, "E1193", "Expected 'in'");
        auto coll = parse_expression();
        consume(TokenType::CloseParen, "E1194", "Expected ')' after for-in expression");

        auto for_in = std::make_shared<ForInStmt>();
        for_in->var_name = var_tok.text;
        for_in->collection = coll;
        for_in->body = parse_statement();
        for_in->span = for_tok.span.merge(for_in->body ? for_in->body->span : coll->span);
        return for_in;
    }

    auto for_s = std::make_shared<ForStmt>();
    for_s->span = for_tok.span;

    if (!match(TokenType::Semicolon)) {
        if (check(TokenType::Let) || check(TokenType::Const)) {
            for_s->initializer = parse_var_decl();
        } else {
            for_s->initializer = parse_expr_stmt();
        }
    }

    if (!check(TokenType::Semicolon)) {
        for_s->condition = parse_expression();
    }
    consume(TokenType::Semicolon, "E1195", "Expected ';' after for loop condition");

    if (!check(TokenType::CloseParen)) {
        for_s->update = parse_expression();
    }
    consume(TokenType::CloseParen, "E1196", "Expected ')' after for clauses");

    for_s->body = parse_statement();
    if (for_s->body) for_s->span = for_s->span.merge(for_s->body->span);
    return for_s;
}

std::shared_ptr<ReturnStmt> Parser::parse_return() {
    auto ret = std::make_shared<ReturnStmt>();
    Token ret_tok = consume(TokenType::Return, "E1200", "Expected 'return'");
    ret->span = ret_tok.span;

    if (!check(TokenType::Semicolon) && !check(TokenType::CloseBrace) && !is_at_end()) {
        ret->value = parse_expression();
        if (ret->value) ret->span = ret->span.merge(ret->value->span);
    }
    match(TokenType::Semicolon);
    return ret;
}

std::shared_ptr<ExprStmt> Parser::parse_expr_stmt() {
    auto e = std::make_shared<ExprStmt>();
    e->expr = parse_expression();
    if (e->expr) e->span = e->expr->span;
    match(TokenType::Semicolon);
    return e;
}

// Expression Pratt Parser
int Parser::get_token_precedence(TokenType type) const {
    switch (type) {
        case TokenType::Equals:
        case TokenType::PlusEquals:
        case TokenType::MinusEquals:
        case TokenType::StarEquals:
        case TokenType::SlashEquals:
            return 1;
        case TokenType::QuestionQuestion:
            return 2;
        case TokenType::PipePipe:
            return 3;
        case TokenType::AmpAmp:
            return 4;
        case TokenType::EqualsEquals:
        case TokenType::ExclamationEquals:
            return 5;
        case TokenType::Less:
        case TokenType::LessEquals:
        case TokenType::Greater:
        case TokenType::GreaterEquals:
            return 6;
        case TokenType::Plus:
        case TokenType::Minus:
            return 7;
        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::Percent:
            return 8;
        default:
            return 0;
    }
}

std::shared_ptr<Expression> Parser::parse_expression(int min_precedence) {
    auto left = parse_prefix();
    if (!left) return nullptr;

    while (true) {
        int prec = get_token_precedence(peek().type);
        if (prec < min_precedence || prec == 0) break;

        Token op = advance();
        // Assignment is right-associative (prec = 1), arithmetic/relational are left-associative (prec + 1)
        int next_prec = (prec == 1) ? prec : prec + 1;
        auto right = parse_expression(next_prec);

        if (prec == 1) {
            auto assign = std::make_shared<AssignmentExpr>();
            assign->op = op.type;
            assign->target = left;
            assign->value = right;
            assign->span = left->span.merge(right ? right->span : op.span);
            left = assign;
        } else {
            auto bin = std::make_shared<BinaryExpr>();
            bin->op = op.type;
            bin->left = left;
            bin->right = right;
            bin->span = left->span.merge(right ? right->span : op.span);
            left = bin;
        }
    }

    return left;
}

std::shared_ptr<Expression> Parser::parse_prefix() {
    if (match(TokenType::Exclamation) || match(TokenType::Minus) || match(TokenType::Plus)) {
        Token op = previous();
        auto un = std::make_shared<UnaryExpr>();
        un->op = op.type;
        un->is_prefix = true;
        un->operand = parse_expression(9); // High unary precedence
        un->span = op.span.merge(un->operand ? un->operand->span : op.span);
        return un;
    }

    if (match(TokenType::Await)) {
        Token await_tok = previous();
        auto aw = std::make_shared<AwaitExpr>();
        aw->operand = parse_expression(9);
        aw->span = await_tok.span.merge(aw->operand ? aw->operand->span : await_tok.span);
        return aw;
    }

    auto primary = parse_primary();
    return parse_postfix(primary);
}

std::shared_ptr<Expression> Parser::parse_primary() {
    if (match(TokenType::IntegerLiteral)) {
        auto lit = std::make_shared<LiteralExpr>();
        lit->kind = LiteralExpr::Kind::Integer;
        lit->int_val = previous().int_value;
        lit->span = previous().span;
        return lit;
    }
    if (match(TokenType::FloatLiteral)) {
        auto lit = std::make_shared<LiteralExpr>();
        lit->kind = LiteralExpr::Kind::Float;
        lit->float_val = previous().float_value;
        lit->span = previous().span;
        return lit;
    }
    if (match(TokenType::DurationLiteral)) {
        auto lit = std::make_shared<LiteralExpr>();
        lit->kind = LiteralExpr::Kind::Duration;
        lit->duration_unit = previous().duration_unit;
        lit->duration_seconds = previous().duration_seconds;
        lit->float_val = previous().float_value;
        lit->span = previous().span;
        return lit;
    }
    if (match(TokenType::StringLiteral)) {
        auto lit = std::make_shared<LiteralExpr>();
        lit->kind = LiteralExpr::Kind::String;
        lit->string_val = previous().string_value;
        lit->span = previous().span;
        return lit;
    }
    if (match(TokenType::True) || match(TokenType::False)) {
        auto lit = std::make_shared<LiteralExpr>();
        lit->kind = LiteralExpr::Kind::Boolean;
        lit->bool_val = (previous().type == TokenType::True);
        lit->span = previous().span;
        return lit;
    }
    if (match(TokenType::Null)) {
        auto lit = std::make_shared<LiteralExpr>();
        lit->kind = LiteralExpr::Kind::Null;
        lit->span = previous().span;
        return lit;
    }
    if (match(TokenType::This)) {
        auto th = std::make_shared<ThisExpr>();
        th->span = previous().span;
        return th;
    }
    if (match(TokenType::Identifier)) {
        auto id = std::make_shared<IdentifierExpr>();
        id->name = previous().text;
        id->span = previous().span;
        return id;
    }
    if (match(TokenType::OpenBracket)) {
        // Array literal [a, b, c]
        auto arr = std::make_shared<ArrayLiteralExpr>();
        Token open_tok = previous();
        arr->span = open_tok.span;
        while (!check(TokenType::CloseBracket) && !is_at_end()) {
            arr->elements.push_back(parse_expression());
            if (!match(TokenType::Comma)) break;
        }
        Token close_tok = consume(TokenType::CloseBracket, "E1210", "Expected ']'");
        arr->span = arr->span.merge(close_tok.span);
        return arr;
    }
    if (match(TokenType::OpenBrace)) {
        // Object literal { a: 1, b: 2 }
        auto obj = std::make_shared<ObjectLiteralExpr>();
        Token open_tok = previous();
        obj->span = open_tok.span;
        while (!check(TokenType::CloseBrace) && !is_at_end()) {
            Token key = consume(TokenType::Identifier, "E1220", "Expected key name in object literal");
            consume(TokenType::Colon, "E1221", "Expected ':' after object literal key");
            auto val = parse_expression();
            ObjectEntry entry;
            entry.key = key.text;
            entry.value = val;
            entry.span = key.span.merge(val ? val->span : key.span);
            obj->entries.push_back(entry);
            if (!match(TokenType::Comma)) break;
        }
        Token close_tok = consume(TokenType::CloseBrace, "E1222", "Expected '}'");
        obj->span = obj->span.merge(close_tok.span);
        return obj;
    }
    if (match(TokenType::OpenParen)) {
        // Could be grouped expression (a + b) or arrow function () => ...
        Token open_tok = previous();

        // Check if next is ')' followed by '=>'
        if (match(TokenType::CloseParen)) {
            if (match(TokenType::Arrow)) {
                auto arrow = std::make_shared<ArrowFunctionExpr>();
                arrow->span = open_tok.span;
                if (check(TokenType::OpenBrace)) {
                    arrow->body_stmt = parse_block();
                    arrow->span = arrow->span.merge(arrow->body_stmt->span);
                } else {
                    arrow->body_expr = parse_expression();
                    arrow->span = arrow->span.merge(arrow->body_expr ? arrow->body_expr->span : open_tok.span);
                }
                return arrow;
            }
        }

        auto inner = parse_expression();
        Token close_tok = consume(TokenType::CloseParen, "E1230", "Expected ')'");

        // Check for arrow function with params (x: Float) => ...
        if (match(TokenType::Arrow)) {
            auto arrow = std::make_shared<ArrowFunctionExpr>();
            arrow->span = open_tok.span;
            if (check(TokenType::OpenBrace)) {
                arrow->body_stmt = parse_block();
                arrow->span = arrow->span.merge(arrow->body_stmt->span);
            } else {
                arrow->body_expr = parse_expression();
                arrow->span = arrow->span.merge(arrow->body_expr ? arrow->body_expr->span : open_tok.span);
            }
            return arrow;
        }

        if (inner) inner->span = open_tok.span.merge(close_tok.span);
        return inner;
    }

    Token bad = advance();
    m_diagnostics.report_error("E1240", std::format("Unexpected token '{}' in expression", bad.text), bad.span);
    return nullptr;
}

bool Parser::looks_like_generic_call() const {
    if (m_current >= m_tokens.size() || !m_tokens[m_current].is(TokenType::Less)) return false;

    std::size_t cursor = m_current + 1;
    const auto parse_type = [this](auto&& self, std::size_t& index) -> bool {
        if (index >= m_tokens.size() || !m_tokens[index].is(TokenType::Identifier)) return false;
        ++index;
        if (index < m_tokens.size() && m_tokens[index].is(TokenType::Less)) {
            ++index;
            if (!self(self, index)) return false;
            while (index < m_tokens.size() && m_tokens[index].is(TokenType::Comma)) {
                ++index;
                if (!self(self, index)) return false;
            }
            if (index >= m_tokens.size() || !m_tokens[index].is(TokenType::Greater)) return false;
            ++index;
        }
        if (index < m_tokens.size() && m_tokens[index].is(TokenType::Question)) ++index;
        return true;
    };

    if (!parse_type(parse_type, cursor)) return false;
    while (cursor < m_tokens.size() && m_tokens[cursor].is(TokenType::Comma)) {
        ++cursor;
        if (!parse_type(parse_type, cursor)) return false;
    }
    return cursor + 1 < m_tokens.size() &&
        m_tokens[cursor].is(TokenType::Greater) &&
        m_tokens[cursor + 1].is(TokenType::OpenParen);
}

std::shared_ptr<Expression> Parser::parse_postfix(std::shared_ptr<Expression> expr) {
    if (!expr) return nullptr;

    while (true) {
        if (match(TokenType::Dot) || match(TokenType::QuestionDot)) {
            bool is_optional = (previous().type == TokenType::QuestionDot);
            Token member_tok = consume(TokenType::Identifier, "E1250", "Expected member name");
            auto mem = std::make_shared<MemberAccessExpr>();
            mem->object = expr;
            mem->member_name = member_tok.text;
            mem->is_optional = is_optional;
            mem->span = expr->span.merge(member_tok.span);
            expr = mem;
        } else if (match(TokenType::OpenBracket)) {
            auto idx_expr = parse_expression();
            Token close_tok = consume(TokenType::CloseBracket, "E1251", "Expected ']'");
            auto idx = std::make_shared<IndexExpr>();
            idx->object = expr;
            idx->index = idx_expr;
            idx->span = expr->span.merge(close_tok.span);
            expr = idx;
        } else if (match(TokenType::OpenParen)) {
            auto call = std::make_shared<CallExpr>();
            call->callee = expr;
            call->span = expr->span;

            while (!check(TokenType::CloseParen) && !is_at_end()) {
                call->arguments.push_back(parse_expression());
                if (!match(TokenType::Comma)) break;
            }
            Token close_tok = consume(TokenType::CloseParen, "E1252", "Expected ')'");
            call->span = call->span.merge(close_tok.span);
            expr = call;
        } else if (check(TokenType::Less) && dynamic_cast<MemberAccessExpr*>(expr.get()) != nullptr &&
                   looks_like_generic_call()) {
            // Generic call arguments are only unambiguous in postfix position
            // when immediately followed by a call, e.g. assets.load<Texture>(path).
            advance();
            std::vector<TypeAnnotation> type_arguments;
            while (!check(TokenType::Greater) && !is_at_end()) {
                type_arguments.push_back(parse_type_annotation());
                if (!match(TokenType::Comma)) break;
            }
            consume(TokenType::Greater, "E1041", "Expected '>' to close generic call arguments");
            advance();
            auto call = std::make_shared<CallExpr>();
            call->callee = expr;
            call->type_arguments = std::move(type_arguments);
            call->span = expr->span;
            while (!check(TokenType::CloseParen) && !is_at_end()) {
                call->arguments.push_back(parse_expression());
                if (!match(TokenType::Comma)) break;
            }
            Token close_tok = consume(TokenType::CloseParen, "E1252", "Expected ')' after call arguments");
            call->span = call->span.merge(close_tok.span);
            expr = call;
        } else {
            break;
        }
    }

    return expr;
}

} // namespace EngineScript
