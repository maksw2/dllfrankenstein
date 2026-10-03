#pragma once
#include "runtime.hpp"
#include <memory>
#include <vector>

class Interpreter {
public:
    explicit Interpreter(Runtime& runtime) : runtime_(runtime) {}
    void execute_module(const std::shared_ptr<Module>& module, bool echo_call_results = false);
    RuntimeValue invoke(FunctionVersion& function, const std::vector<RuntimeValue>& args);
private:
    Runtime& runtime_;
};
