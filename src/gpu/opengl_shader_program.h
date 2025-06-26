#pragma once

#include <filesystem>
#include <shared/types.h>
#include <string_view>

class ShaderProgram {
private:
  u32 program;
  std::filesystem::file_time_type m_last_modified_vs;
  std::filesystem::file_time_type m_last_modified_fs;

  template<typename Func>
  i32 getLocation(const Func &func, const std::string_view &name);

  std::filesystem::path m_path_vertex_shader;
  std::filesystem::path m_path_fragment_shader;

public:
  ShaderProgram();
  ShaderProgram(std::filesystem::path path_vertex_shader,
                std::filesystem::path path_fragment_shader);
  ~ShaderProgram();

  bool wasSourceModified() const;
  void compileAndLink();

  void activate();
  static void deactivate();
  void listUniforms();

  void setUniform1i(const std::string_view &name, int value);
  void setUniform1ui(const std::string_view &name, u32 value);
  void setUniform1f(const std::string_view &name, float value);
};
