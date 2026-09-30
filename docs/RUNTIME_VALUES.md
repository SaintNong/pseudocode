# NaN-boxed runtime and garbage collector

This branch replaces both the original `std::variant` representation and the
compact-value prototype. Every `RuntimeValue` is a trivially copyable 64-bit word.
The wrapper's `is<T>()` and `as<T>()` interface remains, but `Value` now encodes
types and payloads directly. Scalars and pointers are returned by value; strings
are exposed as immutable references.

On the measured x86-64/GCC build, the original value occupied 40 bytes, the
previous compact prototype occupied 24 bytes, and this value occupies 8 bytes.
The VM's 262,144 value slots consequently occupy 2 MiB instead of 10 MiB or 6 MiB.
Those figures describe slots, not total process memory.

## Encoding

Ordinary IEEE-754 doubles retain their bits, including infinities, subnormals,
and negative zero. Numeric NaNs are canonicalized to
`0x7ff8000000000000` with their original sign preserved (so negative NaNs
still stringify as `-nan`). Seven other positive quiet-NaN prefixes encode the
remaining types:

| Upper 16 bits | Type | Lower 48 bits |
| --- | --- | --- |
| `7ff9` | Null / boolean | 0 = Null, 1 = false, 2 = true |
| `7ffa` | Integer | Original signed 32-bit integer bits |
| `7ffb` | String | Pointer to an immutable HeapString |
| `7ffc` | Array | Pointer to Array |
| `7ffd` | Dictionary | Pointer to Dictionary |
| `7ffe` | Function / class | Pointer to Callable |
| `7fff` | Instance | Pointer to Instance |

The language continues to distinguish integers, floats, booleans and Null.
Strings compare by contents; collections, callables and instances retain their
identity comparison and aliasing rules. Class inheritance, superclass views,
bound methods, higher-order functions, mutable defaults and ordered dictionary
keys retain their language semantics. Strings can share storage safely because
the payload is immutable.

This implementation requires a 64-bit platform, 32-bit C++ `int`, IEEE-754
binary64, and object addresses that fit in 48 bits. Compile-time assertions check
the numeric and slot sizes; pointer encoding checks each address and rejects an
unrepresentable one instead of truncating it. It has been tested on x86-64 Linux.
Other supported platforms need validation before adopting this branch.
NaN payloads and signaling status are not preserved; the language has no API
exposing either.

## Ownership and tracing

All runtime objects, environments, instance-field maps, compiled functions and
bytecode chunks belong to the interpreter's heap. Their pointers are non-owning.
The source contains no `shared_ptr` or `make_shared`; AST and VM ownership still
use `unique_ptr` where their lifetimes are structural.

The collector is non-moving, stop-the-world mark-and-sweep:

1. Mark interpreter globals/current environment, live VM slots and active frames.
2. Mark explicit C++ roots held across nested VM execution.
3. Trace object edges using an iterative gray stack, including constants in
   chunks, array/dictionary values, environments, class methods/defaults,
   instance fields/class contexts, and user-function closures.
4. Delete unreachable objects, reset surviving mark bits, and update the budget.

Native property functions explicitly retain their receivers in a traced
capture list. A lambda capture alone is invisible to the collector. Constructor
instances and argument vectors are rooted while field initializers and
constructors re-enter the VM. Error unwinding restores the live stack and frame
counts, so a failed REPL execution cannot retain stale roots.

Collection happens at bytecode instruction boundaries. Allocations made while
an instruction or the compiler is holding C++ temporaries never collect.
Collection is requested after roughly 1 MiB of newly allocated objects by
default; after a sweep the allowance grows with the estimated live heap.
Container capacity changes and allocator bookkeeping are not fully accounted
for, so this is an allocation budget, not a strict memory limit.
Compilation and individual native operations can temporarily exceed it.
Every remaining allocation is destroyed when its interpreter is destroyed.

The active heap is scoped per thread and restored when a nested interpreter
is destroyed. Objects must not be exchanged between interpreter heaps.
The interpreter and collector are not designed for concurrent execution.

## Debugging and extension rules

- Allocate managed objects with `gcNew<T>()` / `heap.make<T>()`.
- Implement `trace()` for every type that contains managed edges.
- Use `GCRoot` for C++ temporaries that survive a call which can execute bytecode.
- Call `NativeFunction::capture()` for managed receivers captured by native lambdas.
- Do not add allocation-time collection without rooting compiler/instruction
  temporaries first.

`SCSA_GC_STRESS=1` forces collection at every instruction boundary.
`SCSA_GC_THRESHOLD=<bytes>` changes the minimum allocation budget.
`SCSA_GC_STATS=1` prints allocation, collection, reclamation and live-object
counts to stderr at shutdown. `last_live_bytes` is an estimate from the last
sweep, and `reclaimed` excludes objects destroyed during shutdown.

Tests are written in SCSA and Python. CTest runs the full integration suite both
normally and under GC stress. Language tests cover signed integer limits, NaNs
and negative NaNs, infinities, negative zero, distinct dictionary key types and
native captures. Python subprocess checks use collector diagnostics to verify
cyclic reclamation, survival and reclamation of a 100,000-edge graph, and REPL
recovery after repeated VM errors. Slot-size and numeric-format requirements are
compile-time assertions in the implementation. The branch is also checked with
AddressSanitizer, UndefinedBehaviorSanitizer and leak detection.

This collector favors a small, inspectable implementation. It has no generations,
compaction, concurrent marking or arena allocator. The benchmarks measure the
combined representation/ownership/collector change; they do not isolate NaN
boxing's contribution from removing reference counting.
