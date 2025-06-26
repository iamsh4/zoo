#pragma once

#include <thread>

#include "systems/dragon/console.h"

namespace zoo::dragon {
class ConsoleDirector {
public:
  ConsoleDirector(std::shared_ptr<Console> &console);
  ~ConsoleDirector();

  void launch_threads();
  void shutdown_threads();

  enum class ExecutionMode
  {
    Paused,
    Running,
    StepOnce,
  };

  void set_execution_mode(ExecutionMode mode);
  void step_instruction();
  void reset();
  Console *console();

  // void load_psx_exe(const char* path);

  void dump_ram(const char *path, u32 start_address, u32 length);

private:
  std::shared_ptr<Console> m_console;
  bool m_is_shutting_down = false;

  std::thread m_cpu_thread;
  ExecutionMode m_execution_mode = ExecutionMode::Running;

  std::atomic_uint32_t m_remaining_cycles;

  void cpu_thread_func();
};
}
