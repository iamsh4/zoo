#include "file_watch.h"

#if defined(ZOO_OS_LINUX) || defined(ZOO_OS_MACOS)

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <thread>
#include <vector>
#include <sys/stat.h>

class StatBasedFileWatcher final : public FileWatcher {
  struct Watch {
    int id;
    std::string path;
    i64 last_modified;
    Callback callback;
  };

public:
  StatBasedFileWatcher()
  {
    m_thread = std::thread([&] { watch_logic(); });
  }

  ~StatBasedFileWatcher()
  {
    m_shutdown = true;
    m_thread.join();
  }

  // XXX : thread safety to singleton

  i64 stat_to_nanoseconds(struct stat st)
  {
#if defined(ZOO_OS_LINUX)
    return i64(st.st_ctim.tv_nsec) + i64(st.st_ctim.tv_sec) * 1000000000;
#elif defined(ZOO_OS_MACOS)
    return i64(st.st_ctimespec.tv_nsec) + i64(st.st_ctimespec.tv_sec) * 1000000000;
#endif
  }

  int m_watch_counter = 0;
  FileWatchToken add_watch(const char *path, Callback callback)
  {
    printf("Adding watch for '%s'\n", path);
    const int new_id = m_watch_counter++;
    Watch watch {
      .id = new_id,
      .path = path,
      .last_modified = -1,
      .callback = callback,
    };

    struct stat info;
    if (stat(watch.path.c_str(), &info) == 0) {
      watch.last_modified = stat_to_nanoseconds(info);
    }

    m_watches.push_back(watch);
    return FileWatchToken { .id = new_id };
  }

  void remove_watch(FileWatchToken token)
  {
    (void)std::remove_if(m_watches.begin(), m_watches.end(), [&token](const Watch &w) {
      return w.id == token.id;
    });
  }

protected:
  void watch_logic()
  {
    struct stat info;

    while (!m_shutdown) {
      for (Watch &watch : m_watches) {
        if (stat(watch.path.c_str(), &info) == 0) {
          const i64 ctime = stat_to_nanoseconds(info);
          if (watch.last_modified != ctime) {
            watch.callback(Notification {
              .event_bits = NotificationBits::NOTIFICATION_MODIFIED,
            });
          }
          watch.last_modified = ctime;
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }

  bool m_shutdown = false;
  std::thread m_thread;
  std::vector<Watch> m_watches;
};

#endif

FileWatcher *
FileWatcher::singleton()
{
#if defined(ZOO_OS_LINUX) || defined(ZOO_OS_MACOS)
  static FileWatcher *const watcher = new StatBasedFileWatcher();
  return watcher;
#else
  fprintf(
    stderr,
    "Request to get FileWatcher, but none is implemented on this OS. Returning null\n");
  return nullptr;
#endif
}
