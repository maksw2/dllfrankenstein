#pragma once
#include "lexer.hpp"
#include "common.hpp" 

// The Context bucket (passed around, never owns logic)
struct ParseContext {
    const std::vector<Token>& tokens;
    size_t pos;
};

// Main entry point
void parse_and_execute(std::vector<Token>& tokens);
