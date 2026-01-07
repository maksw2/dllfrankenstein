#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum class TokenType {
    TOK_EOF = 0,
    TOK_IDENTIFIER, 
    TOK_INTEGER,
    TOK_FLOAT,
    TOK_STRING,
    TOK_RAW_STRING,
    TOK_VARIABLE,
    TOK_FLAG,
    
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_COMMA, TOK_EQUALS,
    TOK_AMP,

    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH
};

struct Token {
    TokenType type;
    std::string text; 
    int line;         // For error reporting
    uint64_t int_val; // Pre-parsed integer value if type == TOK_INTEGER
};

std::vector<Token> tokenize(const char* source);
