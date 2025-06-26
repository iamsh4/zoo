#include <sys/mman.h>
#include <cstring>
#include <array>

#include "arm64/arm64_routine.h"

namespace fox {
namespace codegen {
namespace arm64 {

#ifdef __arm64__

std::string
exec(const char *cmd)
{
  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }

  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }

  return result;
}

std::string
Routine::disassemble() const
{
  FILE *const f = fopen("/tmp/penguin_disas", "wb");
  fwrite(data(), 1, size(), f);
  fclose(f);

  // On MacOS, gobjdump is installed via Homebrew 'binutils' package
  static char command[1024];
  snprintf(
    command,
    sizeof(command),
    "/opt/homebrew/opt/binutils/bin/gobjdump -b binary -m aarch64 -D /tmp/penguin_disas");
  return exec(command);
}

#else

std::string
Routine::disassemble() const
{
  return "Not supported outside ARM64";
}

#endif

void
Routine::debug_print()
{
  printf("Routine disassembly:\n%s\n", disassemble().c_str());
  return;
}

}
}
}
