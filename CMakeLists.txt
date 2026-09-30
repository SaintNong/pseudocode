# =========================================================
# CMake configuration
# =========================================================
cmake_minimum_required(VERSION 3.15)
project(scsa LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

# =========================================================
# Find Sources
# =========================================================
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.hpp"
)

# =========================================================
# Embed version info into binary
# =========================================================
set(VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp")

add_custom_target(generate_version
    COMMAND ${CMAKE_COMMAND} 
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} 
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR} 
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GetGitVersion.cmake"
    BYPRODUCTS "${VERSION_HEADER}"
    COMMENT "Updated Git version info"
)

# =========================================================
# Add Executable
# =========================================================
add_executable(scsa ${SOURCES})

# Make sure we wait until after version-info is generated
add_dependencies(scsa generate_version)
target_include_directories(scsa PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

# =========================================================
# Configure compiler and flags
# =========================================================
if(MSVC)
    target_compile_options(scsa PRIVATE /W4)
else()
    target_compile_options(scsa PRIVATE -Wall -Wextra -Werror)
endif()

# =========================================================
# Code Coverage
# =========================================================
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(scsa PRIVATE --coverage)
        target_link_options(scsa PRIVATE --coverage)
    endif()
endif()


# =========================================================
# Integration tests
# =========================================================
add_test(
    NAME integration_tests
    COMMAND python tester.py $<TARGET_FILE:scsa>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

add_test(NAME gc_checks
    COMMAND python gc_checks.py $<TARGET_FILE:scsa>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
add_test(NAME gc_stress_integration
    COMMAND ${CMAKE_COMMAND} -E env SCSA_GC_STRESS=1 python tester.py $<TARGET_FILE:scsa>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# =========================================================
# clang-format targets
# =========================================================
find_program(CLANG_FORMAT_EXE clang-format)
if(CLANG_FORMAT_EXE)
    # Target: cmake --build build --target format
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${SOURCES}
        COMMENT "Running clang-format..."
    )

    # Target: cmake --build build --target format-check
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run -Werror ${SOURCES}
        COMMENT "Checking format style..."
    )
else()
    message(WARNING "clang-format not found. Format targets will not be available.")
endif()
