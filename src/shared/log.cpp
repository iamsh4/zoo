#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <functional>
#include <mutex>
#include <algorithm>

#include "shared/types.h"
#include "shared/log.h"

#include "shared/ansi_color_constants.h"

namespace Log {

static std::mutex log_mutex;
// static u32 enabled_modules = 0xFFFFFFFFu;
static u32 enabled_modules = 0x00000002u;
LogLevel level = LogLevel::Verbose;

size_t
hash_line(const char *s)
{
  // http://www.cse.yorku.ca/~oz/hash.html
  size_t h = 5381;
  int c;
  while ((c = *s++))
    h = ((h << 5) + h) + c;
  return h;
}

void
module_show(const LogModule module)
{
  enabled_modules |= (1lu << module);
}

void
module_show_all()
{
  enabled_modules = 0xFFFFFFFFu;
}

void
module_hide(const LogModule module)
{
  enabled_modules &= ~(1lu << module);
}

void
module_hide_all()
{
  enabled_modules = 0u;
}

bool
is_module_enabled(const LogModule module)
{
  return enabled_modules & (1lu << module);
}

// Total allocated log entries that will exist during the lifetime of the application
static const u32 log_entries_size = 1 << 15;

// All the log entries themselves
LogEntry log_entries[log_entries_size];

// The current number of log entries you can display
u32 current_entry_count = 0;

// Current index into the entry array (wraps). This is always pointing at the next
// entry to write to. Last is current - 1, etc.
u32 current_entry_index = 0;

const LogEntry &
get_nth_entry(u32 index)
{
  return log_entries[(current_entry_index + log_entries_size -
                      (current_entry_count - (index + 1))) %
                     log_entries_size];
}

u32
get_current_entry_count()
{
  return current_entry_count;
}

void
clear_all_entries()
{
  current_entry_count = 0;
}

void
emit_log_entry(LogLevel log_level, LogModule module, const char *format, va_list arglist)
{
  // Lock when adding a log entry
  std::lock_guard<std::mutex> local_lock(log_mutex);

  // Populate the next log entry
  LogEntry &log_entry(log_entries[current_entry_index]);
  vsnprintf(&log_entry.message[0], LOG_ENTRY_LENGTH, format, arglist);
  log_entry.entry_time = 0; // TODO
  log_entry.level = log_level;
  log_entry.module = module;

  current_entry_count = ::std::min(log_entries_size, current_entry_count + 1);
  current_entry_index = (current_entry_index + 1) % log_entries_size;
}

void
info(const LogModule module, const char *format, va_list varargs)
{
  if (level >= LogLevel::Info && is_module_enabled(module)) {
    emit_log_entry(LogLevel::Info, module, format, varargs);
  }
}

void
warn(const LogModule module, const char *format, va_list varargs)
{
  if (level >= LogLevel::Warn && is_module_enabled(module)) {
    emit_log_entry(LogLevel::Warn, module, format, varargs);
  }
}

void
error(const LogModule module, const char *format, va_list varargs)
{
  if (level >= LogLevel::Error && is_module_enabled(module)) {
    emit_log_entry(LogLevel::Error, module, format, varargs);
  }
}

void
debug(const LogModule module, const char *format, va_list varargs)
{
  if (level >= LogLevel::Debug && is_module_enabled(module)) {
    emit_log_entry(LogLevel::Debug, module, format, varargs);
  }
}

void
verbose(const LogModule module, const char *format, va_list varargs)
{
  // Verbose is a little special. It's so chatty, that we don't even want to
  // create the log entry unless we're currently showing verbose messages.
  if (level >= LogLevel::Verbose && is_module_enabled(module)) {
    emit_log_entry(LogLevel::Verbose, module, format, varargs);
  }
}

};
