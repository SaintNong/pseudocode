#include "compiler.hpp"
#include "opcodes.hpp"
#include <iostream>
#include <stdexcept>

Compiler::Compiler(ErrorReporter &reporter, Compiler *enclosing, std::string fnName)
    : reporter(reporter), enclosing(enclosing) {
    currentFn        = gcNew<CompiledFunction>();
    currentFn->chunk = gcNew<Chunk>();
    currentFn->arity = 0;
    currentFn->name  = fnName;
}

CompiledFunction *Compiler::compile(const std::vector<StmtPtr> &statements) {
    for (const auto &stmt : statements) {
        if (stmt) {
            compileStatement(stmt.get());
        }
    }
    emitByte(OP_NULL, currentChunk().lines.empty() ? 1 : currentChunk().lines.back());
    emitByte(OP_RETURN, currentChunk().lines.empty() ? 1 : currentChunk().lines.back());
    return currentFn;
}

CompiledFunction *Compiler::compileExpressionOnly(Expr *expr) {
    compileExpression(expr);
    emitByte(OP_RETURN, 1);
    return currentFn;
}

void Compiler::emitByte(uint8_t byte, size_t line) {
    currentChunk().write(byte, line);
}

void Compiler::emitShort(uint16_t value, size_t line) {
    emitByte((value >> 8) & 0xff, line);
    emitByte(value & 0xff, line);
}

void Compiler::emitConstant(RuntimeValue value, size_t line) {
    size_t index = currentChunk().addConstant(value);
    if (index > 65535) {
        errorAt(line, "Too many constants in one chunk.");
    }
    emitByte(OP_CONSTANT, line);
    emitShort(static_cast<uint16_t>(index), line);
}

size_t Compiler::emitJump(uint8_t instruction, size_t line) {
    emitByte(instruction, line);
    emitByte(0xff, line);
    emitByte(0xff, line);
    return currentChunk().code.size() - 2;
}

void Compiler::patchJump(size_t offset, size_t line) {
    size_t jump = currentChunk().code.size() - offset - 2;
    if (jump > 65535) {
        errorAt(line, "Too much code to jump over.");
    }
    currentChunk().code[offset]     = (jump >> 8) & 0xff;
    currentChunk().code[offset + 1] = jump & 0xff;
}

void Compiler::emitLoop(size_t loopStart, size_t line) {
    emitByte(OP_LOOP, line);
    size_t offset = currentChunk().code.size() - loopStart + 2;
    if (offset > 65535) {
        errorAt(line, "Loop body too large.");
    }
    emitShort(static_cast<uint16_t>(offset), line);
}

void Compiler::beginScope() {
    scopeDepth++;
}

void Compiler::endScope(size_t line) {
    scopeDepth--;
    while (!locals.empty() && locals.back().depth > scopeDepth) {
        emitByte(OP_POP, line);
        locals.pop_back();
        nextLocalSlot--;
    }
}

int Compiler::addLocal(const std::string &name, size_t line, bool reserveSlot) {
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].name == name) {
            return locals[i].slot;
        }
    }

    Local local;
    local.name  = name;
    local.depth = scopeDepth;
    local.slot  = nextLocalSlot++;
    locals.push_back(local);
    if (reserveSlot) {
        emitByte(OP_NULL, line);
    }
    return local.slot;
}

int Compiler::resolveLocal(const std::string &name) {
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].name == name) {
            return locals[i].slot;
        }
    }
    return -1;
}

void Compiler::errorAt(size_t line, const std::string &message) {
    reporter.report(ErrorType::Syntax, line, 0, message, 1);
}

// =================================================================================================
//           Expression Compilation
// =================================================================================================

void Compiler::compileExpression(Expr *expr) {
    if (!expr)
        return;
    if (auto *litExpr = dynamic_cast<LiteralExpr *>(expr)) {
        compileLiteralExpr(litExpr);
    } else if (auto *varExpr = dynamic_cast<VariableExpr *>(expr)) {
        compileVariableExpr(varExpr);
    } else if (auto *assignExpr = dynamic_cast<AssignExpr *>(expr)) {
        compileAssignExpr(assignExpr);
    } else if (auto *binExpr = dynamic_cast<BinaryExpr *>(expr)) {
        compileBinaryExpr(binExpr);
    } else if (auto *unExpr = dynamic_cast<UnaryExpr *>(expr)) {
        compileUnaryExpr(unExpr);
    } else if (auto *callExpr = dynamic_cast<CallExpr *>(expr)) {
        compileCallExpr(callExpr);
    } else if (auto *getExpr = dynamic_cast<GetExpr *>(expr)) {
        compileGetExpr(getExpr);
    } else if (auto *arrAccExpr = dynamic_cast<ArrayAccessExpr *>(expr)) {
        compileArrayAccessExpr(arrAccExpr);
    } else if (auto *arrLitExpr = dynamic_cast<ArrayLitExpr *>(expr)) {
        compileArrayLitExpr(arrLitExpr);
    } else if (auto *dictLitExpr = dynamic_cast<DictLitExpr *>(expr)) {
        compileDictLitExpr(dictLitExpr);
    } else if (auto *newExpr = dynamic_cast<NewExpr *>(expr)) {
        compileNewExpr(newExpr);
    }
}

void Compiler::compileLiteralExpr(LiteralExpr *expr) {
    Token token = expr->token;
    size_t line = token.line;

    if (token.type == TOK_INTEGER) {
        RuntimeValue val;
        val.value = std::stoi(token.lexeme);
        emitConstant(val, line);
    } else if (token.type == TOK_FLOAT) {
        RuntimeValue val;
        val.value = std::stod(token.lexeme);
        emitConstant(val, line);
    } else if (token.type == TOK_STRING) {
        RuntimeValue val;
        val.value = token.lexeme;
        emitConstant(val, line);
    } else if (token.type == TOK_TRUE) {
        emitByte(OP_TRUE, line);
    } else if (token.type == TOK_FALSE) {
        emitByte(OP_FALSE, line);
    } else {
        emitByte(OP_NULL, line);
    }
}

void Compiler::compileVariableExpr(VariableExpr *expr) {
    std::string name = expr->name.lexeme;
    size_t line      = expr->name.line;

    int slot = resolveLocal(name);
    if (slot != -1) {
        emitByte(OP_GET_LOCAL, line);
        emitShort(static_cast<uint16_t>(slot), line);
    } else {
        RuntimeValue val;
        val.value      = name;
        size_t nameIdx = currentChunk().addConstant(val);
        emitByte(OP_GET_GLOBAL, line);
        emitShort(static_cast<uint16_t>(nameIdx), line);
    }
}

void Compiler::compileAssignExpr(AssignExpr *expr) {
    size_t line = expr->anchor.line;

    if (auto *varExpr = dynamic_cast<VariableExpr *>(expr->target.get())) {
        std::string name = varExpr->name.lexeme;

        // Check if variable already exists as a local
        int slot = resolveLocal(name);

        if (slot == -1 && enclosing != nullptr) {
            // New local variable inside a function — the compiled value
            // becomes the local's stack slot directly (no OP_SET_LOCAL needed)
            compileExpression(expr->value.get());
            addLocal(name, line);
            lastAssignWasNewLocal = true;
            return;
        }

        compileExpression(expr->value.get());

        if (slot != -1) {
            emitByte(OP_SET_LOCAL, line);
            emitShort(static_cast<uint16_t>(slot), line);
        } else {
            RuntimeValue val;
            val.value      = name;
            size_t nameIdx = currentChunk().addConstant(val);
            emitByte(OP_SET_GLOBAL, line);
            emitShort(static_cast<uint16_t>(nameIdx), line);
        }
    } else if (auto *getExpr = dynamic_cast<GetExpr *>(expr->target.get())) {
        compileExpression(getExpr->object.get());
        compileExpression(expr->value.get());

        RuntimeValue val;
        val.value      = getExpr->name.lexeme;
        size_t nameIdx = currentChunk().addConstant(val);
        emitByte(OP_SET_PROPERTY, line);
        emitShort(static_cast<uint16_t>(nameIdx), line);
    } else if (auto *arrayAccess = dynamic_cast<ArrayAccessExpr *>(expr->target.get())) {
        compileExpression(arrayAccess->array.get());
        compileExpression(arrayAccess->index.get());
        compileExpression(expr->value.get());
        emitByte(OP_INDEX_SET, line);
    }
}

void Compiler::compileBinaryExpr(BinaryExpr *expr) {
    size_t line = expr->op.line;

    if (expr->op.type == TOK_AND) {
        compileExpression(expr->left.get());
        size_t endJump = emitJump(OP_AND_JUMP, line);
        emitByte(OP_POP, line);
        compileExpression(expr->right.get());
        patchJump(endJump, line);
        return;
    }
    if (expr->op.type == TOK_OR) {
        compileExpression(expr->left.get());
        size_t endJump = emitJump(OP_OR_JUMP, line);
        emitByte(OP_POP, line);
        compileExpression(expr->right.get());
        patchJump(endJump, line);
        return;
    }

    compileExpression(expr->left.get());
    compileExpression(expr->right.get());

    switch (expr->op.type) {
    case TOK_PLUS:
        emitByte(OP_ADD, line);
        break;
    case TOK_MINUS:
        emitByte(OP_SUBTRACT, line);
        break;
    case TOK_MULTIPLY:
        emitByte(OP_MULTIPLY, line);
        break;
    case TOK_DIVIDE:
        emitByte(OP_DIVIDE, line);
        break;
    case TOK_MOD:
        emitByte(OP_MOD, line);
        break;
    case TOK_EQUAL:
        emitByte(OP_EQUAL, line);
        break;
    case TOK_NOT_EQUAL:
        emitByte(OP_NOT_EQUAL, line);
        break;
    case TOK_GREATER_THAN:
        emitByte(OP_GREATER, line);
        break;
    case TOK_GT_OR_EQ:
        emitByte(OP_GREATER_EQUAL, line);
        break;
    case TOK_LESS_THAN:
        emitByte(OP_LESS, line);
        break;
    case TOK_LT_OR_EQ:
        emitByte(OP_LESS_EQUAL, line);
        break;
    case TOK_IN:
        emitByte(OP_IN, line);
        break;
    default:
        errorAt(line, "Unknown binary operator.");
    }
}

void Compiler::compileUnaryExpr(UnaryExpr *expr) {
    size_t line = expr->op.line;
    compileExpression(expr->right.get());

    switch (expr->op.type) {
    case TOK_MINUS:
        emitByte(OP_NEGATE, line);
        break;
    case TOK_NOT:
        emitByte(OP_NOT, line);
        break;
    default:
        errorAt(line, "Unknown unary operator.");
    }
}

void Compiler::compileCallExpr(CallExpr *expr) {
    size_t line = expr->anchor.line;
    compileExpression(expr->callee.get());

    for (const auto &arg : expr->args) {
        compileExpression(arg.get());
    }

    emitByte(OP_CALL, line);
    emitByte(static_cast<uint8_t>(expr->args.size()), line);
}

void Compiler::compileGetExpr(GetExpr *expr) {
    size_t line = expr->name.line;
    compileExpression(expr->object.get());

    RuntimeValue val;
    val.value      = expr->name.lexeme;
    size_t nameIdx = currentChunk().addConstant(val);
    emitByte(OP_GET_PROPERTY, line);
    emitShort(static_cast<uint16_t>(nameIdx), line);
}

void Compiler::compileArrayAccessExpr(ArrayAccessExpr *expr) {
    size_t line = expr->anchor.line;
    compileExpression(expr->array.get());
    compileExpression(expr->index.get());
    emitByte(OP_INDEX_GET, line);
}

void Compiler::compileArrayLitExpr(ArrayLitExpr *expr) {
    size_t line = expr->anchor.line;
    for (const auto &elem : expr->elements) {
        compileExpression(elem.get());
    }
    emitByte(OP_ARRAY, line);
    emitShort(static_cast<uint16_t>(expr->elements.size()), line);
}

void Compiler::compileDictLitExpr(DictLitExpr *expr) {
    size_t line = expr->anchor.line;
    for (const auto &entry : expr->entries) {
        compileExpression(entry.key.get());
        compileExpression(entry.value.get());
    }
    emitByte(OP_DICT, line);
    emitShort(static_cast<uint16_t>(expr->entries.size()), line);
}

void Compiler::compileNewExpr(NewExpr *expr) {
    size_t line = expr->className.line;
    for (const auto &arg : expr->args) {
        compileExpression(arg.get());
    }
    RuntimeValue val;
    val.value      = expr->className.lexeme;
    size_t nameIdx = currentChunk().addConstant(val);
    emitByte(OP_NEW_INSTANCE, line);
    emitShort(static_cast<uint16_t>(nameIdx), line);
    emitByte(static_cast<uint8_t>(expr->args.size()), line);
}

// =================================================================================================
//           Statement Compilation
// =================================================================================================

void Compiler::compileStatement(Stmt *stmt) {
    if (!stmt)
        return;
    if (auto *exprStmt = dynamic_cast<ExpressionStmt *>(stmt)) {
        compileExpressionStmt(exprStmt);
    } else if (auto *retStmt = dynamic_cast<ReturnStmt *>(stmt)) {
        compileReturnStmt(retStmt);
    } else if (auto *blockStmt = dynamic_cast<BlockStmt *>(stmt)) {
        compileBlockStmt(blockStmt);
    } else if (auto *ifStmt = dynamic_cast<IfStmt *>(stmt)) {
        compileIfStmt(ifStmt);
    } else if (auto *funcStmt = dynamic_cast<FunctionStmt *>(stmt)) {
        compileFunctionStmt(funcStmt);
    } else if (auto *classStmt = dynamic_cast<ClassStmt *>(stmt)) {
        compileClassStmt(classStmt);
    } else if (auto *whileStmt = dynamic_cast<WhileStmt *>(stmt)) {
        compileWhileStmt(whileStmt);
    } else if (auto *forInStmt = dynamic_cast<ForInStmt *>(stmt)) {
        compileForInStmt(forInStmt);
    } else if (auto *forStmt = dynamic_cast<ForStmt *>(stmt)) {
        compileForStmt(forStmt);
    } else if (auto *caseStmt = dynamic_cast<CaseStmt *>(stmt)) {
        compileCaseStmt(caseStmt);
    } else if (auto *repeatStmt = dynamic_cast<RepeatUntilStmt *>(stmt)) {
        compileRepeatUntilStmt(repeatStmt);
    }
}

void Compiler::compileExpressionStmt(ExpressionStmt *stmt) {
    lastAssignWasNewLocal = false;
    compileExpression(stmt->expression.get());
    if (lastAssignWasNewLocal) {
        // The expression created a new local variable — the pushed value
        // IS the local's stack slot, so don't pop it
        lastAssignWasNewLocal = false;
    } else {
        emitByte(OP_POP, currentChunk().lines.empty() ? 1 : currentChunk().lines.back());
    }
}

void Compiler::compileReturnStmt(ReturnStmt *stmt) {
    size_t line = currentChunk().lines.empty() ? 1 : currentChunk().lines.back();
    if (stmt->value) {
        compileExpression(stmt->value.get());
    } else {
        emitByte(OP_NULL, line);
    }
    emitByte(OP_RETURN, line);
}

void Compiler::compileBlockStmt(BlockStmt *stmt) {
    for (const auto &s : stmt->statements) {
        compileStatement(s.get());
    }
}

void Compiler::compileIfStmt(IfStmt *stmt) {
    size_t line = stmt->keyword.line;

    compileExpression(stmt->condition.get());
    size_t thenJump = emitJump(OP_JUMP_IF_FALSE, line);

    beginScope();
    for (const auto &s : stmt->thenBranch) {
        compileStatement(s.get());
    }
    endScope(line);

    std::vector<size_t> endJumps;
    size_t endJump = emitJump(OP_JUMP, line);
    endJumps.push_back(endJump);

    patchJump(thenJump, line);

    for (const auto &elseIf : stmt->elseIfBranches) {
        compileExpression(elseIf.condition.get());
        size_t nextJump = emitJump(OP_JUMP_IF_FALSE, line);

        beginScope();
        for (const auto &s : elseIf.body) {
            compileStatement(s.get());
        }
        endScope(line);

        size_t skipJump = emitJump(OP_JUMP, line);
        endJumps.push_back(skipJump);

        patchJump(nextJump, line);
    }

    beginScope();
    for (const auto &s : stmt->elseBranch) {
        compileStatement(s.get());
    }
    endScope(line);

    for (size_t jump : endJumps) {
        patchJump(jump, line);
    }
}

void Compiler::compileFunctionStmt(FunctionStmt *stmt) {
    size_t line      = stmt->name.line;
    std::string name = stmt->name.lexeme;

    Compiler subCompiler(reporter, this, name);
    subCompiler.currentFn->arity = static_cast<int>(stmt->params.size());

    for (const auto &param : stmt->params) {
        subCompiler.addLocal(param.lexeme, line, false);
    }

    auto compiled = subCompiler.compile(stmt->body);

    RuntimeValue funcVal;
    funcVal.value   = static_cast<Callable *>(gcNew<UserFunction>(compiled, nullptr));
    size_t constIdx = currentChunk().addConstant(funcVal);

    if (enclosing != nullptr) {
        emitByte(OP_CLOSURE, line);
        emitShort(static_cast<uint16_t>(constIdx), line);
        addLocal(name, line);
    } else {
        emitByte(OP_CLOSURE, line);
        emitShort(static_cast<uint16_t>(constIdx), line);

        RuntimeValue nameVal;
        nameVal.value  = name;
        size_t nameIdx = currentChunk().addConstant(nameVal);
        emitByte(OP_DEFINE_GLOBAL, line);
        emitShort(static_cast<uint16_t>(nameIdx), line);
    }
}

void Compiler::compileClassStmt(ClassStmt *stmt) {
    size_t line           = stmt->name.line;
    std::string className = stmt->name.lexeme;

    RuntimeValue classNameVal;
    classNameVal.value = className;
    size_t nameIdx     = currentChunk().addConstant(classNameVal);

    emitByte(OP_CLASS, line);
    emitShort(static_cast<uint16_t>(nameIdx), line);

    if (stmt->superclass.type != TOK_EOF) {
        VariableExpr superVar(stmt->superclass);
        compileVariableExpr(&superVar);
        emitByte(OP_INHERIT, line);
    }

    for (const auto &attr : stmt->attributes) {
        if (auto *assignment = dynamic_cast<AssignExpr *>(attr.get())) {
            if (auto *varExpr = dynamic_cast<VariableExpr *>(assignment->target.get())) {
                std::string fieldName = varExpr->name.lexeme;

                Compiler subCompiler(reporter, this, className + "::" + fieldName + "_init");
                auto compiledInit = subCompiler.compileExpressionOnly(assignment->value.get());

                RuntimeValue initVal;
                initVal.value = static_cast<Callable *>(gcNew<UserFunction>(compiledInit, nullptr));
                size_t initConstIdx = currentChunk().addConstant(initVal);

                emitByte(OP_CLOSURE, line);
                emitShort(static_cast<uint16_t>(initConstIdx), line);

                RuntimeValue attrNameVal;
                attrNameVal.value  = fieldName;
                size_t attrNameIdx = currentChunk().addConstant(attrNameVal);

                emitByte(OP_ATTRIBUTE, line);
                emitShort(static_cast<uint16_t>(attrNameIdx), line);
            }
        }
    }

    for (const auto &method : stmt->methods) {
        if (auto *funcStmt = dynamic_cast<FunctionStmt *>(method.get())) {
            std::string methodName = funcStmt->name.lexeme;

            Compiler subCompiler(reporter, this, className + "::" + methodName);
            subCompiler.currentFn->arity = static_cast<int>(funcStmt->params.size());

            for (const auto &param : funcStmt->params) {
                subCompiler.addLocal(param.lexeme, line, false);
            }

            auto compiledMethod = subCompiler.compile(funcStmt->body);

            RuntimeValue methodVal;
            methodVal.value = static_cast<Callable *>(gcNew<UserFunction>(compiledMethod, nullptr));
            size_t methodConstIdx = currentChunk().addConstant(methodVal);

            emitByte(OP_CLOSURE, line);
            emitShort(static_cast<uint16_t>(methodConstIdx), line);

            RuntimeValue methodNameVal;
            methodNameVal.value  = methodName;
            size_t methodNameIdx = currentChunk().addConstant(methodNameVal);

            emitByte(OP_METHOD, line);
            emitShort(static_cast<uint16_t>(methodNameIdx), line);
        }
    }

    if (enclosing != nullptr) {
        addLocal(className, line);
    } else {
        emitByte(OP_DEFINE_GLOBAL, line);
        emitShort(static_cast<uint16_t>(nameIdx), line);
    }
}

void Compiler::compileWhileStmt(WhileStmt *stmt) {
    size_t line      = stmt->keyword.line;
    size_t loopStart = currentChunk().code.size();

    compileExpression(stmt->condition.get());
    size_t exitJump = emitJump(OP_JUMP_IF_FALSE, line);

    beginScope();
    for (const auto &s : stmt->body) {
        compileStatement(s.get());
    }
    endScope(line);

    emitLoop(loopStart, line);
    patchJump(exitJump, line);
}

void Compiler::compileForInStmt(ForInStmt *stmt) {
    size_t line = stmt->keyword.line;

    // Reserve 4 stack slots for loop variables (var, arr copy, index, size)
    int varSlot = addLocal(stmt->variable.lexeme, line, true);
    addLocal("__arr_" + stmt->variable.lexeme, line, true);
    addLocal("__idx_" + stmt->variable.lexeme, line, true);
    addLocal("__size_" + stmt->variable.lexeme, line, true);

    // Push iterable value on top of reserved slots
    compileExpression(stmt->iterable.get());

    // OP_FOR_IN_PREPARE pops the iterable and fills the 4 reserved slots
    emitByte(OP_FOR_IN_PREPARE, line);
    emitShort(static_cast<uint16_t>(varSlot), line);

    size_t loopStart = currentChunk().code.size();

    beginScope();
    for (const auto &s : stmt->body) {
        compileStatement(s.get());
    }
    endScope(line);

    emitByte(OP_FOR_IN_LOOP, line);
    emitShort(static_cast<uint16_t>(varSlot), line);
    emitShort(static_cast<uint16_t>(loopStart), line);

    // Clean up the 4 local slots
    emitByte(OP_POP, line);
    emitByte(OP_POP, line);
    emitByte(OP_POP, line);
    emitByte(OP_POP, line);
    for (int i = 0; i < 4; i++) {
        if (!locals.empty()) {
            locals.pop_back();
            nextLocalSlot--;
        }
    }
}

void Compiler::compileForStmt(ForStmt *stmt) {
    size_t line = stmt->keyword.line;

    // Push start value onto stack → becomes the loop variable's slot
    compileExpression(stmt->start.get());
    int varSlot = addLocal(stmt->variable.lexeme, line);

    // Push end value onto stack → becomes the limit slot
    compileExpression(stmt->end.get());
    int limitSlot = addLocal("__limit_" + stmt->variable.lexeme, line);

    // Push placeholder for step → becomes the step slot
    emitByte(OP_NULL, line);
    addLocal("__step_" + stmt->variable.lexeme, line);

    emitByte(OP_FOR_PREPARE, line);
    emitShort(static_cast<uint16_t>(varSlot), line);
    size_t exitJump = emitJump(OP_JUMP_IF_FALSE, line);

    size_t loopStart = currentChunk().code.size();

    beginScope();
    for (const auto &s : stmt->body) {
        compileStatement(s.get());
    }
    endScope(line);

    emitByte(OP_FOR_LOOP, line);
    emitShort(static_cast<uint16_t>(varSlot), line);
    emitShort(static_cast<uint16_t>(loopStart), line);

    patchJump(exitJump, line);

    // Clean up the 3 local slots (var, limit, step)
    emitByte(OP_POP, line);
    emitByte(OP_POP, line);
    emitByte(OP_POP, line);
    // Remove locals from compiler tracking
    for (int i = 0; i < 3; i++) {
        if (!locals.empty()) {
            locals.pop_back();
            nextLocalSlot--;
        }
    }

    (void) limitSlot;
}

void Compiler::compileCaseStmt(CaseStmt *stmt) {
    size_t line = stmt->keyword.line;

    compileExpression(stmt->condition.get());

    std::vector<size_t> endJumps;

    for (const auto &arm : stmt->arms) {
        std::vector<size_t> bodyJumps;
        size_t nextArmJump = 0;

        for (size_t i = 0; i < arm.values.size(); ++i) {
            emitByte(OP_DUP, line);
            compileExpression(arm.values[i].get());
            emitByte(OP_EQUAL, line);

            if (i < arm.values.size() - 1) {
                size_t nextValJump = emitJump(OP_JUMP_IF_FALSE, line);
                bodyJumps.push_back(emitJump(OP_JUMP, line));
                patchJump(nextValJump, line);
            } else {
                nextArmJump = emitJump(OP_JUMP_IF_FALSE, line);
            }
        }

        for (size_t jump : bodyJumps) {
            patchJump(jump, line);
        }

        emitByte(OP_POP, line);
        for (const auto &s : arm.body) {
            compileStatement(s.get());
        }

        endJumps.push_back(emitJump(OP_JUMP, line));
        patchJump(nextArmJump, line);
    }

    emitByte(OP_POP, line);
    for (const auto &s : stmt->defaultBranch) {
        compileStatement(s.get());
    }

    for (size_t jump : endJumps) {
        patchJump(jump, line);
    }
}

void Compiler::compileRepeatUntilStmt(RepeatUntilStmt *stmt) {
    size_t line      = stmt->keyword.line;
    size_t loopStart = currentChunk().code.size();

    beginScope();
    for (const auto &s : stmt->body) {
        compileStatement(s.get());
    }
    endScope(line);

    compileExpression(stmt->condition.get());
    emitByte(OP_NOT, line);
    size_t exitJump = emitJump(OP_JUMP_IF_FALSE, line);
    emitLoop(loopStart, line);
    patchJump(exitJump, line);
}
