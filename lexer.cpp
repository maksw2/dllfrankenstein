#include "lexer.hpp"
#include <cctype>
#include <cstdlib>
#include <cstring>

static bool is_ident_char(char c) {
    return isalnum(c) || c == '_' || c == '.'; // allowed in names
}

std::vector<Token> tokenize(const char* src) {
    std::vector<Token> tokens;
    int line = 1;
    const char* p = src;

    while (*p) {
        // 1. Skip Whitespace
        if (isspace(*p)) {
            if (*p == '\n') line++;
            p++;
            continue;
        }

        // 2. Skip Comments
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue; 
        }

        // 3. Flags (e.g. --print-result, --assert=zero)
        // We catch this BEFORE subtraction (-) because -- is distinct.
        if (p[0] == '-' && p[1] == '-') {
            const char* start = p;
            p += 2; // skip --
            while (is_ident_char(*p) || *p == '-' || *p == '=') {
                p++;
            }
            tokens.push_back({TokenType::TOK_FLAG, std::string(start, p - start), line, 0});
            continue;
        }

        // 4. Variables ($var)
        // Merging $ and name makes parsing much safer
        if (*p == '$') {
            const char* start = p;
            p++; // skip $
            while (is_ident_char(*p)) p++;
            tokens.push_back({TokenType::TOK_VARIABLE, std::string(start, p - start), line, 0});
            continue;
        }

        // 5. Numbers (Hex, Int, Float)
        // Check for digit OR .digit (like .5f)
        if (isdigit(*p) || (*p == '.' && isdigit(p[1]))) {
            const char* start = p;
            bool is_hex = false;
            bool is_float = false;

            if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
                is_hex = true;
                p += 2;
            } else if (*p == '.') {
                is_float = true;
                p++;
            }

            while (isxdigit(*p) || *p == '.') {
                if (*p == '.') is_float = true;
                p++;
            }
            
            // Handle float suffix 'f'
            if (*p == 'f' || *p == 'F') {
                is_float = true;
                p++;
            }

            std::string text(start, p - start);
            
            if (is_float) {
                // We store the raw string for floats, value=0 for now
                tokens.push_back({TokenType::TOK_FLOAT, text, line, 0});
            } else {
                uint64_t val = strtoull(text.c_str(), NULL, is_hex ? 16 : 10);
                tokens.push_back({TokenType::TOK_INTEGER, text, line, val});
            }
            continue;
        }

        // 6. Strings
        if (*p == '"') {
            p++;
            std::string str_content;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) {
                    switch (p[1]) {
                        case 'n': str_content += '\n'; break;
                        case 't': str_content += '\t'; break;
                        case 'r': str_content += '\r'; break;
                        case '\\': str_content += '\\'; break;
                        case '"': str_content += '"'; break;
                        default: str_content += p[1]; break;
                    }
                    p += 2;
                } else {
                    str_content += *p++;
                }
            }
            if (*p == '"') p++;
            tokens.push_back({TokenType::TOK_STRING, str_content, line, 0});
            continue;
        }

        // 7. Identifiers
        if (isalpha(*p) || *p == '_') {
            const char* start = p;
            while (is_ident_char(*p)) p++;
            tokens.push_back({TokenType::TOK_IDENTIFIER, std::string(start, p - start), line, 0});
            continue;
        }

        // 8. Symbols
        TokenType type = TokenType::TOK_EOF;
        switch (*p) {
            case '(': type = TokenType::TOK_LPAREN; break;
            case ')': type = TokenType::TOK_RPAREN; break;
            case '{': type = TokenType::TOK_LBRACE; break;
            case '}': type = TokenType::TOK_RBRACE; break;
            case ',': type = TokenType::TOK_COMMA; break;
            case '=': type = TokenType::TOK_EQUALS; break;
            // $ handled above
            case '+': type = TokenType::TOK_PLUS; break;
            case '-': type = TokenType::TOK_MINUS; break; // Single minus
            case '*': type = TokenType::TOK_STAR; break;
            case '/': type = TokenType::TOK_SLASH; break;
            case ';': type = TokenType::TOK_SEMICOLON; break; // Just in case
        }

        if (type != TokenType::TOK_EOF) {
            tokens.push_back({type, std::string(1, *p), line, 0});
            p++;
            continue;
        }

        // Unknown
        p++;
    }

    tokens.push_back({TokenType::TOK_EOF, "", line, 0});
    return tokens;
}
