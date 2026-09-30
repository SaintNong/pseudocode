#pragma once

#include "ast.hpp"
#include "chunk.hpp"
#include "errors.hpp"
#include <memory>
#include <string>
#include <vector>

struct Local {
    std::string name;
    int depth;
    int slot;
};

class Compiler {
private:
    ErrorReporter &reporter;
    std::vector<Local> locals;
    int scopeDepth             = 0;
    int nextLocalSlot          = 0;
    bool lastAssignWasNewLocal = false;

    Compiler *enclosing = nullptr;
    CompiledFunction *currentFn;

    Chunk &currentChunk() {
        return *currentFn->chunk;
    }

    void emitByte(uint8_t byte, size_t line);
    void emitShort(uint16_t value, size_t line);
    void emitConstant(RuntimeValue value, size_t line);
    size_t emitJump(uint8_t instruction, size_t line);
    void patchJump(size_t offset, size_t line);
    void emitLoop(size_t loopStart, size_t line);

    void beginScope();
    void endScope(size_t line);
    int addLocal(const std::string &name, size_t line, bool reserveSlot = false);
    int resolveLocal(const std::string &name);

    // Expression compilation (manual dispatch)
    void compileExpression(Expr *expr);
    void compileLiteralExpr(LiteralExpr *expr);
    void compileVariableExpr(VariableExpr *expr);
    void compileAssignExpr(AssignExpr *expr);
    void compileBinaryExpr(BinaryExpr *expr);
    void compileUnaryExpr(UnaryExpr *expr);
    void compileCallExpr(CallExpr *expr);
    void compileGetExpr(GetExpr *expr);
    void compileArrayAccessExpr(ArrayAccessExpr *expr);
    void compileArrayLitExpr(ArrayLitExpr *expr);
    void compileDictLitExpr(DictLitExpr *expr);
    void compileNewExpr(NewExpr *expr);

    // Statement compilation (manual dispatch)
    void compileStatement(Stmt *stmt);
    void compileExpressionStmt(ExpressionStmt *stmt);
    void compileReturnStmt(ReturnStmt *stmt);
    void compileBlockStmt(BlockStmt *stmt);
    void compileIfStmt(IfStmt *stmt);
    void compileFunctionStmt(FunctionStmt *stmt);
    void compileClassStmt(ClassStmt *stmt);
    void compileWhileStmt(WhileStmt *stmt);
    void compileForInStmt(ForInStmt *stmt);
    void compileForStmt(ForStmt *stmt);
    void compileCaseStmt(CaseStmt *stmt);
    void compileRepeatUntilStmt(RepeatUntilStmt *stmt);

    void errorAt(size_t line, const std::string &message);

public:
    Compiler(ErrorReporter &reporter, Compiler *enclosing = nullptr, std::string fnName = "");

    CompiledFunction *compile(const std::vector<StmtPtr> &statements);
    CompiledFunction *compileExpressionOnly(Expr *expr);
};
