cmake_minimum_required(VERSION 3.16)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Default to Release build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
  # set(CMAKE_BUILD_TYPE Debug)
endif()

string(APPEND CMAKE_CXX_FLAGS " -Wall")
# string(APPEND CMAKE_CXX_FLAGS " -Werror")
string(APPEND CMAKE_CXX_FLAGS " -Wno-unused-private-field")
string(APPEND CMAKE_CXX_FLAGS " -Wno-error=unused-but-set-variable")
string(APPEND CMAKE_CXX_FLAGS " -Wno-error=unused-variable")
string(APPEND CMAKE_CXX_FLAGS " -Wno-error=unused-function")

# Tracy profiling
if(PROFILE)
  string(APPEND CMAKE_CXX_FLAGS " -DTRACY_ENABLE -DTRACY_FIBERS")
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "Release")
  string(APPEND CMAKE_CXX_FLAGS " -Wno-return-type")
endif()

# string(APPEND CMAKE_CXX_FLAGS " -fsanitize=thread")

if(APPLE)
  string(APPEND CMAKE_CXX_FLAGS " -I/opt/homebrew/include")
endif()

if(COVERAGE)
  string(APPEND CMAKE_CXX_FLAGS " -fprofile-arcs -coverage --coverage -ftest-coverage -fprofile-abs-path")

  # find required tools
  find_program(LCOV lcov REQUIRED)
  find_program(GENHTML genhtml REQUIRED)

  # add coverage target
  add_custom_target(coverage
    # gather data
    COMMAND ${LCOV} --directory . --capture --output-file coverage.info
    # generate report
    COMMAND ${GENHTML} --demangle-cpp -o coverage coverage.info
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

  add_custom_target(coverage-clean
    COMMAND find . -name "*.gcda" -type f -delete
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

project(zoo)

# Helpful settings across all builds
set(ZOO_BASE ${CMAKE_CURRENT_SOURCE_DIR})
set(ZOO_THIRD_PARTY_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libchdr/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracy/public
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu-native/ffi/webgpu-headers
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu-native/ffi
)

# Set the appropriate OS define
if(UNIX AND NOT APPLE)
    add_compile_definitions(ZOO_OS_LINUX=1)
    add_compile_definitions(ZOO_ARCH_X86_64=1)
elseif(APPLE)
    add_compile_definitions(ZOO_OS_MACOS=1)
    add_compile_definitions(ZOO_ARCH_ARM=1)
elseif(WIN32)
    add_compile_definitions(ZOO_OS_WINDOWS=1)
    add_compile_definitions(ZOO_ARCH_X86_64=1)
else()
    message(FATAL_ERROR "Unsupported OS")
endif()

add_subdirectory(third_party)

# Build libfox + tests
add_subdirectory(fox)

set(ZOO_THIRD_PARTY_LIBS
  chdr-static
  lzma
  z
  ${WEBGPU_NATIVE_SHARED_LIB}
  imgui
  jsoncpp_static
)

if (APPLE)
  list(APPEND ZOO_THIRD_PARTY_LIBS "-framework QuartzCore" "-framework CoreFoundation" "-framework Metal")
endif()

# Build libzoo + tests + targets
add_subdirectory(src)
