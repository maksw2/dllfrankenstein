#include "lexer.hpp"
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace {
struct Scanner {
    std::string_view source;
    size_t pos = 0;
    uint32_t line = 1;
    uint32_t column = 1;
    std::vector<Token> out;

    char peek(size_t n = 0) const {
        const size_t i = pos + n;
        return i < source.size() ? source[i] : '\0';
    }

    char advance() {
        if (pos >= source.size()) return '\0';
        const char c = source[pos++];
        if (c == '\n') { ++line; column = 1; }
        else { ++column; }
        return c;
    }

    SourceSpan span_from(size_t start, uint32_t start_line, uint32_t start_column) const {
        return {static_cast<uint32_t>(start), static_cast<uint32_t>(pos - start), start_line, start_column};
    }

    [[noreturn]] void error(std::string message, size_t start, uint32_t start_line, uint32_t start_column) const {
        throw DiagnosticError(std::move(message), {static_cast<uint32_t>(start), 1, start_line, start_column});
    }

    void push(TokenKind kind, size_t start, uint32_t sl, uint32_t sc, std::string text = {}) {
        if (text.empty() && pos > start) text.assign(source.substr(start, pos - start));
        out.push_back(Token{kind, std::move(text), span_from(start, sl, sc)});
    }

    void scan_number() {
        const size_t start = pos;
        const uint32_t sl = line, sc = column;
        bool is_float = false;
        bool is_hex = false;

        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            is_hex = true;
            advance(); advance();
            if (!std::isxdigit(static_cast<unsigned char>(peek()))) error("expected hexadecimal digits", start, sl, sc);
            while (std::isxdigit(static_cast<unsigned char>(peek()))) advance();
        } else {
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            if (peek() == '.') {
                is_float = true;
                advance();
                while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            }
            if (peek() == 'e' || peek() == 'E') {
                is_float = true;
                advance();
                if (peek() == '+' || peek() == '-') advance();
                if (!std::isdigit(static_cast<unsigned char>(peek()))) error("expected exponent digits", start, sl, sc);
                while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            }
            if (peek() == 'f' || peek() == 'F') { is_float = true; advance(); }
        }

        const std::string text(source.substr(start, pos - start));
        Token token;
        token.kind = is_float ? TokenKind::Float : TokenKind::Integer;
        token.text = text;
        token.span = span_from(start, sl, sc);
        if (is_float) {
            std::string parse = text;
            if (!parse.empty() && (parse.back() == 'f' || parse.back() == 'F')) parse.pop_back();
            token.floating = std::strtod(parse.c_str(), nullptr);
        } else {
            const char* begin = text.data() + (is_hex ? 2 : 0);
            const char* end = text.data() + text.size();
            auto [ptr, ec] = std::from_chars(begin, end, token.integer, is_hex ? 16 : 10);
            if (ec != std::errc{} || ptr != end) error("invalid integer literal", start, sl, sc);
        }
        out.push_back(std::move(token));
    }

    void scan_string() {
        const size_t start = pos;
        const uint32_t sl = line, sc = column;
        advance();
        std::string value;
        while (peek() != '\0' && peek() != '"') {
            char c = advance();
            if (c != '\\') { value.push_back(c); continue; }
            if (peek() == '\0') throw IncompleteInput{};
            char e = advance();
            switch (e) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case '0': value.push_back('\0'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                case 'x': {
                    auto hex = [](char h) -> int {
                        if (h >= '0' && h <= '9') return h - '0';
                        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                        return -1;
                    };
                    int hi = hex(peek()), lo = hex(peek(1));
                    if (hi < 0 || lo < 0) error("\\x escape requires exactly two hex digits", start, sl, sc);
                    advance(); advance();
                    value.push_back(static_cast<char>((hi << 4) | lo));
                    break;
                }
                default: value.push_back(e); break;
            }
        }
        if (peek() != '"') throw IncompleteInput{};
        advance();
        push(TokenKind::String, start, sl, sc, std::move(value));
    }

    void scan_identifier() {
        const size_t start = pos;
        const uint32_t sl = line, sc = column;
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
        push(TokenKind::Identifier, start, sl, sc);
    }

    void scan_variable() {
        const size_t start = pos;
        const uint32_t sl = line, sc = column;
        advance();
        if (!(std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_')) error("expected variable name after '$'", start, sl, sc);
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
        push(TokenKind::Variable, start, sl, sc);
    }

    void run() {
        while (pos < source.size()) {
            const char c = peek();
            if (std::isspace(static_cast<unsigned char>(c))) { advance(); continue; }
            if (c == ';' || (c == '/' && peek(1) == '/')) {
                while (peek() != '\0' && peek() != '\n') advance();
                continue;
            }
            if (c == '$') { scan_variable(); continue; }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') { scan_identifier(); continue; }
            if (std::isdigit(static_cast<unsigned char>(c))) { scan_number(); continue; }
            if (c == '"') { scan_string(); continue; }

            const size_t start = pos;
            const uint32_t sl = line, sc = column;
            auto two = [&](char second, TokenKind yes, TokenKind no) {
                advance();
                if (peek() == second) { advance(); push(yes, start, sl, sc); }
                else push(no, start, sl, sc);
            };

            switch (c) {
                case '(': advance(); push(TokenKind::LParen, start, sl, sc); break;
                case ')': advance(); push(TokenKind::RParen, start, sl, sc); break;
                case '{': advance(); push(TokenKind::LBrace, start, sl, sc); break;
                case '}': advance(); push(TokenKind::RBrace, start, sl, sc); break;
                case '[': advance(); push(TokenKind::LBracket, start, sl, sc); break;
                case ']': advance(); push(TokenKind::RBracket, start, sl, sc); break;
                case ',': advance(); push(TokenKind::Comma, start, sl, sc); break;
                case '.': advance(); push(TokenKind::Dot, start, sl, sc); break;
                case '@': advance(); push(TokenKind::At, start, sl, sc); break;
                case '+': advance(); push(TokenKind::Plus, start, sl, sc); break;
                case '-': advance(); push(TokenKind::Minus, start, sl, sc); break;
                case '*': advance(); push(TokenKind::Star, start, sl, sc); break;
                case '/': advance(); push(TokenKind::Slash, start, sl, sc); break;
                case '%': advance(); push(TokenKind::Percent, start, sl, sc); break;
                case '^': advance(); push(TokenKind::Caret, start, sl, sc); break;
                case '~': advance(); push(TokenKind::Tilde, start, sl, sc); break;
                case '=': two('=', TokenKind::EqualEqual, TokenKind::Equal); break;
                case '!': two('=', TokenKind::BangEqual, TokenKind::Bang); break;
                case '&': two('&', TokenKind::AmpAmp, TokenKind::Amp); break;
                case '|': two('|', TokenKind::PipePipe, TokenKind::Pipe); break;
                case '<':
                    advance();
                    if (peek() == '<') { advance(); push(TokenKind::ShiftLeft, start, sl, sc); }
                    else if (peek() == '=') { advance(); push(TokenKind::LessEqual, start, sl, sc); }
                    else push(TokenKind::Less, start, sl, sc);
                    break;
                case '>':
                    advance();
                    if (peek() == '>') { advance(); push(TokenKind::ShiftRight, start, sl, sc); }
                    else if (peek() == '=') { advance(); push(TokenKind::GreaterEqual, start, sl, sc); }
                    else push(TokenKind::Greater, start, sl, sc);
                    break;
                default: error("unexpected character '" + std::string(1, c) + "'", start, sl, sc);
            }
        }
        out.push_back(Token{TokenKind::Eof, {}, {static_cast<uint32_t>(pos), 0, line, column}});
    }
};
}

std::vector<Token> tokenize(std::string_view source) {
    Scanner s{source};
    s.run();
    return std::move(s.out);
}

const char* token_kind_name(TokenKind kind) {
    switch (kind) {
        case TokenKind::Eof: return "end of input";
        case TokenKind::Identifier: return "identifier";
        case TokenKind::Variable: return "variable";
        case TokenKind::Integer: return "integer";
        case TokenKind::Float: return "float";
        case TokenKind::String: return "string";
        case TokenKind::LParen: return "("; case TokenKind::RParen: return ")";
        case TokenKind::LBrace: return "{"; case TokenKind::RBrace: return "}";
        case TokenKind::LBracket: return "["; case TokenKind::RBracket: return "]";
        case TokenKind::Comma: return ","; case TokenKind::Dot: return "."; case TokenKind::At: return "@";
        case TokenKind::Equal: return "="; case TokenKind::EqualEqual: return "==";
        case TokenKind::Bang: return "!"; case TokenKind::BangEqual: return "!=";
        case TokenKind::Less: return "<"; case TokenKind::LessEqual: return "<=";
        case TokenKind::Greater: return ">"; case TokenKind::GreaterEqual: return ">=";
        case TokenKind::Plus: return "+"; case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*"; case TokenKind::Slash: return "/";
        case TokenKind::Percent: return "%"; case TokenKind::Amp: return "&";
        case TokenKind::AmpAmp: return "&&"; case TokenKind::Pipe: return "|";
        case TokenKind::PipePipe: return "||"; case TokenKind::Caret: return "^";
        case TokenKind::Tilde: return "~"; case TokenKind::ShiftLeft: return "<<";
        case TokenKind::ShiftRight: return ">>";
    }
    return "token";
}
