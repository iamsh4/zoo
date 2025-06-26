#include <vector>
#include <stdexcept>
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>
#endif

#include "shared/file.h"
#include "opengl_shader_program.h"

void
ShaderProgram::listUniforms()
{
  GLint i;
  GLint count;

  GLint size;  // size of the variable
  GLenum type; // type of the variable (float, vec3 or mat4, etc)

  const GLsizei bufSize = 16; // maximum name length
  GLchar pname[bufSize];      // variable name in GLSL
  GLsizei length;             // name length

  glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
  printf("Active Uniforms fpr program %d: %d\n", program, count);

  for (i = 0; i < count && i < 10; i++) {
    glGetActiveUniform(program, (GLuint)i, bufSize, &length, &size, &type, pname);
    printf("Uniform #%d Type: %u Name: %s\n", i, type, pname);
  }
}

GLuint
compile_shader(const std::string_view &shader_source, GLuint shader_type)
{
  auto shader = glCreateShader(shader_type);

  const char *source = shader_source.data();
  glShaderSource(shader, 1, &source, 0);
  glCompileShader(shader);

  GLint is_compiled;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &is_compiled);

  if (is_compiled == GL_FALSE) {
    GLint maxLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

    std::vector<GLchar> errorLog(maxLength);
    glGetShaderInfoLog(shader, maxLength, &maxLength, &errorLog[0]);

    printf("%s\n", &errorLog[0]);
    throw std::runtime_error("Could not compile shader");
  }

  return shader;
}

GLuint
linkProgram(GLint vertex_shader, GLint fragment_shader)
{
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);

  GLint is_linked;
  glGetProgramiv(program, GL_LINK_STATUS, &is_linked);

  if (is_linked == GL_FALSE) {
    GLint maxLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

    std::vector<GLchar> infoLog(maxLength);
    glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

    printf("%s\n", &infoLog[0]);
    throw std::runtime_error("Could not link shaders");
  }

  return program;
}

template<typename Func>
GLint
ShaderProgram::getLocation(const Func &func, const std::string_view &name)
{
  if (!program) {
    throw std::runtime_error("Shader program is not compiled.");
  }

  GLint location = func(program, name.data());
  if (location == -1) {
    printf("Could not find uniform '%s'\n", name.data());
    throw std::runtime_error("Could not locate shader uniform");
  }

  return location;
}

ShaderProgram::ShaderProgram() : program(0) {}

ShaderProgram::ShaderProgram(std::filesystem::path path_vertex_shader,
                             std::filesystem::path path_fragment_shader)
  : program(0),
    m_path_vertex_shader(path_vertex_shader),
    m_path_fragment_shader(path_fragment_shader)
{
  compileAndLink();
}

ShaderProgram::~ShaderProgram()
{
  if (program) {
    glDeleteProgram(program);
  }
}

void
ShaderProgram::compileAndLink()
{
  const std::string source_vs = read_file_to_string(m_path_vertex_shader.c_str());
  const std::string source_fs = read_file_to_string(m_path_fragment_shader.c_str());

  try {
    GLuint vertex_shader   = compile_shader(source_vs, GL_VERTEX_SHADER);
    GLuint fragment_shader = compile_shader(source_fs, GL_FRAGMENT_SHADER);

    program = linkProgram(vertex_shader, fragment_shader);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

  } catch (const std::runtime_error &ex) {
    printf("Failed to compile and link\n");
  }

  m_last_modified_vs = std::filesystem::last_write_time(m_path_vertex_shader);
  m_last_modified_fs = std::filesystem::last_write_time(m_path_fragment_shader);
}

bool
ShaderProgram::wasSourceModified() const
{
  auto mod_vs = std::filesystem::last_write_time(m_path_vertex_shader);
  auto mod_fs = std::filesystem::last_write_time(m_path_fragment_shader);
  return (mod_vs > m_last_modified_vs) || (mod_fs > m_last_modified_fs);
}

void
ShaderProgram::activate()
{
  glUseProgram(program);
}

void
ShaderProgram::deactivate()
{
  glUseProgram(0);
}

void
ShaderProgram::setUniform1i(const std::string_view &name, int value)
{
  glUniform1i(getLocation(glGetUniformLocation, name), value);
}

void
ShaderProgram::setUniform1ui(const std::string_view &name, u32 value)
{
  glUniform1ui(getLocation(glGetUniformLocation, name), value);
}

void
ShaderProgram::setUniform1f(const std::string_view &name, float value)
{
  glUniform1f(getLocation(glGetUniformLocation, name), value);
}
