#include "systems/dragon/director.h"

#include <csignal>

namespace zoo::dragon {

const u32 RUN_FOREVER = 0xFFFF'FFFF;

ConsoleDirector::ConsoleDirector(std::shared_ptr<Console> &console) : m_console(console)
{
  m_remaining_cycles = RUN_FOREVER;
  set_execution_mode(ExecutionMode::Running);
}

ConsoleDirector::~ConsoleDirector() {}

void
ConsoleDirector::cpu_thread_func()
{
  while (!m_is_shutting_down) {
    if (m_execution_mode == ExecutionMode::Paused) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (m_remaining_cycles > 0) {
        m_execution_mode = ExecutionMode::Running;
      }
    } else if (m_execution_mode == ExecutionMode::StepOnce) {
      m_console->step_instruction();
      m_execution_mode = ExecutionMode::Paused;
    } else {
      // We're in Running mode.
      try {
        if (m_remaining_cycles == 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else if (m_remaining_cycles == RUN_FOREVER) {
          m_console->step_instruction();
        } else {
          m_console->step_instruction();
          m_remaining_cycles--;
        }
      } catch (std::exception &e) {
        printf("Exception during CPU execution... %s\n", e.what());
        std::raise(SIGINT);
        m_execution_mode = ExecutionMode::Paused;
        m_remaining_cycles = 0;
      }
    }

    if(m_console->is_internal_pause_requested()) {
      m_execution_mode = ExecutionMode::Paused;
      m_remaining_cycles = 0;
      m_console->set_internal_pause(false);
    }
  }
}

void
ConsoleDirector::launch_threads()
{
  m_cpu_thread = std::thread([&] { cpu_thread_func(); });
}

void
ConsoleDirector::shutdown_threads()
{
  m_is_shutting_down = true;
  if (m_cpu_thread.joinable()) {
    m_cpu_thread.join();
  }
}

void
ConsoleDirector::step_instruction()
{
  set_execution_mode(ExecutionMode::StepOnce);
}

void
ConsoleDirector::reset()
{
  m_console->reset();
}

Console *
ConsoleDirector::console()
{
  return m_console.get();
}

void
ConsoleDirector::set_execution_mode(ConsoleDirector::ExecutionMode mode)
{
  if (mode == ExecutionMode::Running) {
    m_remaining_cycles = RUN_FOREVER;
  } else if (mode == ExecutionMode::StepOnce) {
    m_remaining_cycles = 1;
  } else {
    m_remaining_cycles = 0;
  }
}

// void
// ConsoleDirector::load_psx_exe(const char *path)
// {
//   // https://zanneth.com/2018/11/01/psx-exe-file-format.html

//   FILE *f = fopen(path, "rb");
//   fseek(f, 0, SEEK_END);

//   i64 file_len;
//   if ((file_len = ftell(f)) < 0) {
//     fclose(f);
//     throw std::runtime_error("Failed to determine file size");
//   }
//   fseek(f, 0, SEEK_SET);

//   // Read header
//   u8 header[0x800];
//   fread(header, sizeof(header), 1, f);

//   if (0 != strncmp((char *)&header[0], "PS-X EXE", 8)) {
//     throw std::runtime_error("PSX-EXE file has bad magic number");
//   }

//   u32 start_pc;
//   u32 start_sp; // SP

//   u32 text_section_ram_address;
//   u32 text_section_size;

//   memcpy(&start_pc, &header[0x10], 4);
//   memcpy(&text_section_ram_address, &header[0x18], 4);
//   memcpy(&text_section_size, &header[0x1C], 4);
//   memcpy(&start_sp, &header[0x30], 4);

//   printf("psx-exe: start PC 0x%08x\n", start_pc);
//   printf("psx-exe: start SP 0x%08x\n", start_sp);
//   printf("psx-exe: program load address 0x%08x\n", text_section_ram_address);
//   printf("psx-exe: program length 0x%08x\n", text_section_size);
//   printf("PSX-EXE region string: %s\n", (char *)&header[0x4c]);

//   // as/sert(start_sp != 0);
//   if (start_sp == 0) {
//     printf("PSX-EXE file had zero SP, setting a default one.\n");
//     start_sp = 0x801ffde0; // 0x200000 - 1024;
//     // start_sp = text_section_ram_address + text_section_size + 1024 * 2;
//   }

//   start_pc |= 0x8000'0000;

//   // Read `.text` section data immediately after the header
//   std::vector<u8> text_data(text_section_size);
//   fseek(f, 0x800, SEEK_SET);
//   fread(text_data.data(), text_section_size, 1, f);

//   fclose(f);

//   // TODO: dma should understand this perhaps
//   text_section_ram_address &= 0xfff'ffff;

//   // Write state to console
//   m_console->memory()->dma_write(
//     text_section_ram_address, text_data.data(), text_section_size);
//   m_console->cpu()->set_register(guest::r3000::Registers::PC, start_pc);
//   m_console->cpu()->set_register(guest::r3000::Registers::SP, start_sp);
// }

void
ConsoleDirector::dump_ram(const char *path, u32 start_address, u32 length)
{
  m_console->memory()->dump_u32(path, start_address, length);
}

}
