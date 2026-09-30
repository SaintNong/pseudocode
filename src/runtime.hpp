#pragma once

#include "gc.hpp"
#include "token.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

// --- Forward Declarations ---
struct Callable;
struct Instance;
struct Dictionary;
class Interpreter;
struct Chunk;
class UserClass;
class UserFunction;

struct CompiledFunction : HeapObject {
    Chunk *chunk = nullptr;
    void trace(GarbageCollector &gc) override;
    int arity;
    std::string name;
};

/**
 * Null Type
 * Represents the absence of a value in Pseudocode.
 */
using Null = std::monostate;

// --- Runtime Value System ---

struct Array;
using ArrayPtr    = Array *;
using DictPtr     = Dictionary *;
using CallablePtr = Callable *;
using InstancePtr = Instance *;

struct HeapString final : HeapObject {
    const std::string text;
    explicit HeapString(std::string s) : text(std::move(s)) {
    }
    size_t retainedBytes() const override {
        return allocationSize + text.capacity();
    }
};

// IEEE-754 binary64. Seven positive quiet-NaN prefixes encode non-double values.
// Numeric NaNs use 0x7ff8000000000000 with their original sign, outside the tags.
// Object pointers must fit in the low 48 bits; validate before encoding.
class Value {
    static constexpr uint64_t MASK     = 0xffff000000000000ULL;
    static constexpr uint64_t PAYLOAD  = 0x0000ffffffffffffULL;
    static constexpr uint64_t SPECIAL  = 0x7ff9000000000000ULL;
    static constexpr uint64_t INTEGER  = 0x7ffa000000000000ULL;
    static constexpr uint64_t STRING   = 0x7ffb000000000000ULL;
    static constexpr uint64_t ARRAY    = 0x7ffc000000000000ULL;
    static constexpr uint64_t DICT     = 0x7ffd000000000000ULL;
    static constexpr uint64_t CALLABLE = 0x7ffe000000000000ULL;
    static constexpr uint64_t INSTANCE = 0x7fff000000000000ULL;
    uint64_t bits                      = SPECIAL;

    void encodePointer(uint64_t tag, const void *pointer) {
        const auto address = reinterpret_cast<uintptr_t>(pointer);
        if (address & ~PAYLOAD)
            throw std::runtime_error("NaN boxing requires pointers that fit in 48 bits");
        bits = tag | address;
    }

public:
    Value() = default;
    Value(Null) : bits(SPECIAL) {
    }
    Value(bool b) : bits(SPECIAL | (b ? 2 : 1)) {
    }
    Value(int n) : bits(INTEGER | static_cast<uint32_t>(n)) {
    }
    Value(double d) {
        if (std::isnan(d))
            bits = 0x7ff8000000000000ULL | (std::signbit(d) ? 0x8000000000000000ULL : 0);
        else
            std::memcpy(&bits, &d, sizeof(bits));
    }
    Value(std::string s) {
        encodePointer(STRING, gcNew<HeapString>(std::move(s)));
    }
    Value(const char *s) : Value(std::string(s)) {
    }
    Value(ArrayPtr a) {
        encodePointer(ARRAY, a);
    }
    Value(DictPtr d) {
        encodePointer(DICT, d);
    }
    Value(CallablePtr c) {
        encodePointer(CALLABLE, c);
    }
    Value(InstancePtr i) {
        encodePointer(INSTANCE, i);
    }

    template <typename T> bool holds() const {
        const uint64_t tag = bits & MASK;
        if constexpr (std::is_same_v<T, Null>)
            return bits == SPECIAL;
        else if constexpr (std::is_same_v<T, bool>)
            return tag == SPECIAL && bits != SPECIAL;
        else if constexpr (std::is_same_v<T, int>)
            return tag == INTEGER;
        else if constexpr (std::is_same_v<T, double>)
            return (bits & 0x7fff000000000000ULL) <= 0x7ff8000000000000ULL;
        else if constexpr (std::is_same_v<T, std::string>)
            return tag == STRING;
        else if constexpr (std::is_same_v<T, ArrayPtr>)
            return tag == ARRAY;
        else if constexpr (std::is_same_v<T, DictPtr>)
            return tag == DICT;
        else if constexpr (std::is_same_v<T, CallablePtr>)
            return tag == CALLABLE;
        else if constexpr (std::is_same_v<T, InstancePtr>)
            return tag == INSTANCE;
        else
            static_assert(!sizeof(T), "Unsupported value type");
    }

    template <typename T> decltype(auto) get() const {
        if (!holds<T>())
            throw std::bad_variant_access();
        if constexpr (std::is_same_v<T, Null>)
            return Null{};
        else if constexpr (std::is_same_v<T, bool>)
            return bits == (SPECIAL | 2);
        else if constexpr (std::is_same_v<T, int>) {
            const uint32_t payload = static_cast<uint32_t>(bits);
            int32_t result;
            std::memcpy(&result, &payload, sizeof(result));
            return static_cast<int>(result);
        } else if constexpr (std::is_same_v<T, double>) {
            double result;
            std::memcpy(&result, &bits, sizeof(result));
            return result;
        } else if constexpr (std::is_same_v<T, std::string>)
            return (reinterpret_cast<HeapString *>(bits & PAYLOAD)->text);
        else
            return reinterpret_cast<T>(bits & PAYLOAD);
    }
    size_t index() const {
        if (holds<Null>())
            return 0;
        if (holds<int>())
            return 1;
        if (holds<double>())
            return 2;
        if (holds<bool>())
            return 3;
        return 4 + ((bits & MASK) - STRING) / (uint64_t{1} << 48);
    }
    HeapObject *heapObject() const;
};

struct RuntimeValue {
    Value value;
    template <typename T> bool is() const {
        return value.holds<T>();
    }
    template <typename T> decltype(auto) as() const {
        return value.get<T>();
    }
    void trace(GarbageCollector &gc) const {
        gc.mark(value.heapObject());
    }
};
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
static_assert(sizeof(int) == 4 && sizeof(void *) == 8);
static_assert(sizeof(RuntimeValue) == 8, "Every runtime slot must fit in one word");
static_assert(std::is_trivially_copyable_v<RuntimeValue>);

struct Array final : HeapObject, std::vector<RuntimeValue> {
    void trace(GarbageCollector &gc) override {
        for (const auto &value : *this)
            value.trace(gc);
    }
    size_t retainedBytes() const override {
        return allocationSize + capacity() * sizeof(RuntimeValue);
    }
};

struct Fields final : HeapObject, std::map<std::string, RuntimeValue> {
    void trace(GarbageCollector &gc) override {
        for (const auto &entry : *this)
            entry.second.trace(gc);
    }
};

using DictKey = std::variant<int, bool, std::string>;

inline DictKey toDictKey(const RuntimeValue &val) {
    if (val.is<int>())
        return val.as<int>();
    if (val.is<bool>())
        return val.as<bool>();
    if (val.is<std::string>())
        return val.as<std::string>();
    throw std::logic_error("Internal error: invalid dictionary key type in toDictKey");
}

inline RuntimeValue fromDictKey(const DictKey &key) {
    RuntimeValue rv;
    if (std::holds_alternative<int>(key)) {
        rv.value = std::get<int>(key);
    } else if (std::holds_alternative<bool>(key)) {
        rv.value = std::get<bool>(key);
    } else {
        rv.value = std::get<std::string>(key);
    }
    return rv;
}

/**
 * Dictionary Type
 * An ordered collection of key-value pairs. Keys must be strings, integers, or booleans.
 */
struct Dictionary : HeapObject {
    void trace(GarbageCollector &gc) override {
        for (const auto &entry : entries)
            entry.second.trace(gc);
    }
    size_t retainedBytes() const override {
        return allocationSize + keys.capacity() * sizeof(DictKey) +
               entries.size() * (sizeof(DictKey) + sizeof(RuntimeValue) + 2 * sizeof(void *));
    }
    std::vector<DictKey> keys;
    std::unordered_map<DictKey, RuntimeValue> entries;
};

inline bool isValidDictKey(const RuntimeValue &key) {
    return key.is<int>() || key.is<std::string>() || key.is<bool>();
}

inline void setDictEntry(Dictionary &dict, const RuntimeValue &key, const RuntimeValue &value) {
    DictKey dk = toDictKey(key);
    if (dict.entries.find(dk) == dict.entries.end()) {
        dict.keys.push_back(dk);
    }
    dict.entries[dk] = value;
}

// --- Execution Exceptions ---

/**
 * RuntimeError
 * Exception thrown when a terminal error occurs during script execution.
 * Captures the problematic token for high-quality error reporting.
 */
class RuntimeError : public std::runtime_error {
public:
    // The token where the error occurred (stored by value to avoid dangling references)
    const Token token;

    /**
     * Create a new runtime error
     * @param token The token associated with the error
     * @param message Descriptive error message
     */
    RuntimeError(const Token &token, const std::string &message)
        : std::runtime_error(message), token(token) {
    }
};

// --- Scope Management ---

/**
 * Environment
 * Manages variable bindings and scope nesting.
 * Each environment holds a map of identifiers to values and points to its parent scope.
 */
class Environment : public HeapObject {
    // Variable storage for the current scope
    std::map<std::string, RuntimeValue> values;
    // Pointer to the surrounding (outer) scope
    Environment *enclosing;

public:
    /**
     * Create a top-level (global) environment
     */
    void trace(GarbageCollector &gc) override {
        gc.mark(enclosing);
        for (const auto &entry : values)
            entry.second.trace(gc);
    }
    Environment() : enclosing(nullptr) {
    }

    /**
     * Create a local environment nested within another
     * @param enclosing The parent environment
     */
    Environment(Environment *enclosing) : enclosing(enclosing) {
    }

    /**
     * Define or update a variable in the current scope
     * @param name The identifier name
     * @param value The value to bind
     */
    void define(const std::string &name, RuntimeValue value) {
        values[name] = value;
    }

    /**
     * Retrieve a variable's value, searching up the scope chain
     * @param name The token representing the variable name
     * @return The found RuntimeValue
     * @throws RuntimeError if the variable is not defined in any accessible scope
     */
    RuntimeValue get(const Token &name) {
        if (values.count(name.lexeme)) {
            return values.at(name.lexeme);
        }
        if (enclosing)
            return enclosing->get(name);

        throw RuntimeError(name, "Undefined variable '" + name.lexeme + "'.");
    }
};

using EnvironmentPtr = Environment *;

// --- Core Runtime Interfaces ---

/**
 * Callable Interface
 * Represents anything that can be "called" like a function or constructor.
 */
struct Callable : HeapObject {
    virtual ~Callable() = default;

    /**
     * @return The number of arguments the callable expects
     */
    virtual int arity() = 0;

    /**
     * Invoke the callable
     * @param interpreter The active interpreter instance
     * @param arguments List of evaluated runtime arguments
     * @return The result of the invocation
     */
    virtual RuntimeValue call(Interpreter &interpreter, std::vector<RuntimeValue> arguments) = 0;

    /**
     * @return A string representation of the callable (e.g. <FUNCTION name>)
     */
    virtual std::string toString() = 0;
};

/**
 * Instance Structure
 * Represents a concrete instance of a user-defined class.
 * Holds its own state (fields) and maintains a link to its class definition.
 */
struct Instance : HeapObject {
    void trace(GarbageCollector &gc) override {
        gc.mark(klass);
        gc.mark(fields);
        gc.mark(superclassContext);
    }
    // Reference to the class that created this instance
    Callable *klass;
    // Instance-specific field storage (shared between views)
    Fields *fields;
    // Optional superclass override for 'super' lookups
    Callable *superclassContext;

    /**
     * Create a new instance of a class
     * @param k GC-managed class definition
     */
    Instance(Callable *k) : klass(k), fields(gcNew<Fields>()), superclassContext(nullptr) {
    }

    /**
     * Create a new view of an instance with a different class context (for super)
     * @param other The instance to copy from
     * @param context The superclass context to use
     */
    Instance(const Instance &other, Callable *context)
        : klass(other.klass), fields(other.fields), superclassContext(context) {
    }

    /**
     * Access a property on this instance
     * @param name The token representing the property name
     * @return The value of the property
     * @throws RuntimeError if the property does not exist
     */
    RuntimeValue get(const Token &name) {
        if (fields->count(name.lexeme)) {
            return fields->at(name.lexeme);
        }
        throw RuntimeError(name, "Undefined property '" + name.lexeme + "'.");
    }

    /**
     * Set the value of a property on this instance
     * @param name The token representing the property name
     * @param value The new value to assign
     */
    void set(const Token &name, RuntimeValue value) {
        (*fields)[name.lexeme] = value;
    }
};

class UserFunction : public Callable {
    CompiledFunction *compiledFn;
    Environment *closure;
    UserClass *definingClass;

public:
    void trace(GarbageCollector &gc) override;
    UserFunction(CompiledFunction *compiledFn, Environment *closure,
                 UserClass *definingClass = nullptr);

    UserFunction *bind(Instance *instance);
    int arity() override;
    RuntimeValue call(Interpreter &interpreter, std::vector<RuntimeValue> arguments) override;
    std::string toString() override;
    CompiledFunction *getCompiledFunction() const {
        return compiledFn;
    }
    Environment *getClosure() const {
        return closure;
    }
};

typedef std::function<RuntimeValue(Interpreter &, std::vector<RuntimeValue>)> NativeFn;

class NativeFunction : public Callable {
    NativeFn function;
    std::vector<RuntimeValue> captures;
    int _arity;

public:
    void capture(RuntimeValue value) {
        captures.push_back(value);
    }
    void trace(GarbageCollector &gc) override {
        for (const auto &value : captures)
            value.trace(gc);
    }
    NativeFunction(int arity, NativeFn function);
    int arity() override;
    RuntimeValue call(Interpreter &interpreter, std::vector<RuntimeValue> arguments) override;
    std::string toString() override;
};

class UserClass : public Callable {
    std::string name;
    UserClass *superclass;
    std::map<std::string, Callable *> methods;
    std::map<std::string, CompiledFunction *> defaultFields;

public:
    void trace(GarbageCollector &gc) override;
    UserClass(const std::string &n, UserClass *s = nullptr);
    UserClass *getSuperclass() const;
    void setSuperclass(UserClass *s) {
        superclass = s;
    }
    std::string getName() const;
    void addMethod(const std::string &methodName, Callable *method);
    void addField(const std::string &fieldName, CompiledFunction *valueFunc);
    UserFunction *findMethod(const std::string &methodName);
    UserFunction *findConstructor();
    int arity() override;
    RuntimeValue call(Interpreter &interpreter, std::vector<RuntimeValue> arguments) override;
    void initializeFields(Interpreter &interpreter, Instance *instance);
    std::string toString() override;
};

/**
 * Stringify Utility
 * Converts any RuntimeValue into its string representation for display.
 * Handles recursion for collections like arrays.
 * @param v The runtime value to stringify
 * @return String representation of the value
 */
inline std::string stringify(const RuntimeValue &v) {
    // Handle Null
    if (v.is<Null>())
        return "Null";

    // Handle Integers
    if (v.is<int>()) {
        return std::to_string(v.as<int>());
    }

    // Handle Doubles (with trailing zero trimming)
    if (v.is<double>()) {
        std::string text = std::to_string(v.as<double>());
        text.erase(text.find_last_not_of('0') + 1, std::string::npos);
        if (text.back() == '.')
            text.pop_back();
        return text;
    }

    // Handle Booleans
    if (v.is<bool>())
        return v.as<bool>() ? "true" : "false";

    // Handle Strings
    if (v.is<std::string>())
        return v.as<std::string>();

    // Handle Arrays (Recursive)
    if (v.is<ArrayPtr>()) {
        auto arr           = v.as<ArrayPtr>();
        std::string result = "[";
        for (size_t i = 0; i < arr->size(); ++i) {
            result += stringify((*arr)[i]);
            if (i < arr->size() - 1)
                result += ", ";
        }
        result += "]";
        return result;
    }

    // Handle Dictionaries (Recursive)
    if (v.is<Dictionary *>()) {
        auto dict          = v.as<Dictionary *>();
        std::string result = "{";
        for (size_t i = 0; i < dict->keys.size(); ++i) {
            DictKey k = dict->keys[i];
            result += stringify(fromDictKey(k));
            result += ": ";
            result += stringify(dict->entries.at(k));
            if (i < dict->keys.size() - 1)
                result += ", ";
        }
        result += "}";
        return result;
    }

    // Handle Callables (Functions/Classes)
    if (v.is<Callable *>())
        return v.as<Callable *>()->toString();

    // Handle Class Instances
    if (v.is<Instance *>())
        return v.as<Instance *>()->klass->toString();

    return "unknown";
}

inline HeapObject *Value::heapObject() const {
    if (holds<std::string>())
        return reinterpret_cast<HeapString *>(bits & PAYLOAD);
    if (holds<ArrayPtr>())
        return get<ArrayPtr>();
    if (holds<DictPtr>())
        return get<DictPtr>();
    if (holds<CallablePtr>())
        return get<CallablePtr>();
    if (holds<InstancePtr>())
        return get<InstancePtr>();
    return nullptr;
}
