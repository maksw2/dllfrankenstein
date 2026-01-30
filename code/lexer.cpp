#include "lexer.hpp"
#include <cctype>
#include <charconv>
#include <cctype>
#include <stdexcept>

std::vector<Token> tokenize(std::string_view src) {
    std::vector<Token> tokens;
    int line = 1;
    size_t cursor = 0;

    // Helper to peek at the current character
    auto peek = [&](int offset = 0) -> char {
        if (cursor + offset >= src.size()) return '\0';
        return src[cursor + offset];
    };

    // Helper to advance and track lines automatically
    auto advance = [&](int count = 1) {
        for (int i = 0; i < count; ++i) {
            if (cursor < src.size()) {
                if (src[cursor] == '\n') line++;
                cursor++;
            }
        }
    };

    while (cursor < src.size()) {
        char c = peek();

        // Skip Whitespace
        if (std::isspace(static_cast<unsigned char>(c))) {
            advance();
            continue;
        }

        // Skip Comments
        if (c == ';' || (c == '/' && peek(1) == '/')) {
            while (peek() != '\0' && peek() != '\n') {
                // We strictly do NOT use advance() here because we want 
                // the newline to be handled by the whitespace logic 
                // in the next iteration (to keep logic consistent).
                cursor++; 
            }
            continue; 
        }

        const size_t start = cursor;

        // Flags (e.g. --print)
        if (c == '-' && peek(1) == '-') {
            advance(2); // skip --
            while (isalnum(peek()) || peek() == '-' || peek() == '_' || peek() == '=') {
                advance();
            }
            tokens.push_back({TokenType::TOK_FLAG, std::string(src.substr(start, cursor - start)), line, 0});
            continue;
        }

        // Variables ($var)
        if (c == '$') {
            advance(); // skip $
            while (isalnum(peek()) || peek() == '_') advance();
            tokens.push_back({TokenType::TOK_VARIABLE, std::string(src.substr(start, cursor - start)), line, 0});
            continue;
        }

        // Numbers
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
            bool is_hex = (c == '0' && (peek(1) == 'x' || peek(1) == 'X'));
            bool is_float = false;

            if (is_hex) advance(2);
            else if (c == '.') { is_float = true; advance(); }

            while (std::isxdigit(static_cast<unsigned char>(peek())) || peek() == '.') {
                if (peek() == '.') is_float = true;
                advance();
            }
            
            if (peek() == 'f' || peek() == 'F') {
                is_float = true;
                advance();
            }

            std::string_view text = src.substr(start, cursor - start);
            
            if (is_float) {
                tokens.push_back({TokenType::TOK_FLOAT, std::string(text), line, 0});
            } else {
                uint64_t val = 0;
                // C++17 std::from_chars is faster and cleaner than strtoull
                // It does not require a null-terminated string.
                std::from_chars(text.data() + (is_hex ? 2 : 0), text.data() + text.size(), val, is_hex ? 16 : 10);
                tokens.push_back({TokenType::TOK_INTEGER, std::string(text), line, val});
            }
            continue;
        }

        // Strings
        if (c == '"') {
            advance(); // Enter string
            std::string str_content;
            
            while (peek() != '\0' && peek() != '"') {
                char curr = peek();
                if (curr == '\\' && peek(1)) {
                    // Handle escapes
                    char next = peek(1);
                    switch (next) {
                        case 'n': str_content += '\n'; break;
                        case 't': str_content += '\t'; break;
                        case 'r': str_content += '\r'; break;
                        case '"': str_content += '"'; break;
                        case '\\': str_content += '\\'; break;
                        default: str_content += next; break;
                    }
                    advance(2); // Consume \ and char
                } else {
                    // CRITICAL FIX: advance() tracks newlines here automatically
                    str_content += curr;
                    advance(); 
                }
            }
            
            if (peek() == '"') advance(); // Closing quote
            tokens.push_back({TokenType::TOK_STRING, str_content, line, 0});
            continue;
        }

        // Identifiers
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_' || peek() == '.') {
                advance();
            }
            tokens.push_back({TokenType::TOK_IDENTIFIER, std::string(src.substr(start, cursor - start)), line, 0});
            continue;
        }

        // Symbols
        TokenType type = TokenType::TOK_EOF;
        switch (c) {
            case '(': type = TokenType::TOK_LPAREN; break;
            case ')': type = TokenType::TOK_RPAREN; break;
            case '{': type = TokenType::TOK_LBRACE; break;
            case '}': type = TokenType::TOK_RBRACE; break;
            case ',': type = TokenType::TOK_COMMA; break;
            case '=': type = TokenType::TOK_EQUALS; break;
            case '+': type = TokenType::TOK_PLUS; break;
            case '-': type = TokenType::TOK_MINUS; break;
            case '*': type = TokenType::TOK_STAR; break;
            case '/': type = TokenType::TOK_SLASH; break;
            case '&': type = TokenType::TOK_AMP; break;
            case '|': type = TokenType::TOK_PIPE; break;
            case '^': type = TokenType::TOK_XOR; break;
            case '~': type = TokenType::TOK_TILDE; break;
            
            case '<': 
                if (peek(1) == '<') { type = TokenType::TOK_LSHIFT; advance(); } // eat extra char
                else type = TokenType::TOK_LT;
                break;
            case '>': 
                if (peek(1) == '>') { type = TokenType::TOK_RSHIFT; advance(); }
                else type = TokenType::TOK_GT;
                break;
        }

        if (type != TokenType::TOK_EOF) {
            tokens.push_back({type, std::string(1, c), line, 0});
            advance();
            continue;
        }

        // Unknown character
        throw std::runtime_error("Unexpected character '" + std::string(1, c) + "' at line " + std::to_string(line));
    }

    tokens.push_back({TokenType::TOK_EOF, "", line, 0});
    return tokens;
}
