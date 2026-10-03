#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
#include <utility>

struct SourceSpan {
    uint32_t offset = 0;
    uint32_t length = 0;
    uint32_t line = 1;
    uint32_t column = 1;
};

inline SourceSpan merge_spans(SourceSpan a, SourceSpan b) {
    if (a.length == 0) return b;
    if (b.length == 0) return a;
    SourceSpan out = a;
    const uint32_t end = b.offset + b.length;
    out.length = end > a.offset ? end - a.offset : a.length;
    return out;
}

struct DiagnosticError : public std::runtime_error {
    SourceSpan span;
    explicit DiagnosticError(std::string message, SourceSpan where = {})
        : std::runtime_error(std::move(message)), span(where) {}
};

struct IncompleteInput : public std::exception {
    const char* what() const noexcept override { return "incomplete input"; }
};
