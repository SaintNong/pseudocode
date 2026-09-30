# Architecture: SCSA Bytecode Virtual Machine

This document outlines the architecture and execution model of the SCSA Pseudocode Bytecode Virtual Machine (VM).

On the NaN-boxing experiment branch, runtime slots are 64-bit tagged words and
runtime objects are managed by a non-moving mark-and-sweep collector. See
[runtime values and ownership](RUNTIME_VALUES.md) for encoding, root tracing,
collection boundaries and extension rules.

## Compilation Pipeline

The interpreter executes code through a multi-stage compilation pipeline:
1. **Lexical Analysis (`src/lexer.cpp`)**: Tokenizes the raw source code text into locatable, typed tokens.
2. **Parsing (`src/parser.cpp`)**: A hybrid recursive-descent and Pratt parser parses tokens into an Abstract Syntax Tree (AST) representing statements and expressions.
3. **Bytecode Compilation (`src/compiler.cpp`)**: Traverses the AST and emits flat, compact bytecode instructions into `Chunk` objects.
4. **VM Execution (`src/vm.cpp`)**: A stack-based virtual machine interprets the bytecode instruction chunks.

---

## Bytecode & Chunk Representation

### Instructions & Opcodes
All VM operations are defined as 1-byte opcodes in `src/opcodes.hpp`. Some opcodes are followed by parameters (inline arguments) in the bytecode stream:
- **`OP_CONSTANT <const_index_u16>`**: Pushes a constant from the constant pool.
- **`OP_GET_LOCAL / OP_SET_LOCAL <slot_u16>`**: Reads/writes a variable in the current call frame's stack.
- **`OP_JUMP / OP_JUMP_IF_FALSE <offset_u16>`**: Control-flow jumps.

### Chunk Structure (`src/chunk.hpp`)
A `Chunk` packages:
- A flat byte array (`code`) representing the instructions.
- A line number array (`lines`) for runtime error anchoring.
- A constant pool (`constants`) storing large/complex values (numbers, strings, compiled functions, classes).

---

## The Virtual Machine & Call Frames

The VM is a stack-based runtime:
* **The Stack**: All temporary expression values, method parameters, and local variables are stored in a contiguous array of `RuntimeValue` structures.
* **Call Frames**: Each call frame (`CallFrame` in `src/vm.hpp`) tracks the execution state of a function or method:
  - `function`: A pointer to the `CompiledFunction` being run.
  - `ip`: The instruction pointer tracking the current instruction.
  - `slotsBase`: The stack index where the frame's arguments and local variables begin.
* **Re-entrant Execution**: Invocations originating from native standard library functions or interpreter boundaries trigger a recursive call to `VM::run()`. The VM tracks `runInitialFrameCount` to ensure return instructions pop back to the correct execution depth without overflowing or terminating outer frames prematurely.

---

## Stack and Scope Management

One of the most critical parts of the bytecode VM's correctness is the alignment of compile-time local offsets with the runtime stack.

### Implicit Stack Offsets
Locals reside directly on the stack. The compiler tracks the position of every declared variable:
- When a new variable is assigned inside a function body, the compiled RHS value is pushed onto the stack and naturally serves as the local variable's stack slot.
- The compiler maps the variable's name to this stack slot index, which is resolved at runtime via `OP_GET_LOCAL` / `OP_SET_LOCAL` relative to the current call frame's `slotsBase`.

### Lexical Scope Scoping
To maintain stack symmetry across loop iterations and conditional branches:
* **Loop Bodies**: `FOR`, `WHILE`, and `REPEAT-UNTIL` bodies are wrapped in a nested compiler scope (`beginScope()` and `endScope()`). Any local variables defined within the loop body are popped from the stack at the end of each iteration to prevent the stack from growing indefinitely.
* **Conditional Branches**: `IF`, `ELSE-IF`, and `ELSE` branches are similarly wrapped in scopes. This ensures that variables declared inside a branch are cleaned up at the end of that branch, and no unbalanced pops occur if a branch is not taken.
