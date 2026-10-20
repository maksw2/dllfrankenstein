#include "parser.hpp"
#include <algorithm>
#include <string_view>

namespace {
class Parser {
public:
    explicit Parser(std::shared_ptr<Module> module) : module_(std::move(module)), tokens_(module_->tokens) {}

    void parse() {
        while (!at(TokenKind::Eof)) module_->statements.push_back(parse_statement(true));
    }

private:
    std::shared_ptr<Module> module_;
    const std::vector<Token>& tokens_;
    size_t pos_ = 0;

    const Token& peek(size_t n = 0) const {
        const size_t i = std::min(pos_ + n, tokens_.size() - 1);
        return tokens_[i];
    }
    bool at(TokenKind k, size_t n = 0) const { return peek(n).kind == k; }
    bool keyword(std::string_view s, size_t n = 0) const { return at(TokenKind::Identifier, n) && peek(n).text == s; }
    const Token& advance() { return tokens_[pos_++]; }
    bool match(TokenKind k) { if (at(k)) { advance(); return true; } return false; }
    bool match_keyword(std::string_view s) { if (keyword(s)) { advance(); return true; } return false; }

    [[noreturn]] void fail(std::string message, SourceSpan where = {}) const {
        if (where.length == 0) where = peek().span;
        if (at(TokenKind::Eof)) throw IncompleteInput{};
        throw DiagnosticError(std::move(message), where);
    }

    const Token& consume(TokenKind k, std::string_view message) {
        if (at(k)) return advance();
        if (at(TokenKind::Eof)) throw IncompleteInput{};
        fail(std::string(message) + ", got " + token_kind_name(peek().kind));
    }

    const Token& consume_identifier(std::string_view message) { return consume(TokenKind::Identifier, message); }

    bool is_aggregate_definition_start() const {
        if (!(keyword("struct") || keyword("union"))) return false;
        return at(TokenKind::Identifier, 1) && (at(TokenKind::LBrace, 2) || keyword("pack", 2));
    }

    bool looks_like_function_definition() const {
        if (!at(TokenKind::Identifier) || !at(TokenKind::Identifier, 1) || !at(TokenKind::LParen, 2)) return false;
        size_t i = pos_ + 2;
        int depth = 0;
        for (; i < tokens_.size(); ++i) {
            if (tokens_[i].kind == TokenKind::LParen) ++depth;
            else if (tokens_[i].kind == TokenKind::RParen) {
                if (--depth == 0) return i + 1 < tokens_.size() && tokens_[i + 1].kind == TokenKind::LBrace;
            } else if (tokens_[i].kind == TokenKind::Eof) return false;
        }
        return false;
    }

    uint32_t parse_pack_suffix() {
        if (!match_keyword("pack")) return 8;
        consume(TokenKind::LParen, "expected '(' after pack");
        const Token& n = consume(TokenKind::Integer, "expected integer pack value");
        if (n.integer != 1 && n.integer != 2 && n.integer != 4 && n.integer != 8 && n.integer != 16)
            fail("pack must be one of 1, 2, 4, 8, or 16", n.span);
        consume(TokenKind::RParen, "expected ')' after pack value");
        return static_cast<uint32_t>(n.integer);
    }

    std::unique_ptr<TypeSyntax> wrap_array_type(std::unique_ptr<TypeSyntax> element, const Token& count, const Token& close) {
        if (count.integer == 0 || count.integer > UINT32_MAX)
            fail("array size must be between 1 and 2^32-1", count.span);
        auto array = std::make_unique<TypeSyntax>();
        array->kind = TypeSyntax::Kind::Array;
        array->element_type = std::move(element);
        array->element_count = static_cast<uint32_t>(count.integer);
        array->span = merge_spans(array->element_type->span, close.span);
        return array;
    }

    std::unique_ptr<TypeSyntax> wrap_array_type(std::unique_ptr<TypeSyntax> element, std::unique_ptr<Expr> count,
                                                const Token& close) {
        auto array = std::make_unique<TypeSyntax>();
        array->kind = TypeSyntax::Kind::Array;
        array->element_type = std::move(element);
        array->element_count_expr = std::move(count);
        array->span = merge_spans(array->element_type->span, close.span);
        return array;
    }

    std::unique_ptr<TypeSyntax> parse_array_suffix(std::unique_ptr<TypeSyntax> element) {
        if (at(TokenKind::Integer) && at(TokenKind::RBracket, 1)) {
            const Token& count = advance();
            const Token& close = consume(TokenKind::RBracket, "expected ']' after array size");
            return wrap_array_type(std::move(element), count, close);
        }
        if (at(TokenKind::RBracket)) fail("expected array element count expression");
        auto count = parse_expression();
        const Token& close = consume(TokenKind::RBracket, "expected ']' after array size expression");
        return wrap_array_type(std::move(element), std::move(count), close);
    }

    std::unique_ptr<TypeSyntax> parse_type_syntax(bool allow_anonymous = true) {
        std::unique_ptr<TypeSyntax> type;
        const SourceSpan start = peek().span;
        if (allow_anonymous && (keyword("struct") || keyword("union"))) {
            const bool is_union = keyword("union");
            advance();
            type = std::make_unique<TypeSyntax>();
            type->kind = is_union ? TypeSyntax::Kind::AnonymousUnion : TypeSyntax::Kind::AnonymousStruct;
            if (keyword("pack")) { type->explicit_pack = true; type->pack = parse_pack_suffix(); }
            consume(TokenKind::LBrace, "expected '{' in anonymous aggregate type");
            type->fields = parse_field_list();
            const Token& close = consume(TokenKind::RBrace, "expected '}' after anonymous aggregate type");
            type->span = merge_spans(start, close.span);
        } else {
            const Token& name = consume_identifier("expected type name");
            type = std::make_unique<TypeSyntax>();
            type->kind = TypeSyntax::Kind::Named;
            type->name = name.text;
            type->span = name.span;
        }

        while (match(TokenKind::LBracket)) type = parse_array_suffix(std::move(type));
        return type;
    }

    std::vector<FieldSyntax> parse_field_list() {
        std::vector<FieldSyntax> fields;
        while (!at(TokenKind::RBrace)) {
            if (at(TokenKind::Eof)) throw IncompleteInput{};
            FieldSyntax field;
            const SourceSpan start = peek().span;
            field.type = parse_type_syntax(true);
            const Token& name = consume_identifier("expected field name");
            field.name = name.text;
            if (match(TokenKind::LBracket)) {
                field.type = parse_array_suffix(std::move(field.type));
                field.span = merge_spans(start, field.type->span);
            } else {
                field.span = merge_spans(start, name.span);
            }
            fields.push_back(std::move(field));
            if (match(TokenKind::Comma)) {
                if (at(TokenKind::RBrace)) break;
                continue;
            }
            if (!at(TokenKind::RBrace)) fail("expected ',' between aggregate fields");
        }
        return fields;
    }

    std::unique_ptr<Stmt> parse_aggregate_definition() {
        auto stmt = std::make_unique<Stmt>();
        const Token& kw = advance();
        stmt->kind = kw.text == "union" ? StmtKind::UnionDefinition : StmtKind::StructDefinition;
        const Token& name = consume_identifier("expected aggregate type name");
        stmt->name = name.text;
        if (keyword("pack")) { stmt->explicit_pack = true; stmt->pack = parse_pack_suffix(); }
        consume(TokenKind::LBrace, "expected '{' after aggregate name");
        stmt->fields = parse_field_list();
        const Token& close = consume(TokenKind::RBrace, "expected '}' after aggregate definition");
        stmt->span = merge_spans(kw.span, close.span);
        return stmt;
    }

    std::unique_ptr<Stmt> parse_function_definition() {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::FunctionDefinition;
        const SourceSpan start = peek().span;
        stmt->return_type = parse_type_syntax(false);
        const Token& name = consume_identifier("expected function name");
        stmt->name = name.text;
        consume(TokenKind::LParen, "expected '(' after function name");
        if (!at(TokenKind::RParen)) {
            while (true) {
                ParameterSyntax p;
                const SourceSpan ps = peek().span;
                p.type = parse_type_syntax(false);
                const Token& var = consume(TokenKind::Variable, "expected parameter variable");
                p.name = var.text;
                p.span = merge_spans(ps, var.span);
                stmt->parameters.push_back(std::move(p));
                if (!match(TokenKind::Comma)) break;
            }
        }
        consume(TokenKind::RParen, "expected ')' after parameters");
        auto body = parse_block();
        stmt->body = std::move(body->body);
        stmt->span = merge_spans(start, body->span);
        return stmt;
    }

    std::unique_ptr<Stmt> parse_block() {
        const Token& open = consume(TokenKind::LBrace, "expected '{'");
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::Block;
        while (!at(TokenKind::RBrace)) {
            if (at(TokenKind::Eof)) throw IncompleteInput{};
            stmt->body.push_back(parse_statement(false));
        }
        const Token& close = advance();
        stmt->span = merge_spans(open.span, close.span);
        return stmt;
    }

    std::unique_ptr<Stmt> parse_switch_statement() {
        const SourceSpan start = tokens_[pos_ - 1].span;
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::Switch;

        consume(TokenKind::LParen, "expected '(' after switch");
        stmt->expr = parse_expression();
        consume(TokenKind::RParen, "expected ')' after switch expression");
        consume(TokenKind::LBrace, "expected '{' after switch expression");

        bool saw_default = false;
        while (!at(TokenKind::RBrace)) {
            if (at(TokenKind::Eof)) throw IncompleteInput{};

            SwitchArm arm;
            if (match_keyword("case")) {
                if (saw_default) fail("case cannot appear after default in switch", tokens_[pos_ - 1].span);
                const SourceSpan arm_start = tokens_[pos_ - 1].span;
                arm.values.push_back(parse_expression());
                while (match(TokenKind::Comma)) {
                    if (at(TokenKind::LBrace)) fail("expected case expression after ','");
                    arm.values.push_back(parse_expression());
                }
                auto block = parse_block();
                arm.body = std::move(block->body);
                arm.span = merge_spans(arm_start, block->span);
            } else if (match_keyword("default")) {
                if (saw_default) fail("duplicate default in switch", tokens_[pos_ - 1].span);
                saw_default = true;
                const SourceSpan arm_start = tokens_[pos_ - 1].span;
                arm.is_default = true;
                auto block = parse_block();
                arm.body = std::move(block->body);
                arm.span = merge_spans(arm_start, block->span);
            } else {
                fail("expected 'case' or 'default' in switch");
            }
            stmt->switch_arms.push_back(std::move(arm));
        }

        const Token& close = advance();
        stmt->span = merge_spans(start, close.span);
        return stmt;
    }

    std::unique_ptr<Stmt> parse_statement(bool top_level) {
        if (is_aggregate_definition_start()) return parse_aggregate_definition();
        if (top_level && looks_like_function_definition()) return parse_function_definition();
        if (at(TokenKind::LBrace)) return parse_block();

        if (match_keyword("if")) {
            const SourceSpan start = tokens_[pos_ - 1].span;
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = StmtKind::If;
            consume(TokenKind::LParen, "expected '(' after if");
            stmt->expr = parse_expression();
            consume(TokenKind::RParen, "expected ')' after if condition");
            auto block = parse_block();
            stmt->body = std::move(block->body);
            SourceSpan end = block->span;
            if (match_keyword("else")) {
                if (keyword("if")) stmt->else_branch = parse_statement(false);
                else stmt->else_branch = parse_block();
                end = stmt->else_branch->span;
            }
            stmt->span = merge_spans(start, end);
            return stmt;
        }

        if (match_keyword("switch")) return parse_switch_statement();

        if (match_keyword("while")) {
            const SourceSpan start = tokens_[pos_ - 1].span;
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = StmtKind::While;
            consume(TokenKind::LParen, "expected '(' after while");
            stmt->expr = parse_expression();
            consume(TokenKind::RParen, "expected ')' after while condition");
            auto block = parse_block();
            stmt->body = std::move(block->body);
            stmt->span = merge_spans(start, block->span);
            return stmt;
        }

        if (match_keyword("break")) {
            auto stmt = std::make_unique<Stmt>(); stmt->kind = StmtKind::Break; stmt->span = tokens_[pos_ - 1].span; return stmt;
        }
        if (match_keyword("continue")) {
            auto stmt = std::make_unique<Stmt>(); stmt->kind = StmtKind::Continue; stmt->span = tokens_[pos_ - 1].span; return stmt;
        }
        if (match_keyword("return")) {
            auto stmt = std::make_unique<Stmt>(); stmt->kind = StmtKind::Return; stmt->span = tokens_[pos_ - 1].span;
            if (!at(TokenKind::RBrace)) stmt->expr = parse_expression();
            if (stmt->expr) stmt->span = merge_spans(stmt->span, stmt->expr->span);
            return stmt;
        }
        if (match_keyword("assert")) {
            const SourceSpan start = tokens_[pos_ - 1].span;
            auto stmt = std::make_unique<Stmt>(); stmt->kind = StmtKind::Assert;
            consume(TokenKind::LParen, "expected '(' after assert");
            stmt->expr = parse_expression();
            if (match(TokenKind::Comma)) {
                const Token& message = consume(TokenKind::String, "assert message must be a string literal");
                stmt->name = message.text;
            }
            const Token& close = consume(TokenKind::RParen, "expected ')' after assert");
            stmt->span = merge_spans(start, close.span);
            return stmt;
        }

        if (keyword("case")) fail("'case' is only allowed directly inside switch");
        if (keyword("default")) fail("'default' is only allowed directly inside switch");

        // Assignment gets priority at statement position. This is what lets places be
        // lvalues without making arbitrary expression statements legal.
        if (at(TokenKind::Variable) || looks_like_typed_memory_access_start()) {
            const size_t save = pos_;
            std::unique_ptr<Expr> target;
            try {
                target = parse_place_expression();
            } catch (const DiagnosticError&) {
                // Reparse as a call/expression below, where the useful error is produced.
                pos_ = save;
                target.reset();
            }
            if (target && match(TokenKind::Equal)) {
                auto stmt = std::make_unique<Stmt>();
                stmt->kind = StmtKind::Assignment;
                stmt->target = std::move(target);
                // Once '=' has been consumed this is unambiguously an assignment.
                // Preserve RHS diagnostics instead of rewinding and reporting the
                // unrelated "only calls" expression-statement error.
                stmt->value = parse_expression();
                stmt->span = merge_spans(stmt->target->span, stmt->value->span);
                return stmt;
            }
            pos_ = save;
        }

        auto expr = parse_expression();
        if (expr->kind != ExprKind::Call) fail("only calls may be used as expression statements", expr->span);
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::Call;
        stmt->span = expr->span;
        stmt->expr = std::move(expr);
        return stmt;
    }

    bool looks_like_typed_memory_access_start() const {
        if (!at(TokenKind::Identifier)) return false;
        size_t i = pos_ + 1;
        while (i < tokens_.size() && tokens_[i].kind == TokenKind::LBracket) {
            int depth = 0;
            do {
                if (tokens_[i].kind == TokenKind::LBracket) ++depth;
                else if (tokens_[i].kind == TokenKind::RBracket) --depth;
                else if (tokens_[i].kind == TokenKind::Eof) return false;
                ++i;
            } while (i < tokens_.size() && depth > 0);
            if (depth != 0) return false;
        }
        return i < tokens_.size() && tokens_[i].kind == TokenKind::At;
    }

    std::unique_ptr<Expr> parse_place_expression() {
        std::unique_ptr<Expr> expr;
        if (looks_like_typed_memory_access_start()) {
            const SourceSpan start = peek().span;
            auto type = parse_type_syntax(false);
            consume(TokenKind::At, "expected '@' after memory access type");
            auto d = std::make_unique<Expr>();
            d->kind = ExprKind::Deref;
            d->type = std::move(type);
            d->lhs = parse_unary();
            d->span = merge_spans(start, d->lhs->span);
            expr = std::move(d);
        } else {
            const Token& v = consume(TokenKind::Variable, "expected assignable variable or typed memory access");
            expr = std::make_unique<Expr>();
            expr->kind = ExprKind::Variable;
            expr->text = v.text;
            expr->span = v.span;
        }
        while (true) {
            if (match(TokenKind::Dot)) {
                const Token& member = consume_identifier("expected member name after '.'");
                auto m = std::make_unique<Expr>();
                m->kind = ExprKind::Member; m->lhs = std::move(expr); m->text = member.text;
                m->span = merge_spans(m->lhs->span, member.span); expr = std::move(m);
            } else if (match(TokenKind::LBracket)) {
                auto idx = parse_expression();
                const Token& close = consume(TokenKind::RBracket, "expected ']' after index");
                auto n = std::make_unique<Expr>();
                n->kind = ExprKind::Index; n->lhs = std::move(expr); n->rhs = std::move(idx);
                n->span = merge_spans(n->lhs->span, close.span); expr = std::move(n);
            } else break;
        }
        return expr;
    }

    int precedence(TokenKind k) const {
        switch (k) {
            case TokenKind::PipePipe: return 1;
            case TokenKind::AmpAmp: return 2;
            case TokenKind::Pipe: return 3;
            case TokenKind::Caret: return 4;
            case TokenKind::Amp: return 5;
            case TokenKind::EqualEqual: case TokenKind::BangEqual: return 6;
            case TokenKind::Less: case TokenKind::LessEqual: case TokenKind::Greater: case TokenKind::GreaterEqual: return 7;
            case TokenKind::ShiftLeft: case TokenKind::ShiftRight: return 8;
            case TokenKind::Plus: case TokenKind::Minus: return 9;
            case TokenKind::Star: case TokenKind::Slash: case TokenKind::Percent: return 10;
            default: return 0;
        }
    }

    std::unique_ptr<Expr> parse_expression(int min_prec = 1) {
        auto left = parse_unary();
        while (true) {
            const int p = precedence(peek().kind);
            if (p < min_prec) break;
            const Token op = advance();
            auto right = parse_expression(p + 1);
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::Binary; node->op = op.kind;
            node->span = merge_spans(left->span, right->span);
            node->lhs = std::move(left); node->rhs = std::move(right);
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<Expr> parse_unary() {
        if (at(TokenKind::Minus) || at(TokenKind::Bang) || at(TokenKind::Tilde)) {
            const Token op = advance();
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::Unary; node->op = op.kind; node->lhs = parse_unary();
            node->span = merge_spans(op.span, node->lhs->span);
            return node;
        }
        if (match(TokenKind::Amp)) {
            const SourceSpan start = tokens_[pos_ - 1].span;
            if (at(TokenKind::Identifier) && !at(TokenKind::LParen, 1)) {
                const Token& fn = advance();
                auto node = std::make_unique<Expr>();
                node->kind = ExprKind::FunctionAddress; node->text = fn.text; node->span = merge_spans(start, fn.span);
                return node;
            }
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::Unary; node->op = TokenKind::Amp; node->lhs = parse_place_expression();
            node->span = merge_spans(start, node->lhs->span);
            return node;
        }
        return parse_postfix();
    }

    std::unique_ptr<Expr> parse_postfix() {
        auto expr = parse_primary();
        while (true) {
            if (expr->kind == ExprKind::Variable && match(TokenKind::LParen)) {
                const SourceSpan start = expr->span;
                const std::string target = expr->text;
                auto call = std::make_unique<Expr>();
                call->kind = ExprKind::Call;
                call->call_target = CallTargetKind::Variable;
                call->target_name = target;
                parse_call_arguments(*call);
                const Token& close = consume(TokenKind::RParen, "expected ')' after call arguments");
                call->explicit_signature = !call->args.empty() && call->args.front().explicitly_typed;
                call->span = merge_spans(start, close.span);
                expr = std::move(call);
            } else if (match(TokenKind::Dot)) {
                const Token& member = consume_identifier("expected member name after '.'");
                auto m = std::make_unique<Expr>();
                m->kind = ExprKind::Member; m->lhs = std::move(expr); m->text = member.text;
                m->span = merge_spans(m->lhs->span, member.span); expr = std::move(m);
            } else if (match(TokenKind::LBracket)) {
                auto index = parse_expression();
                const Token& close = consume(TokenKind::RBracket, "expected ']' after index");
                auto n = std::make_unique<Expr>();
                n->kind = ExprKind::Index; n->lhs = std::move(expr); n->rhs = std::move(index);
                n->span = merge_spans(n->lhs->span, close.span); expr = std::move(n);
            } else break;
        }
        return expr;
    }

    bool is_builtin_type_name(std::string_view name) const {
        static constexpr std::string_view names[] = {
            "i8","i16","i32","i64","u8","u16","u32","u64","f32","f64","str","wstr","ptr","void",
            "int8_t","int16_t","int32_t","int64_t","uint8_t","uint16_t","uint32_t","uint64_t","float","double"
        };
        return std::find(std::begin(names), std::end(names), name) != std::end(names);
    }

    bool starts_expression(const Token& t) const {
        switch (t.kind) {
            case TokenKind::Variable: case TokenKind::Integer: case TokenKind::Float: case TokenKind::String:
            case TokenKind::Identifier: case TokenKind::LParen: case TokenKind::Minus: case TokenKind::Bang:
            case TokenKind::Tilde: case TokenKind::Amp: return true;
            default: return false;
        }
    }

    bool looks_like_typed_argument() const {
        if (!at(TokenKind::Identifier)) return false;
        if (at(TokenKind::LParen, 1) || at(TokenKind::LBrace, 1) || at(TokenKind::Dot, 1)) return false;
        return starts_expression(peek(1));
    }

    void parse_call_arguments(Expr& call) {
        if (at(TokenKind::RParen)) return;
        const bool typed = looks_like_typed_argument();
        while (true) {
            CallArg arg;
            arg.explicitly_typed = typed;
            const SourceSpan start = peek().span;
            if (typed) arg.type = parse_type_syntax(false);
            else if (looks_like_typed_argument()) fail("cannot mix typed and untyped call arguments");
            arg.value = parse_expression();
            arg.span = merge_spans(start, arg.value->span);
            call.args.push_back(std::move(arg));
            if (!match(TokenKind::Comma)) break;
            if (at(TokenKind::RParen)) fail("trailing comma is not allowed in call arguments");
            if (typed != looks_like_typed_argument()) fail("cannot mix typed and untyped call arguments");
        }
    }

    std::unique_ptr<Expr> parse_explicit_call(std::unique_ptr<TypeSyntax> return_type, CallTargetKind target_kind,
                                              std::string module, std::string target, SourceSpan start) {
        auto call = std::make_unique<Expr>();
        call->kind = ExprKind::Call; call->explicit_signature = true;
        call->return_type = std::move(return_type); call->call_target = target_kind;
        call->module_name = std::move(module); call->target_name = std::move(target);
        consume(TokenKind::LParen, "expected '(' in call");
        parse_call_arguments(*call);
        const Token& close = consume(TokenKind::RParen, "expected ')' after call arguments");
        call->span = merge_spans(start, close.span);
        return call;
    }

    std::unique_ptr<Expr> parse_named_or_external() {
        const SourceSpan start = peek().span;

        // module.dll return_type target(...)
        if (at(TokenKind::Identifier) && at(TokenKind::Dot, 1) && at(TokenKind::Identifier, 2) && at(TokenKind::Identifier, 3)) {
            const size_t save = pos_;
            std::string module = advance().text;
            advance();
            module += "." + advance().text;
            auto ret = parse_type_syntax(false);
            if ((at(TokenKind::Identifier) || at(TokenKind::Variable)) && at(TokenKind::LParen, 1)) {
                const Token target = advance();
                return parse_explicit_call(std::move(ret), target.kind == TokenKind::Variable ? CallTargetKind::Variable : CallTargetKind::External,
                                           std::move(module), target.text, start);
            }
            pos_ = save;
        }

        // return_type target(...)
        if (at(TokenKind::Identifier) && (at(TokenKind::Identifier, 1) || at(TokenKind::Variable, 1)) && at(TokenKind::LParen, 2)) {
            auto ret = parse_type_syntax(false);
            const Token target = advance();
            return parse_explicit_call(std::move(ret), target.kind == TokenKind::Variable ? CallTargetKind::Variable : CallTargetKind::Named,
                                       {}, target.text, start);
        }

        const size_t type_save = pos_;
        auto type = parse_type_syntax(false);
        const bool builtin_type = type->kind == TypeSyntax::Kind::Named && is_builtin_type_name(type->name);

        // A type followed by '@' is a typed memory access. The address expression's
        // raw scalar bits are used as the address; the type controls only the access.
        if (match(TokenKind::At)) {
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::Deref;
            node->type = std::move(type);
            node->lhs = parse_unary();
            node->span = merge_spans(start, node->lhs->span);
            return node;
        }

        if (at(TokenKind::LBrace)) {
            if (type->kind == TypeSyntax::Kind::Array) return parse_array_initializer(std::move(type), start);
            return parse_aggregate_initializer(std::move(type), start);
        }

        // An array type by itself is an automatic array allocation. Without an
        // initializer the storage is intentionally left uninitialized.
        if (type->kind == TypeSyntax::Kind::Array) {
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::Array;
            node->span = type->span;
            node->type = std::move(type);
            return node;
        }

        // Built-in type names are reserved. This keeps the typed-value form
        // `i32 ($x + 1)` distinct from ordinary function calls without using whitespace.
        if (builtin_type && at(TokenKind::LParen)) {
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::TypedValue;
            node->type = std::move(type);
            node->lhs = parse_primary();
            node->span = merge_spans(start, node->lhs->span);
            return node;
        }

        if (type->kind == TypeSyntax::Kind::Named && at(TokenKind::LParen)) {
            auto call = std::make_unique<Expr>();
            call->kind = ExprKind::Call; call->call_target = CallTargetKind::Named; call->target_name = type->name;
            consume(TokenKind::LParen, "expected '(' in call");
            parse_call_arguments(*call);
            const Token& close = consume(TokenKind::RParen, "expected ')' after call arguments");
            call->explicit_signature = !call->args.empty() && call->args.front().explicitly_typed;
            call->span = merge_spans(start, close.span);
            return call;
        }

        if (starts_expression(peek())) {
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::TypedValue;
            node->type = std::move(type);
            node->lhs = parse_unary();
            node->span = merge_spans(start, node->lhs->span);
            return node;
        }

        // If it wasn't a type-led construct, it may be an ordinary named call.
        pos_ = type_save;
        const Token& name = consume_identifier("expected identifier");
        if (match(TokenKind::LParen)) {
            auto call = std::make_unique<Expr>();
            call->kind = ExprKind::Call; call->call_target = CallTargetKind::Named; call->target_name = name.text;
            parse_call_arguments(*call);
            const Token& close = consume(TokenKind::RParen, "expected ')' after call arguments");
            call->explicit_signature = !call->args.empty() && call->args.front().explicitly_typed;
            call->span = merge_spans(name.span, close.span);
            return call;
        }

        fail("bare identifiers are not values; expected a call, type application, or initializer", name.span);
    }

    std::unique_ptr<Expr> parse_array_initializer(std::unique_ptr<TypeSyntax> type, SourceSpan start) {
        auto node = std::make_unique<Expr>();
        node->kind = ExprKind::Array; node->type = std::move(type);
        node->array_has_initializer = true;
        consume(TokenKind::LBrace, "expected '{' in array initializer");
        if (!at(TokenKind::RBrace)) {
            while (true) {
                node->elements.push_back(parse_expression());
                if (!match(TokenKind::Comma)) break;
                if (at(TokenKind::RBrace)) break;
            }
        }
        const Token& close = consume(TokenKind::RBrace, "expected '}' after array initializer");
        node->span = merge_spans(start, close.span);
        return node;
    }

    std::unique_ptr<Expr> parse_aggregate_initializer(std::unique_ptr<TypeSyntax> type, SourceSpan start) {
        auto node = std::make_unique<Expr>();
        node->kind = ExprKind::Aggregate; node->type = std::move(type);
        consume(TokenKind::LBrace, "expected '{' in aggregate initializer");
        if (!at(TokenKind::RBrace)) {
            while (true) {
                InitField field;
                const Token& dot = consume(TokenKind::Dot, "expected '.field = value' in aggregate initializer");
                const Token& name = consume_identifier("expected field name after '.'");
                consume(TokenKind::Equal, "expected '=' after initializer field");
                field.name = name.text;
                field.value = parse_expression();
                field.span = merge_spans(dot.span, field.value->span);
                node->initializers.push_back(std::move(field));
                if (!match(TokenKind::Comma)) break;
                if (at(TokenKind::RBrace)) break;
            }
        }
        const Token& close = consume(TokenKind::RBrace, "expected '}' after aggregate initializer");
        node->span = merge_spans(start, close.span);
        return node;
    }

    std::unique_ptr<Expr> parse_primary() {
        const Token t = peek();
        if (match(TokenKind::Integer)) {
            auto e = std::make_unique<Expr>(); e->kind = ExprKind::Integer; e->integer = t.integer; e->span = t.span; return e;
        }
        if (match(TokenKind::Float)) {
            auto e = std::make_unique<Expr>(); e->kind = ExprKind::Float; e->floating = t.floating; e->text = t.text; e->span = t.span; return e;
        }
        if (match(TokenKind::String)) {
            auto e = std::make_unique<Expr>(); e->kind = ExprKind::String; e->text = t.text; e->span = t.span;
            while (at(TokenKind::String)) { e->text += advance().text; e->span = merge_spans(e->span, tokens_[pos_ - 1].span); }
            return e;
        }
        if (match(TokenKind::Variable)) {
            auto e = std::make_unique<Expr>(); e->kind = ExprKind::Variable; e->text = t.text; e->span = t.span; return e;
        }
        if (match(TokenKind::LParen)) {
            auto e = parse_expression();
            const Token& close = consume(TokenKind::RParen, "expected ')' after expression");
            e->span = merge_spans(t.span, close.span);
            return e;
        }

        if (keyword("sizeof") || keyword("alignof")) {
            const bool is_size = keyword("sizeof");
            const Token& kw = advance();
            consume(TokenKind::LParen, "expected '('");
            auto e = std::make_unique<Expr>();
            e->kind = is_size ? ExprKind::SizeOf : ExprKind::AlignOf;
            if (at(TokenKind::Variable)) e->lhs = parse_expression();
            else e->type = parse_type_syntax(true);
            const Token& close = consume(TokenKind::RParen, "expected ')'");
            e->span = merge_spans(kw.span, close.span);
            return e;
        }

        if (keyword("offsetof")) {
            const Token& kw = advance();
            consume(TokenKind::LParen, "expected '(' after offsetof");
            auto e = std::make_unique<Expr>(); e->kind = ExprKind::OffsetOf;
            e->type = parse_type_syntax(true);
            consume(TokenKind::Comma, "expected ',' after offsetof type");
            e->member_path.push_back(consume_identifier("expected member name").text);
            while (match(TokenKind::Dot)) e->member_path.push_back(consume_identifier("expected member name").text);
            const Token& close = consume(TokenKind::RParen, "expected ')' after offsetof");
            e->span = merge_spans(kw.span, close.span);
            return e;
        }

        if (keyword("struct") || keyword("union")) {
            auto type = parse_type_syntax(true);
            if (type->kind == TypeSyntax::Kind::Array) {
                if (at(TokenKind::LBrace)) return parse_array_initializer(std::move(type), t.span);
                auto node = std::make_unique<Expr>();
                node->kind = ExprKind::Array;
                node->span = type->span;
                node->type = std::move(type);
                return node;
            }
            if (!at(TokenKind::LBrace)) fail("anonymous aggregate type in an expression must be immediately initialized", type->span);
            return parse_aggregate_initializer(std::move(type), t.span);
        }

        if (at(TokenKind::Identifier)) return parse_named_or_external();

        if (at(TokenKind::Eof)) throw IncompleteInput{};
        fail("expected expression");
    }
};
}

std::shared_ptr<Module> parse_module(std::string source, std::string source_name) {
    auto module = std::make_shared<Module>();
    module->source_name = std::move(source_name);
    module->source = std::move(source);
    module->tokens = tokenize(module->source);
    Parser parser(module);
    parser.parse();
    return module;
}
