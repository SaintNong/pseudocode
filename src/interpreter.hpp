#pragma once

#include "ast.hpp"
#include "errors.hpp"
#include "runtime.hpp"
#include "vm.hpp"
#include <memory>
#include <vector>

#define VARIADIC_ARITY -1

class Interpreter {
public:
    GarbageCollector heap;
    Environment *globals;
    Environment *environment;
    ErrorReporter &reporter;
    std::unique_ptr<VM> vm;

    Interpreter(ErrorReporter &reporterRef);

    void interpret(const std::vector<StmtPtr> &statements);
    RuntimeValue evaluate(Expr *expr);

    RuntimeValue runFunction(CompiledFunction *compiledFn, const std::vector<RuntimeValue> &args,
                             Environment *closure);
};
