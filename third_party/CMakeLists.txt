cmake_minimum_required(VERSION 3.16)
project(zoo_third_party)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(Vulkan REQUIRED)

set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# imgui
add_library(imgui)
target_compile_features(imgui PRIVATE cxx_std_17)
target_sources(imgui PRIVATE
  ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl.cpp
  ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
  ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_vulkan.cpp
  ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
  ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
  ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
  ${THIRD_PARTY_DIR}/imgui/imgui.cpp
)
target_include_directories(imgui PUBLIC
  ${THIRD_PARTY_DIR}/imgui/
  ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(
  imgui PUBLIC
  OpenGL::GL
  Vulkan::Vulkan
  SDL2::SDL2
  GLEW::glew
)

# libchdr
set(WITH_SYSTEM_ZLIB ON)
add_subdirectory(${THIRD_PARTY_DIR}/libchdr)

# jsoncpp
add_subdirectory(${THIRD_PARTY_DIR}/jsoncpp)

# wgpu-native
set(
  WEBGPU_NATIVE_SHARED_LIB
  ${THIRD_PARTY_DIR}/wgpu-native/target/debug/libwgpu_native.a
  PARENT_SCOPE
)
