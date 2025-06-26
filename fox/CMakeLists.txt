cmake_minimum_required(VERSION 3.16)
project(fox)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(GTest REQUIRED)
find_package(fmt REQUIRED)

# Create your static library without any source files initially.
add_library(fox STATIC "")

# Specify that our library uses c++17 features.
target_compile_features(fox PUBLIC cxx_std_17)
set_target_properties(fox PROPERTIES CXX_STANDARD_REQUIRED ON)

# Fox wants to include files from this folder, but also treat 'fox/...' which
# sits in the parent (base) directory
set(FOX_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include ${ZOO_BASE})

# Add include directories.
target_include_directories(fox PUBLIC ${FOX_INCLUDE_DIRS})

# Add all the .cpp files in the base directory and subdirectories to the library,
# excluding those in the 'tests' directory.
file(GLOB_RECURSE SOURCES "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX ".*tests/.*") # Exclude tests
target_sources(fox PRIVATE ${SOURCES})

# Define a function to generate files using a python script
function(add_codegen target script input output)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/${output}
        COMMAND Python3::Interpreter
          ${CMAKE_CURRENT_SOURCE_DIR}/${script}
          ${CMAKE_CURRENT_SOURCE_DIR}/${input}
          ${CMAKE_CURRENT_SOURCE_DIR}/${output}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${input}
        COMMENT "Codegen ${output} via ${script}"
        VERBATIM
    )

    target_sources(
        ${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${output}
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/${output}
        PROPERTIES GENERATED TRUE
    )
endfunction()

# amd64 backend codegen
add_codegen(fox amd64/amd64.py amd64/amd64_assembler.csv amd64/amd64_assembler.h)
add_codegen(fox amd64/amd64.py amd64/amd64_assembler.csv amd64/amd64_assembler.cpp)

# arm64 backend codegen
add_codegen(fox arm64/arm64.py arm64/arm64_assembler.json arm64/arm64_assembler.h)
add_codegen(fox arm64/arm64.py arm64/arm64_assembler.json arm64/arm64_assembler.cpp)

##########
# Generate the test binaries
set(FOX_REQUIRED_LIBS fmt::fmt GTest::gtest)

file(GLOB TEST_SOURCES "tests/*.cpp")  # Replace this with the correct path to your test files
foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} fox ${FOX_REQUIRED_LIBS})
    target_include_directories(${test_name} PRIVATE ${FOX_INCLUDE_DIRS})
endforeach()

