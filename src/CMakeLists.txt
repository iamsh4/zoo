cmake_minimum_required(VERSION 2.8...3.16)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GTest REQUIRED)
find_package(GLEW REQUIRED)
find_package(fmt REQUIRED)
find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)
find_package(jsoncpp REQUIRED)

project(zoo)

# Zoo build include dirs
set(ZOO_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ZOO_BASE}
    ${ZOO_BASE}/fox
    ${ZOO_BASE}/fox/include
    ${ZOO_THIRD_PARTY_INCLUDE_DIRS}
    ${jsoncpp_SOURCE_DIR}/include
)

# Zoo library dependencies
set(ZOO_LIBS
    fmt::fmt
    GTest::gtest
    OpenGL::GL
    Vulkan::Vulkan
    SDL2::SDL2
    GLEW::glew
    zoo
    fox
    ${ZOO_THIRD_PARTY_LIBS}
)

string(APPEND CMAKE_CXX_FLAGS " -Wno-format -Wno-format-security")

# SH4 guest cpu codegen
set(SH4_OPCODE_IN ${ZOO_BASE}/src/guest/sh4/sh4_opcode.in)
set(SH4_OPCODE_CPP ${ZOO_BASE}/src/guest/sh4/sh4_opcode.cpp)
add_custom_command(
    OUTPUT ${SH4_OPCODE_CPP}
    COMMAND Python3::Interpreter
        ${ZOO_BASE}/src/guest/sh4/sh4_opcode.py ${SH4_OPCODE_IN} > ${SH4_OPCODE_CPP}
    DEPENDS ${SH4_OPCODE_IN}
    COMMENT "Codegen SH4 Guest CPU"
    VERBATIM
)
set_source_files_properties(${SH4_OPCODE_CPP} PROPERTIES GENERATED TRUE)

# libzoo is comprised of all the C++ files here (minus tests and targets)
file(GLOB_RECURSE ZOO_SOURCES "*.cpp")
list(FILTER ZOO_SOURCES EXCLUDE REGEX ".*tests/.*") # Exclude tests
list(FILTER ZOO_SOURCES EXCLUDE REGEX ".*targets/.*") # Exclude targets
list(APPEND ZOO_SOURCES ${SH4_OPCODE_CPP}) # Add in the codegen

# Build libzoo
add_library(zoo STATIC "")

if(APPLE)
    target_compile_definitions(zoo PRIVATE GL_SILENCE_DEPRECATION=1)
    target_compile_options(zoo PRIVATE "-Wno-deprecated-declarations")
endif()

target_compile_features(zoo PRIVATE cxx_std_17)
target_include_directories(zoo PUBLIC ${ZOO_INCLUDE_DIRS})
target_sources(zoo PRIVATE ${ZOO_SOURCES} ${ZOO_BASE}/third_party/tracy/TracyClient.cpp)
if(PROFILE)
    target_sources(zoo PRIVATE ${ZOO_BASE}/third_party/tracy/TracyClient.cpp)
endif()    

# Generate the test binaries
file(GLOB ZOO_TEST_SOURCES "tests/*.cpp")
foreach(test_src ${ZOO_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} ${ZOO_LIBS})
    target_include_directories(${test_name} PRIVATE ${ZOO_INCLUDE_DIRS})
endforeach()

# Generate the target binaries
file(GLOB ZOO_TARGET_SOURCES "targets/*.cpp")
foreach(target_src ${ZOO_TARGET_SOURCES})
    get_filename_component(target_name ${target_src} NAME_WE)
    add_executable(${target_name} ${target_src})
    target_link_libraries(${target_name} ${ZOO_LIBS})
    target_include_directories(${target_name} PRIVATE ${ZOO_INCLUDE_DIRS})
endforeach()
