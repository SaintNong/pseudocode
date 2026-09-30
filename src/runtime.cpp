#include "runtime.hpp"
#include "interpreter.hpp"

void CompiledFunction::trace(GarbageCollector &gc) {
    gc.mark(chunk);
}

void UserFunction::trace(GarbageCollector &gc) {
    gc.mark(compiledFn);
    gc.mark(closure);
    gc.mark(definingClass);
}

void UserClass::trace(GarbageCollector &gc) {
    gc.mark(superclass);
    for (const auto &entry : methods)
        gc.mark(entry.second);
    for (const auto &entry : defaultFields)
        gc.mark(entry.second);
}

UserFunction::UserFunction(CompiledFunction *compiledFn, Environment *closure,
                           UserClass *definingClass)
    : compiledFn(compiledFn), closure(closure), definingClass(definingClass) {
}

UserFunction *UserFunction::bind(Instance *instance) {
    auto environment = gcNew<Environment>(closure);
    RuntimeValue instanceValue;
    if (instance->superclassContext == nullptr) {
        auto boundInstance  = gcNew<Instance>(*instance, definingClass);
        instanceValue.value = boundInstance;
    } else {
        instanceValue.value = instance;
    }
    environment->define("this", instanceValue);
    return gcNew<UserFunction>(compiledFn, environment, definingClass);
}

int UserFunction::arity() {
    return compiledFn->arity;
}

RuntimeValue UserFunction::call(Interpreter &interpreter, std::vector<RuntimeValue> arguments) {
    return interpreter.runFunction(compiledFn, arguments, closure);
}

std::string UserFunction::toString() {
    return "<FUNCTION " + compiledFn->name + ">";
}

NativeFunction::NativeFunction(int arity, NativeFn function) : function(function), _arity(arity) {
}

int NativeFunction::arity() {
    return _arity;
}

RuntimeValue NativeFunction::call(Interpreter &interpreter, std::vector<RuntimeValue> arguments) {
    return function(interpreter, arguments);
}

std::string NativeFunction::toString() {
    return "<NATIVE FUNCTION>";
}

UserClass::UserClass(const std::string &n, UserClass *s) : name(n), superclass(s) {
}

UserClass *UserClass::getSuperclass() const {
    return superclass;
}

std::string UserClass::getName() const {
    return name;
}

void UserClass::addMethod(const std::string &methodName, Callable *method) {
    methods[methodName] = method;
}

void UserClass::addField(const std::string &fieldName, CompiledFunction *valueFunc) {
    defaultFields[fieldName] = valueFunc;
}

UserFunction *UserClass::findMethod(const std::string &methodName) {
    if (methods.count(methodName)) {
        return dynamic_cast<UserFunction *>(methods.at(methodName));
    }
    if (superclass != nullptr) {
        return superclass->findMethod(methodName);
    }
    return nullptr;
}

UserFunction *UserClass::findConstructor() {
    UserFunction *constructor = findMethod(name);
    if (constructor != nullptr)
        return constructor;
    if (superclass != nullptr) {
        return superclass->findConstructor();
    }
    return nullptr;
}

int UserClass::arity() {
    auto constructor = findConstructor();
    if (constructor != nullptr)
        return constructor->arity();
    return 0;
}

RuntimeValue UserClass::call(Interpreter &interpreter, std::vector<RuntimeValue> arguments) {
    auto instance = gcNew<Instance>(static_cast<Callable *>(this));
    GCRoot root([&](GarbageCollector &gc) {
        gc.mark(this);
        gc.mark(instance);
        for (const auto &argument : arguments)
            argument.trace(gc);
    });
    initializeFields(interpreter, instance);
    auto constructor = findConstructor();
    if (constructor != nullptr) {
        constructor->bind(instance)->call(interpreter, arguments);
    }
    return RuntimeValue{instance};
}

void UserClass::initializeFields(Interpreter &interpreter, Instance *instance) {
    if (superclass != nullptr) {
        superclass->initializeFields(interpreter, instance);
    }
    for (const auto &[key, func] : defaultFields) {
        (*instance->fields)[key] = interpreter.runFunction(func, {}, nullptr);
    }
}

std::string UserClass::toString() {
    return "<CLASS " + name + ">";
}
