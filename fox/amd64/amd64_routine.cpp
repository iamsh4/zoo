// vim: expandtab:ts=2:sw=2

/* vfork is deprecated on MacOS */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <cstdio>
#include <cstdlib>

#ifndef _WIN64
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
#endif

#include "amd64/amd64_routine.h"

namespace fox {
namespace codegen {
namespace amd64 {

std::string
Routine::disassemble() const
{
#ifndef _WIN64
  int read_pipe[2];
  int write_pipe[2];

  signal(SIGPIPE, SIG_IGN);

  /* Read pipe is used to read the text result back. */
  if (pipe(read_pipe) != 0) {
    perror("pipe");
    return "(error)";
  }

  /* Write pipe is used to write the binary sequence to. */
  if (pipe(write_pipe) != 0) {
    close(read_pipe[0]);
    close(read_pipe[1]);
    perror("pipe");
    return "(error)";
  }

  const pid_t child_pid = vfork();
  if (child_pid < 0) {
    perror("vfork");
    close(read_pipe[0]);
    close(read_pipe[1]);
    close(write_pipe[0]);
    close(write_pipe[1]);
    return "(error)";
  }

  if (child_pid == 0) {
    const char *argv[] = { "ndisasm", "-b64", "/dev/stdin", NULL };

    close(read_pipe[0]);
    close(write_pipe[1]);

    dup2(write_pipe[0], 0);
    dup2(read_pipe[1], 1);
    close(write_pipe[0]);
    close(read_pipe[1]);

    execvp("ndisasm", const_cast<char **>(argv));
    perror("execvp");
    exit(255);
  }

  close(read_pipe[1]);
  close(write_pipe[0]);

  /* Write binary data to the disassembly program and read the resulting
   * source code. This must be done in parallel with select() to avoid a
   * deadlock. */
  const int max_fd = std::max(read_pipe[0], write_pipe[1]);
  size_t offset = 0lu;
  std::string result;
  while (true) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(read_pipe[0], &read_set);

    fd_set write_set;
    FD_ZERO(&write_set);
    if (offset < m_data_size) {
      FD_SET(write_pipe[1], &write_set);
    }

    const int select_result = select(max_fd + 1, &read_set, &write_set, NULL, NULL);
    if (select_result < 0) {
      perror("select");
      break;
    }

    if (FD_ISSET(read_pipe[0], &read_set)) {
      char buffer[4096];
      const ssize_t read_result = read(read_pipe[0], &buffer[0], sizeof(buffer));
      if (read_result < 0 || (read_result == 0 && offset != m_data_size)) {
        perror("read");
        break;
      } else if (read_result == 0) {
        break;
      }
      result.append(buffer, read_result);
    }

    if (offset < m_data_size && FD_ISSET(write_pipe[1], &write_set)) {
      const ssize_t write_result =
        write(write_pipe[1], &m_storage.second[offset], m_data_size - offset);
      if (write_result <= 0) {
        perror("write");
        break;
      }
      offset += write_result;

      if (offset == m_data_size) {
        close(write_pipe[1]);
        write_pipe[1] = -1;
      }
    }
  }

  close(read_pipe[0]);
  if (write_pipe[1] >= 0) {
    close(write_pipe[1]);
  }
  waitpid(child_pid, NULL, 0);

  return result;
#else
  return "Not supported under windows";
#endif
}

void
Routine::debug_print()
{
  printf("%s", disassemble().c_str());
}

}
}
}

#pragma GCC diagnostic pop
