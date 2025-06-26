#pragma once

#include <cstdint>
#include <cstdarg>

namespace Log {

enum LogLevel
{
  None = 0,
  Error,
  Warn,
  Info,
  Debug,
  Verbose
};
extern LogLevel level;

enum LogModule : uint32_t
{
  SH4,
  GDROM,
  MAPLE,
  GRAPHICS,
  G2,
  GUI,
  AUDIO,
  MODEM,
  HOLLY,
  HOLLY2,
  MEMTABLE,
  PENGUIN,
};

static const uint32_t LOG_ENTRY_LENGTH = 256;
struct LogEntry {
  LogModule module;
  LogLevel level;
  char message[LOG_ENTRY_LENGTH];
  uint64_t entry_time;
};

void module_show(const LogModule module);
void module_show_all();

void module_hide(const LogModule module);
void module_hide_all();

bool is_module_enabled(const LogModule module);

const LogEntry &get_nth_entry(uint32_t index);
uint32_t get_current_entry_count();
void clear_all_entries();

void info(const LogModule module, const char *format, va_list varargs);
void warn(const LogModule module, const char *format, va_list varargs);
void error(const LogModule module, const char *format, va_list varargs);
void debug(const LogModule module, const char *format, va_list varargs);
void verbose(const LogModule module, const char *format, va_list varargs);

template<LogModule module>
class Logger {
public:
#define EMIT_METHOD(_level)                                                              \
  void _level(const char *format, ...) const                                             \
  {                                                                                      \
    va_list varargs;                                                                     \
    va_start(varargs, format);                                                           \
    Log::_level(module, format, varargs);                                                \
    va_end(varargs);                                                                     \
  }

  EMIT_METHOD(info)
  EMIT_METHOD(warn)
  EMIT_METHOD(error)
  EMIT_METHOD(debug)
  EMIT_METHOD(verbose)

#undef EMIT_METHOD
};

};
