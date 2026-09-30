#pragma once

#include "token.hpp"
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
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

struct CompiledFunction {
    std::shared_ptr<Chunk> chunk;
    int arity;
    std::string name;
};

/**
 * Null Type
 * Represents the absence of a value in Pseudocode.
 */
using Null = std::monostate;

// --- Runtime Value System ---

using ArrayPtr = std::shared_ptr<std::vector<struct RuntimeValue>>;
using CallablePtr = std::shared_ptr<Callable>;
using InstancePtr = std::shared_ptr<Instance>;

/**
 * Compact value storage: primitives are inline, while all heap-backed values
 * share a single pointer-sized alternative. RuntimeValue retains the same
 * typed access interface so the VM and language semantics stay unchanged.
 */
class Value {
public:
    using HeapPayload = std::variant<std::string, ArrayPtr, std::shared_ptr<Dictionary>,
                                     CallablePtr, InstancePtr>;

private:
    using HeapPtr = std::shared_ptr<HeapPayload>;
    using Storage = std::variant<Null, int, double, bool, HeapPtr>;
    Storage storage{Null{}};

    template <typename T> static constexpr bool isHeapType =
        std::is_same_v<T, std::string> || std::is_same_v<T, ArrayPtr> ||
        std::is_same_v<T, std::shared_ptr<Dictionary>> || std::is_same_v<T, CallablePtr> ||
        std::is_same_v<T, InstancePtr>;

    template <typename T> void assign(T &&input) {
        using U = std::decay_t<T>;
        if constexpr (std::is_same_v<U, Null> || std::is_same_v<U, int> ||
                      std::is_same_v<U, double> || std::is_same_v<U, bool>) {
            storage = std::forward<T>(input);
        } else if constexpr (std::is_same_v<U, std::string> ||
                             std::is_convertible_v<T, std::string>) {
            storage = std::make_shared<HeapPayload>(
                std::in_place_type<std::string>, std::string(std::forward<T>(input)));
        } else if constexpr (std::is_same_v<U, ArrayPtr> ||
                             std::is_same_v<U, std::shared_ptr<Dictionary>> ||
                             std::is_same_v<U, InstancePtr> ||
                             std::is_convertible_v<T, CallablePtr>) {
            using HeapType = std::conditional_t<std::is_convertible_v<T, CallablePtr>,
                                                CallablePtr, U>;
            storage = std::make_shared<HeapPayload>(
                std::in_place_type<HeapType>, HeapType(std::forward<T>(input)));
        } else {
            static_assert(!sizeof(U), "Unsupported RuntimeValue alternative");
        }
    }

public:
    Value() = default;
    Value(const Value &) = default;
    Value(Value &&) noexcept = default;
    Value &operator=(const Value &) = default;
    Value &operator=(Value &&) noexcept = default;

    template <typename T,
              typename U = std::decay_t<T>,
              std::enable_if_t<!std::is_same_v<U, Value>, int> = 0>
    Value(T &&input) {
        assign(std::forward<T>(input));
    }

    template <typename T,
              typename U = std::decay_t<T>,
              std::enable_if_t<!std::is_same_v<U, Value>, int> = 0>
    Value &operator=(T &&input) {
        assign(std::forward<T>(input));
        return *this;
    }

    size_t index() const {
        if (storage.index() != 4)
            return storage.index();
        return std::get<HeapPtr>(storage)->index() + 4;
    }

    template <typename T> bool holds() const {
        if constexpr (isHeapType<T>) {
            return storage.index() == 4 &&
                   std::holds_alternative<T>(*std::get<HeapPtr>(storage));
        } else {
            return std::holds_alternative<T>(storage);
        }
    }

    template <typename T> T &get() {
        if constexpr (isHeapType<T>)
            return std::get<T>(*std::get<HeapPtr>(storage));
        else
            return std::get<T>(storage);
    }

    template <typename T> const T &get() const {
        if constexpr (isHeapType<T>)
            return std::get<T>(*std::get<HeapPtr>(storage));
        else
            return std::get<T>(storage);
    }
};

/**
 * RuntimeValue Wrapper
 * A thin wrapper around the Value variant.
 * Provides convenience methods for type checking and safe casting.
 */
struct RuntimeValue {
    Value value;

    /**
     * Check if the value is of a specific type
     * @tparam T The type to check for
     * @return true if the variant holds type T
     */
    template <typename T> bool is() const {
        return value.template holds<T>();
    }

    /**
     * Cast the value to a specific type (const)
     * @tparam T The type to cast to
     * @return Const reference to the value as type T
     */
    template <typename T> const T &as() const {
        return value.template get<T>();
    }

    /**
     * Cast the value to a specific type (mutable)
     * @tparam T The type to cast to
     * @return Reference to the value as type T
     */
    template <typename T> T &as() {
        return value.template get<T>();
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
struct Dictionary {
    std::vector<DictKey> keys;
    std::unordered_map<DictKey, RuntimeValue> entries;
};

using DictPtr = std::shared_ptr<Dictionary>;

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
class Environment : public std::enable_shared_from_this<Environment> {
    // Variable storage for the current scope
    std::map<std::string, RuntimeValue> values;
    // Pointer to the surrounding (outer) scope
    std::shared_ptr<Environment> enclosing;

public:
    /**
     * Create a top-level (global) environment
     */
    Environment() : enclosing(nullptr) {
    }

    /**
     * Create a local environment nested within another
     * @param enclosing The parent environment
     */
    Environment(std::shared_ptr<Environment> enclosing) : enclosing(enclosing) {
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

using EnvironmentPtr = std::shared_ptr<Environment>;

// --- Core Runtime Interfaces ---

/**
 * Callable Interface
 * Represents anything that can be "called" like a function or constructor.
 */
struct Callable {
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
struct Instance {
    // Reference to the class that created this instance
    std::shared_ptr<Callable> klass;
    // Instance-specific field storage (shared between views)
    std::shared_ptr<std::map<std::string, RuntimeValue>> fields;
    // Optional superclass override for 'super' lookups
    std::shared_ptr<Callable> superclassContext;

    /**
     * Create a new instance of a class
     * @param k Shared pointer to the class definition
     */
    Instance(std::shared_ptr<Callable> k)
        : klass(k), fields(std::make_shared<std::map<std::string, RuntimeValue>>()),
          superclassContext(nullptr) {
    }

    /**
     * Create a new view of an instance with a different class context (for super)
     * @param other The instance to copy from
     * @param context The superclass context to use
     */
    Instance(const Instance &other, std::shared_ptr<Callable> context)
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
    std::shared_ptr<CompiledFunction> compiledFn;
    std::shared_ptr<Environment> closure;
    std::shared_ptr<UserClass> definingClass;

public:
    UserFunction(std::shared_ptr<CompiledFunction> compiledFn, std::shared_ptr<Environment> closure,
                 std::shared_ptr<UserClass> definingClass = nullptr);

    std::shared_ptr<UserFunction> bind(std::shared_ptr<Instance> instance);
    int arity() override;
    RuntimeValue call(Interpreter &interpreter, std::vector<RuntimeValue> arguments) override;
    std::string toString() override;
    std::shared_ptr<CompiledFunction> getCompiledFunction() const {
        return compiledFn;
    }
    std::shared_ptr<Environment> getClosure() const {
        return closure;
    }
};

typedef std::function<RuntimeValue(Interpreter &, std::vector<RuntimeValue>)> NativeFn;

class NativeFunction : public Callable {
    NativeFn function;
    int _arity;

public:
    NativeFunction(int arity, NativeFn function);
    int arity() override;
    RuntimeValue call(Interpreter &interpreter, std::vector<RuntimeValue> arguments) override;
    std::string toString() override;
};

class UserClass : public Callable, public std::enable_shared_from_this<UserClass> {
    std::string name;
    std::shared_ptr<UserClass> superclass;
    std::map<std::string, std::shared_ptr<Callable>> methods;
    std::map<std::string, std::shared_ptr<CompiledFunction>> defaultFields;

public:
    UserClass(const std::string &n, std::shared_ptr<UserClass> s = nullptr);
    std::shared_ptr<UserClass> getSuperclass() const;
    void setSuperclass(std::shared_ptr<UserClass> s) {
        superclass = s;
    }
    std::string getName() const;
    void addMethod(const std::string &methodName, std::shared_ptr<Callable> method);
    void addField(const std::string &fieldName, std::shared_ptr<CompiledFunction> valueFunc);
    std::shared_ptr<UserFunction> findMethod(const std::string &methodName);
    std::shared_ptr<UserFunction> findConstructor();
    int arity() override;
    RuntimeValue call(Interpreter &interpreter, std::vector<RuntimeValue> arguments) override;
    void initializeFields(Interpreter &interpreter, std::shared_ptr<Instance> instance);
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
    if (v.is<std::shared_ptr<std::vector<RuntimeValue>>>()) {
        auto arr           = v.as<std::shared_ptr<std::vector<RuntimeValue>>>();
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
    if (v.is<std::shared_ptr<Dictionary>>()) {
        auto dict          = v.as<std::shared_ptr<Dictionary>>();
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
    if (v.is<std::shared_ptr<Callable>>())
        return v.as<std::shared_ptr<Callable>>()->toString();

    // Handle Class Instances
    if (v.is<std::shared_ptr<Instance>>())
        return v.as<std::shared_ptr<Instance>>()->klass->toString();

    return "unknown";
}
