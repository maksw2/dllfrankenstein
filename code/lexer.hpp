#pragma once
#include "source.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class TokenKind : uint16_t {
    Eof,
    Identifier,
    Variable,
    Integer,
    Float,
    String,

    LParen, RParen,
    LBrace, RBrace,
    LBracket, RBracket,
    Comma, Dot,

    Equal,
    EqualEqual,
    Bang,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Amp,
    AmpAmp,
    Pipe,
    PipePipe,
    Caret,
    Tilde,
    ShiftLeft,
    ShiftRight,
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string text;
    SourceSpan span{};
    uint64_t integer = 0;
    double floating = 0.0;
};

std::vector<Token> tokenize(std::string_view source);
const char* token_kind_name(TokenKind kind);
