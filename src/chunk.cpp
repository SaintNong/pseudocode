#include "chunk.hpp"

void Chunk::write(uint8_t byte, size_t line) {
    const size_t previous = retainedBytes();
    code.push_back(byte);
    lines.push_back(line);
    accountGrowth(previous);
}

size_t Chunk::addConstant(RuntimeValue value) {
    const size_t previous = retainedBytes();
    constants.push_back(value);
    accountGrowth(previous);
    return constants.size() - 1;
}

size_t Chunk::getLine(size_t offset) const {
    if (offset < lines.size()) {
        return lines[offset];
    }
    return 0;
}
