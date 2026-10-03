#pragma once
#include "ast.hpp"
#include <memory>
#include <string>
#include <string_view>

std::shared_ptr<Module> parse_module(std::string source, std::string source_name = "<input>");
